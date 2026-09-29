/* transponder.c —— 应答机双轨制实现（基准：core/transponder_controller.py） */
#include "link/transponder.h"
#include "link/protocol.h"

#include <string.h>

void xpdr_init(xpdr_t *x)
{
    memset(x, 0, sizeof(*x));
    x->alt_mode = true;   /* 默认 ALT（与 Python/面板一致） */
    strcpy(x->squawk, "1200");
    x->dll_can_write_mode = -1;
    x->dll_can_write_code = -1;
}

bool xpdr_code_valid(const char *code)
{
    return fsd_squawk_valid(code);
}

const char *xpdr_set_mode(xpdr_t *x, bool alt_mode,
                          int (*dll_write)(void *ud, bool alt), void *ud)
{
    /* 1. 虚拟状态即时生效 */
    x->alt_mode = alt_mode;

    /* 2. DLL 轨道（尽力而为；已知不支持则直接降级） */
    if (x->dll_can_write_mode != 0 && dll_write) {
        int rc = dll_write(ud, alt_mode);
        if (rc == 0) {
            x->dll_can_write_mode = 1;
            return NULL;
        }
        x->dll_can_write_mode = 0;
        return "AFS4 不支持外部控制应答机模式，请手动调整游戏内面板";
    }
    if (x->dll_can_write_mode == 0)
        return "AFS4 不支持外部控制应答机模式（已降级为虚拟状态）";
    return NULL;
}

const char *xpdr_set_code(xpdr_t *x, const char *code,
                          int (*dll_write)(void *ud, const char *code), void *ud)
{
    if (!xpdr_code_valid(code))
        return "无效的应答机代码（须为 4 位八进制，每位 0-7）";

    strncpy(x->squawk, code, sizeof(x->squawk) - 1);
    x->squawk[sizeof(x->squawk) - 1] = '\0';

    if (x->dll_can_write_code != 0 && dll_write) {
        int rc = dll_write(ud, code);
        if (rc == 0) {
            x->dll_can_write_code = 1;
            return NULL;
        }
        x->dll_can_write_code = 0;
        return "DLL 写入应答机代码失败，请在游戏面板手动输入";
    }
    return NULL;
}

void xpdr_ident(xpdr_t *x, double now, double duration)
{
    x->ident = true;
    x->ident_until = now + duration;
}

bool xpdr_tick(xpdr_t *x, double now)
{
    if (x->ident && now >= x->ident_until) {
        x->ident = false;
        return true;
    }
    return false;
}

const char *xpdr_ap_code(const xpdr_t *x)
{
    return x->alt_mode ? x->squawk : "0000";
}

char xpdr_letter(const xpdr_t *x)
{
    if (!x->alt_mode)
        return 'S';
    return x->ident ? 'Y' : 'N';
}
