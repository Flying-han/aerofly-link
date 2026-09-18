/* bridge.c —— AeroflyBridge.dll 桥接客户端实现（基准：core/dll_bridge.py） */
#include "link/bridge.h"
#include "link/json.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#define RAD_TO_DEG   57.29577951308232
#define MPS_TO_KTS   1.94384
#define MPS_TO_FPM   196.8504
#define RETRY_BASE   2.0
#define RETRY_CAP    30.0

static const char *HOST = "127.0.0.1";
#define TEL_PORT 12345
#define CMD_PORT 12346

void br_init(bridge_t *b)
{
    memset(b, 0, sizeof(*b));
    b->tel_sock = INVALID_SOCKET;
    fsd_linebuf_init(&b->lb);
    strcpy(b->last.xpdr_code, "0000");
}

/* ── 单位/约定转换（与 Python VAR_MAP 一致）── */

static double heading_to_compass(double v)
{
    /* 弧度数学约定（East=0 逆时针）→ 罗盘度（North=0 顺时针）；
       |v|>6.5 视为已输入度（兼容部分构建） */
    double math_deg = (fabs(v) <= 6.5) ? fmod(v * RAD_TO_DEG, 360.0) : fmod(v, 360.0);
    return fmod(90.0 - math_deg, 360.0);
}

static bool get_num(const char *line, const char *key, double *out)
{
    return jsn_number(line, key, out);
}

static void parse_telemetry(bridge_t *b, const char *line)
{
    telem_t t;
    memset(&t, 0, sizeof(t));
    strcpy(t.xpdr_code, "0000");
    strcpy(t.model, "");
    double v;

    /* 位置：AFS4 弧度 → 度；经度归一化 [-180,180] */
    if (get_num(line, "Aircraft.Latitude", &v))
        t.lat = v * RAD_TO_DEG;
    if (get_num(line, "Aircraft.Longitude", &v))
        t.lon = fmod(v * RAD_TO_DEG + 180.0, 360.0) - 180.0;
    if (get_num(line, "Aircraft.Altitude", &v))
        t.alt_m = v;
    if (get_num(line, "Aircraft.HeightAboveGround", &v))
        t.agl_m = v;

    /* 姿态与速度 */
    if (get_num(line, "Aircraft.TrueHeading", &v))
        t.hdg_true = heading_to_compass(v);
    if (get_num(line, "Aircraft.IndicatedAirspeed", &v))
        t.ias_kts = v * MPS_TO_KTS;
    if (get_num(line, "Aircraft.GroundSpeed", &v))
        t.gs_kts = v * MPS_TO_KTS;
    if (get_num(line, "Aircraft.VerticalSpeed", &v))
        t.vs_fpm = v * MPS_TO_FPM;

    /* 无线电（Hz → kHz*10） */
    if (get_num(line, "Communication.COM1Frequency", &v))
        t.com1_khz10 = (int)(v / 1000.0);
    if (get_num(line, "Communication.COM2Frequency", &v))
        t.com2_khz10 = (int)(v / 1000.0);
    if (jsn_number(line, "Communication.TransponderCode", &v))
        _snprintf(t.xpdr_code, sizeof(t.xpdr_code) - 1, "%04d", (int)v);
    t.xpdr_code[sizeof(t.xpdr_code) - 1] = '\0';

    if (get_num(line, "Aircraft.OnGround", &v))
        t.on_ground = (v != 0.0);
    jsn_string(line, "Aircraft.Name", t.model, sizeof(t.model));

    t.timestamp = net_now();

    /* data_valid=0 的包装帧已在上层过滤；无位置键时视为无效帧忽略 */
    if (t.lat == 0.0 && t.lon == 0.0 && t.alt_m == 0.0)
        return;

    b->last = t;
    b->has_telem = true;
    if (b->on_telem)
        b->on_telem(b->ud, &t);
}

static void set_connected(bridge_t *b, bool up)
{
    if (b->tel_connected != up) {
        b->tel_connected = up;
        if (b->on_conn)
            b->on_conn(b->ud, up);
    }
}

void br_start(bridge_t *b)
{
    b->want_connected = true;
    b->next_retry = 0;    /* 立即尝试 */
}

void br_stop(bridge_t *b)
{
    b->want_connected = false;
    if (b->tel_sock != INVALID_SOCKET) {
        closesocket(b->tel_sock);
        b->tel_sock = INVALID_SOCKET;
    }
    set_connected(b, false);
}

