/**
 * message.h —— FSD 报文构造与解析
 * ================================================================
 * 构造器与 Python core/fsd_client.py 的 _build_* / send_* 逐字节对齐；
 * 解析器对应 _dispatch / _handle_tm / _handle_at_traffic 的行为子集
 * （协议层骨架只覆盖客户端必需消息，服务端推送的未知前缀一律 UNKNOWN）。
 *
 * 错误码约定（全部构造/解析函数适用）：
 *   0   成功
 *  -1   缓冲不足（截断放弃写入，保证 out 未定义时不越界）
 *  -2   参数无效 / 协议语义错误
 */
#ifndef LINK_FSD_MESSAGE_H
#define LINK_FSD_MESSAGE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* ── 构造：登录认证 #AP ──
 * 格式: #AP<callsign>:SERVER:<cid>:<password>:<rating>:<revision>:<simtype>:<realname>
 * realname 为 NULL/空时回退为 callsign（与 Python 一致）。
 */
typedef struct {
    const char *callsign;
    const char *cid;
    const char *password;
    const char *realname;      /* 可为 NULL */
    int rating;
    int revision;              /* FSD_REVISION_LEGACY / FSD_REVISION_VATSIM */
    const char *sim_type_code; /* Aerofly 无标准编号，固定 "0" */
} fsd_auth_args;

int fsd_build_auth(char *out, size_t cap, const fsd_auth_args *args);

/* ── 构造：VATSIM 前置 ident $ID（challenge 留空跳过挑战）── */
typedef struct {
    const char *callsign;
    const char *client_id_hex;   /* 未授权客户端 "0000" */
    const char *client_name;
    const char *client_version;
    const char *simulator_type;  /* "Aerofly FS 4" */
} fsd_ident_args;

int fsd_build_ident(char *out, size_t cap, const fsd_ident_args *args);

/* ── 构造：飞行员位置报告 @ ──
 * 格式: @<mode>:<callsign>:<xpdr>:<rating>:<lat:.5f>:<lon:.5f>:<alt>:<gs>:<pbh>:0
 * 坐标为十进制 5 位小数（Swift toTokens() 约定；packed 格式会被服务器丢弃）。
 * pitch/bank 恒为 0（服务器不校验）。alt_diff 恒为 0。
 */
typedef struct {
    const char *callsign;
    const char *xpdr;        /* 4 位八进制；NULL 时回退 "7000" */
    int rating;
    double lat, lon;
    int alt_ft, gs_kts;
    float heading_deg;
    bool on_ground;
    char mode_letter;        /* 'N' / 'S' / 'Y'（fsd_xpdr_letter 产出） */
} fsd_position_args;

int fsd_build_position(char *out, size_t cap, const fsd_position_args *args);

/* ── 构造：文本消息 #TM（text 中的 ':' 替换为空格防止字段错位）── */
int fsd_build_tm(char *out, size_t cap, const char *callsign,
                 const char *dest, const char *text);

/* ── 构造：Pong $PO（响应 $PI ping）── */
int fsd_build_pong(char *out, size_t cap, const char *callsign,
                   const char *sender);

/* ── 构造：最小飞行计划 $FP（加入广播列表所需；提交后可被完整 $FP 覆盖）── */
int fsd_build_minimal_flightplan(char *out, size_t cap,
                                 const char *callsign, const char *realname);

/* ── 解析：消息分类（对应 Python _dispatch 的前缀表）── */
typedef enum {
    FSD_MSG_UNKNOWN = 0,
    FSD_MSG_TM,          /* #TM  文本消息                */
    FSD_MSG_SB,          /* #SB  服务器广播              */
    FSD_MSG_PILOT_POS,   /* @/^  飞行员位置更新          */
    FSD_MSG_ATC_POS,     /* #AP  其他客户端位置报告      */
    FSD_MSG_DP,          /* #DP  客户端断开              */
    FSD_MSG_CQ,          /* $CQ  客户端查询（CAPS 等）   */
    FSD_MSG_PI,          /* $PI  ping                    */
    FSD_MSG_ZC,          /* $ZC  认证挑战                */
    FSD_MSG_DI,          /* $DI  服务器标识              */
    FSD_MSG_ERROR        /* #ER  服务器错误              */
} fsd_msg_type;

