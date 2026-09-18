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

bool jsn_string_array(const char *doc, const char *key,
                      char out[][JSN_STR_CAP], size_t max, size_t *count)
{
    const char *v = find_value(doc, key);
    if (!v || *v != '[')
        return false;
    v++;

    size_t n = 0;
    while (*v && *v != ']' && n < max) {
        while (*v == ' ' || *v == ',' || *v == '\r' || *v == '\n' || *v == '\t')
            v++;
        if (*v == ']')
            break;
        if (*v != '"')
            return false;   /* 数组只支持字符串元素 */

        /* 就地借用 jsn_string 的解析：临时截取该元素 */
        const char *e = v + 1;
        while (*e && *e != '"')
            e++;   /* 简化：数组元素不支持转义引号（服务器地址不含） */
        if (!*e)
            return false;

        size_t len = (size_t)(e - (v + 1));
        if (len >= JSN_STR_CAP)
            len = JSN_STR_CAP - 1;
        memcpy(out[n], v + 1, len);
        out[n][len] = '\0';
        n++;
        v = e + 1;
    }
    if (count)
        *count = n;
    return true;
}
