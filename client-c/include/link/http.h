/**
 * http.h —— FSD-JWT 登录令牌获取（WinHTTPS，系统 winhttp.dll，无外部依赖）
 * ================================================================
 * ASC/swift 兼容网络（ADR 0002 / docs/compatibility/fsd-jwt.md）的
 * VATSIM 模式登录流程：
 *   1. 服务器问候 $DISERVER:CLIENT:<ident>:<challenge>
 *   2. 客户端 $ID（结构合法即可，进入 JWT 换取流程）
 *   3. POST {jwt_url}  body {"cid":"..","password":".."}
 *      200 → {"success":true,"token":"<JWT>","expires_in":n}
 *   4. #AP 密码位填 JWT，revision=100
 */
#ifndef LINK_HTTP_H
#define LINK_HTTP_H

#include <stddef.h>

/* JSON 字符串转义（"\ 与控制字符），out 容量不足返回 -1 */
int http_json_escape(const char *in, char *out, size_t cap);

/* 同步 HTTPS POST JSON（阻塞，超时由 timeout_ms 约束；GUI 登录期一次）。
 * url 形如 https://host[:port]/path。
 * 成功（HTTP 200 且响应含 token）返回 0，token 填入 out；
 * 失败返回 -1，err 填人类可读原因。 */
int http_post_json(const char *url, const char *body,
                   char *resp, size_t resp_cap,
                   char *out_token, size_t token_cap,
                   char *err, size_t err_cap,
                   int timeout_ms);

/* FSD-JWT 获取组合步骤：转义 → POST → 提取 token。
 * 成功 0；失败 -1 且 err 含原因（用于断线文案）。 */
int jwt_acquire(const char *url, const char *cid, const char *password,
                char *token, size_t token_cap,
                char *err, size_t err_cap);

#endif /* LINK_HTTP_H */