fsd_msg_type fsd_classify(const char *line);

/* ── 解析：#TM 文本消息 ──
 * "#TM<src>:<dest>:<text>"；字段为定长拷贝，安全失败返回 -2。
 * 仅两段（无 text）时 text 置空串——与 Python 宽容行为一致。
 */
typedef struct {
    char from[64];
    char to[64];
    char text[400];
} fsd_tm_fields;

int fsd_parse_tm(const char *line, fsd_tm_fields *out);

/* ── 解析：@ 飞行员位置更新 ──
 * 兼容 "MC:...:@..." 包装（取最后一个 '@' 起的载荷，与 Python 一致）。
 * 需要 >=10 个字段；pbh 解包得到罗盘航向与地面状态。
 */
typedef struct {
    char callsign[16];
    char xpdr[8];
    int rating;
    double lat, lon;         /* 十进制度（packed 输入自动展开） */
    int alt_ft, gs_kts;
    uint32_t pbh;
    bool on_ground;
    float heading_deg;       /* 罗盘度，North=0 顺时针 */
} fsd_pilot_position;

int fsd_parse_position(const char *line, fsd_pilot_position *out);

/* ── 解析：#AP 客户端位置报告 ──
 * "#AP<callsign>:<type>[:<alt_ft>...]"——仅提取呼号/机型/高度
 *（无坐标，Python _handle_ap_traffic 同语义）。缺 ':' 或截断安全失败。
 */
typedef struct {
    char callsign[16];
    char type[8];
    int  alt_ft;
    bool has_alt;
} fsd_atc_pos;

int fsd_parse_atc_pos(const char *line, fsd_atc_pos *out);

/* ── $FP 飞行计划：字段集与规范化 ──
 * 字段集（调用方视角，字符串可为栈/配置缓冲，发送期间被只读使用）。
 * 规范化（基准 core/fsd_client.py send_flight_plan 537-617）：
 *   - aircraft/wake/dep/dest/altn：strip+大写
 *   - tas：strip+大写+去前导 'N'（可留空）
 *   - cruise_alt：仅 strip（不大写，Python 如此）
 *   - dep_time/actual_dep/eet/endurance：Python int() 严格语义数字化
 *     （"0830"→"830"，非全数字→"0"）；actual 空则回退 dep_time 再回退 "0"；
 *     eet/endurance 需 "H:MM"，无 ':' 两段均 "0"（含 "2:05:30" 的尾巴判非法）
 *   - route/remarks：':' → ' '（FSD 分隔符防护，不做 strip）
 *   - type：首字母大写，缺省 'I'
 */
typedef struct {
    const char *type;
    const char *aircraft;
    const char *wake;
    const char *tas;
    const char *dep, *dest, *altn;
    const char *dep_time, *actual_dep_time;
    const char *cruise_alt;
    const char *route, *remarks, *pilot;
    const char *eet, *endurance;
} fsd_plan_fields;

typedef struct {
    char type;
    char aircraft[48];
    char wake[16];
    char tas[16];
    char dep[16], dest[16], altn[16];
    char dep_time[16];
    char actual_dep[16];
    char cruise_alt[16];
    char eet_h[8], eet_m[8];
    char fuel_h[8], fuel_m[8];
    char pilot[64];
    char route[256];
    char remarks[256];
} fsd_plan_norm;

void fsd_normalize_plan(const fsd_plan_fields *in, fsd_plan_norm *out);

#endif /* LINK_FSD_MESSAGE_H */
