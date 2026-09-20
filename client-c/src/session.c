/* session.c —— FSD 会话状态机实现（基准：core/fsd_client.py） */
#include "link/session.h"
#include "link/protocol.h"

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

/* 握手期标志位（diag_flags） */
#define HF_DI_SEEN   0x1u   /* 收到 $DI（Swift 私服标识） */
#define HF_ID_SENT   0x2u   /* $ID 已发送 */
#define HF_AUTH_SENT 0x4u   /* #AP 已发送 */

#define HANDSHAKE_TIMEOUT 10.0
#define GREETING_TIMEOUT   3.0
#define KEEPALIVE_INTERVAL 30.0
#define READ_TIMEOUT       90.0   /* 接收超时（3×心跳，Python _read_timeout） */

/* Swift 兼容能力集（Python CLIENT_CAPABILITIES） */
static const char *CAPS_REPLY =
    "ATCINFO=1:MODELDESC=1:ACCONFIG=1:FASTPOS=0:VISUPDATE=0:INTERIMPOS=0";

static void set_state(sess_t *s, sess_state_t st, const char *msg)
{
    s->state = st;
    if (s->on_status)
        s->on_status(s->ud, st, msg);
}

static int sess_send_raw(sess_t *s, const char *line)
{
    if (s->sock == INVALID_SOCKET)
        return -1;
    char buf[FSD_MAX_LINE + 2];
    int n = _snprintf(buf, sizeof(buf) - 2, "%s\r\n", line);
    if (n < 0)
        return -1;
    buf[n] = '\0';
    int rc = net_send_all(s->sock, buf, (size_t)n, 2000);
    return rc == 0 ? 0 : -1;
}

void sess_init(sess_t *s)
{
    memset(s, 0, sizeof(*s));
    s->sock = INVALID_SOCKET;
    fsd_linebuf_init(&s->lb);
    strcpy(s->last_xpdr, "1200");
    s->last_mode = 'N';
}

/* ── 连接发起 ── */

int sess_connect(sess_t *s)
{
    if (!s->callsign[0] || !s->cid[0] || !s->server[0]) {
        set_state(s, SESS_DISCONNECTED,
                  "无法连接：呼号 / CID / 服务器地址不能为空");
        return -1;
    }

    /* 复位运行时状态 */
    s->diag_flags = 0;
    s->has_position = false;
    s->n_traffic = 0;
    s->last_recv = net_now();
    fsd_linebuf_init(&s->lb);

    SOCKET sock = INVALID_SOCKET;
    int rc = net_nb_connect(s->server, s->port, &sock);
    if (rc < 0) {
        char msg[160];
        _snprintf(msg, sizeof(msg) - 1, "连接失败 %s:%d", s->server, s->port);
        msg[sizeof(msg) - 1] = '\0';
        set_state(s, SESS_DISCONNECTED, msg);
        return 0;   /* 结果经回调通知，不视为编程错误 */
    }
    s->sock = sock;
    s->deadline = net_now() + HANDSHAKE_TIMEOUT;
    set_state(s, SESS_CONNECTING, "TCP 连接中...");
    return 0;
}

void sess_disconnect(sess_t *s, const char *reason)
{
    if (s->state == SESS_ONLINE && s->sock != INVALID_SOCKET) {
        char dp[32];
        _snprintf(dp, sizeof(dp) - 1, "#DP%s", s->callsign);
        dp[sizeof(dp) - 1] = '\0';
        sess_send_raw(s, dp);   /* 尽力而为 */
    }
    if (s->sock != INVALID_SOCKET) {
        closesocket(s->sock);
        s->sock = INVALID_SOCKET;
    }
    s->n_traffic = 0;
    if (s->state != SESS_DISCONNECTED)
        set_state(s, SESS_DISCONNECTED, reason ? reason : "已断开");
}

/* ── 握手 ── */

