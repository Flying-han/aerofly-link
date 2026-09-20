/*
 * test_main.c —— core 协议层单元测试
 * ================================================================
 * 与 tests/test_fsd_protocol.py、tests/test_fsd_client.py 数值对齐
 * （docs/C_REWRITE_PLAN.md §8 测试策略第 2 条）。
 * 零依赖极简断言：任何失败使进程以 1 退出。
 */
#include "link/frame.h"
#include "link/message.h"
#include "link/protocol.h"

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

#define CHECK_STR(got, want) do {                                            \
        g_checks++;                                                          \
        if (strcmp((got), (want)) != 0) {                                    \
            g_failed++;                                                      \
            printf("  FAIL %s:%d\n    got : %s\n    want: %s\n",             \
                   __FILE__, __LINE__, (got), (want));                       \
        }                                                                    \
    } while (0)

static void angle_diff_ok(float a, float b, const char *what)
{
    float d = fabsf(a - b);
    if (d > 180.0f)
        d = 360.0f - d;
    g_checks++;
    if (d >= 1.0f) {
        g_failed++;
        printf("  FAIL %s: %.2f vs %.2f\n", what, a, b);
    }
}

/* feed 便捷封装：喂入全部字节，循环产出所有行；返回产出行数 */
static int feed_all(fsd_linebuf *lb, const char *data, size_t n,
                    char *out, size_t cap, int *dropped)
{
    int lines = 0;
    size_t off = 0;
    while (off < n) {
        size_t consumed = 0;
        int rc = fsd_linebuf_feed(lb, data + off, n - off, &consumed,
                                  out, cap);
        off += consumed;
        if (rc == 1)
            lines++;
        else if (rc == -1)
            (*dropped)++;
        else if (rc < -1)
            return rc;
        if (consumed == 0)
            break;   /* 防御死循环（契约上不应发生） */
    }
    return lines;
}

/* ── PBH ── */
static void test_pbh(void)
{
    printf("[pbh]\n");
    CHECK(fsd_pack_pbh(0.0f, 0.0f, 90.0f, false) == 1024u); /* Swift 基准 */

    static const float headings[] = { 0, 45, 90, 135, 180, 225, 270, 315, 359 };
    for (size_t i = 0; i < sizeof(headings) / sizeof(headings[0]); i++) {
        uint32_t pbh = fsd_pack_pbh(0.0f, 0.0f, headings[i], false);
        float hdg;
        bool og;
        fsd_unpack_pbh(pbh, NULL, NULL, &hdg, &og);
        angle_diff_ok(hdg, headings[i], "heading roundtrip");
        CHECK(og == false);
    }

    CHECK(((fsd_pack_pbh(0.0f, 0.0f, 0.0f, true) >> 1) & 1u) == 1u);

    /* pitch/bank 往返（floor 量化，容差 1°） */
    float p, b, h;
    fsd_unpack_pbh(fsd_pack_pbh(5.0f, -10.0f, 180.0f, false), &p, &b, &h, NULL);
    CHECK(fabsf(p - 5.0f) <= 1.0f);
    CHECK(fabsf(b - (-10.0f)) <= 1.0f);
    angle_diff_ok(h, 180.0f, "pbh heading");
}

/* ── 应答机 ── */
static void test_xpdr(void)
{
    printf("[xpdr]\n");
    CHECK(fsd_xpdr_letter("STBY") == 'S');
    CHECK(fsd_xpdr_letter("stby") == 'S');
    CHECK(fsd_xpdr_letter("ALT") == 'N');
    CHECK(fsd_xpdr_letter("ModeC") == 'N');
    CHECK(fsd_xpdr_letter("IDENT") == 'Y');
    CHECK(fsd_xpdr_letter("") == 'N');
    CHECK(fsd_xpdr_letter(NULL) == 'N');

    CHECK(fsd_squawk_valid("1200"));
    CHECK(fsd_squawk_valid("7000"));
    CHECK(!fsd_squawk_valid("8888"));
    CHECK(!fsd_squawk_valid("123"));
    CHECK(!fsd_squawk_valid(""));
    CHECK(!fsd_squawk_valid(NULL));
}

