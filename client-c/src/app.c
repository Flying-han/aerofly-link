/* app.c —— 应用编排器实现（基准：main_window.py + transponder_controller.py） */
#include "link/app.h"
#include "link/protocol.h"

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define REPORT_INTERVAL 1.0
#define SYNC_INTERVAL   5.0
#define IDENT_DURATION  5.0

static void app_logk(app_t *a, app_log_kind_t kind, const char *fmt, ...)
{
    if (!a->on_log)
        return;
    char line[APP_LOG_MAX];
    va_list ap;
    va_start(ap, fmt);
    _vsnprintf(line, sizeof(line) - 1, fmt, ap);
    line[sizeof(line) - 1] = '\0';
    va_end(ap);
    a->on_log(a->ud, kind, line);
}

/* 系统行简写（绝大多数日志为 SYS 类） */
static void app_log(app_t *a, const char *fmt, ...)
{
    va_list ap;
    char line[APP_LOG_MAX];
    va_start(ap, fmt);
    _vsnprintf(line, sizeof(line) - 1, fmt, ap);
    line[sizeof(line) - 1] = '\0';
    va_end(ap);
    app_logk(a, APP_LOG_SYS, "%s", line);
}

/* ── 回调桥：各模块 → app → UI ── */

static void sess_status_cb(void *ud, sess_state_t st, const char *msg)
{
    app_t *a = (app_t *)ud;
    static const char *names[] = { "未连接", "连接中", "认证中", "已连接" };
    app_log(a, "[FSD] %s", msg);
    if (a->on_status) {
        char line[192];
        _snprintf(line, sizeof(line) - 1, "%s | %s",
                  (st == SESS_ONLINE) ? "● 已连接" : names[st], msg);
        line[sizeof(line) - 1] = '\0';
        a->on_status(a->ud, line);
    }
}

static void sess_tm_cb(void *ud, const char *from, const char *to, const char *text)
{
    app_t *a = (app_t *)ud;
    app_logk(a, APP_LOG_IN, "%s → %s: %s", from, to, text);
}

static void sess_debug_cb(void *ud, const char *line)
{
    app_t *a = (app_t *)ud;
    if (a->on_debug)
        a->on_debug(a->ud, line);
}

static void bridge_conn_cb(void *ud, bool connected)
{
    app_t *a = (app_t *)ud;
    app_log(a, "[DLL] 遥测端口%s", connected ? "已连接" : "已断开（自动重连）");
}

static int dll_write_mode_cb(void *ud, bool alt)
{
    app_t *a = (app_t *)ud;
    char resp[128];
    int rc = br_send_command(&a->bridge, "set_xpdr_mode", false, 0,
                             alt ? "ALT" : "STBY", resp, sizeof(resp));
    /* AeroflyBridge 不支持模式写入：unsupported/错误都视为降级 */
    return (rc == 0 && strstr(resp, "\"ok\"")) ? 0 : -1;
}

static int dll_write_code_cb(void *ud, const char *code)
{
    app_t *a = (app_t *)ud;
    double num = (double)atoi(code);
    char resp[128];
    int rc = br_send_command(&a->bridge, "Communication.TransponderCode",
                             true, num, NULL, resp, sizeof(resp));
    return (rc == 0 && strstr(resp, "\"ok\"")) ? 0 : -1;
}