static void handshake_send_auth(sess_t *s)
{
    char line[FSD_MAX_LINE];

    /* VATSIM dialect 或确认了 $DI 的私服：先发 $ID（挑战留空） */
    if (s->vatsim || (s->diag_flags & HF_DI_SEEN)) {
        fsd_ident_args id = {
            .callsign = s->callsign,
            .client_id_hex = "0000",
            .client_name = "Aerofly Link",
            .client_version = "1.0",
            .simulator_type = "Aerofly FS 4",
        };
        if (fsd_build_ident(line, sizeof(line), &id) == 0)
            sess_send_raw(s, line);
        s->diag_flags |= HF_ID_SENT;
    }

    fsd_auth_args auth = {
        .callsign = s->callsign, .cid = s->cid, .password = s->password,
        .realname = s->realname, .rating = s->rating,
        .revision = s->vatsim ? FSD_REVISION_VATSIM : FSD_REVISION_LEGACY,
        .sim_type_code = "0",
    };
    if (fsd_build_auth(line, sizeof(line), &auth) == 0)
        sess_send_raw(s, line);
    s->diag_flags |= HF_AUTH_SENT;
    s->deadline = net_now() + HANDSHAKE_TIMEOUT;
}

static void handshake_online(sess_t *s)
{
    char line[FSD_MAX_LINE];

    /* 先置在线态：以下发送（$FP 等）带在线守卫 */
    s->state = SESS_ONLINE;

    /* 声明 SquawkBox 客户端类型（服务器路由 @ 包所需） */
    _snprintf(line, sizeof(line) - 1, "#SB%s:SERVER", s->callsign);
    line[sizeof(line) - 1] = '\0';
    sess_send_raw(s, line);

    /* 最小飞行计划：加入广播列表（字段默认值与 Python _send_minimal_flight_plan 一致） */
    sess_fp_t fp;
    memset(&fp, 0, sizeof(fp));
    fp.type = "I";
    fp.aircraft = "B738/L";
    fp.tas = "0";
    fp.dep = "ZZZZ";
    fp.dest = "ZZZZ";
    fp.altn = "ZZZZ";
    fp.cruise_alt = "FL000";
    fp.route = "NOFP";
    fp.pilot = s->realname[0] ? s->realname : s->callsign;
    sess_send_flightplan(s, &fp);

    /* 初始位置（等真实遥测后由周期报告覆盖） */
    sess_set_initial_position(s, s->init_lat, s->init_lon, s->init_alt_ft);

    s->next_keepalive = net_now() + KEEPALIVE_INTERVAL;
    if (s->on_status)
        s->on_status(s->ud, SESS_ONLINE, "已连接并认证");
}

static void handshake_line(sess_t *s, const char *line)
{
    if (!line[0])
        return;

    if (line[0] == '#') {
        if (strncmp(line, "#ER", 3) == 0) {
            sess_disconnect(s, line);   /* 服务器拒绝/认证失败 */
            return;
        }
        if (s->diag_flags & HF_AUTH_SENT) {
            /* 认证后的任意 # 消息（#TM 欢迎 / #AA 回显 / #SB）= 认证通过 */
            handshake_online(s);
            return;
        }
        if (strncmp(line, "#AA", 3) == 0) {
            handshake_send_auth(s);
            return;
        }
        /* #TM 欢迎词（MOTD）等：问候的一部分，继续读 */
        return;
    }
    if (line[0] == '$') {
        if (strncmp(line, "$DI", 3) == 0) {
            s->diag_flags |= HF_DI_SEEN;
            return;
        }
        if (strncmp(line, "$CR", 3) == 0 || strncmp(line, "$PC", 3) == 0) {
            if (s->diag_flags & HF_AUTH_SENT)
                handshake_online(s);
            return;
        }
        return;   /* 其他 $ 消息（$ZC 等）握手期忽略 */
    }
    /* 其他内容：认证已发则视为通过（legacy 服务器行为宽容，与 Python 一致） */
    if (s->diag_flags & HF_AUTH_SENT)
        handshake_online(s);
}

/* ── 数据接收 ── */

static void traffic_upsert_full(sess_t *s, const char *cs, double lat,
                                double lon, int alt_ft, int gs_kts,
                                float heading_deg, bool on_ground)
{
    size_t slot = s->n_traffic;
    for (size_t i = 0; i < s->n_traffic; i++) {
        if (s->traffic[i].used
            && strcmp(s->traffic[i].callsign, cs) == 0) {
            slot = i;
            break;
        }
    }
    if (slot >= SESS_MAX_TRAFFIC)
        slot = 0;   /* 表满：覆盖最旧（数组头） */

    sess_traffic_t *t = &s->traffic[slot];
    strncpy(t->callsign, cs, sizeof(t->callsign) - 1);
    t->callsign[sizeof(t->callsign) - 1] = '\0';
    t->lat = lat;
    t->lon = lon;
    t->alt_ft = alt_ft;
    t->gs_kts = gs_kts;
    t->heading_deg = heading_deg;
    t->on_ground = on_ground;
    t->used = true;
    if (slot == s->n_traffic && s->n_traffic < SESS_MAX_TRAFFIC)
        s->n_traffic++;
}

