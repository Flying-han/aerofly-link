/**
 * transponder.h —— 应答机双轨制（虚拟状态 + DLL 写入尽力而为）
 * ================================================================
 * 基准：core/transponder_controller.py。VATSIM 简化：仅 STBY / ALT 两态。
 * 虚拟状态即时生效；DLL 写入失败即降级（记 warning，后续跳过 DLL 轨道）。
 */
#ifndef LINK_TRANSPONDER_H
#define LINK_TRANSPONDER_H

#include <stdbool.h>

typedef struct {
    bool alt_mode;             /* true=ALT（上报），false=STBY（静默） */
    char squawk[8];            /* 4 位八进制，默认 1200 */
    bool ident;                /* IDENT 脉冲激活 */
    double ident_until;        /* net_now() 秒 */

    /* DLL 写入能力：-1 未测试，0 不支持（降级），1 支持 */
    int  dll_can_write_mode;
    int  dll_can_write_code;
} xpdr_t;

void xpdr_init(xpdr_t *x);

/* 合法性：4 位八进制 */
bool xpdr_code_valid(const char *code);

/* 设置模式。dll_write：回调写 DLL（可为 NULL）；返回 warning 或 NULL。 */
const char *xpdr_set_mode(xpdr_t *x, bool alt_mode,
                          int (*dll_write)(void *ud, bool alt), void *ud);

/* 设置代码。返回 warning 或 NULL。 */
const char *xpdr_set_code(xpdr_t *x, const char *code,
                          int (*dll_write)(void *ud, const char *code), void *ud);

/* 触发 IDENT（duration 秒后由 xpdr_tick 自动清除）。 */
void xpdr_ident(xpdr_t *x, double now, double duration);

/* 周期驱动：IDENT 到期清除。返回清除瞬间为 true（UI 刷新用）。 */
bool xpdr_tick(xpdr_t *x, double now);

/* #AP 应答机代码：STBY → "0000"（调用方据此跳过上报） */
const char *xpdr_ap_code(const xpdr_t *x);

/* FSD 模式字母（fsd_xpdr_letter 的快捷方式） */
char xpdr_letter(const xpdr_t *x);

#endif /* LINK_TRANSPONDER_H */
