/* protocol.c —— FSD 协议纯函数实现（基准：core/fsd_protocol.py） */
#include "link/protocol.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* 与 Swift / Python 基准一致的量化系数 */
#define PBH_PITCH_MULT (256.0f / 90.0f)
#define PBH_BANK_MULT  (512.0f / 180.0f)
#define PBH_HDG_MULT   (1024.0f / 360.0f)

#define PBH_MASK 0x3FFu

static int32_t s10(uint32_t v)
{
    /* 10 位有符号展开 */
    return (v & 0x200u) ? (int32_t)v - 0x400 : (int32_t)v;
}

uint32_t fsd_pack_pbh(float pitch_deg, float bank_deg, float heading_deg,
                      bool on_ground)
{
    /* pitch/bank 取 floor（协议中反转），heading 向零截断 —— 与 Python 一致 */
    int32_t p = (int32_t)floorf(pitch_deg * -PBH_PITCH_MULT);
    int32_t b = (int32_t)floorf(bank_deg * -PBH_BANK_MULT);
    int32_t h = (int32_t)(heading_deg * PBH_HDG_MULT);

    uint32_t up = (uint32_t)(p & (int32_t)PBH_MASK);
    uint32_t ub = (uint32_t)(b & (int32_t)PBH_MASK);
    uint32_t uh = (uint32_t)(h & (int32_t)PBH_MASK);

    return (up << 22) | (ub << 12) | (uh << 2) | ((uint32_t)(on_ground ? 1 : 0) << 1);
}

void fsd_unpack_pbh(uint32_t pbh, float *pitch_deg, float *bank_deg,
                    float *heading_deg, bool *on_ground)
{
    uint32_t p_raw = (pbh >> 22) & PBH_MASK;
    uint32_t b_raw = (pbh >> 12) & PBH_MASK;
    uint32_t h_raw = (pbh >> 2) & PBH_MASK;

    if (pitch_deg)
        *pitch_deg = floorf((float)s10(p_raw) / -PBH_PITCH_MULT);
    if (bank_deg)
        *bank_deg = floorf((float)s10(b_raw) / -PBH_BANK_MULT);
    if (heading_deg)
        *heading_deg = (float)h_raw / PBH_HDG_MULT;
    if (on_ground)
        *on_ground = ((pbh >> 1) & 1u) == 1u;
}

char fsd_xpdr_letter(const char *xpdr_mode)
{
    if (!xpdr_mode)
        return 'N';
    char c = (char)toupper((unsigned char)xpdr_mode[0]);
    /* 完整匹配与 Python 一致：仅 STBY/IDENT 特判，其余归 N */
    if (c == 'S' && _stricmp(xpdr_mode, "STBY") == 0)
        return 'S';
    if (c == 'I' && _stricmp(xpdr_mode, "IDENT") == 0)
        return 'Y';
    return 'N';
}

bool fsd_squawk_valid(const char *code)
{
    if (!code || strlen(code) != 4)
        return false;
    for (int i = 0; i < 4; i++) {
        if (code[i] < '0' || code[i] > '7')
            return false;
    }
    return true;
}

double fsd_parse_coord(const char *raw, bool *ok)
{
    if (ok)
        *ok = false;
    if (!raw || !*raw)
        return 0.0;

    char *end = NULL;
    double val = strtod(raw, &end);
    if (end == raw)
        return 0.0;   /* 完全不是数字 */
    if (ok)
        *ok = true;

    if (fabs(val) <= 180.0)
        return val;   /* 十进制格式 */

    /* packed 格式 DDMM.mmm（纬度）或 DDDMM.mmm（经度）。
     * int 截断向零——与 Python int() 语义一致（负 packed 坐标同样成立）。 */
    double deg = (double)(int)(val / 100.0);
    double minutes = val - deg * 100.0;
    return deg + minutes / 60.0;
}

double fsd_distance_nm(double lat1, double lon1, double lat2, double lon2)
{
    const double earth_nm = 3440.065; /* 地球半径，海里 */
    double lat1r = lat1 * M_PI / 180.0;
    double lat2r = lat2 * M_PI / 180.0;
    double dlat = lat2r - lat1r;
    double dlon = (lon2 - lon1) * M_PI / 180.0;

    double a = sin(dlat / 2.0) * sin(dlat / 2.0)
             + cos(lat1r) * cos(lat2r) * sin(dlon / 2.0) * sin(dlon / 2.0);
    if (a > 1.0)
        a = 1.0;
    return 2.0 * asin(sqrt(a)) * earth_nm;
}