static void traffic_remove(sess_t *s, const char *callsign)
{
    for (size_t i = 0; i < s->n_traffic; i++) {
        if (s->traffic[i].used
            && strcmp(s->traffic[i].callsign, callsign) == 0) {
            memmove(&s->traffic[i], &s->traffic[i + 1],
                    (s->n_traffic - i - 1) * sizeof(sess_traffic_t));
            s->n_traffic--;
            return;
        }
    }
}

/* 呼号归一化：alnum_only=false 去首尾空白+大写；true 只保留字母数字+大写 */
static void norm_cs(const char *in, char *out, size_t cap, bool alnum_only)
{
    size_t j = 0;
    if (!alnum_only) {
        const char *b = in, *e = in + strlen(in);
        while (b < e && (*b == ' ' || *b == '\t')) b++;
        while (e > b && (e[-1] == ' ' || e[-1] == '\t')) e--;
        for (const char *p = b; p < e && j < cap - 1; p++)
            out[j++] = (char)toupper((unsigned char)*p);
    } else {
        for (const char *p = in; *p && j < cap - 1; p++)
            if (isalnum((unsigned char)*p))
                out[j++] = (char)toupper((unsigned char)*p);
    }
    out[j] = '\0';
}

static bool is_own_callsign(const sess_t *s, const char *callsign)
{
    if (!callsign[0] || !s->callsign[0])
        return false;
    /* Python _is_own_callsign：strip+大写直比；不等则双方仅留字母数字再比
     *（容忍 "@TST123"、" TST123 " 包装；TST123 与 TST1234 不误判） */
    char a[32], b[32];
    norm_cs(s->callsign, a, sizeof(a), false);
    norm_cs(callsign, b, sizeof(b), false);
    if (a[0] && strcmp(a, b) == 0)
        return true;
    norm_cs(s->callsign, a, sizeof(a), true);
    norm_cs(callsign, b, sizeof(b), true);
    return a[0] && strcmp(a, b) == 0;
}

static void online_line(sess_t *s, const char *line)
{
    switch (fsd_classify(line)) {
    case FSD_MSG_TM: {
        fsd_tm_fields tm;
        if (fsd_parse_tm(line, &tm) == 0 && s->on_tm)
            s->on_tm(s->ud, tm.from, tm.to, tm.text);
        break;
    }
    case FSD_MSG_SB: {
        const char *text = (line[3]) ? line + 3 : "";
        if (s->on_tm)
            s->on_tm(s->ud, "SERVER", "BROADCAST", text);
        break;
    }
    case FSD_MSG_PILOT_POS: {
        fsd_pilot_position p;
        if (fsd_parse_position(line, &p) != 0)
            break;
        if (is_own_callsign(s, p.callsign))
            break;   /* 服务器回传自身 */
        traffic_upsert_full(s, p.callsign, p.lat, p.lon, p.alt_ft,
                            p.gs_kts, p.heading_deg, p.on_ground);
        if (s->on_traffic)
            s->on_traffic(s->ud, s->traffic, s->n_traffic);
        break;
    }
    case FSD_MSG_ATC_POS: {
        /* #AP：呼号/机型/高度（无坐标——app_nearby_count 的 haversine
         * 天然跳过零坐标项，Python 同语义） */
        fsd_atc_pos ap;
        if (fsd_parse_atc_pos(line, &ap) != 0)
            break;
        if (is_own_callsign(s, ap.callsign))
            break;   /* 服务器回传自身 */
        traffic_upsert_full(s, ap.callsign, 0.0, 0.0,
                            ap.has_alt ? ap.alt_ft : 0, 0, 0.0f, false);
        if (s->on_traffic)
            s->on_traffic(s->ud, s->traffic, s->n_traffic);
        break;
    }
    case FSD_MSG_DP:
        traffic_remove(s, line + 3);
        if (s->on_traffic)
            s->on_traffic(s->ud, s->traffic, s->n_traffic);
        break;
    case FSD_MSG_CQ: {
        /* $CQ<sender>:<receiver>:<qtype> —— CAPS 必答，否则被踢（Swift 兼容） */
        const char *body = line + 3;
        const char *c1 = strchr(body, ':');
        const char *c2 = c1 ? strchr(c1 + 1, ':') : NULL;
        if (c1 && c2 && strncmp(c2 + 1, "CAPS", 4) == 0) {
            char reply[FSD_MAX_LINE];
            int n = _snprintf(reply, sizeof(reply) - 1,
                              "$CR%s:%.*s:CAPS:%s",
                              s->callsign, (int)(c2 - (c1 + 1)), c1 + 1,
                              CAPS_REPLY);
            reply[n] = '\0';
            sess_send_raw(s, reply);
        }
        break;
    }
    case FSD_MSG_PI: {
        /* $PI<sender>:<receiver> → $PO<me>:<sender> */
        const char *body = line + 3;
        const char *c1 = strchr(body, ':');
        if (c1) {
            char reply[96];
            int n = _snprintf(reply, sizeof(reply) - 1, "$PO%s:%.*s",
                              s->callsign, (int)(c1 - body), body);
            reply[n] = '\0';
            sess_send_raw(s, reply);
        }
        break;
    }
    case FSD_MSG_ZC: {
        /* VATSIMAuth 挑战：空响应防超时（与 Python 策略一致） */
        const char *body = line + 3;
        const char *c1 = strchr(body, ':');
        const char *c2 = c1 ? strchr(c1 + 1, ':') : NULL;
        if (c1 && c2) {
            char reply[128];
            int n = _snprintf(reply, sizeof(reply) - 1,
                              "$ZR%s:%.*s:00000000000000000000000000000000",
                              s->callsign, (int)(c2 - (c1 + 1)), c1 + 1);
            reply[n] = '\0';
            sess_send_raw(s, reply);
        }
        break;
    }
    case FSD_MSG_ERROR:
        sess_disconnect(s, line);
        break;
    default:
        break;   /* #AP/#AA/#PC/$DI/#DL 等被动消息：忽略 */
    }
}

