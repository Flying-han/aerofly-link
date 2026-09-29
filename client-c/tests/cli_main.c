/*
 * cli_main.c —— Aerofly Link 无头客户端（P2）
 * ================================================================
 * 功能与 Python 版对齐（除 GUI 外全量）：FSD 连接认证、1Hz 位置上报、
 * keepalive、应答机双轨制、飞行计划、文本通讯、内嵌模拟 DLL、配置持久化。
 *
 * 用法::
 *   aeroflylink-cli.exe                       # 读 %APPDATA%/AeroflyLink/settings.json
 *   aeroflylink-cli.exe --password XXX        # 密码仅命令行传入（不落盘）
 *   aeroflylink-cli.exe --mock                # 无游戏环境（内嵌模拟 DLL）
 *   aeroflylink-cli.exe --server 127.0.0.1:6809 --callsign CES2101
 *
 * 控制台命令（输入行）：
 *   @呼号 消息     发送 #TM（缺省目标 UNICOM）
 *   /fp            提交暂存飞行计划（配置中的 fp_* 字段）
 *   /ident         触发 IDENT
 *   /stby /alt     应答机模式切换
 *   /squawk 7000   设置代码
 *   /quit          退出
 */
#include "link/app.h"

#include <stdio.h>
#include <string.h>
#include <windows.h>

static volatile BOOL g_run = TRUE;

/* stdin 线程 → 主循环 的整行投递（仅进程内交互） */
typedef struct {
    char buf[512];
    volatile int ready;
} cli_stdin_ctx_t;

static DWORD WINAPI cli_stdin_thread(LPVOID param)
{
    cli_stdin_ctx_t *ictx = (cli_stdin_ctx_t *)param;
    char line[512];
    while (g_run) {
        if (!fgets(line, sizeof(line), stdin))
            break;
        line[strcspn(line, "\r\n")] = '\0';
        memcpy(ictx->buf, line, sizeof(ictx->buf));
        ictx->buf[sizeof(ictx->buf) - 1] = '\0';
        ictx->ready = 1;
        while (ictx->ready && g_run)
            Sleep(10);
    }
    return 0;
}

static BOOL WINAPI ctrl_handler(DWORD ev)
{
    (void)ev;
    g_run = FALSE;
    return TRUE;
}

static void console_log(void *ud, app_log_kind_t kind, const char *line)
{
    (void)ud; (void)kind;
    printf("%s\n", line);
}

static void console_warning(void *ud, const char *msg)
{
    (void)ud;
    printf("[警告] %s\n", msg);
}

/* 协议原始行跟踪（AEROFLYLINK_DEBUG 开启时由 app 接线）→ stderr */
static void console_debug(void *ud, const char *line)
{
    (void)ud;
    fprintf(stderr, "TRACE %s\n", line);
}

static void console_status(void *ud, const char *line)
{
    console_log(ud, APP_LOG_SYS, line);
}

