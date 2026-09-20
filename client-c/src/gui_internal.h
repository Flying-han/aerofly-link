/*
 * gui_internal.h —— gui.c / gui_pages.c 共享内部声明（不对构建外暴露）
 * ================================================================
 * gui.c：窗口/绘制/定时器/回调/工厂函数（Win32 管线）
 * gui_pages.c：连接页/工作区构建、配置↔UI、动作（页面语义）
 */
#ifndef LINK_GUI_INTERNAL_H
#define LINK_GUI_INTERNAL_H

#include "link/app.h"

#include <windows.h>

/* ── Apple 配色（RGB 宏）── */
#define C_BG      RGB(0x1C, 0x1C, 0x1E)
#define C_CARD    RGB(0x2C, 0x2C, 0x2E)
#define C_HOVER   RGB(0x3A, 0x3A, 0x3C)
#define C_INPUT   RGB(0x2C, 0x2C, 0x2E)
#define C_LOGBG   RGB(0x23, 0x23, 0x26)
#define C_ACCENT  RGB(0x0A, 0x84, 0xFF)
#define C_ACC_PRS RGB(0x07, 0x68, 0xCC)
#define C_ACC_HOV RGB(0x21, 0x92, 0xFF)
#define C_TEXT    RGB(0xF5, 0xF5, 0xF7)
#define C_DIM     RGB(0x98, 0x98, 0x9D)
#define C_GREEN   RGB(0x30, 0xD1, 0x58)
#define C_ORANGE  RGB(0xFF, 0x9F, 0x0A)
#define C_RED     RGB(0xFF, 0x45, 0x3A)
#define C_LINE    RGB(0x38, 0x38, 0x3A)

/* ── 控件 ID ── */
enum {
    IDC_CALLSIGN = 100, IDC_CID, IDC_PASSWORD, IDC_REALNAME, IDC_MODEL,
    IDC_ECO, IDC_TYPE, IDC_SERVER, IDC_CONNECT, IDC_CONNECT_STATUS,
    IDC_WS_DISCONNECT = 200, IDC_WS_STATUS,
    IDC_XPDR_CODE, IDC_Q1200, IDC_Q7000, IDC_Q7700, IDC_Q7600, IDC_Q7500,
    IDC_STBY, IDC_ALT, IDC_IDENT, IDC_XPDR_WARNING,
    IDC_FP_AIRCRAFT, IDC_FP_WAKE, IDC_FP_TAS, IDC_FP_DEP, IDC_FP_DEST,
    IDC_FP_ALTN, IDC_FP_CRUISE, IDC_FP_ROUTE, IDC_FP_REMARKS,
    IDC_FP_SUBMIT, IDC_FP_STATUS,
    IDC_LOG, IDC_MSG, IDC_SEND,
    IDC_SRV_ADD, IDC_SRV_DEL,
    IDC_SB_CONN = 300, IDC_SB_XPDR, IDC_SB_FLIGHT, IDC_SB_CS, IDC_SB_DLL,
    IDC_MOCK
};

#define TIMER_APP 1
#define TIMER_MS  100
#define WARN_HIDE_SECS 5.0
#define IDENT_SECS     5.0

/* 主窗口客户区逻辑尺寸（SC() 按 DPI 缩放） */
#define WIN_W 640
#define WIN_H 800

typedef struct { HWND h; bool hover; WNDPROC old; } obtn_t;

typedef struct {
    app_t   app;
    HWND    wnd;
    int     dpi;
    HFONT   f_title, f_section, f_body, f_small, f_code, f_btn, f_log;
    HBRUSH  br_bg, br_card, br_input, br_logbg;
    double  warn_until;
    bool    ident_cooldown;
    double  ident_until;
    HWND    focus_edit;
    int     page_shown;                    /* -1=未初始化 0=连接页 1=工作区 */

    obtn_t  ob[24];
    int     nob;
    WNDPROC old_msg_proc, old_xpdr_proc;

    HWND    page_conn[24]; int n_conn;      /* 连接页控件（页面切换） */
    HWND    page_ws[40];   int n_ws;

    /* 连接页 */
    HWND    ed_callsign, ed_cid, ed_password, ed_realname, ed_model;
    HWND    cb_eco, cb_type, cb_server;
    HWND    btn_connect, lbl_status;
    /* 工作区 */
    HWND    lbl_ws_status, btn_disconnect;
    HWND    ed_xpdr, btn_q[5], btn_stby, btn_alt, btn_ident, lbl_warning;
    HWND    ed_ac, cb_wake, ed_tas, ed_dep, ed_dest, ed_altn, ed_cruise;
    HWND    ed_route, ed_remarks, btn_fp, lbl_fp_status;
    HWND    ed_log, ed_msg, btn_send;
    HWND    sb_conn, sb_xpdr, sb_flight, sb_callsign, sb_dll, btn_mock;
    /* 卡片矩形（父窗口绘制） */
    RECT    rc_card_xp, rc_card_fp, rc_card_log;
    char    password[64];
} gui_t;

extern gui_t G;

/* ── gui.c 提供：缩放 / 文本 / 控件工厂 / 日志 ── */
int SC(int v);
int u16(const char *s, wchar_t *w, int wcap);
int u8(const wchar_t *w, char *s, int cap);
void log_append(const char *line);
void log_msg(app_log_kind_t kind, const char *line);
HFONT mkfont(int h_px, int weight, const wchar_t *face);
HWND mk(HWND parent, const wchar_t *cls, const wchar_t *text,
        DWORD style, int x, int y, int w, int h, int id);
HWND mk_label(HWND parent, const wchar_t *text, int x, int y, int w);
HWND mk_edit(HWND parent, int id, int x, int y, int w, int h);
void reg_btn(HWND h);

/* ── gui_pages.c 提供：页面构建 / 配置转换 / 动作 ── */
void build_conn_page(HWND wnd);
void build_ws_page(HWND wnd);
void sync_pages(void);
void ui_from_cfg(void);
void cfg_from_ui_conn(void);   /* 连接字段（退出保存路径只调此） */
void cfg_from_ui_fp(void);     /* 飞行计划字段（连接/提交时保存） */
void reset_fp_fields(void);    /* 断开重置（对齐 Python reset_fields） */
void apply_eco_type(void);
void do_connect(HWND wnd);
void do_submit_fp(HWND wnd);

#endif /* LINK_GUI_INTERNAL_H */
