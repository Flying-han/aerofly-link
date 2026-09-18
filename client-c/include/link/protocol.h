/**
 * protocol.h —— FSD 协议纯函数层：位打包 / 坐标 / 应答机映射
 * ================================================================
 * 与 Python 基准实现 core/fsd_protocol.py 逐条对齐（见 docs/C_REWRITE_PLAN.md §5）。
 * 本层为纯函数：无 I/O、无内存分配、无 WinSock 依赖，可在宿主机直接单测。
 *
 * 约定：
 *   - 解析函数通过出参 bool *ok 报告成功/失败，输入视为敌意数据。
 *   - 所有角度单位为度；坐标为十进制度；距离为海里。
 */
#ifndef LINK_FSD_PROTOCOL_H
#define LINK_FSD_PROTOCOL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* ── 协议版本（与 fsd_protocol.py 一致）── */
#define FSD_REVISION_LEGACY 9    /* FSD V3.000 draft 9 / Swift private */
#define FSD_REVISION_VATSIM 100  /* VATSIM 现代协议 */

/* 默认飞行员等级：S1（回归约束：OBS=1 无法被部分服务器纳入广播列表） */
#define FSD_RATING_DEFAULT 2

/* ── PBH (Pitch/Bank/Heading) 32 位打包 ──
 * 与 Swift packPBH 一致的位布局（LSB 起）：
 *   bit 0    未用
 *   bit 1    onGround
 *   bit 2-11 heading（10 位无符号）
 *   bit 12-21 bank（10 位有符号，反转）
 *   bit 22-31 pitch（10 位有符号，反转）
 * pitch/bank 采用 floor 舍入；heading 向零截断——与 Python 基准一致。
 */
uint32_t fsd_pack_pbh(float pitch_deg, float bank_deg, float heading_deg,
                      bool on_ground);

/* 解包为角度值。任一出参可为 NULL（调用方不关心该字段）。 */
void fsd_unpack_pbh(uint32_t pbh, float *pitch_deg, float *bank_deg,
                    float *heading_deg, bool *on_ground);

/* ── 应答机 ── */

/* 模式字母（FSD serializer 约定）：STBY→'S'，IDENT→'Y'，其余→'N'。 */
char fsd_xpdr_letter(const char *xpdr_mode);

/* 4 位八进制校验（每位 0-7）。 */
bool fsd_squawk_valid(const char *code);

/* ── 坐标 ── */

/* 解析 FSD 坐标：|v|<=180 视为十进制度，否则按 DDMM.mmm packed 格式。
 * 失败（非数字/NULL）时 *ok=false 并返回 0.0。 */
double fsd_parse_coord(const char *raw, bool *ok);

/* 大圆距离（海里）。 */
double fsd_distance_nm(double lat1, double lon1, double lat2, double lon2);

#endif /* LINK_FSD_PROTOCOL_H */
