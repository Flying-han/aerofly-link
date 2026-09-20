/* http.c —— FSD-JWT 登录令牌获取（WinHTTP 实现） */
#include "link/http.h"
#include "link/json.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#include <winhttp.h>

int http_json_escape(const char *in, char *out, size_t cap)
{
    size_t j = 0;
    for (const char *p = in ? in : ""; *p; p++) {
        unsigned char c = (unsigned char)*p;
        const char *esc = NULL;
        char buf[8];
        switch (c) {
        case '"':  esc = "\\\""; break;
        case '\\': esc = "\\\\"; break;
        case '\n': esc = "\\n";  break;
        case '\r': esc = "\\r";  break;
        case '\t': esc = "\\t";  break;
        default:
            if (c < 0x20) {
                _snprintf(buf, sizeof(buf) - 1, "\\u%04x", c);
                buf[7] = '\0';
                esc = buf;
            }
            break;
        }
        if (esc) {
            size_t n = strlen(esc);
            if (j + n >= cap)
                return -1;
            memcpy(out + j, esc, n);
            j += n;
        } else {
            if (j + 1 >= cap)
                return -1;
            out[j++] = (char)c;
        }
    }
    out[j] = '\0';
    return 0;
}

/* 解析 https://host[:port]/path */
static int parse_url(const char *url, char *host, size_t host_cap,
                     int *port, char *path, size_t path_cap)
{
    if (!url || strncmp(url, "https://", 8) != 0)
        return -1;
    const char *p = url + 8;
    const char *slash = strchr(p, '/');
    const char *colon = strchr(p, ':');
    if (!slash)
        slash = p + strlen(p);
    size_t hl = (colon && colon < slash) ? (size_t)(colon - p)
                                         : (size_t)(slash - p);
    if (hl == 0 || hl >= host_cap)
        return -1;
    memcpy(host, p, hl);
    host[hl] = '\0';
    *port = (colon && colon < slash) ? atoi(colon + 1) : 443;
    if (*port <= 0 || *port > 65535)
        return -1;
    if (*slash == '\0') {
        if (path_cap < 2)
            return -1;
        strcpy(path, "/");
    } else {
        if (strlen(slash) >= path_cap)
            return -1;
        strcpy(path, slash);
    }
    return 0;
}

int http_post_json(const char *url, const char *body,
                   char *resp, size_t resp_cap,
                   char *out_token, size_t token_cap,
                   char *err, size_t err_cap,
                   int timeout_ms)
{
    char host[256], path[512];
    int port;
    if (err_cap)
        err[0] = '\0';
    if (parse_url(url, host, sizeof(host), &port, path, sizeof(path)) != 0) {
        if (err_cap)
            _snprintf(err, err_cap - 1, "JWT URL 非法（须 https://host/path）");
        return -1;
    }

    int ret = -1;
    HINTERNET ses = NULL, conn = NULL, req = NULL;
    do {
        ses = WinHttpOpen(L"AeroflyLink", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                          WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
        if (!ses) {
            if (err_cap) _snprintf(err, err_cap - 1, "WinHTTP 初始化失败");
            break;
        }
        WinHttpSetTimeouts(ses, timeout_ms, timeout_ms, timeout_ms, timeout_ms);

        wchar_t whost[256];
        MultiByteToWideChar(CP_UTF8, 0, host, -1, whost, 256);
        conn = WinHttpConnect(ses, whost, (INTERNET_PORT)port, 0);
        if (!conn) {
            if (err_cap) _snprintf(err, err_cap - 1,
                                   "无法连接 %s:%d（网络/DNS）", host, port);
            break;
        }

        wchar_t wpath[512];
        MultiByteToWideChar(CP_UTF8, 0, path, -1, wpath, 512);
        req = WinHttpOpenRequest(conn, L"POST", wpath, NULL,
                                 WINHTTP_NO_REFERER,
                                 WINHTTP_DEFAULT_ACCEPT_TYPES,
                                 WINHTTP_FLAG_SECURE);
        if (!req) {
            if (err_cap) _snprintf(err, err_cap - 1, "请求构造失败");
            break;
        }

        /* TLS 证书错误不豁免：JWT 是凭据，拒绝降级 */
        wchar_t hdrs[] = L"Content-Type: application/json\r\n";
        int body_len = (int)strlen(body);
        if (!WinHttpSendRequest(req, hdrs, (DWORD)-1,
                                (LPVOID)body, (DWORD)body_len,
                                (DWORD)body_len, 0)) {
            if (err_cap) _snprintf(err, err_cap - 1,
                                   "JWT 请求发送失败（网络/超时 %dms）",
                                   timeout_ms);
            break;
        }
        if (!WinHttpReceiveResponse(req, NULL)) {
            if (err_cap) _snprintf(err, err_cap - 1, "JWT 响应接收失败");
            break;
        }

        DWORD status = 0, sz = sizeof(status);
        WinHttpQueryHeaders(req, WINHTTP_QUERY_STATUS_CODE
                            | WINHTTP_QUERY_FLAG_NUMBER,
                            WINHTTP_HEADER_NAME_BY_INDEX,
                            &status, &sz, WINHTTP_NO_HEADER_INDEX);

        size_t got = 0;
        for (;;) {
            DWORD avail = 0;
            if (!WinHttpQueryDataAvailable(req, &avail))
                break;
            if (avail == 0 || got + avail >= resp_cap)
                break;
            DWORD read = 0;
            if (!WinHttpReadData(req, resp + got, avail, &read))
                break;
            if (read == 0)
                break;
            got += read;
        }
        if (got >= resp_cap)
            got = resp_cap - 1;
        resp[got] = '\0';

        if (status != 200) {
            if (err_cap)
                _snprintf(err, err_cap - 1,
                          "JWT 换取被拒绝（HTTP %lu）：检查 CID/密码",
                          (unsigned long)status);
            break;
        }
        if (!jsn_string(resp, "token", out_token, token_cap)) {
            if (err_cap) _snprintf(err, err_cap - 1, "JWT 响应缺少 token");
            break;
        }
        ret = 0;
    } while (0);

    if (req) WinHttpCloseHandle(req);
    if (conn) WinHttpCloseHandle(conn);
    if (ses) WinHttpCloseHandle(ses);
    return ret;
}

int jwt_acquire(const char *url, const char *cid, const char *password,
                char *token, size_t token_cap,
                char *err, size_t err_cap)
{
    char esc_cid[128], esc_pw[512], body[768];
    if (http_json_escape(cid, esc_cid, sizeof(esc_cid)) != 0
        || http_json_escape(password, esc_pw, sizeof(esc_pw)) != 0) {
        if (err_cap) _snprintf(err, err_cap - 1, "凭据含不可转义字符");
        return -1;
    }
    _snprintf(body, sizeof(body) - 1,
              "{\"cid\":\"%s\",\"password\":\"%s\"}", esc_cid, esc_pw);
    body[sizeof(body) - 1] = '\0';

    char resp[2048];
    return http_post_json(url, body, resp, sizeof(resp),
                          token, token_cap, err, err_cap, 10000);
}
