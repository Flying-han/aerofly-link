/* net.c —— WinSock 薄封装实现 */
#include "link/net.h"

#include <stdio.h>
#include <string.h>

static int g_wsa_refs = 0;

int net_init(void)
{
    if (g_wsa_refs++ > 0)
        return 0;
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        g_wsa_refs = 0;
        return -1;
    }
    return 0;
}

void net_cleanup(void)
{
    if (g_wsa_refs > 0 && --g_wsa_refs == 0)
        WSACleanup();
}

int net_nb_connect(const char *host, int port, SOCKET *out)
{
    char port_str[16];
    _snprintf(port_str, sizeof(port_str) - 1, "%d", port);
    port_str[sizeof(port_str) - 1] = '\0';

    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;          /* FSD 部署均为 IPv4，按需扩展 */
    hints.ai_socktype = SOCK_STREAM;

    struct addrinfo *res = NULL;
    if (getaddrinfo(host, port_str, &hints, &res) != 0 || !res)
        return -1;

    SOCKET s = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (s == INVALID_SOCKET) {
        freeaddrinfo(res);
        return -1;
    }

    /* 非阻塞模式 */
    u_long mode = 1;
    ioctlsocket(s, FIONBIO, &mode);

    int rc = connect(s, res->ai_addr, (int)res->ai_addrlen);
    freeaddrinfo(res);

    if (rc == 0) {
        *out = s;
        return 0;                       /* 立即建立（本机回环常见） */
    }
    if (WSAGetLastError() == WSAEWOULDBLOCK) {
        *out = s;
        return 1;                       /* 进行中 */
    }
    closesocket(s);
    return -1;
}

int net_wait_writable(SOCKET s, int timeout_ms)
{
    fd_set w;
    FD_ZERO(&w);
    FD_SET(s, &w);
    struct timeval tv = { timeout_ms / 1000, (timeout_ms % 1000) * 1000 };
    int rc = select(0, NULL, &w, NULL, &tv);
    return (rc == 0) ? 0 : (rc > 0 ? 1 : -1);
}

int net_wait_readable(SOCKET s, int timeout_ms)
{
    fd_set r;
    FD_ZERO(&r);
    FD_SET(s, &r);
    struct timeval tv = { timeout_ms / 1000, (timeout_ms % 1000) * 1000 };
    int rc = select(0, &r, NULL, NULL, &tv);
    return (rc == 0) ? 0 : (rc > 0 ? 1 : -1);
}

int net_connect_finished(SOCKET s)
{
    int err = 0;
    int len = sizeof(err);
    if (getsockopt(s, SOL_SOCKET, SO_ERROR, (char *)&err, &len) != 0)
        return -1;
    return err ? -1 : 0;
}

int net_send_all(SOCKET s, const void *buf, size_t n, int timeout_ms)
{
    const char *p = (const char *)buf;
    size_t sent = 0;
    DWORD start = GetTickCount();

    while (sent < n) {
        int rc = send(s, p + sent, (int)(n - sent), 0);
        if (rc > 0) {
            sent += (size_t)rc;
            continue;
        }
        if (rc < 0 && WSAGetLastError() != WSAEWOULDBLOCK)
            return -1;

        if (GetTickCount() - start > (DWORD)timeout_ms)
            return -1;                  /* 超时 */
        int ready = net_wait_writable(s, 50);
        if (ready < 0)
            return -1;
    }
    return 0;
}

double net_now(void)
{
    return (double)GetTickCount64() / 1000.0;
}