/* ── 坐标与距离 ── */
static void test_coords(void)
{
    printf("[coords]\n");
    bool ok;
    CHECK(fsd_parse_coord("31.1434", &ok) > 31.14 && ok);
    CHECK(fabs(fsd_parse_coord("-0.4614", &ok) - (-0.4614)) < 1e-9 && ok);

    /* packed 纬度 3938.17 → 39°38.17' */
    CHECK(fabs(fsd_parse_coord("3938.17", &ok) - (39.0 + 38.17 / 60.0)) < 1e-4 && ok);
    /* packed 经度 11623.29 */
    CHECK(fabs(fsd_parse_coord("11623.29", &ok) - (116.0 + 23.29 / 60.0)) < 1e-4 && ok);
    /* 非法输入安全失败 */
    CHECK(fsd_parse_coord("abc", &ok) == 0.0 && !ok);
    CHECK(fsd_parse_coord("", &ok) == 0.0 && !ok);
    CHECK(fsd_parse_coord(NULL, &ok) == 0.0 && !ok);

    /* ZSPD → ZBAA ≈ 594nm（与 pytest 同一容差带） */
    double d = fsd_distance_nm(31.1434, 121.8082, 40.0801, 116.5846);
    CHECK(d > 560.0 && d < 630.0);
    CHECK(fsd_distance_nm(31.0, 121.0, 31.0, 121.0) < 1e-6);
    double d1 = fsd_distance_nm(10.0, 20.0, 30.0, 40.0);
    double d2 = fsd_distance_nm(30.0, 40.0, 10.0, 20.0);
    CHECK(fabs(d1 - d2) < 1e-9);
}

/* ── 报文构造 ── */
static void test_builders(void)
{
    printf("[builders]\n");
    char line[FSD_MAX_LINE];

    /* auth：与 Python _build_auth_line 对齐（legacy revision 9） */
    fsd_auth_args auth = {
        .callsign = "TST123", .cid = "1234567", .password = "secret",
        .realname = NULL,               /* 空则回退 callsign */
        .rating = FSD_RATING_DEFAULT, .revision = FSD_REVISION_LEGACY,
        .sim_type_code = "0",
    };
    CHECK(fsd_build_auth(line, sizeof(line), &auth) == 0);
    CHECK_STR(line, "#APTST123:SERVER:1234567:secret:2:9:0:TST123");

    auth.realname = "Han Pilot";
    auth.revision = FSD_REVISION_VATSIM;
    CHECK(fsd_build_auth(line, sizeof(line), &auth) == 0);
    CHECK_STR(line, "#APTST123:SERVER:1234567:secret:2:100:0:Han Pilot");

    /* ident：挑战留空 */
    fsd_ident_args ident = {
        .callsign = "TST123", .client_id_hex = "0000",
        .client_name = "Aerofly Link", .client_version = "1.0",
        .simulator_type = "Aerofly FS 4",
    };
    CHECK(fsd_build_ident(line, sizeof(line), &ident) == 0);
    CHECK_STR(line, "$IDTST123:SERVER:0000:Aerofly Link:1.0:Aerofly FS 4:WIN:0:");

    /* 位置报告：5 位小数十进制坐标 + PBH（朝向 90° → pbh=1024） */
    fsd_position_args pos = {
        .callsign = "TST123", .xpdr = "1200", .rating = 2,
        .lat = 31.1434, .lon = 121.8082, .alt_ft = 11480, .gs_kts = 136,
        .heading_deg = 90.0f, .on_ground = false, .mode_letter = 'N',
    };
    CHECK(fsd_build_position(line, sizeof(line), &pos) == 0);
    CHECK_STR(line, "@N:TST123:1200:2:31.14340:121.80820:11480:136:1024:0");

    /* 缓冲不足必须报 -1 而不是越界 */
    char tiny[16];
    CHECK(fsd_build_position(tiny, sizeof(tiny), &pos) == -1);

    /* 文本消息：分隔符净化 + 空参拒绝 */
    CHECK(fsd_build_tm(line, sizeof(line), "TST123", "UNICOM", "hello: world") == 0);
    CHECK_STR(line, "#TMTST123:UNICOM:hello  world");
    CHECK(fsd_build_tm(line, sizeof(line), "TST123", "UNICOM", "   ") == -2);
    CHECK(fsd_build_tm(line, sizeof(line), "TST123", "", "hi") == -2);

    /* pong / 最小飞行计划 */
    CHECK(fsd_build_pong(line, sizeof(line), "TST123", "ZGGG_TWR") == 0);
    CHECK_STR(line, "$POTST123:ZGGG_TWR");
    CHECK(fsd_build_minimal_flightplan(line, sizeof(line), "TST123", "Han") == 0);
    CHECK_STR(line,
              "$FPTST123:SERVER:I:B738/L:0:ZZZZ:0:0:FL000:ZZZZ:0:0:0:0:ZZZZ:OPR/Han:NOFP");
}

