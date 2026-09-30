/* json.c —— 扁平 JSON 提取器实现 */
#include "link/json.h"

#include <stdlib.h>
#include <stdint.h>
#include <string.h>

/* Return the byte after a JSON string, respecting escaped quotes. */
static const char *skip_json_string(const char *p)
{
    if (!p || *p != '"')
        return NULL;
    for (++p; *p; ++p) {
        unsigned char c = (unsigned char)*p;
        if (c == '"')
            return p + 1;
        if (c < 0x20)
            return NULL;
        if (c == '\\') {
            if (!p[1] || (unsigned char)p[1] < 0x20)
                return NULL;
            ++p;
        }
    }
    return NULL;
}

static int hex_value(unsigned char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static bool read_hex4(const char *p, uint32_t *value)
{
    uint32_t v = 0;
    for (int i = 0; i < 4; ++i) {
        int h = hex_value((unsigned char)p[i]);
        if (h < 0)
            return false;
        v = (v << 4) | (uint32_t)h;
    }
    *value = v;
    return true;
}

static bool append_utf8(uint32_t cp, char *out, size_t cap, size_t *used)
{
    unsigned char bytes[4];
    size_t n;
    if (cp == 0)
        return false; /* C strings cannot represent embedded JSON NUL safely. */
    if (cp <= 0x7f) {
        bytes[0] = (unsigned char)cp;
        n = 1;
    } else if (cp <= 0x7ff) {
        bytes[0] = (unsigned char)(0xc0 | (cp >> 6));
        bytes[1] = (unsigned char)(0x80 | (cp & 0x3f));
        n = 2;
    } else if (cp <= 0xffff && !(cp >= 0xd800 && cp <= 0xdfff)) {
        bytes[0] = (unsigned char)(0xe0 | (cp >> 12));
        bytes[1] = (unsigned char)(0x80 | ((cp >> 6) & 0x3f));
        bytes[2] = (unsigned char)(0x80 | (cp & 0x3f));
        n = 3;
    } else if (cp <= 0x10ffff) {
        bytes[0] = (unsigned char)(0xf0 | (cp >> 18));
        bytes[1] = (unsigned char)(0x80 | ((cp >> 12) & 0x3f));
        bytes[2] = (unsigned char)(0x80 | ((cp >> 6) & 0x3f));
        bytes[3] = (unsigned char)(0x80 | (cp & 0x3f));
        n = 4;
    } else {
        return false;
    }
    if (*used + n >= cap)
        return false;
    memcpy(out + *used, bytes, n);
    *used += n;
    return true;
}

/* Decode one JSON string into UTF-8. Malformed escapes fail closed. */
static bool decode_json_string(const char *start, char *out, size_t cap,
                               const char **after)
{
    if (!start || !out || cap == 0 || *start != '"')
        return false;
    const unsigned char *p = (const unsigned char *)start + 1;
    size_t used = 0;
    while (*p) {
        uint32_t cp;
        if (*p == '"') {
            out[used] = '\0';
            if (after)
                *after = (const char *)(p + 1);
            return true;
        }
        if (*p < 0x20)
            return false;
        if (*p != '\\') {
            if (used + 1 >= cap)
                return false;
            out[used++] = (char)*p++;
            continue;
        }

        ++p;
        switch (*p++) {
        case '"': cp = '"'; break;
        case '\\': cp = '\\'; break;
        case '/': cp = '/'; break;
        case 'b': cp = '\b'; break;
        case 'f': cp = '\f'; break;
        case 'n': cp = '\n'; break;
        case 'r': cp = '\r'; break;
        case 't': cp = '\t'; break;
        case 'u': {
            uint32_t first;
            if (!read_hex4((const char *)p, &first))
                return false;
            p += 4;
            if (first >= 0xd800 && first <= 0xdbff) {
                uint32_t second;
                if (p[0] != '\\' || p[1] != 'u'
                    || !read_hex4((const char *)p + 2, &second)
                    || second < 0xdc00 || second > 0xdfff)
                    return false;
                p += 6;
                cp = 0x10000 + ((first - 0xd800) << 10)
                   + (second - 0xdc00);
            } else if (first >= 0xdc00 && first <= 0xdfff) {
                return false;
            } else {
                cp = first;
            }
            break;
        }
        default:
            return false;
        }
        if (!append_utf8(cp, out, cap, &used))
            return false;
    }
    return false;
}

/* Locate an object key and return its value start; nested JSON is unnecessary. */
static const char *find_value(const char *doc, const char *key)
{
    if (!doc || !key)
        return NULL;

    size_t klen = strlen(key);
    const char *p = doc;

    while ((p = strchr(p, '"')) != NULL) {
        const char *key_start = p + 1;
        const char *after_key = skip_json_string(p);
        if (!after_key)
            return NULL;   /* 引号不闭合，文档损坏 */
        const char *key_end = after_key - 1;

        if ((size_t)(key_end - key_start) == klen
            && memcmp(key_start, key, klen) == 0) {
            const char *v = after_key;
            while (*v == ' ' || *v == '\t' || *v == '\r' || *v == '\n')
                v++;
            if (*v == ':') {
                do { ++v; } while (*v == ' ' || *v == '\t'
                                    || *v == '\r' || *v == '\n');
                return *v ? v : NULL;
            }
        }
        p = after_key;
    }
    return NULL;
}

bool jsn_number(const char *doc, const char *key, double *out)
{
    const char *v = find_value(doc, key);
    if (!v || *v == '"')
        return false;   /* 字符串值不按数字读 */

    char *end = NULL;
    double n = strtod(v, &end);
    if (end == v)
        return false;
    if (out)
        *out = n;
    return true;
}

bool jsn_string(const char *doc, const char *key, char *out, size_t cap)
{
    const char *v = find_value(doc, key);
    return cap > 0 && decode_json_string(v, out, cap, NULL);
}

/* 解析 v 指向的 '[' 起的字符串数组，追加到 out[*n]（上限 max）。
 * 简化：元素不支持转义引号（服务器地址不含）。 */
static bool array_append(const char *v, char out[][JSN_STR_CAP],
                         size_t max, size_t *n)
{
    v++;   /* 跳过 '[' */
    while (*v && *v != ']' && *n < max) {
        while (*v == ' ' || *v == ',' || *v == '\r' || *v == '\n' || *v == '\t')
            v++;
        if (*v == ']')
            break;
        if (*v != '"')
            return false;   /* 数组只支持字符串元素 */

        const char *after = NULL;
        if (!decode_json_string(v, out[*n], JSN_STR_CAP, &after))
            return false;
        (*n)++;
        v = after;
    }
    return true;
}

bool jsn_string_array(const char *doc, const char *key,
                      char out[][JSN_STR_CAP], size_t max, size_t *count)
{
    const char *v = find_value(doc, key);
    if (!v || *v != '[')
        return false;
    size_t n = 0;
    if (!array_append(v, out, max, &n))
        return false;
    if (count)
        *count = n;
    return true;
}

/* find_value 的范围版：在 [lo, hi) 内定位 key 的值起点 */
static const char *find_value_bounded(const char *lo, const char *hi,
                                      const char *key)
{
    size_t klen = strlen(key);
    const char *p = lo;
    while (p < hi && (p = memchr(p, '"', (size_t)(hi - p))) != NULL) {
        const char *key_start = p + 1;
        const char *after_key = skip_json_string(p);
        if (!after_key || after_key > hi)
            return NULL;
        const char *key_end = after_key - 1;
        if ((size_t)(key_end - key_start) == klen
            && memcmp(key_start, key, klen) == 0) {
            const char *v = after_key;
            while (v < hi && (*v == ' ' || *v == '\t' || *v == '\r'
                              || *v == '\n'))
                v++;
            if (v < hi && *v == ':') {
                do { ++v; } while (v < hi && (*v == ' ' || *v == '\t'
                                               || *v == '\r' || *v == '\n'));
                return v < hi ? v : NULL;
            }
        }
        p = after_key;
    }
    return NULL;
}

bool jsn_read_servers(const char *doc, const char *key,
                      char out[][JSN_STR_CAP], size_t max, size_t *count)
{
    const char *v = find_value(doc, key);
    if (!v)
        return false;
    if (*v == '[')
        return jsn_string_array(doc, key, out, max, count);
    if (*v != '{')
        return false;

    /* per-ECO dict：按 vatsim→private→legacy 顺序拼接。对象范围限定
     *（深度配对，起始 1——开括号已消费；组值是字符串数组，无嵌套 '{'） */
    static const char *const groups[] = { "vatsim", "private", "legacy" };
    const char *obj = v + 1;
    int depth = 1;
    const char *obj_end = obj;
    for (; *obj_end; ) {
        if (*obj_end == '"') {
            const char *after = skip_json_string(obj_end);
            if (!after)
                return false;
            obj_end = after;
        } else if (*obj_end == '{') {
            depth++;
            obj_end++;
        } else if (*obj_end == '}') {
            if (--depth == 0)
                break;
            obj_end++;
        } else {
            obj_end++;
        }
    }
    if (*obj_end != '}')
        return false;

    size_t n = 0;
    for (size_t g = 0; g < sizeof(groups) / sizeof(groups[0]); g++) {
        const char *gv = find_value_bounded(obj, obj_end, groups[g]);
        if (!gv || *gv != '[')
            continue;
        size_t before = n;
        if (!array_append(gv, out, max, &n))
            n = before;   /* 该组畸形：整组跳过（安全失败） */
    }

    /* 跨组精确去重，保持首现顺序 */
    size_t w = 0;
    for (size_t i = 0; i < n; i++) {
        bool dup = false;
        for (size_t j = 0; j < w; j++) {
            if (strcmp(out[j], out[i]) == 0) {
                dup = true;
                break;
            }
        }
        if (!dup) {
            if (w != i)
                memcpy(out[w], out[i], JSN_STR_CAP);
            w++;
        }
    }
    if (count)
        *count = w;
    return true;
}
