/**
 * frame.h —— FSD 行组帧（CRLF / LF 分隔的行协议）
 * ================================================================
 * 对应 Python 端 asyncio StreamReader.readline 的语义：
 *   - 跨 feed 的粘包缓冲；
 *   - 行结束符 \r\n 或 \n，返回的行不含行尾；
 *   - 超长行被整行丢弃（防无界内存增长），丢弃对调用方可见（返回 -1）。
 *
 * 契约：一次 feed 至多产出一条行；*consumed 报告本次实际消耗的字节数。
 * 当返回值 >=0 且 *consumed < n 时，调用方必须用剩余字节再次 feed
 * （一次喂入可能包含多条行）。缓冲定长，无堆分配，数据永不丢失。
 */
#ifndef LINK_FRAME_H
#define LINK_FRAME_H

#include <stdbool.h>
#include <stddef.h>

/* FSD 单行上限。$FP 长航路可能到数百字节，1024 足够且可防滥用。 */
#define FSD_MAX_LINE 1024

typedef struct {
    char     buf[FSD_MAX_LINE];
    size_t   len;
    bool     overflow;   /* 当前未完成行已超限；等待换行后整行丢弃 */
} fsd_linebuf;

void fsd_linebuf_init(fsd_linebuf *lb);

/* 喂入原始字节。
 * 出参 *consumed：本次消耗的字节数（与返回值无关，总是尽可能多地消耗）。
 * 返回：
 *   1  out 中得到一条完整行（NUL 结尾，不含行尾）
 *   0  需要更多数据
 *  -1  一条超长行被丢弃（连接可继续，后续行正常）
 *  -2  调用方契约错误（out 容量 < FSD_MAX_LINE / 参数为 NULL）
 */
int fsd_linebuf_feed(fsd_linebuf *lb, const char *data, size_t n,
                     size_t *consumed, char *out, size_t cap);

#endif /* LINK_FRAME_H */