/* ── 报文解析 ── */
static void test_parsers(void)
{
    printf("[parsers]\n");
    CHECK(fsd_classify("#TM") == FSD_MSG_TM);
    CHECK(fsd_classify("#SB") == FSD_MSG_SB);
    CHECK(fsd_classify("@N:CS") == FSD_MSG_PILOT_POS);
    CHECK(fsd_classify("^N:CS") == FSD_MSG_PILOT_POS);
    CHECK(fsd_classify("#APCS:...") == FSD_MSG_ATC_POS);
    CHECK(fsd_classify("#DPCS") == FSD_MSG_DP);
    CHECK(fsd_classify("#ER:msg") == FSD_MSG_ERROR);
    CHECK(fsd_classify("$CQCS:SRV:CAPS") == FSD_MSG_CQ);
    CHECK(fsd_classify("$PICS:SRV") == FSD_MSG_PI);
    CHECK(fsd_classify("$ZCCS:SRV:key") == FSD_MSG_ZC);
    CHECK(fsd_classify("$DI...") == FSD_MSG_DI);
    CHECK(fsd_classify("$XXwhatever") == FSD_MSG_UNKNOWN);
    CHECK(fsd_classify("") == FSD_MSG_UNKNOWN);
    CHECK(fsd_classify(NULL) == FSD_MSG_UNKNOWN);

    /* #TM：两段与三段形式；text 含冒号不截断 */
    fsd_tm_fields tm;
    CHECK(fsd_parse_tm("#TMZGGG_TWR:TST123:跑道 02L 可以落地", &tm) == 0);
    CHECK_STR(tm.from, "ZGGG_TWR");
    CHECK_STR(tm.to, "TST123");
    CHECK_STR(tm.text, "跑道 02L 可以落地");

    CHECK(fsd_parse_tm("#TMSERVER:BROADCAST", &tm) == 0);
    CHECK_STR(tm.from, "SERVER");
    CHECK_STR(tm.to, "BROADCAST");
    CHECK_STR(tm.text, "");

    CHECK(fsd_parse_tm("#TMNOCOLON", &tm) == -2);
    CHECK(fsd_parse_tm("@N:x", &tm) == -2);

    /* @ 位置：构造 → 解析 往返 */
    char line[FSD_MAX_LINE];
    fsd_position_args pos = {
        .callsign = "CES2101", .xpdr = "1200", .rating = 2,
        .lat = 31.1434, .lon = 121.8082, .alt_ft = 11480, .gs_kts = 136,
        .heading_deg = 270.0f, .on_ground = false, .mode_letter = 'N',
    };
    CHECK(fsd_build_position(line, sizeof(line), &pos) == 0);

    fsd_pilot_position ac;
    CHECK(fsd_parse_position(line, &ac) == 0);
    CHECK_STR(ac.callsign, "CES2101");
    CHECK_STR(ac.xpdr, "1200");
    CHECK(ac.rating == 2);
    CHECK(fabs(ac.lat - 31.1434) < 1e-4);
    CHECK(fabs(ac.lon - 121.8082) < 1e-4);
    CHECK(ac.alt_ft == 11480 && ac.gs_kts == 136);
    angle_diff_ok(ac.heading_deg, 270.0f, "parsed heading");
    CHECK(ac.on_ground == false);

    /* MC 包装 + packed 坐标输入 */
    CHECK(fsd_parse_position(
        "MC:SRV:CS:T:1:0:@N:ANA19:7000:2:3938.17:11623.29:35000:450:1024:0",
        &ac) == 0);
    CHECK_STR(ac.callsign, "ANA19");
    CHECK(fabs(ac.lat - (39.0 + 38.17 / 60.0)) < 1e-4);

    /* 字段不足安全失败 */
    CHECK(fsd_parse_position("@N:ANA19:7000:2:31.1:121.8:0:0:0", &ac) == -2);
    CHECK(fsd_parse_position("no position here", &ac) == -2);

    /* #AP：呼号/机型/高度提取（Python _handle_ap_traffic 对齐） */
    {
        fsd_atc_pos ap;
        CHECK(fsd_parse_atc_pos("#APZZZ_TWR:B738:35000", &ap) == 0);
        CHECK_STR(ap.callsign, "ZZZ_TWR");
        CHECK_STR(ap.type, "B738");
        CHECK(ap.has_alt && ap.alt_ft == 35000);

        CHECK(fsd_parse_atc_pos("#APZZZ_TWR:B738", &ap) == 0);
        CHECK(ap.has_alt == false && ap.alt_ft == 0);

        CHECK(fsd_parse_atc_pos("#APZZZ_TWR:B738:abc", &ap) == 0);
        CHECK(ap.has_alt == false);

        CHECK(fsd_parse_atc_pos("#APNOCOLON", &ap) == -2);
        CHECK(fsd_parse_atc_pos("@N:X", &ap) == -2);
    }
}