int main(int argc, char **argv)
{
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCtrlHandler(ctrl_handler, TRUE);

    cfg_t cfg;
    cfg_defaults(&cfg);
    char cfg_path[MAX_PATH];
    cfg_default_path(cfg_path, sizeof(cfg_path));

    const char *password = NULL;
    const char *override_server = NULL;
    const char *override_callsign = NULL;
    const char *override_cid = NULL;
    int use_mock = 0;

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--config") && i + 1 < argc)
            strncpy(cfg_path, argv[++i], sizeof(cfg_path) - 1);
        else if (!strcmp(argv[i], "--password") && i + 1 < argc)
            password = argv[++i];
        else if (!strcmp(argv[i], "--mock"))
            use_mock = 1;
        else if (!strcmp(argv[i], "--server") && i + 1 < argc)
            override_server = argv[++i];
        else if (!strcmp(argv[i], "--callsign") && i + 1 < argc)
            override_callsign = argv[++i];
        else if (!strcmp(argv[i], "--cid") && i + 1 < argc)
            override_cid = argv[++i];
        else if (!strcmp(argv[i], "--help") || !strcmp(argv[i], "-h")) {
            printf("usage: aeroflylink-cli [--config FILE] [--password PW] [--mock]\n"
                   "                       [--server host:port] [--callsign CS]\n");
            return 0;
        }
    }

    cfg_load(&cfg, cfg_path);
    if (override_server) {
        char host[128] = "";
        int port = 6809;
        if (sscanf(override_server, "%127[^:]:%d", host, &port) >= 1) {
            strncpy(cfg.server, host, sizeof(cfg.server) - 1);
            cfg.port = port;
        }
    }
    if (override_callsign)
        strncpy(cfg.callsign, override_callsign, sizeof(cfg.callsign) - 1);
    if (override_cid)
        strncpy(cfg.cid, override_cid, sizeof(cfg.cid) - 1);

    if (net_init() != 0) {
        fprintf(stderr, "WSAStartup failed\n");
        return 1;
    }

    app_t app;
    app_init(&app, &cfg);
    app.on_log = console_log;
    app.on_debug = console_debug;
    app.on_status = console_status;
    app.on_warning = console_warning;
    if (password)
        app_set_password(&app, password);

    /* 飞行计划暂存（来自配置的 fp 字段） */
#define FP(field, key) strncpy(app.field, cfg.key, sizeof(app.field) - 1)
    FP(fp_aircraft_buf, fp_aircraft);  FP(fp_wake_buf, fp_wake);
    FP(fp_tas_buf, fp_tas);            FP(fp_dep_buf, fp_dep);
    FP(fp_dest_buf, fp_dest);          FP(fp_altn_buf, fp_altn);
    FP(fp_cruise_buf, fp_cruise);      FP(fp_route_buf, fp_route);
    FP(fp_remarks_buf, fp_remarks);    FP(fp_eet_buf, fp_eet);
    FP(fp_endur_buf, fp_endur);
#undef FP
    strcpy(app.fp_type, "I");

    printf("Aerofly Link CLI %s —— %s@%s:%d (%s/%s)\n",
           AEROFLYLINK_VERSION, cfg.callsign, cfg.server, cfg.port,
           cfg.eco, cfg.type);
    printf("配置: %s\n输入 /help 查看控制台命令\n", cfg_path);

    if (use_mock)
        app_toggle_mock(&app, true);
    app_connect_fsd(&app);

    /* stdin 读取线程：整行投递，主循环消费（数据面仍是单线程 select） */
    static cli_stdin_ctx_t ictx;
    HANDLE stdin_thread = CreateThread(NULL, 0, cli_stdin_thread, &ictx, 0, NULL);
    (void)stdin_thread;

    while (g_run) {
        app_poll(&app, 50);
        if (ictx.ready) {
            char *in = ictx.buf;
            if (!strncmp(in, "/quit", 5)) {
                g_run = FALSE;
            } else if (!strncmp(in, "/help", 5)) {
                printf("@呼号 消息 | /fp | /ident | /stby | /alt | /squawk NNNN | /quit\n");
            } else if (!strncmp(in, "/fp", 3)) {
                app_submit_flightplan(&app);
            } else if (!strncmp(in, "/ident", 6)) {
                app_ident(&app);
            } else if (!strncmp(in, "/stby", 5)) {
                app_set_xpdr_mode(&app, false);
            } else if (!strncmp(in, "/alt", 4)) {
                app_set_xpdr_mode(&app, true);
            } else if (!strncmp(in, "/squawk ", 8)) {
                app_set_xpdr_code(&app, in + 8);
            } else if (in[0]) {
                app_send_chat(&app, in);
            }
            ictx.ready = 0;
        }
    }

    printf("\n正在断开...\n");
    app_disconnect_fsd(&app);
    app_toggle_mock(&app, false);
    cfg_save(&app.cfg, cfg_path);   /* 退出时保存（不含密码，ADR 0003） */
    net_cleanup();
    return 0;
}
