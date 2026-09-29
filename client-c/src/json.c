/* json.c —— 扁平 JSON 提取器实现 */
#include "link/json.h"

#include <stdlib.h>
#include <string.h>

/* 定位 key 对应值的起始指针。
 * 返回值指向值首字符（" 中心或数字首字符），找不到返回 NULL。
 * 实现按"键名逐个匹配"扫描，避免对整个文档做词法分析。 */
static const char *find_value(const char *doc, const char *key)
{
    if (!doc || !key)
        return NULL;

    size_t klen = strlen(key);
    const char *p = doc;

    while ((p = strchr(p, '"')) != NULL) {
        const char *key_start = p + 1;
        const char *key_end = strchr(key_start, '"');
        if (!key_end)
            return NULL;   /* 引号不闭合，文档损坏 */

        if ((size_t)(key_end - key_start) == klen
            && memcmp(key_start, key, klen) == 0) {
            /* 键名匹配，找值：跳过冒号与空白 */
            const char *v = key_end + 1;
            while (*v == ' ' || *v == '\t' || *v == '\r' || *v == '\n' || *v == ':')
                v++;
            return *v ? v : NULL;
        }
        p = key_end + 1;
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
    if (!v || *v != '"')
        return false;
    v++;   /* 跳过开头引号 */

    size_t j = 0;
    while (*v && *v != '"' && j < cap - 1) {
        /* 最小转义支持：\" \\ \/ 以及 \n \t（遥测 Aircraft.Name 可能含转义） */
        if (*v == '\\' && v[1]) {
            v++;
            switch (*v) {
            case 'n': out[j++] = '\n'; break;
            case 't': out[j++] = '\t'; break;
            default:  out[j++] = *v;   break;   /* \" \\ \/ 等 */
            }
        } else {
            out[j++] = *v;
        }
        v++;
    }
    out[j] = '\0';
    return *v == '"';   /* 必须以引号闭合才算成功 */
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

        const char *e = v + 1;
        while (*e && *e != '"')
            e++;
        if (!*e)
            return false;

        size_t len = (size_t)(e - (v + 1));
        if (len >= JSN_STR_CAP)
            len = JSN_STR_CAP - 1;
        memcpy(out[*n], v + 1, len);
        out[*n][len] = '\0';
        (*n)++;
        v = e + 1;
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
        const char *key_end = key_start;
        while (key_end < hi && *key_end != '"')
            key_end++;
        if (key_end >= hi)
            return NULL;
        if ((size_t)(key_end - key_start) == klen
            && memcmp(key_start, key, klen) == 0) {
            const char *v = key_end + 1;
            while (v < hi && (*v == ' ' || *v == '\t' || *v == '\r'
                              || *v == '\n' || *v == ':'))
                v++;
            return v < hi ? v : NULL;
        }
        p = key_end + 1;
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
    for (; *obj_end; obj_end++) {
        if (*obj_end == '{') {
            depth++;
        } else if (*obj_end == '}') {
            if (--depth == 0)
                break;
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
