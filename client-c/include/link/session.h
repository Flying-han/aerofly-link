/**
 * session.h —— FSD 会话状态机（连接 → 握手 → 在线）
 * ================================================================
 * 行为基准：Python core/fsd_client.py 的 connect()/接收分发/keepalive
 * （docs/C_REWRITE_PLAN.md §5）。单线程非阻塞驱动：
 *
 *   sess_connect()      发起非阻塞连接
 *   sess_on_event()     select 就绪时喂入（可读/可写）
 *   sess_tick()         周期调用：握手超时、keepalive
 *   sess_send_*()       在线期间的主动发送
 *
 * 与 Python 的差异（有意为之）：
 *   - 不自动重连 FSD（与 Python 一致；DLL 桥接才有自动重连）
 *   - traffic 表定长（MAX_TRAFFIC=64），超出丢弃最旧
 *   - on_traffic 每包触发、不做 Python 的 1/s 节流（当前 UI 仅定时
 *     取 nearby 数，无逐帧消费方）
 */
#ifndef LINK_SESSION_H
#define LINK_SESSION_H

#include "link/frame.h"
#include "link/message.h"
#include "link/net.h"

#define SESS_MAX_TRAFFIC 64

typedef enum {
    SESS_DISCONNECTED = 0,
    SESS_CONNECTING,     /* TCP 连接进行中 */
    SESS_HANDSHAKE,      /* 问候 + 认证交换 */
    SESS_ONLINE
} sess_state_t;

/* 其他飞机（traffic 表项） */
typedef struct {
    char    callsign[16];
    double  lat, lon;
    int     alt_ft, gs_kts;
    float   heading_deg;
    bool    on_ground;
    bool    used;
} sess_traffic_t;

/* 飞行计划字段（对应 Python send_flight_plan 的 plan 字典） */
typedef struct {
    const char *type;          /* I/V/S/D 首字母自动取 */
    const char *aircraft;      /* B738 或完整 H/B772/L */
    const char *wake;          /* Light|Medium|Heavy|Super */
    const char *tas;
    const char *dep, *dest, *altn;
    const char *dep_time, *actual_dep_time;
    const char *cruise_alt;
    const char *route, *remarks, *pilot;
    const char *eet, *endurance;
} sess_fp_t;

typedef struct {
    /* 必需配置（由调用方在 connect 前填好） */
    char callsign[16];
    char cid[32];
    char password[64];
    char realname[64];
    char server[128];
    int  port;
    int  rating;
    bool vatsim;              /* true=VATSIM dialect(rev100+$ID)，false=legacy(rev9) */
    double init_lat, init_lon;
    int  init_alt_ft;

    /* 回调（均可为 NULL） */
    void *ud;
    void (*on_status)(void *ud, sess_state_t st, const char *msg);
    void (*on_tm)(void *ud, const char *from, const char *to, const char *text);
    void (*on_traffic)(void *ud, const sess_traffic_t *list, size_t n);

    /* 运行时（sess_* 内部维护） */
    sess_state_t    state;
    SOCKET          sock;
    fsd_linebuf     lb;
    char            scratch[FSD_MAX_LINE];   /* feed 输出复用 */
    double          deadline;                /* 当前阶段超时 */
    double          last_recv;               /* 最近一次收到下行数据的时刻（读超时用） */
    double          next_keepalive;
    unsigned         diag_flags;             /* 握手期标志（$DI seen 等） */

    /* 位置缓存：keepalive 复用最后上报值，避免 0 值跳变（Python 基准行为） */
    double   last_lat, last_lon;
    int      last_alt_ft, last_gs;
    uint32_t last_pbh;
    char     last_xpdr[8];
    char     last_mode;
    bool     has_position;

    sess_traffic_t traffic[SESS_MAX_TRAFFIC];
    size_t         n_traffic;
} sess_t;

void sess_init(sess_t *s);

/* 填好配置后调用；立即返回，结果经 on_status 回调。
 * 0=已发起，-1=参数错误或解析失败。 */
int sess_connect(sess_t *s);

/* 主动断开（发送 #DP 后关闭）。 */
void sess_disconnect(sess_t *s, const char *reason);

/* 事件驱动（由 app 的 select 循环调用） */
void sess_on_readable(sess_t *s);
void sess_on_writable(sess_t *s);

/* 周期驱动：握手超时与 keepalive。now = net_now()。 */
void sess_tick(sess_t *s, double now);

/* select 前收集 fd：把会话 socket 计入 *max（如在线/连接中） */
void sess_collect_fds(const sess_t *s, fd_set *r, fd_set *w, int *max);

/* ── 在线期间发送 ── */

/* 位置报告。zero 坐标防护与 last-valid 缓存为内部行为（对齐 Python）。
 * 返回 0=已发送，1=已跳过（未在线/STBY/零坐标），-1=发送失败。 */
int sess_send_position(sess_t *s, double lat, double lon, int alt_ft,
                       int gs_kts, float heading_deg, const char *xpdr,
                       char mode_letter);

int sess_send_tm(sess_t *s, const char *dest, const char *text);
int sess_send_flightplan(sess_t *s, const sess_fp_t *fp);

/* 供 app：keepalive 复用的位置缓存写入（遥测不可用时的初始位置） */
void sess_set_initial_position(sess_t *s, double lat, double lon, int alt_ft);

#endif /* LINK_SESSION_H */
