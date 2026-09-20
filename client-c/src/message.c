/* message.c —— FSD 报文构造与解析（基准：core/fsd_client.py） */
#include "link/message.h"
#include "link/frame.h"
#include "link/protocol.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* 安全追加：成功返回 0，缓冲不足返回 -1（不越界） */
static int appendf(char *out, size_t cap, size_t *pos, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(out + *pos, cap - *pos, fmt, ap);
    va_end(ap);
    if (n < 0 || (size_t)n >= cap - *pos) {
        out[cap - 1] = '\0';
        return -1;
    }
    *pos += (size_t)n;
    return 0;
}

/* NULL 安全非空校验 */
static const char *nonempty(const char *s)
{
    return (s && *s) ? s : NULL;
}

int fsd_build_auth(char *out, size_t cap, const fsd_auth_args *a)
{
    if (!out || cap == 0 || !a || !nonempty(a->callsign) || !a->cid || !a->password
        || !nonempty(a->sim_type_code) || a->rating <= 0)
        return -2;
    const char *rname = nonempty(a->realname) ? a->realname : a->callsign;
    size_t pos = 0;
    return appendf(out, cap, &pos,
                   "#AP%s:SERVER:%s:%s:%d:%d:%s:%s",
                   a->callsign, a->cid, a->password,
                   a->rating, a->revision, a->sim_type_code, rname);
}

int fsd_build_ident(char *out, size_t cap, const fsd_ident_args *a)
{
    if (!out || cap == 0 || !a || !nonempty(a->callsign))
        return -2;
    size_t pos = 0;
    return appendf(out, cap, &pos,
                   "$ID%s:SERVER:%s:%s:%s:%s:WIN:0:",
                   a->callsign,
                   a->client_id_hex ? a->client_id_hex : "0000",
                   a->client_name ? a->client_name : "Aerofly Link",
                   a->client_version ? a->client_version : "1.0",
                   a->simulator_type ? a->simulator_type : "Aerofly FS 4");
}

int fsd_build_position(char *out, size_t cap, const fsd_position_args *p)
{
    if (!out || cap == 0 || !p || !nonempty(p->callsign))
        return -2;
    const char *xpdr = nonempty(p->xpdr) ? p->xpdr : "7000";
    char mode = p->mode_letter ? p->mode_letter : 'N';
    size_t pos = 0;
    int rc = appendf(out, cap, &pos,
                     "@%c:%s:%s:%d:%.5f:%.5f:%d:%d:",
                     mode, p->callsign, xpdr, p->rating,
                     p->lat, p->lon, p->alt_ft, p->gs_kts);
    if (rc != 0)
        return rc;
    uint32_t pbh = fsd_pack_pbh(0.0f, 0.0f, p->heading_deg, p->on_ground);
    return appendf(out, cap, &pos, "%u:0", (unsigned)pbh);
}

int fsd_build_tm(char *out, size_t cap, const char *callsign,
                 const char *dest, const char *text)
{
    if (!out || cap == 0 || !nonempty(callsign) || !nonempty(dest) || !text)
        return -2;

    /* ':' 是 FSD 字段分隔符：替换为空格后去首尾空白——与 Python
     * send_text_message 的 replace(':',' ').strip() 语义一致 */
    char clean[384];
    size_t j = 0;
    for (size_t i = 0; text[i] && j < sizeof(clean) - 1; i++) {
        clean[j++] = (text[i] == ':') ? ' ' : text[i];
    }
    clean[j] = '\0';
    size_t start = 0;
    while (start < j && clean[start] == ' ')
        start++;
    while (j > start && clean[j - 1] == ' ')
        j--;
    clean[j] = '\0';
    if (j == start)
        return -2;   /* 全空白（与 Python 拒绝空消息一致） */
    if (start > 0)
        memmove(clean, clean + start, j - start + 1);

    size_t pos = 0;
    return appendf(out, cap, &pos, "#TM%s:%s:%s", callsign, dest, clean);
}

int fsd_build_pong(char *out, size_t cap, const char *callsign,
                   const char *sender)
{
    if (!out || cap == 0 || !nonempty(callsign) || !nonempty(sender))
        return -2;
    size_t pos = 0;
    return appendf(out, cap, &pos, "$PO%s:%s", callsign, sender);
}

int fsd_build_minimal_flightplan(char *out, size_t cap,
                                 const char *callsign, const char *realname)
{
    if (!out || cap == 0 || !nonempty(callsign))
        return -2;
    const char *rname = nonempty(realname) ? realname : callsign;
    size_t pos = 0;
    return appendf(out, cap, &pos,
                   "$FP%s:SERVER:I:B738/L:0:ZZZZ:0:0:FL000:ZZZZ:0:0:0:0:ZZZZ:OPR/%s:NOFP",
                   callsign, rname);
}

/* ── 解析 ── */