static void telemetry_up(bridge_t *b, SOCKET s)
{
    b->tel_sock = s;
    fsd_linebuf_init(&b->lb);
    b->fails = 0;
    set_connected(b, true);
}

static void telemetry_down(bridge_t *b)
{
    if (b->tel_sock != INVALID_SOCKET) {
        closesocket(b->tel_sock);
        b->tel_sock = INVALID_SOCKET;
    }
    set_connected(b, false);
    if (b->want_connected) {
        b->fails++;
        double delay = RETRY_BASE;
        for (int i = 1; i < b->fails && delay < RETRY_CAP; i++)
            delay *= 2;
        if (delay > RETRY_CAP)
            delay = RETRY_CAP;
        b->next_retry = net_now() + delay;
    }
}

void br_tick(bridge_t *b, double now)
{
    if (!b->want_connected || b->tel_sock != INVALID_SOCKET)
        return;
    if (now < b->next_retry)
        return;

    SOCKET s = INVALID_SOCKET;
    int rc = net_nb_connect(HOST, TEL_PORT, &s);
    if (rc == 0) {
        telemetry_up(b, s);
    } else if (rc == 1) {
        b->tel_sock = s;   /* 连接进行中，等 on_writable/tick 完成 */
        if (net_wait_writable(s, 4000) > 0 && net_connect_finished(s) == 0)
            telemetry_up(b, s);
        else
            telemetry_down(b);
    } else {
        b->fails++;
        double delay = RETRY_BASE;
        for (int i = 1; i < b->fails && delay < RETRY_CAP; i++)
            delay *= 2;
        if (delay > RETRY_CAP)
            delay = RETRY_CAP;
        b->next_retry = net_now() + delay;
    }
}

void br_on_readable(bridge_t *b)
{
    if (b->tel_sock == INVALID_SOCKET)
        return;

    char chunk[65536];
    for (;;) {
        int n = recv(b->tel_sock, chunk, sizeof(chunk), 0);
        if (n == 0) {
            telemetry_down(b);   /* DLL 关闭连接（游戏退出/重载） */
            return;
        }
        if (n < 0) {
            int err = WSAGetLastError();
            if (err == WSAEWOULDBLOCK)
                return;
            telemetry_down(b);
            return;
        }

        size_t off = 0;
        while (off < (size_t)n) {
            size_t consumed = 0;
            int rc = fsd_linebuf_feed(&b->lb, chunk + off, (size_t)n - off,
                                      &consumed, b->scratch, sizeof(b->scratch));
            off += consumed;
            if (rc == 1 && b->scratch[0])
                parse_telemetry(b, b->scratch);
            if (consumed == 0)
                break;
        }
    }
}

void br_collect_fds(const bridge_t *b, fd_set *r, int *max)
{
    (void)max;
    if (b->tel_sock != INVALID_SOCKET && b->tel_connected)
        FD_SET(b->tel_sock, r);
}

/* ── 命令端口（短连接）── */

int br_send_command(bridge_t *b, const char *variable,
                    bool has_num, double num_val,
                    const char *str_val, char *resp, size_t resp_cap)
{
    (void)b;
    char payload[256];
    int n;
    if (has_num)
        n = _snprintf(payload, sizeof(payload) - 1,
                      "{\"variable\":\"%s\",\"value\":%.6g}", variable, num_val);
    else
        n = _snprintf(payload, sizeof(payload) - 1,
                      "{\"variable\":\"%s\",\"value\":\"%s\"}", variable,
                      str_val ? str_val : "");
    if (n < 0)
        return -1;
    payload[n] = '\0';

    SOCKET s = INVALID_SOCKET;
    int rc = net_nb_connect(HOST, CMD_PORT, &s);
    if (rc == 1) {
        if (net_wait_writable(s, 1000) <= 0 || net_connect_finished(s) != 0)
            rc = -1;
        else
            rc = 0;
    }
    if (rc != 0) {
        if (s != INVALID_SOCKET)
            closesocket(s);
        return -1;
    }

    char wire[260];
    _snprintf(wire, sizeof(wire) - 1, "%s\n", payload);
    int fail = net_send_all(s, wire, strlen(wire), 1000);

    if (!fail && net_wait_readable(s, 1000) > 0) {
        char buf[256];
        int got = recv(s, buf, (int)sizeof(buf) - 1, 0);
        if (got > 0) {
            buf[got] = '\0';
            if (resp && resp_cap)
                strncpy(resp, buf, resp_cap - 1), resp[resp_cap - 1] = '\0';
        }
    }
    closesocket(s);
    return fail == 0 ? 0 : -1;
}