/* ── 行组帧 ── */
static void test_frame(void)
{
    printf("[frame]\n");
    char out[FSD_MAX_LINE];
    fsd_linebuf lb;
    int dropped;

    /* 跨 feed 粘包：一条行分三次喂入 */
    fsd_linebuf_init(&lb);
    {
        size_t consumed = 0;
        CHECK(fsd_linebuf_feed(&lb, "#TMA:B:C", 8, &consumed, out, sizeof(out)) == 0);
        CHECK(consumed == 8);
        CHECK(fsd_linebuf_feed(&lb, "HEL", 3, &consumed, out, sizeof(out)) == 0);
        CHECK(consumed == 3);
        CHECK(fsd_linebuf_feed(&lb, "LO\r\n@N:X", 8, &consumed, out, sizeof(out)) == 1);
        CHECK(consumed == 4);          /* "LO\r\n" 共 4 字节；"@N:X" 待处理 */
        CHECK_STR(out, "#TMA:B:CHELLO");
        /* 剩余字节继续喂（从消耗点续传） */
        CHECK(fsd_linebuf_feed(&lb, "@N:X", 4, &consumed, out, sizeof(out)) == 0);
        CHECK(consumed == 4);
        CHECK(fsd_linebuf_feed(&lb, ":1:2\r\n", 6, &consumed, out, sizeof(out)) == 1);
        CHECK_STR(out, "@N:X:1:2");
    }

    /* 一次 feed 多条行：数据永不丢失（消耗计数 + 调用方循环） */
    fsd_linebuf_init(&lb);
    dropped = 0;
    CHECK(feed_all(&lb, "line1\nline2\nline3", 17, out, sizeof(out), &dropped) == 2);
    CHECK(dropped == 0);
    CHECK(fsd_linebuf_feed(&lb, "\n", 1, &(size_t){0}, out, sizeof(out)) == 1);
    CHECK_STR(out, "line3");

    /* \r\n 与 \n 等价（\r 单独跨 feed 也要剥离） */
    fsd_linebuf_init(&lb);
    CHECK(fsd_linebuf_feed(&lb, "abc\r", 4, &(size_t){0}, out, sizeof(out)) == 0);
    CHECK(fsd_linebuf_feed(&lb, "\n", 1, &(size_t){0}, out, sizeof(out)) == 1);
    CHECK_STR(out, "abc");

    /* 超长行整行丢弃：无换行的喂入不结算；换行到达时返回 -1；连接可继续 */
    fsd_linebuf_init(&lb);
    dropped = 0;
    {
        char big[FSD_MAX_LINE + 64];
        memset(big, 'x', sizeof(big));
        CHECK(feed_all(&lb, big, sizeof(big), out, sizeof(out), &dropped) == 0);
        CHECK(dropped == 0);   /* 尚无换行，丢弃未结算 */
        /* "next" 是超长行的延续，随换行一起被丢弃 */
        CHECK(fsd_linebuf_feed(&lb, "next\n", 5, &(size_t){0},
                               out, sizeof(out)) == -1);
        CHECK(fsd_linebuf_feed(&lb, "real\n", 5, &(size_t){0},
                               out, sizeof(out)) == 1);
        CHECK_STR(out, "real");
    }

    /* 空 feed 与坏参数 */
    fsd_linebuf_init(&lb);
    {
        size_t consumed = 99;
        CHECK(fsd_linebuf_feed(&lb, NULL, 0, &consumed, out, sizeof(out)) == 0);
        CHECK(consumed == 0);
    }
    {
        char smallcap[16];
        CHECK(fsd_linebuf_feed(&lb, "x\n", 2, &(size_t){0},
                               smallcap, sizeof(smallcap)) == -2);
    }
}

int main(void)
{
    printf("Aerofly Link C core 协议层测试\n===============================\n");
    test_pbh();
    test_xpdr();
    test_coords();
    test_builders();
    test_parsers();
    test_frame();
    printf("===============================\n%d checks, %d failed\n",
           g_checks, g_failed);
    if (g_failed == 0)
        printf("ALL TESTS PASSED\n");
    return g_failed ? 1 : 0;
}