fsd_msg_type fsd_classify(const char *line)
{
    if (!line || !*line)
        return FSD_MSG_UNKNOWN;
    if (line[0] == '@' || line[0] == '^')
        return FSD_MSG_PILOT_POS;
    if (strncmp(line, "#TM", 3) == 0)  return FSD_MSG_TM;
    if (strncmp(line, "#SB", 3) == 0)  return FSD_MSG_SB;
    if (strncmp(line, "#AP", 3) == 0)  return FSD_MSG_ATC_POS;
    if (strncmp(line, "#DP", 3) == 0)  return FSD_MSG_DP;
    if (strncmp(line, "#ER", 3) == 0)  return FSD_MSG_ERROR;
    if (strncmp(line, "$CQ", 3) == 0)  return FSD_MSG_CQ;
    if (strncmp(line, "$PI", 3) == 0)  return FSD_MSG_PI;
    if (strncmp(line, "$ZC", 3) == 0)  return FSD_MSG_ZC;
    if (strncmp(line, "$DI", 3) == 0)  return FSD_MSG_DI;
    return FSD_MSG_UNKNOWN;
}

/* 定长拷贝：返回 0 成功，-1 源过长 */
static int copy_bounded(char *dst, size_t cap, const char *src, size_t n)
{
    if (n >= cap)
        return -1;
    memcpy(dst, src, n);
    dst[n] = '\0';
    return 0;
}

int fsd_parse_tm(const char *line, fsd_tm_fields *out)
{
    if (!line || !out || strncmp(line, "#TM", 3) != 0)
        return -2;

    out->from[0] = out->to[0] = out->text[0] = '\0';

    const char *body = line + 3;
    const char *c1 = strchr(body, ':');
    if (!c1)
        return -2;                       /* 无 dest 字段 */
    const char *c2 = strchr(c1 + 1, ':');

    if (copy_bounded(out->from, sizeof(out->from), body, (size_t)(c1 - body)) != 0)
        return -2;
    if (!c2) {                            /* 仅两段：text 视为空 */
        if (copy_bounded(out->to, sizeof(out->to), c1 + 1, strlen(c1 + 1)) != 0)
            return -2;
        return 0;
    }
    if (copy_bounded(out->to, sizeof(out->to), c1 + 1, (size_t)(c2 - (c1 + 1))) != 0)
        return -2;
    if (copy_bounded(out->text, sizeof(out->text), c2 + 1, strlen(c2 + 1)) != 0)
        return -2;
    return 0;
}

int fsd_parse_atc_pos(const char *line, fsd_atc_pos *out)
{
    if (!line || !out || strncmp(line, "#AP", 3) != 0)
        return -2;

    out->callsign[0] = out->type[0] = '\0';
    out->alt_ft = 0;
    out->has_alt = false;

    const char *body = line + 3;
    const char *c1 = strchr(body, ':');
    if (!c1)
        return -2;                       /* 无字段区（Python 无 ':' 即跳过） */
    if (copy_bounded(out->callsign, sizeof(out->callsign), body,
                     (size_t)(c1 - body)) != 0)
        return -2;

    const char *rest = c1 + 1;
    const char *c2 = strchr(rest, ':');
    size_t type_len = c2 ? (size_t)(c2 - rest) : strlen(rest);
    if (copy_bounded(out->type, sizeof(out->type), rest, type_len) != 0)
        return -2;

    if (c2) {
        const char *c3 = strchr(c2 + 1, ':');
        size_t alt_len = c3 ? (size_t)(c3 - (c2 + 1)) : strlen(c2 + 1);
        if (alt_len > 0 && alt_len < 12) {
            char alt[12];
            memcpy(alt, c2 + 1, alt_len);
            alt[alt_len] = '\0';
            char *end = NULL;
            long v = strtol(alt, &end, 10);
            if (end && *end == '\0' && end != alt) {
                out->alt_ft = (int)v;
                out->has_alt = true;
            }
        }
    }
    return 0;
}

int fsd_parse_position(const char *line, fsd_pilot_position *out)
{
    if (!line || !out)
        return -2;

    /* 兼容 MC 包装：取首个 '@' 起的载荷（与 Python line.index('@') 一致） */
    const char *at = strchr(line, '@');
    if (!at)
        return -2;

    char payload[FSD_MAX_LINE];
    if (copy_bounded(payload, sizeof(payload), at + 1, strlen(at + 1)) != 0)
        return -2;

    /* 就地按 ':' 切分字段指针 */
    char *fields[16];
    int nfields = 0;
    fields[nfields++] = payload;
    for (char *p = payload; *p && nfields < 16; p++) {
        if (*p == ':') {
            *p = '\0';
            fields[nfields++] = p + 1;
        }
    }
    /* @ 包至少 10 字段：flag cs xpdr rating lat lon alt gs pbh flags */
    if (nfields < 10)
        return -2;

    memset(out, 0, sizeof(*out));
    if (copy_bounded(out->callsign, sizeof(out->callsign), fields[1],
                     strlen(fields[1])) != 0)
        return -2;
    if (copy_bounded(out->xpdr, sizeof(out->xpdr), fields[2],
                     strlen(fields[2])) != 0)
        return -2;

    out->rating = (int)strtol(fields[3], NULL, 10);
    out->lat = fsd_parse_coord(fields[4], NULL);
    out->lon = fsd_parse_coord(fields[5], NULL);
    out->alt_ft = (int)strtol(fields[6], NULL, 10);
    out->gs_kts = (int)strtol(fields[7], NULL, 10);
    out->pbh = (uint32_t)strtoul(fields[8], NULL, 10);

    float hdg;
    fsd_unpack_pbh(out->pbh, NULL, NULL, &hdg, &out->on_ground);
    out->heading_deg = hdg;
    return 0;
}
