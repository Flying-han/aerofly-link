/* mock.c —— 内嵌模拟 AeroflyBridge.dll 服务器（基准：core/mock_server.py） */
#include "link/mock.h"
#include "link/json.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static const char *HOST = "127.0.0.1";
#define TEL_PORT 12345
#define CMD_PORT 12346
#define PUSH_INTERVAL 0.1   /* 10Hz，与 Python 内置 Mock 一致 */

static int listen_on(SOCKET *s, int port)
{
    SOCKET sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock == INVALID_SOCKET)
        return -1;
    BOOL reuse = TRUE;
    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, (const char *)&reuse, sizeof(reuse));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons((u_short)port);
    addr.sin_addr.s_addr = inet_addr(HOST);

    u_long mode = 1;
    ioctlsocket(sock, FIONBIO, &mode);

    if (bind(sock, (struct sockaddr *)&addr, sizeof(addr)) == SOCKET_ERROR
        || listen(sock, SOMAXCONN) == SOCKET_ERROR) {
        closesocket(sock);
        return -1;
    }
    *s = sock;
    return 0;
}

int mocksrv_start(mocksrv_t *m)
{
    memset(m, 0, sizeof(*m));
    m->tel_listen = m->cmd_listen = INVALID_SOCKET;
    for (int i = 0; i < MOCK_MAX_CLIENTS; i++)
        m->tel_clients[i] = INVALID_SOCKET;

    if (listen_on(&m->tel_listen, TEL_PORT) != 0
        || listen_on(&m->cmd_listen, CMD_PORT) != 0) {
        mocksrv_stop(m);
        return -1;
    }
    strcpy(m->xpdr, "7000");
    m->t0 = net_now();
    m->center_lat = 31.1434;
    m->center_lon = 121.8082;
    m->alt_m = 3500.0;
    m->running = true;
    return 0;
}

void mocksrv_stop(mocksrv_t *m)
{
    m->running = false;
    if (m->tel_listen != INVALID_SOCKET) closesocket(m->tel_listen);
    if (m->cmd_listen != INVALID_SOCKET) closesocket(m->cmd_listen);
    m->tel_listen = m->cmd_listen = INVALID_SOCKET;
    for (int i = 0; i < MOCK_MAX_CLIENTS; i++) {
        if (m->tel_clients[i] != INVALID_SOCKET)
            closesocket(m->tel_clients[i]);
        m->tel_clients[i] = INVALID_SOCKET;
    }
    m->n_tel = 0;
}

/* 心形航线遥测（与 Python mock_server._generate_telemetry 相同方程） */
static int build_frame(const mocksrv_t *m, double now, char *out, size_t cap)
{
    double t = now - m->t0;
    double ang = t * 0.06;
    double s = sin(ang), c = cos(ang);

    double hx = s * s * s;                                /* x = sin³(t) */
    double hy = (13 * c - 5 * cos(2 * ang) - 2 * cos(3 * ang)
                 - cos(4 * ang)) / 17.0;
    double dx = 3 * s * s * c;
    double dy = (-13 * s + 10 * sin(2 * ang) + 6 * sin(3 * ang)
                 + 4 * sin(4 * ang)) / 16.0;
    double hdg_deg = fmod(atan2(dx, dy) * 180.0 / M_PI + 90.0, 360.0);

    double lat = m->center_lat + 0.02 * hy;
    double lon = m->center_lon + 0.02 * hx;
    double alt = m->alt_m + 200.0 * sin(t * 0.03);
    double hdg_math_rad = (90.0 - hdg_deg) * M_PI / 180.0;   /* 罗盘 → 数学弧度 */

    return _snprintf(out, cap - 1,
        "{\"Aircraft.Latitude\":%.9f,\"Aircraft.Longitude\":%.9f,"
        "\"Aircraft.Altitude\":%.3f,\"Aircraft.HeightAboveGround\":800.0,"
        "\"Aircraft.TrueHeading\":%.9f,\"Aircraft.MagneticHeading\":%.9f,"
        "\"Aircraft.Pitch\":0.05,\"Aircraft.Bank\":0.03,"
        "\"Aircraft.IndicatedAirspeed\":72.0,\"Aircraft.GroundSpeed\":70.0,"
        "\"Aircraft.VerticalSpeed\":2.5,\"Aircraft.MachNumber\":0.42,"
        "\"Communication.COM1Frequency\":122800000,"
        "\"Communication.COM2Frequency\":119500000,"
        "\"Communication.TransponderCode\":%d,"
        "\"Aircraft.Gear\":0.0,\"Aircraft.Flaps\":0.0,"
        "\"Aircraft.OnGround\":0.0,\"Aircraft.OnRunway\":0.0,"
        "\"Aircraft.AngleOfAttack\":0.04,\"Aircraft.RateOfTurn\":0.0,"
        "\"Aircraft.Name\":\"B738\","
        "\"Autopilot.Master\":1.0,\"Autopilot.SelectedAltitude\":%.3f,"
        "\"Autopilot.SelectedHeading\":%.9f,"
        "\"Autopilot.SelectedVerticalSpeed\":0.0,"
        "\"Aircraft.EngineRunning1\":1.0,\"Aircraft.EngineRunning2\":1.0}",
        lat * M_PI / 180.0, lon * M_PI / 180.0, alt,
        hdg_math_rad, hdg_math_rad,
        atoi(m->xpdr), alt + 200.0 * sin(t * 0.03 + 1.0), hdg_math_rad);
}

