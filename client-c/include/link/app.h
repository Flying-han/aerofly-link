/**
 * app.h —— 应用编排器：会话 + 桥接 + 应答机 + Mock + 定时器
 * ================================================================
 * 单线程驱动（GUI 的 WM_TIMER 或 CLI 的循环均可）：
 *   app_poll(&app, timeout_ms)   —— select 全部 fd 并驱动各模块 tick
 *
 * 行为基准（对齐 Python main_window.py）：
 *   - 位置上报 1Hz（仅 ONLINE 且 ALT 且代码非 0000；遥测无效时用初始位置）
 *   - keepalive 30s（session 内部）
 *   - 同步检查 5s（代码比对；DLL 不暴露模式故不做模式比对，见 ADR 0005）
 *   - IDENT 8s 自动清除
 */
#ifndef LINK_APP_H
#define LINK_APP_H

#include "link/bridge.h"
#include "link/config.h"
#include "link/mock.h"
#include "link/session.h"
#include "link/transponder.h"

#define APP_LOG_MAX 512

typedef struct {
    /* 输出回调（均可 NULL） */
    void *ud;
    void (*on_log)(void *ud, const char *line);                 /* 通讯/系统日志 */
    void (*on_debug)(void *ud, const char *line);               /* 协议原始行跟踪 */
    void (*on_status)(void *ud, const char *line);              /* 连接状态行 */
    void (*on_xpdr)(void *ud);                                  /* 应答机状态变化 */
    void (*on_warning)(void *ud, const char *msg);              /* 面板警告（已去重） */

    cfg_t     cfg;
    sess_t    sess;
    bridge_t  bridge;
    xpdr_t    xpdr;
    mocksrv_t mock;
    bool      mock_on;

    /* 飞行计划（UI 提交后暂存；save 时持久化） */
    sess_fp_t fp;
    char      fp_type[4], fp_aircraft_buf[8], fp_wake_buf[16], fp_tas_buf[8];
    char      fp_dep_buf[8], fp_dest_buf[8], fp_altn_buf[8], fp_cruise_buf[12];
    char      fp_route_buf[256], fp_remarks_buf[256], fp_pilot_buf[64];
    char      fp_eet_buf[8], fp_endur_buf[8], fp_dep_time_buf[8], fp_act_buf[8];

    /* 运行时 */
    double t_next_report;     /* 1Hz 位置上报 */
    double t_next_sync;       /* 5s 同步检查 */
    char   last_warning[256]; /* 警告去重（与 Python 修复一致） */
} app_t;

void app_init(app_t *a, const cfg_t *cfg);

/* 连接发起（读 cfg 连接参数）。 */
void app_connect_fsd(app_t *a);
void app_disconnect_fsd(app_t *a);

/* GUI 路径注入密码（仅内存；cfg_save 永不落盘，ADR 0003）。 */
void app_set_password(app_t *a, const char *pw);

/* 模拟 DLL 开关。 */
const char *app_toggle_mock(app_t *a, bool on);   /* 返回 NULL 或错误信息 */

/* UI 动作 */
int  app_send_chat(app_t *a, const char *text);   /* "@dest msg" 或缺省 UNICOM */
void app_set_xpdr_mode(app_t *a, bool alt);
void app_set_xpdr_code(app_t *a, const char *code);
void app_ident(app_t *a);
int  app_submit_flightplan(app_t *a);             /* 用暂存 fp 字段发送 */

/* 周期驱动：select 等待至多 timeout_ms 后驱动全部 tick。 */
void app_poll(app_t *a, int timeout_ms);

/* 状态摘要（状态栏文本）：连接/应答机/高度速度/DLL。各缓冲独立。 */
void app_status_text(const app_t *a, char *conn, size_t conn_cap,
                     char *xpdr_line, size_t xpdr_cap,
                     char *flight, size_t flight_cap,
                     char *dll, size_t dll_cap);

/* 附近飞机数（<10nm，自身遥测有效时才统计；对齐 Python 修复）。 */
int  app_nearby_count(const app_t *a);

#endif /* LINK_APP_H */
