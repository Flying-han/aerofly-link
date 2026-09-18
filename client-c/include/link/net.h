/**
 * net.h —— WinSock 薄封装（全非阻塞，单线程 select 模型）
 * ================================================================
 * 线程模型（docs/C_REWRITE_PLAN.md §4）：整个客户端单线程，所有 socket
 * 非阻塞，select 统一等待；定时器由调用方的时间戳驱动。无锁。
 */
#ifndef LINK_NET_H
#define LINK_NET_H

#include <stdbool.h>
#include <stddef.h>
#include <winsock2.h>
#include <ws2tcpip.h>

/* 进程级初始化（WSAStartup）。可重复调用；exit 对应 net_cleanup。 */
int net_init(void);
void net_cleanup(void);

/* 非阻塞连接：立即返回。connect 进行中返回 1，已建立返回 0，
 * 失败返回 -1（errno/WSAGetLastError 有详情）。
 * host 支持 IPv4 点分与主机名（getaddrinfo 阻塞解析，通常毫秒级）。 */
int net_nb_connect(const char *host, int port, SOCKET *out);

/* 等待 socket 可写（connect 完成探测）或可读。
 * 返回 1 就绪，0 超时，-1 错误。 */
int net_wait_writable(SOCKET s, int timeout_ms);
int net_wait_readable(SOCKET s, int timeout_ms);

/* connect 完成后的状态检查：0 成功，-1 失败（取 SO_ERROR）。 */
int net_connect_finished(SOCKET s);

/* 阻塞发送全部（短报文/命令用，timeout_ms 上限）。0 成功。 */
int net_send_all(SOCKET s, const void *buf, size_t n, int timeout_ms);

/* 单调时钟（秒）。用于全部定时器。 */
double net_now(void);

#endif /* LINK_NET_H */