void sess_on_readable(sess_t *s)
{
    if (s->sock == INVALID_SOCKET)
        return;

    char chunk[8192];
    int got_lines = 0;

    for (;;) {
        int n = recv(s->sock, chunk, sizeof(chunk), 0);
        if (n == 0) {
            sess_disconnect(s, "连接已断开（服务器关闭）");
            return;
        }
        if (n < 0) {
            int err = WSAGetLastError();
            if (err == WSAEWOULDBLOCK)
                break;   /* 数据读完了 */
            sess_disconnect(s, "接收错误");
            return;
        }
        s->last_recv = net_now();

        size_t off = 0;
        while (off < (size_t)n) {
            size_t consumed = 0;
            int rc = fsd_linebuf_feed(&s->lb, chunk + off, (size_t)n - off,
                                      &consumed, s->scratch, sizeof(s->scratch));
            off += consumed;
            if (rc == 1) {
                got_lines++;
                if (s->state == SESS_HANDSHAKE)
                    handshake_line(s, s->scratch);
                else if (s->state == SESS_ONLINE)
                    online_line(s, s->scratch);
                if (s->state == SESS_DISCONNECTED)
                    return;   /* 行内触发了断开 */
            } else if (rc < 0 && rc != -1) {
                sess_disconnect(s, "协议帧错误");
                return;
            }
            if (consumed == 0)
                break;
        }
    }

    /* 握手期读完一批问候且认证未发（无 #AA）：按 Python 静默语义，
     * 1s 问候静默后发送认证（sess_tick 到点触发） */
    if (got_lines && s->state == SESS_HANDSHAKE
        && !(s->diag_flags & HF_AUTH_SENT))
        s->deadline = net_now() + 1.0;
}

void sess_on_writable(sess_t *s)
{
    if (s->state != SESS_CONNECTING)
        return;
    if (net_connect_finished(s->sock) != 0) {
        char msg[160];
        _snprintf(msg, sizeof(msg) - 1, "连接失败 %s:%d", s->server, s->port);
        msg[sizeof(msg) - 1] = '\0';
        sess_disconnect(s, msg);
        return;
    }
    /* TCP 建立 → 读问候（服务器先说话：$DI / #AA / #TM） */
    set_state(s, SESS_HANDSHAKE, "TCP 已建立，等待服务器问候...");
    s->deadline = net_now() + GREETING_TIMEOUT + 2.0;
}