void app_init(app_t *a, const cfg_t *cfg)
{
    memset(a, 0, sizeof(*a));
    a->cfg = *cfg;

    sess_init(&a->sess);
    br_init(&a->bridge);
    xpdr_init(&a->xpdr);

    /* 会话配置 */
    strncpy(a->sess.callsign, cfg->callsign, sizeof(a->sess.callsign) - 1);
    strncpy(a->sess.cid, cfg->cid, sizeof(a->sess.cid) - 1);
    strncpy(a->sess.realname, cfg->realname, sizeof(a->sess.realname) - 1);
    strncpy(a->sess.server, cfg->server, sizeof(a->sess.server) - 1);
    a->sess.port = cfg->port;
    a->sess.rating = cfg->rating;
    a->sess.vatsim = (strcmp(cfg->type, "vatsim") == 0);
    /* CLI 路径：密码可从 cfg（内存中）带入；GUI 路径用 app_set_password */
    strncpy(a->sess.password, cfg->password, sizeof(a->sess.password) - 1);
    a->sess.password[sizeof(a->sess.password) - 1] = '\0';

    /* 模拟初始位置（mock_alt 为米 → 英尺；对齐 Python _init_alt_m 转换） */
    a->sess.init_lat = (cfg->mock_lat[0]) ? atof(cfg->mock_lat) : 51.4775;
    a->sess.init_lon = (cfg->mock_lon[0]) ? atof(cfg->mock_lon) : -0.4614;
    double alt_m = (cfg->mock_alt[0]) ? atof(cfg->mock_alt) : 3500.0;
    a->sess.init_alt_ft = (int)(alt_m * 3.28084);

    /* 回调接线 */
    a->sess.ud = a;
    a->sess.on_status = sess_status_cb;
    a->sess.on_tm = sess_tm_cb;
    a->sess.on_debug = sess_debug_cb;
    a->sess.trace = (getenv("AEROFLYLINK_DEBUG") != NULL);
    a->bridge.ud = a;
    a->bridge.on_conn = bridge_conn_cb;
}

/* 密码只经内存传递（GUI 输入；cfg_save 永不落盘，ADR 0003） */
void app_set_password(app_t *a, const char *pw)
{
    strncpy(a->sess.password, pw ? pw : "", sizeof(a->sess.password) - 1);
    a->sess.password[sizeof(a->sess.password) - 1] = '\0';
}

void app_connect_fsd(app_t *a)
{
    br_start(&a->bridge);           /* 遥测桥自动重连（幂等） */
    sess_connect(&a->sess);
}

void app_disconnect_fsd(app_t *a)
{
    sess_disconnect(&a->sess, "已断开连接");
}

const char *app_toggle_mock(app_t *a, bool on)
{
    if (on == a->mock_on)
        return NULL;
    if (on) {
        if (mocksrv_start(&a->mock) != 0)
            return "模拟DLL 端口 12345/12346 被占用（可能已开启游戏或另一实例）";
        a->mock.center_lat = a->sess.init_lat;
        a->mock.center_lon = a->sess.init_lon;
        /* Mock 高度用米（与遥测单位一致）；cfg.mock_alt 即米 */
        a->mock.alt_m = (a->cfg.mock_alt[0]) ? atof(a->cfg.mock_alt) : 3500.0;
        a->mock_on = true;
        br_stop(&a->bridge);       /* 让位给 Mock，稍后桥会连上 Mock 端口 */
        br_start(&a->bridge);
        app_log(a, "[Mock] 模拟 DLL 已启动（心形航线 @ %.4f,%.4f）",
             a->mock.center_lat, a->mock.center_lon);
    } else {
        mocksrv_stop(&a->mock);
        a->mock_on = false;
        br_stop(&a->bridge);
        br_start(&a->bridge);      /* 回到真实 DLL 重连循环 */
        app_log(a, "[Mock] 模拟 DLL 已停止");
    }
    return NULL;
}

/* ── UI 动作 ── */

int app_send_chat(app_t *a, const char *text)
{
    const char *body = text;
    char dest[64] = "UNICOM";
    char msg[384];

    if (text[0] == '@') {
        body = text + 1;
        const char *sp = strchr(body, ' ');
        if (sp) {
            size_t dl = (size_t)(sp - body);
            if (dl >= sizeof(dest)) dl = sizeof(dest) - 1;
            memcpy(dest, body, dl);
            dest[dl] = '\0';
            body = sp + 1;
        } else {
            body = text + 1;   /* 只有目标无内容 → 整体作为目标？拒绝 */
            return -1;
        }
    }
    strncpy(msg, body, sizeof(msg) - 1);
    msg[sizeof(msg) - 1] = '\0';
    if (!msg[0])
        return -1;

    int rc = sess_send_tm(&a->sess, dest, msg);
    if (rc == 0)
        app_logk(a, APP_LOG_OUT, "我 → %s: %s", dest, msg);
    else
        app_log(a, "[系统] 未连接到服务器，无法发送消息");
    return rc;
}

