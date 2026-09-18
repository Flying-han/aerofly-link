/**
 * json.h —— 扁平 JSON 提取器（专用，非通用库）
 * ================================================================
 * 覆盖两类本项目真实数据（见 docs/C_REWRITE_PLAN.md §5 YAGNI 决策）：
 *   1. AeroflyBridge 遥测帧：扁平对象，值为数字或字符串
 *      {"Aircraft.Latitude":0.54,"Aircraft.Name":"B738",...}
 *   2. settings.json：扁平对象，值为字符串/数字/字符串数组
 *
 * 不支持嵌套与完整 JSON 语法——按需扩展，拒绝造轮子。
 * 输入视为敌意：任何格式异常安全返回 false。
 */
#ifndef LINK_JSON_H
#define LINK_JSON_H

#include <stdbool.h>
#include <stddef.h>

#define JSN_STR_CAP 80   /* 字符串数组单项容量（服务器地址等足够） */

/* 取扁平对象中 key 对应的数字（整数/浮点皆可）。成功返回 true。 */
bool jsn_number(const char *doc, const char *key, double *out);

/* 取扁平对象中 key 对应的字符串（去引号，限长拷贝）。 */
bool jsn_string(const char *doc, const char *key, char *out, size_t cap);

/* 取 key 对应的字符串数组（如 settings.json 的 "servers"）。
 * out 为 JSN_STR_CAP 步长的二维数组；实际数量写入 *count。 */
bool jsn_string_array(const char *doc, const char *key,
                      char out[][JSN_STR_CAP], size_t max, size_t *count);

#endif /* LINK_JSON_H */
