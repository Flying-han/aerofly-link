/* frame.c —— CRLF/LF 行组帧实现 */
#include "link/frame.h"

#include <string.h>

void fsd_linebuf_init(fsd_linebuf *lb)
{
    lb->len = 0;
    lb->overflow = false;
}

int fsd_linebuf_feed(fsd_linebuf *lb, const char *data, size_t n,
                     size_t *consumed, char *out, size_t cap)
{
    if (!lb || !out || !consumed || cap < FSD_MAX_LINE)
        return -2;
    if (!data && n > 0)
        return -2;

    size_t i = 0;
    *consumed = 0;

    for (; i < n; i++) {
        char c = data[i];

        if (c != '\n') {
            if (lb->overflow)
                continue;   /* 正在丢弃超长行 */

            if (lb->len >= FSD_MAX_LINE - 1) {
                /* 行超限：标记丢弃，继续扫到换行 */
                lb->overflow = true;
                lb->len = 0;
                continue;
            }
            lb->buf[lb->len++] = c;
            continue;
        }

        /* 一行结束：先记账（含换行符），再决定返回什么 */
        i++;
        *consumed = i;

        if (lb->overflow) {
            /* 超长行丢弃完毕，恢复接收 */
            lb->overflow = false;
            lb->len = 0;
            return -1;
        }

        size_t line_len = lb->len;
        if (line_len > 0 && lb->buf[line_len - 1] == '\r')
            line_len--;     /* \r\n 与 \n 等价 */
        memcpy(out, lb->buf, line_len);
        out[line_len] = '\0';
        lb->len = 0;
        return 1;
    }

    *consumed = i;   /* 数据耗尽仍未遇到换行 */
    return 0;
}