void app_set_xpdr_mode(app_t *a, bool alt)
{
    const char *warn = xpdr_set_mode(&a->xpdr, alt, dll_write_mode_cb, a);
    if (warn && a->on_warning)
        a->on_warning(a->ud, warn);
    if (a->on_xpdr)
        a->on_xpdr(a->ud);
}

void app_set_xpdr_code(app_t *a, const char *code)
{
    const char *warn = xpdr_set_code(&a->xpdr, code, dll_write_code_cb, a);
    if (warn && a->on_warning)
        a->on_warning(a->ud, warn);
    if (a->on_xpdr)
        a->on_xpdr(a->ud);
}

void app_ident(app_t *a)
{
    xpdr_ident(&a->xpdr, net_now(), IDENT_DURATION);
    if (a->on_xpdr)
        a->on_xpdr(a->ud);
}

int app_submit_flightplan(app_t *a)
{
    sess_fp_t fp;
    fp.type = a->fp_type;
    fp.aircraft = a->fp_aircraft_buf;
    fp.wake = a->fp_wake_buf;
    fp.tas = a->fp_tas_buf;
    fp.dep = a->fp_dep_buf;
    fp.dest = a->fp_dest_buf;
    fp.altn = a->fp_altn_buf;
    fp.dep_time = a->fp_dep_time_buf;
    fp.actual_dep_time = a->fp_act_buf;
    fp.cruise_alt = a->fp_cruise_buf;
    fp.route = a->fp_route_buf;
    fp.remarks = a->fp_remarks_buf;
    fp.pilot = a->cfg.realname;
    fp.eet = a->fp_eet_buf;
    fp.endurance = a->fp_endur_buf;
    int rc = sess_send_flightplan(&a->sess, &fp);
    app_log(a, rc == 0 ? "[系统] 飞行计划已提交" : "[系统] 飞行计划提交失败");
    return rc;
}

/* ── 周期任务 ── */

static void position_report(app_t *a, double now)
{
    if (now < a->t_next_report)
        return;
    a->t_next_report = now + REPORT_INTERVAL;

    if (a->sess.state != SESS_ONLINE)
        return;
    if (strcmp(xpdr_ap_code(&a->xpdr), "0000") == 0)
        return;   /* STBY：ATC 看不到，不上报 */

    const telem_t *t = a->bridge.has_telem ? &a->bridge.last : NULL;
    double lat, lon;
    int alt_ft;
    double gs, hdg;

    if (t) {
        lat = t->lat;
        lon = t->lon;
        alt_ft = (int)(t->alt_m * 3.28084);
        gs = t->gs_kts;
        hdg = t->hdg_true;
    } else {
        lat = a->sess.init_lat;
        lon = a->sess.init_lon;
        alt_ft = a->sess.init_alt_ft;
        gs = 0;
        hdg = 0;
    }
    sess_send_position(&a->sess, lat, lon, alt_ft, (int)gs,
                       (float)hdg, a->xpdr.squawk, xpdr_letter(&a->xpdr));
}

static void sync_check(app_t *a, double now)
{
    if (now < a->t_next_sync)
        return;
    a->t_next_sync = now + SYNC_INTERVAL;

    /* 仅代码比对（DLL 不暴露模式，见 ADR 0005）；遥测无效时跳过 */
    if (!a->bridge.has_telem)
        return;
    const char *warning = NULL;
    if (strcmp(a->bridge.last.xpdr_code, a->xpdr.squawk) != 0
        && strcmp(a->bridge.last.xpdr_code, "0000") != 0) {
        static char buf[160];
        _snprintf(buf, sizeof(buf) - 1,
                  "AFS4 应答机代码为 %s，客户端设置为 %s",
                  a->bridge.last.xpdr_code, a->xpdr.squawk);
        buf[sizeof(buf) - 1] = '\0';
        warning = buf;
    }
    /* 去重：同一警告持续期间不重复弹（Python 修复行为） */
    if (warning && a->on_warning && strcmp(warning, a->last_warning) != 0) {
        strncpy(a->last_warning, warning, sizeof(a->last_warning) - 1);
        a->last_warning[sizeof(a->last_warning) - 1] = '\0';
        a->on_warning(a->ud, warning);
    } else if (!warning) {
        a->last_warning[0] = '\0';
    }
}

