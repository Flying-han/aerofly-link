/*
 * test_p2.c —— 传输/会话/桥接/配置集成测试
 * ================================================================
 * 全部使用本机回环与嵌入式 Mock，无外部依赖：
 *   1. FSD 会话握手全流程（脚本化 mock FSD 服务器）
 *   2. C 桥接 ↔ C 内嵌 Mock 契约（遥测解析 + 命令写入）
 *   3. settings.json 读写与密码不落盘（ADR 0003 回归）
 */
#include "link/app.h"
#include "link/config.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

static int g_checks = 0;
static int g_failed = 0;

#define CHECK(cond) do {                                                     \
        g_checks++;                                                          \
        if (!(cond)) {                                                       \
            g_failed++;                                                      \
            printf("  FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);         \
        }                                                                    \
    } while (0)

/* ── 1. FSD 会话握手 ── */

static sess_state_t g_state = SESS_DISCONNECTED;
static char g_last_msg[128];
static char g_tm_text[256];

static void st_cb(void *ud, sess_state_t st, const char *msg)
{
    (void)ud;
    g_state = st;
    strncpy(g_last_msg, msg, sizeof(g_last_msg) - 1);
}
static void tm_cb(void *ud, const char *from, const char *to, const char *text)
{
    (void)ud; (void)from; (void)to;
    strncpy(g_tm_text, text, sizeof(g_tm_text) - 1);
}

/* 从 socket 读一行（阻塞，2s 超时） */
static int sock_read_line(SOCKET s, char *out, size_t cap)
{
    size_t j = 0;
    while (j < cap - 1) {
        char ch;
        if (net_wait_readable(s, 2000) <= 0)
            return -1;
        int n = recv(s, &ch, 1, 0);
        if (n <= 0)
            return -1;
        if (ch == '\n')
            break;
        if (ch != '\r')
            out[j++] = ch;
    }
    out[j] = '\0';
    return 0;
}

static void test_session_handshake(void)
{
    printf("[session handshake]\n");

    /* 脚本化 mock FSD 服务器 */
    SOCKET lst = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    struct sockaddr_in addr;
    int alen = sizeof(addr);
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    addr.sin_port = 0;
    bind(lst, (struct sockaddr *)&addr, sizeof(addr));
    getsockname(lst, (struct sockaddr *)&addr, &alen);
    int port = ntohs(addr.sin_port);
    listen(lst, 1);

    sess_t s;
    sess_init(&s);
    strcpy(s.callsign, "TST123");
    strcpy(s.cid, "1111111");
    strcpy(s.password, "pw");
    strcpy(s.realname, "Tester");
    strcpy(s.server, "127.0.0.1");
    s.port = port;
    s.rating = 2;
    s.vatsim = false;
    s.init_lat = 31.1434; s.init_lon = 121.8082; s.init_alt_ft = 11483;
    s.on_status = st_cb;
    s.on_tm = tm_cb;

    CHECK(sess_connect(&s) == 0);
    CHECK(g_state == SESS_CONNECTING);

    /* 驱动连接完成 */
    if (net_wait_writable(s.sock, 3000) > 0)
        sess_on_writable(&s);
    CHECK(g_state == SESS_HANDSHAKE);

    SOCKET cli = accept(lst, NULL, NULL);
    CHECK(cli != INVALID_SOCKET);

    /* 服务器问候：$DI 标识 + MOTD；读完后 1s 静默窗口到期发认证 */
    send(cli, "$DI:SRV:SWIFT-FSD:1.0:0:0\r\n#TM welcome\r\n", 40, 0);
    net_wait_readable(s.sock, 1000);
    sess_on_readable(&s);
    sess_tick(&s, net_now() + 2.0);   /* 静默窗口到期（驱动定时器） */

    /* 预期：$DI 触发 $ID（含空挑战），随后 #AP legacy rev9 */
    char line[512];
    CHECK(sock_read_line(cli, line, sizeof(line)) == 0);
    CHECK(strcmp(line, "$IDTST123:SERVER:0000:Aerofly Link:1.0:Aerofly FS 4:WIN:0:") == 0);
    CHECK(sock_read_line(cli, line, sizeof(line)) == 0);
    CHECK(strcmp(line, "#APTST123:SERVER:1111111:pw:2:9:0:Tester") == 0);

    /* 认证通过 */
    send(cli, "#TM authenticated\r\n", 19, 0);
    net_wait_readable(s.sock, 1000);
    sess_on_readable(&s);
    CHECK(g_state == SESS_ONLINE);

    /* 在线后三连发：#SB 声明 / 最小 $FP / 初始 @ */
    CHECK(sock_read_line(cli, line, sizeof(line)) == 0);
    CHECK(strcmp(line, "#SBTST123:SERVER") == 0);
    CHECK(sock_read_line(cli, line, sizeof(line)) == 0);
    g_checks++;
    if (strcmp(line,
        "$FPTST123:SERVER:I:B738/L:0:ZZZZ:0:0:FL000:ZZZZ:0:0:0:0:ZZZZ:OPR/Tester:NOFP") != 0) {
        g_failed++;
        printf("  FAIL $FP got : %s\n", line);
    }
    CHECK(sock_read_line(cli, line, sizeof(line)) == 0);
    CHECK(strcmp(line, "@N:TST123:1200:2:31.14340:121.80820:11483:0:0:0") == 0);

    /* 主动位置上报（1Hz 由 app 驱动；此处直接调） */
    CHECK(sess_send_position(&s, 31.2, 121.9, 11000, 250, 45.0f, "1200", 'N') == 0);
    CHECK(sock_read_line(cli, line, sizeof(line)) == 0);
    CHECK(strncmp(line, "@N:TST123:1200:2:31.20000:121.90000:11000:250:", 46) == 0);

    /* 服务器推送他人位置 + ping → 期望 pong 与 traffic 回调 */
    send(cli, "@N:ANA1:1200:2:31.200:121.900:35000:450:1024:0\r\n"
              "$PISRV01:TST123\r\n", 74, 0);
    net_wait_readable(s.sock, 1000);
    sess_on_readable(&s);
    CHECK(s.n_traffic == 1 && strcmp(s.traffic[0].callsign, "ANA1") == 0);
    CHECK(sock_read_line(cli, line, sizeof(line)) == 0);
    CHECK(strcmp(line, "$POTST123:SRV01") == 0);

    /* 文本消息 */
    CHECK(sess_send_tm(&s, "ZGGG_TWR", "请求放行") == 0);
    CHECK(sock_read_line(cli, line, sizeof(line)) == 0);
    CHECK(strcmp(line, "#TMTST123:ZGGG_TWR:请求放行") == 0);

    /* keepalive：legacy dialect 用 #TM 空跳（与 Python keepalive_loop 一致） */
    sess_tick(&s, net_now() + 31.0);
    CHECK(sock_read_line(cli, line, sizeof(line)) == 0);
    CHECK(strcmp(line, "#TMTST123:SERVER:@") == 0);

    /* 服务器关闭连接 → 自动离线 */
    closesocket(cli);
    net_wait_readable(s.sock, 1000);
    sess_on_readable(&s);
    CHECK(g_state == SESS_DISCONNECTED);

    closesocket(lst);
    sess_disconnect(&s, NULL);
}

/* ── 2. 桥接 ↔ 内嵌 Mock 契约 ── */

static int g_tel_frames = 0;
static telem_t g_last_tel;
static void tel_cb(void *ud, const telem_t *t)
{
    (void)ud;
    g_tel_frames++;
    g_last_tel = *t;
}

static void pump(bridge_t *b, mocksrv_t *m, int ms)
{
    double end = net_now() + ms / 1000.0;
    while (net_now() < end) {
        fd_set r, w;
        FD_ZERO(&r);
        FD_ZERO(&w);
        mocksrv_collect_fds(m, &r, NULL);
        br_collect_fds(b, &r, &w, NULL);
        struct timeval tv = { 0, 10000 };
        select(0, &r, &w, NULL, &tv);
        if (b->tel_sock != INVALID_SOCKET && FD_ISSET(b->tel_sock, &w))
            br_on_writable(b);
        mocksrv_on_readable(m, &r);
        br_on_readable(b);
        double now = net_now();
        br_tick(b, now);
        mocksrv_tick(m, now);
    }
}

static void test_bridge_mock_contract(void)
{
    printf("[bridge-mock contract]\n");
    mocksrv_t mock;
    bridge_t br;

    CHECK(mocksrv_start(&mock) == 0);
    br_init(&br);
    br.on_telem = tel_cb;
    br_start(&br);

    pump(&br, &mock, 1500);
    CHECK(br.tel_connected);
    CHECK(g_tel_frames > 5);          /* 10Hz 推流 */
    CHECK(fabs(g_last_tel.lat - 31.1434) < 0.5);
    CHECK(fabs(g_last_tel.lon - 121.8082) < 0.5);
    CHECK(g_last_tel.hdg_true >= 0.0 && g_last_tel.hdg_true < 360.0);
    CHECK(g_last_tel.gs_kts > 100.0); /* 70 m/s → ~136 kts */
    CHECK(strcmp(g_last_tel.xpdr_code, "7000") == 0);
    CHECK(strcmp(g_last_tel.model, "B738") == 0);

    /* 命令端口：写代码 → 遥测帧应反映新值 */
    char resp[128];
    CHECK(br_send_command(&br, "Communication.TransponderCode",
                          true, 1234.0, NULL, resp, sizeof(resp)) == 0);
    int frames_before = g_tel_frames;
    pump(&br, &mock, 400);
    CHECK(g_tel_frames > frames_before);
    CHECK(strcmp(g_last_tel.xpdr_code, "1234") == 0);

    /* 未知变量：命令发送成功且连接不崩溃。注：嵌入式 Mock 与调用方同线程，
     * 响应需由 pump 读取（真实场景对端是外部 DLL 进程，响应即时返回），
     * 故此处只断言不失败；响应内容语义由 mock 端测试覆盖。 */
    CHECK(br_send_command(&br, "Foo.Bar", true, 1.0, NULL,
                          resp, sizeof(resp)) == 0);

    br_stop(&br);
    mocksrv_stop(&mock);
}

/* ── 3. 配置读写 ── */

static void test_config(void)
{
    printf("[config]\n");
    const char *path = "build\\test_settings.json";
    cfg_t c;
    cfg_defaults(&c);

    strcpy(c.callsign, "CES2101");
    strcpy(c.cid, "1234567");
    strcpy(c.password, "SECRET");      /* 绝不能落盘 */
    strcpy(c.server, "fsd.skeet.top");
    c.port = 6809;
    c.rating = 3;
    strcpy(c.mock_lat, "40.0801");
    strcpy(c.fp_route, "CDY G212 KR");
    strcpy(c.servers[0], "fsd.skeet.top");
    c.nservers = 1;

    CHECK(cfg_save(&c, path) == 0);

    cfg_t r;
    cfg_defaults(&r);
    CHECK(cfg_load(&r, path) == 0);
    CHECK(strcmp(r.callsign, "CES2101") == 0);
    CHECK(strcmp(r.server, "fsd.skeet.top") == 0);
    CHECK(r.port == 6809 && r.rating == 3);
    CHECK(strcmp(r.mock_lat, "40.0801") == 0);
    CHECK(strcmp(r.fp_route, "CDY G212 KR") == 0);
    CHECK(r.nservers == 1 && strcmp(r.servers[0], "fsd.skeet.top") == 0);

    /* ADR 0003 回归：读回的文档不含密码 */
    FILE *f = fopen(path, "rb");
    char doc[8192];
    size_t n = fread(doc, 1, sizeof(doc) - 1, f);
    doc[n] = '\0';
    fclose(f);
    CHECK(strstr(doc, "SECRET") == NULL);
    CHECK(strstr(doc, "password") == NULL);

    /* 缺文件 → 默认值，不致命 */
    cfg_t d;
    cfg_defaults(&d);
    CHECK(cfg_load(&d, "build\\no_such_file.json") == 0);
    CHECK(d.port == 6809);
    remove(path);
}

/* ── 4. JSON 提取器 ── */

static void test_json(void)
{
    printf("[json]\n");
    double v;
    char s[64];

    const char *frame =
        "{\"Aircraft.Latitude\":0.543668,\"Aircraft.Name\":\"B738\","
        "\"Communication.TransponderCode\":1200,\"neg\":-42.5}";
    CHECK(jsn_number(frame, "Aircraft.Latitude", &v) && fabs(v - 0.543668) < 1e-9);
    CHECK(jsn_number(frame, "Communication.TransponderCode", &v) && v == 1200.0);
    CHECK(jsn_number(frame, "neg", &v) && v == -42.5);
    CHECK(jsn_string(frame, "Aircraft.Name", s, sizeof(s)) && strcmp(s, "B738") == 0);
    CHECK(!jsn_number(frame, "missing", &v));
    CHECK(!jsn_string(frame, "Aircraft.Latitude", s, sizeof(s)));  /* 数字非字符串 */

    const char *doc = "{\"server\":\"a:1\",\"servers\":[\"x.net\", \"y.org\"]}";
    char arr[8][JSN_STR_CAP];
    size_t cnt = 0;
    CHECK(jsn_string_array(doc, "servers", arr, 8, &cnt) && cnt == 2);
    CHECK(strcmp(arr[0], "x.net") == 0 && strcmp(arr[1], "y.org") == 0);
}

int main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0); /* 崩溃时不丢缓冲输出，便于定位 */
    printf("Aerofly Link C 传输/应用层测试\n===============================\n");
    net_init();
    test_json();
    test_config();
    test_session_handshake();
    test_bridge_mock_contract();
    net_cleanup();
    printf("===============================\n%d checks, %d failed\n",
           g_checks, g_failed);
    if (g_failed == 0)
        printf("ALL TESTS PASSED\n");
    return g_failed ? 1 : 0;
}