/* ── 周期驱动 ── */

void sess_tick(sess_t *s, double now)
{
    if (s->state == SESS_CONNECTING) {
        if (now >= s->deadline)
            sess_disconnect(s, "连接超时");
        return;
    }

    if (s->state == SESS_HANDSHAKE) {
        if (s->diag_flags & HF_AUTH_SENT) {
            /* 等认证响应：超时按 dialect 处理（与 Python 一致） */
            if (now >= s->deadline) {
                if (s->vatsim)
                    sess_disconnect(s, "认证超时：服务器无响应");
                else
                    handshake_online(s);   /* legacy：静默即通过 */
            }
            return;
        }
        /* 等问候：静默超过问候窗口也照发认证（Python 3s 静默即发） */
        if (now >= s->deadline)
            handshake_send_auth(s);
        return;
    }

    if (s->state != SESS_ONLINE)
        return;

    /* 读超时：长时间无下行数据判死（Python recv_loop 同语义同文案） */
    if (now - s->last_recv >= READ_TIMEOUT) {
        sess_disconnect(s, "连接超时：长时间无数据，服务器可能已关闭连接");
        return;
    }

    /* keepalive：legacy 用 #TM 空跳；VATSIM 用缓存位置重发 @（防跳变） */
    if (now >= s->next_keepalive) {
        s->next_keepalive = now + KEEPALIVE_INTERVAL;
        char line[FSD_MAX_LINE];
        if (!s->vatsim) {
            _snprintf(line, sizeof(line) - 1, "#TM%s:SERVER:@", s->callsign);
            line[sizeof(line) - 1] = '\0';
        } else {
            fsd_position_args p;
            memset(&p, 0, sizeof(p));
            p.callsign = s->callsign;
            p.xpdr = s->last_xpdr;
            p.rating = s->rating;
            p.lat = s->has_position ? s->last_lat : s->init_lat;
            p.lon = s->has_position ? s->last_lon : s->init_lon;
            p.alt_ft = s->last_alt_ft;
            p.gs_kts = s->last_gs;
            p.heading_deg = 0.0f;
            p.mode_letter = s->last_mode;
            uint32_t pbh = s->has_position ? s->last_pbh : 0;
            int n = _snprintf(line, sizeof(line) - 1,
                              "@%c:%s:%s:%d:%.5f:%.5f:%d:%d:%u:0",
                              p.mode_letter, p.callsign, p.xpdr, p.rating,
                              p.lat, p.lon, p.alt_ft, p.gs_kts,
                              (unsigned)pbh);
            line[n] = '\0';
        }
        /* 发送失败视为连接中断（Python drain 抛异常退出循环） */
        if (sess_send_raw(s, line) != 0) {
            sess_disconnect(s, "发送失败（连接中断）");
            return;
        }
    }
}

void sess_collect_fds(const sess_t *s, fd_set *r, fd_set *w, int *max)
{
    (void)max;   /* Windows select 忽略 nfds */
    if (s->sock == INVALID_SOCKET)
        return;
    FD_SET(s->sock, r);
    if (s->state == SESS_CONNECTING)
        FD_SET(s->sock, w);
}

/* ── 在线发送 ── */

int sess_send_position(sess_t *s, double lat, double lon, int alt_ft,
                       int gs_kts, float heading_deg, const char *xpdr,
                       char mode_letter)
{
    if (s->state != SESS_ONLINE)
        return 1;
    if (mode_letter != 'S' && xpdr && strcmp(xpdr, "0000") == 0)
        return 1;   /* STBY 不上报（ATC 看不到） */

    /* 零坐标防护：用 last-valid 兜底（Python 基准行为） */
    if (fabs(lat) < 0.0001 && fabs(lon) < 0.0001) {
        if (!s->has_position)
            return 1;
        lat = s->last_lat;
        lon = s->last_lon;
    }

    fsd_position_args p;
    memset(&p, 0, sizeof(p));
    p.callsign = s->callsign;
    p.xpdr = xpdr;
    p.rating = s->rating;
    p.lat = lat;
    p.lon = lon;
    p.alt_ft = alt_ft;
    p.gs_kts = gs_kts;
    p.heading_deg = heading_deg;
    p.mode_letter = mode_letter;

    char line[FSD_MAX_LINE];
    if (fsd_build_position(line, sizeof(line), &p) != 0)
        return -1;
    if (sess_send_raw(s, line) != 0)
        return -1;

    /* 缓存供 keepalive 复用 */
    s->last_lat = lat;
    s->last_lon = lon;
    s->last_alt_ft = alt_ft;
    s->last_gs = gs_kts;
    s->last_pbh = fsd_pack_pbh(0.0f, 0.0f, heading_deg, false);
    strncpy(s->last_xpdr, xpdr ? xpdr : "7000", sizeof(s->last_xpdr) - 1);
    s->last_xpdr[sizeof(s->last_xpdr) - 1] = '\0';
    s->last_mode = mode_letter;
    s->has_position = true;
    return 0;
}