void app_poll(app_t *a, int timeout_ms)
{
    double now = net_now();

    fd_set r, w;
    FD_ZERO(&r);
    FD_ZERO(&w);
    sess_collect_fds(&a->sess, &r, &w, NULL);
    br_collect_fds(&a->bridge, &r, &w, NULL);
    mocksrv_collect_fds(&a->mock, &r, NULL);

    struct timeval tv = { timeout_ms / 1000, (timeout_ms % 1000) * 1000 };
    select(0, &r, &w, NULL, &tv);

    if (a->sess.sock != INVALID_SOCKET) {
        if (FD_ISSET(a->sess.sock, &w))
            sess_on_writable(&a->sess);
        if (FD_ISSET(a->sess.sock, &r))
            sess_on_readable(&a->sess);
    }
    if (a->bridge.tel_sock != INVALID_SOCKET) {
        if (FD_ISSET(a->bridge.tel_sock, &w))
            br_on_writable(&a->bridge);
        if (FD_ISSET(a->bridge.tel_sock, &r) && a->bridge.tel_connected)
            br_on_readable(&a->bridge);
    }
    mocksrv_on_readable(&a->mock, &r);

    now = net_now();
    sess_tick(&a->sess, now);
    br_tick(&a->bridge, now);
    mocksrv_tick(&a->mock, now);
    position_report(a, now);
    sync_check(a, now);
    if (xpdr_tick(&a->xpdr, now) && a->on_xpdr)
        a->on_xpdr(a->ud);
}

/* ── 状态摘要 ── */

void app_status_text(const app_t *a, char *conn, size_t conn_cap,
                     char *xpdr_line, size_t xpdr_cap,
                     char *flight, size_t flight_cap,
                     char *dll, size_t dll_cap)
{
    static const char *conn_names[] = { "● 未连接", "● 连接中...",
                                        "● 认证中...", "● 已连接" };
    if (conn)
        _snprintf(conn, conn_cap - 1, "%s", conn_names[a->sess.state]);

    if (xpdr_line) {
        _snprintf(xpdr_line, xpdr_cap - 1, "应答机: %s %s%s",
                  a->xpdr.alt_mode ? "ALT" : "STBY", a->xpdr.squawk,
                  a->xpdr.ident ? " IDENT" : "");
    }
    if (flight) {
        if (a->bridge.has_telem)
            _snprintf(flight, flight_cap - 1, "高度: %.0fft  地速: %.0fkts",
                      a->bridge.last.alt_m * 3.28084, a->bridge.last.gs_kts);
        else
            _snprintf(flight, flight_cap - 1, "高度: ---ft  地速: ---kts");
    }
    if (dll) {
        if (a->mock_on)
            _snprintf(dll, dll_cap - 1, "Mock: %s",
                      a->bridge.tel_connected ? "● 已连" : "○ 启动");
        else if (a->bridge.tel_connected)
            _snprintf(dll, dll_cap - 1, "DLL: ● %s",
                      a->bridge.has_telem ? "已连" : "无数据");
        else
            _snprintf(dll, dll_cap - 1, "DLL: ○ 等待");
    }
}

int app_nearby_count(const app_t *a)
{
    if (!a->bridge.has_telem)
        return 0;
    if (a->bridge.last.lat == 0.0 && a->bridge.last.lon == 0.0)
        return 0;
    int n = 0;
    for (size_t i = 0; i < a->sess.n_traffic; i++) {
        const sess_traffic_t *t = &a->sess.traffic[i];
        if (t->used
            && fsd_distance_nm(a->bridge.last.lat, a->bridge.last.lon,
                               t->lat, t->lon) < 10.0)
            n++;
    }
    return n;
}
