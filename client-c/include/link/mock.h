/**
 * mock.h —— 内嵌模拟 AeroflyBridge.dll 服务器
 * ================================================================
 * 与 Python core/mock_server.py 行为一致（心形航线 10Hz 遥测 + 命令端口），
 * 供非正版 AFS4 / 无游戏环境的联机测试（主程序「模拟DLL」按钮）。
 * 输出格式与真实 DLL 完全兼容（扁平 JSON，弧度/米/Hz）。
 */
#ifndef LINK_MOCK_H
#define LINK_MOCK_H

#include "link/net.h"

#define MOCK_MAX_CLIENTS 4

typedef struct {
    SOCKET tel_listen;   /* 12345 */
    SOCKET cmd_listen;   /* 12346 */
    SOCKET tel_clients[MOCK_MAX_CLIENTS];
    int    n_tel;
    double t0;
    char   xpdr[8];
    bool   running;
    /* 模拟航线中心（度/米）；start 后可改，立即生效 */
    double center_lat, center_lon, alt_m;
} mocksrv_t;

/* 启动监听（端口被占用返回 -1，例如已开着游戏或另一个实例）。 */
int  mocksrv_start(mocksrv_t *m);
void mocksrv_stop(mocksrv_t *m);

/* select 前收集监听与客户端 fd。 */
void mocksrv_collect_fds(const mocksrv_t *m, fd_set *r, int *max);

/* select 就绪后调用：accept 新连接 / 读命令 / 推送遥测（内部 10Hz 节流）。 */
void mocksrv_on_readable(mocksrv_t *m, const fd_set *r);
void mocksrv_tick(mocksrv_t *m, double now);

#endif /* LINK_MOCK_H */