int sess_send_tm(sess_t *s, const char *dest, const char *text)
{
    if (s->state != SESS_ONLINE)
        return -1;
    char line[FSD_MAX_LINE];
    if (fsd_build_tm(line, sizeof(line), s->callsign, dest, text) != 0)
        return -1;
    return sess_send_raw(s, line) == 0 ? 0 : -1;
}

int sess_send_flightplan(sess_t *s, const sess_fp_t *fp)
{
    if (s->state != SESS_ONLINE)
        return -1;

    /* 规范化先行：大写/去空白/时间数字化（Python 顺序——先大写
     * 再判完整格式，"h/b772/l" 才能正确直用） */
    fsd_plan_norm norm;
    fsd_normalize_plan(fp, &norm);

    char line[FSD_MAX_LINE];
    int n;

    /* 机型字段：完整格式直用；VATSIM 简化 ICAO；legacy 用 FAA H/B772/L */
    char aircraft[56];
    if (strchr(norm.aircraft, '/')) {
        _snprintf(aircraft, sizeof(aircraft) - 1, "%s", norm.aircraft);
    } else if (s->vatsim) {
        char wl = (norm.wake[0] == 'L') ? 'L' : (norm.wake[0] == 'H') ? 'H'
                : (norm.wake[0] == 'S') ? 'J' : 'M';
        _snprintf(aircraft, sizeof(aircraft) - 1, "%s/%c", norm.aircraft, wl);
    } else {
        const char *prefix = (norm.wake[0] == 'H') ? "H/"
                           : (norm.wake[0] == 'S') ? "J/" : "";
        _snprintf(aircraft, sizeof(aircraft) - 1, "%s%s/L", prefix,
                  norm.aircraft);
    }
    aircraft[sizeof(aircraft) - 1] = '\0';

    /* OPR/<pilot> 追加到备注（Python 基准） */
    char remarks_full[288];
    if (norm.pilot[0] && !strstr(norm.remarks, "OPR/")) {
        _snprintf(remarks_full, sizeof(remarks_full) - 1,
                  norm.remarks[0] ? "OPR/%s %s" : "OPR/%s",
                  norm.pilot, norm.remarks);
    } else {
        _snprintf(remarks_full, sizeof(remarks_full) - 1, "%s",
                  norm.remarks);
    }
    remarks_full[sizeof(remarks_full) - 1] = '\0';

    n = _snprintf(line, sizeof(line) - 1,
                  "$FP%s:SERVER:%c:%s:%s:%s:%s:%s:%s:%s:%s:%s:%s:%s:%s:%s:%s",
                  s->callsign,
                  norm.type,
                  aircraft,
                  norm.tas,
                  norm.dep,
                  norm.dep_time, norm.actual_dep,
                  norm.cruise_alt,
                  norm.dest,
                  norm.eet_h, norm.eet_m, norm.fuel_h, norm.fuel_m,
                  norm.altn,
                  remarks_full,
                  norm.route);
    line[n] = '\0';
    return sess_send_raw(s, line) == 0 ? 0 : -1;
}

void sess_set_initial_position(sess_t *s, double lat, double lon, int alt_ft)
{
    char line[FSD_MAX_LINE];
    int n = _snprintf(line, sizeof(line) - 1,
                      "@N:%s:1200:%d:%.5f:%.5f:%d:0:0:0",
                      s->callsign, s->rating, lat, lon, alt_ft);
    line[n] = '\0';
    sess_send_raw(s, line);

    s->last_lat = lat;
    s->last_lon = lon;
    s->last_alt_ft = alt_ft;
    s->last_gs = 0;
    s->last_pbh = 0;
    strcpy(s->last_xpdr, "1200");
    s->last_mode = 'N';
    s->has_position = true;
}