static void push_telemetry(mocksrv_t *m, double now)
{
    static double next_push = 0;
    if (now < next_push)
        return;
    next_push = now + PUSH_INTERVAL;

    char frame[1024];
    int n = build_frame(m, now, frame, sizeof(frame));
    if (n < 0)
        return;
    frame[n++] = '\n';

    for (int i = 0; i < m->n_tel; i++) {
        int rc = send(m->tel_clients[i], frame, n, 0);
        if (rc == SOCKET_ERROR) {
            int err = WSAGetLastError();
            if (err != WSAEWOULDBLOCK) {
                closesocket(m->tel_clients[i]);
                m->tel_clients[i] = m->tel_clients[m->n_tel - 1];
                m->tel_clients[m->n_tel - 1] = INVALID_SOCKET;
                m->n_tel--;
                i--;
            }
        }
    }
}

static void handle_command(mocksrv_t *m, SOCKET s, const char *line)
{
    char variable[64];
    double value;
    const char *resp;

    if (!jsn_string(line, "variable", variable, sizeof(variable))) {
        resp = "{\"status\":\"error\",\"msg\":\"invalid JSON\"}";
    } else if (strcmp(variable, "Communication.TransponderCode") == 0
               && jsn_number(line, "value", &value)) {
        _snprintf(m->xpdr, sizeof(m->xpdr) - 1, "%04d", (int)value);
        m->xpdr[sizeof(m->xpdr) - 1] = '\0';
        resp = "{\"status\":\"ok\"}";
    } else if ((strcmp(variable, "Communication.COM1Frequency") == 0
                || strcmp(variable, "Communication.COM2Frequency") == 0)
               && jsn_number(line, "value", &value)) {
        resp = "{\"status\":\"ok\"}";
    } else {
        resp = "{\"status\":\"error\",\"msg\":\"unknown variable\"}";
    }

    char wire[160];
    int n = _snprintf(wire, sizeof(wire) - 1, "%s\n", resp);
    send(s, wire, n, 0);
}

void mocksrv_on_readable(mocksrv_t *m, const fd_set *r)
{
    if (!m->running)
        return;

    /* accept 遥测客户端 */
    if (m->tel_listen != INVALID_SOCKET && FD_ISSET(m->tel_listen, r)) {
        SOCKET c = accept(m->tel_listen, NULL, NULL);
        if (c != INVALID_SOCKET && m->n_tel < MOCK_MAX_CLIENTS) {
            u_long mode = 1;
            ioctlsocket(c, FIONBIO, &mode);
            m->tel_clients[m->n_tel++] = c;
        } else if (c != INVALID_SOCKET) {
            closesocket(c);
        }
    }

    /* 命令端口：accept + 逐行处理（短连接语义） */
    if (m->cmd_listen != INVALID_SOCKET && FD_ISSET(m->cmd_listen, r)) {
        SOCKET c = accept(m->cmd_listen, NULL, NULL);
        if (c != INVALID_SOCKET) {
            char buf[512];
            int total = 0, got;
            /* 命令为单行请求-响应；阻塞式读行（短连接，量小） */
            u_long mode = 0;
            ioctlsocket(c, FIONBIO, &mode);   // 切回阻塞读简化处理
            DWORD timeout = 1000;
            setsockopt(c, SOL_SOCKET, SO_RCVTIMEO, (const char *)&timeout,
                       sizeof(timeout));
            while ((got = recv(c, buf + total, (int)sizeof(buf) - 1 - total, 0)) > 0) {
                total += got;
                buf[total] = '\0';
                char *nl;
                while ((nl = strchr(buf, '\n')) != NULL) {
                    *nl = '\0';
                    handle_command(m, c, buf);
                    int rest = (int)(buf + total - nl - 1);
                    if (rest > 0)
                        memmove(buf, nl + 1, (size_t)rest);
                    total = rest;
                }
                if (total <= 0)
                    break;
            }
            closesocket(c);
        }
    }
}

void mocksrv_tick(mocksrv_t *m, double now)
{
    if (m->running)
        push_telemetry(m, now);
}

void mocksrv_collect_fds(const mocksrv_t *m, fd_set *r, int *max)
{
    (void)max;
    if (!m->running)
        return;
    if (m->tel_listen != INVALID_SOCKET)
        FD_SET(m->tel_listen, r);
    if (m->cmd_listen != INVALID_SOCKET)
        FD_SET(m->cmd_listen, r);
    for (int i = 0; i < m->n_tel; i++)
        FD_SET(m->tel_clients[i], r);
}
