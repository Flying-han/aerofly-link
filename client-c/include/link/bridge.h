/**
 * bridge.h —— AeroflyBridge.dll 桥接客户端
 * ================================================================
 * 对接外部开源 AeroflyBridge.dll（ADR 0002，扁平 JSON 契约）：
 *   12345 遥测：DLL 持续推送 JSON 行（弧度/米/Hz 原始单位），
 *               本层做 VAR_MAP 子集的解析与单位转换（对齐 core/dll_bridge.py）
 *   12346 命令：短连接 {"variable":...,"value":...}，每条独立建连
 *
 * 断线自动重连：指数退避（2s 起，封顶 30s，ADR 0005 空闲静默预算）。
 */
#ifndef LINK_BRIDGE_H
#define LINK_BRIDGE_H

#include "link/frame.h"
#include "link/net.h"

/* 遥测快照（客户端实际消费的字段子集；Python Telemetry 的 VAR_MAP 子集） */
typedef struct {
    double lat, lon;          /* 度（弧度输入已转换） */
    double alt_m;             /* 米 */
    double agl_m;
    double hdg_true;          /* 罗盘度 North=0 顺时针（弧度数学约定已转换） */
    double ias_kts, gs_kts;
    double vs_fpm;
    int    com1_khz10, com2_khz10;
    char   xpdr_code[8];
    bool   on_ground;
    char   model[32];
    double timestamp;
} telem_t;

typedef struct {
    /* 运行时 */
    SOCKET       tel_sock;
    bool         tel_connected;
    fsd_linebuf  lb;
    char         scratch[FSD_MAX_LINE];

    /* 重连（指数退避） */
    double   next_retry;      /* 0 = 立即可试 */
    int      fails;
    bool     want_connected;  /* stop 后不再重连 */

    /* 最新遥测 */
    telem_t  last;
    bool     has_telem;

    /* 回调（均可 NULL）：每帧有效遥测 / 连接状态变化 */
    void *ud;
    void (*on_telem)(void *ud, const telem_t *t);
    void (*on_conn)(void *ud, bool connected);
} bridge_t;

void br_init(bridge_t *b);

/* 开始后台连接（自动重连）。host 固定 127.0.0.1。 */
void br_start(bridge_t *b);
void br_stop(bridge_t *b);

void br_on_readable(bridge_t *b);
void br_tick(bridge_t *b, double now);
void br_collect_fds(const bridge_t *b, fd_set *r, int *max);

/* 命令端口短连接。num_val 与 str_val 二选一（另一个传 false/NULL）。
 * 返回 0 成功（resp 为服务器响应行，可为 NULL），-1 失败。
 * 阻塞上限约 1s（低频操作：写 squawk 等手动操作）。 */
int br_send_command(bridge_t *b, const char *variable,
                    bool has_num, double num_val,
                    const char *str_val, char *resp, size_t resp_cap);

#endif /* LINK_BRIDGE_H */
