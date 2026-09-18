/*
 * gui.c —— Aerofly Link Win32 GUI（P3，Apple 风格深色主题）
 * ================================================================
 * 设计语言：Apple HIG 的 Win32 落地（决策记录见 docs/adr/0006-win32-ui.md）
 *   - 配色：背景 #1C1C1E / 卡片与输入 #2C2C2E / 强调 Apple 蓝 #0A84FF /
 *     主文字 #F5F5F7 / 次要 #98989D / 语义色 绿 #30D158 橙 #FF9F0A 红 #FF453A
 *   - 形式：卡片分区 + 大写小字分节标题（去 GROUPBOX）；标签在输入框上方；
 *     主操作胶囊实心按钮、次操作着色文字（owner-draw）；8pt 间距网格
 *   - 质感：输入框无立体边（深底 + 聚焦 1px 蓝描边）；应答机代码等宽大字；
 *     IDENT 激活期橙色呼吸；DWM 深色标题栏 + 圆角窗口
 *
 * 技术栈：comctl32 v6（外挂 manifest）+ PerMonitorV2 DPI（坐标经 SC() 缩放）
 * 线程模型：单线程 WM_TIMER(100ms) 驱动 app_poll(0)，UI 永不阻塞。
 * 文本：源码 UTF-8，经 u16() 转 UTF-16 后使用 W 系列 API。
 */
#include "link/app.h"

#include <math.h>
#include <stdio.h>
#include <string.h>
#include <windows.h>
#include <commctrl.h>
#include <windowsx.h>
#include <uxtheme.h>
#include <dwmapi.h>

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
    IDC_SB_CONN = 300, IDC_SB_XPDR, IDC_SB_FLIGHT, IDC_SB_DLL, IDC_MOCK
};

#define TIMER_APP 1
#define TIMER_MS  100
#define WARN_HIDE_SECS 5.0
#define IDENT_SECS     5.0

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
    HWND    sb_conn, sb_xpdr, sb_flight, sb_dll, btn_mock;
    /* 卡片矩形（父窗口绘制） */
    RECT    rc_card_xp, rc_card_fp, rc_card_log;
    char    password[64];
} gui_t;

static gui_t G;

/* ── 缩放与文本 ── */
static int SC(int v) { return v * G.dpi / 96; }

static int u16(const char *s, wchar_t *w, int wcap)
{
    return MultiByteToWideChar(CP_UTF8, 0, s, -1, w, wcap);
}
static int u8(const wchar_t *w, char *s, int cap)
{
    return WideCharToMultiByte(CP_UTF8, 0, w, -1, s, cap, NULL, NULL);
}

/* ── 日志追加（超长截半，ADR 0005 内存上限）── */
static void log_append(const char *line)
{
    wchar_t wline[600];
    u16(line, wline, 600);
    int len = GetWindowTextLengthW(G.ed_log);
    if (len > 48000) {
        SendMessageW(G.ed_log, EM_SETSEL, 0, len / 2);
        SendMessageW(G.ed_log, EM_REPLACESEL, FALSE, (LPARAM)L"");
    }
    int end = GetWindowTextLengthW(G.ed_log);
    SendMessageW(G.ed_log, EM_SETSEL, end, end);
    wchar_t with_nl[640];
    _snwprintf(with_nl, 639, L"%s\r\n", wline);
    with_nl[639] = 0;
    SendMessageW(G.ed_log, EM_REPLACESEL, FALSE, (LPARAM)with_nl);
    SendMessageW(G.ed_log, EM_SCROLLCARET, 0, 0);
}

/* ── app 回调 ── */
static void cb_log(void *ud, const char *line) { (void)ud; log_append(line); }

static void cb_status(void *ud, const char *line)
{
    (void)ud;
    wchar_t w[256];
    u16(line, w, 256);
    SetWindowTextW(G.lbl_ws_status, w);
    SetWindowTextW(G.lbl_status, w);
}

static void cb_warning(void *ud, const char *msg)
{
    (void)ud;
    wchar_t w[400];
    u16(msg, w, 400);
    SetWindowTextW(G.lbl_warning, w);
    G.warn_until = net_now() + WARN_HIDE_SECS;
    ShowWindow(G.lbl_warning, SW_SHOW);
}

static void cb_xpdr(void *ud) { (void)ud; }

/* ── owner-draw 按钮的 hover 追踪（子类化 Button 类）── */
static LRESULT CALLBACK btn_proc(HWND h, UINT m, WPARAM wp, LPARAM lp)
{
    if (m == WM_MOUSEMOVE) {
        for (int i = 0; i < G.nob; i++) {
            if (G.ob[i].h == h && !G.ob[i].hover) {
                G.ob[i].hover = true;
                TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE, h, 0 };
                TrackMouseEvent(&tme);
                InvalidateRect(h, NULL, FALSE);
            }
        }
    } else if (m == WM_MOUSELEAVE) {
        for (int i = 0; i < G.nob; i++) {
            if (G.ob[i].h == h && G.ob[i].hover) {
                G.ob[i].hover = false;
                InvalidateRect(h, NULL, FALSE);
            }
        }
    }
    for (int i = 0; i < G.nob; i++) {
        if (G.ob[i].h == h)
            return CallWindowProcW(G.ob[i].old, h, m, wp, lp);
    }
    return DefWindowProcW(h, m, wp, lp);
}

static void reg_btn(HWND h)
{
    if (G.nob >= 24 || !h)
        return;
    G.ob[G.nob].h = h;
    G.ob[G.nob].hover = false;
    G.ob[G.nob].old = (WNDPROC)GetWindowLongPtrW(h, GWLP_WNDPROC);
    G.nob++;
    SetWindowLongPtrW(h, GWLP_WNDPROC, (LONG_PTR)btn_proc);
}

static HFONT mkfont(int h_px, int weight, const wchar_t *face)
{
    return CreateFontW(-h_px, 0, 0, 0, weight, 0, 0, 0,
                       DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY,
                       0, face);
}

/* 控件工厂：默认挂 body 字体 */
static HWND mk(HWND parent, const wchar_t *cls, const wchar_t *text,
               DWORD style, int x, int y, int w, int h, int id)
{
    HWND c = CreateWindowExW(0, cls, text, WS_CHILD | style,
                             x, y, w, h, parent,
                             (HMENU)(INT_PTR)id, NULL, NULL);
    SendMessageW(c, WM_SETFONT, (WPARAM)G.f_body, TRUE);
    return c;
}

static HWND mk_label(HWND parent, const wchar_t *text, int x, int y, int w)
{
    return mk(parent, L"STATIC", text, WS_VISIBLE, x, y, w, SC(18), 0);
}

static HWND mk_edit(HWND parent, int id, int x, int y, int w, int h)
{
    return mk(parent, L"EDIT", L"", WS_VISIBLE | ES_AUTOHSCROLL,
              x, y, w, h, id);
}

/* ── 连接页（标签在输入框上方，8pt 网格）── */
static void build_conn_page(HWND wnd)
{
    int x = SC(24), w = SC(600) - SC(48);
    int y = SC(20);

    wchar_t t[128];
    u16("Aerofly Link", t, 128);
    HWND title = mk(wnd, L"STATIC", t, WS_VISIBLE, x, y, w, SC(34), 0);
    SendMessageW(title, WM_SETFONT, (WPARAM)G.f_title, TRUE);
    G.page_conn[G.n_conn++] = title;
    wchar_t sub[128];
    u16("Aerofly FS 4 联机客户端", sub, 128);
    G.page_conn[G.n_conn++] = mk_label(wnd, sub, x, y + SC(36), w);
    y = SC(96);

    struct { const char *label; HWND *out; int id; bool pw; } fields[] = {
        { "呼号", &G.ed_callsign, IDC_CALLSIGN, false },
        { "CID", &G.ed_cid, IDC_CID, false },
        { "密码", &G.ed_password, IDC_PASSWORD, true },
        { "姓名", &G.ed_realname, IDC_REALNAME, false },
        { "机型", &G.ed_model, IDC_MODEL, false },
    };
    for (int i = 0; i < 5; i++) {
        wchar_t lbl[32];
        u16(fields[i].label, lbl, 32);
        G.page_conn[G.n_conn++] = mk_label(wnd, lbl, x, y, w);
        DWORD es = fields[i].pw ? ES_PASSWORD : 0;
        *fields[i].out = CreateWindowExW(0, L"EDIT", L"",
                                         WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL | es,
                                         x, y + SC(22), w, SC(34), wnd,
                                         (HMENU)(INT_PTR)fields[i].id, NULL, NULL);
        SendMessageW(*fields[i].out, WM_SETFONT, (WPARAM)G.f_body, TRUE);
        G.page_conn[G.n_conn++] = *fields[i].out;
        y += SC(68);
    }

    /* Eco + Type 双列 */
    int cw = (w - SC(24)) / 2;
    wchar_t eco_l[8]; u16("ECO", eco_l, 8);
    G.page_conn[G.n_conn++] = mk_label(wnd, eco_l, x, y, cw);
    G.cb_eco = mk(wnd, L"COMBOBOX", L"",
                  WS_VISIBLE | CBS_DROPDOWN,
                  x, y + SC(22), cw, SC(120), IDC_ECO);
    ComboBox_AddString(G.cb_eco, L"VATSIM");
    ComboBox_AddString(G.cb_eco, L"FSD (private)");
    ComboBox_AddString(G.cb_eco, L"FSD (legacy)");
    ComboBox_SetCurSel(G.cb_eco, 1);
    wchar_t type_l[8]; u16("TYPE", type_l, 8);
    G.page_conn[G.n_conn++] = mk_label(wnd, type_l, x + cw + SC(24), y, cw);
    G.cb_type = mk(wnd, L"COMBOBOX", L"",
                   WS_VISIBLE | CBS_DROPDOWN,
                   x + cw + SC(24), y + SC(22), cw, SC(120), IDC_TYPE);
    ComboBox_AddString(G.cb_type, L"FSD [VATSIM]");
    ComboBox_AddString(G.cb_type, L"FSD (Legacy)");
    ComboBox_SetCurSel(G.cb_type, 1);
    y += SC(68);

    /* 服务器 */
    wchar_t srv_l[16]; u16("SERVER", srv_l, 16);
    G.page_conn[G.n_conn++] = mk_label(wnd, srv_l, x, y, w);
    G.cb_server = CreateWindowExW(0, L"COMBOBOX", L"",
                                  WS_CHILD | WS_VISIBLE | CBS_DROPDOWN,
                                  x, y + SC(22), w, SC(200), wnd,
                                  (HMENU)(INT_PTR)IDC_SERVER, NULL, NULL);
    SendMessageW(G.cb_server, WM_SETFONT, (WPARAM)G.f_body, TRUE);
    G.page_conn[G.n_conn++] = G.cb_server;
    y += SC(68);

    /* 主操作：胶囊实心 */
    wchar_t btn_t[16]; u16("连 接 服 务 器", btn_t, 16);
    G.btn_connect = CreateWindowExW(0, L"BUTTON", btn_t,
                                    WS_CHILD | WS_VISIBLE | BS_OWNERDRAW |
                                    BS_DEFPUSHBUTTON,
                                    x, y, w, SC(44), wnd,
                                    (HMENU)(INT_PTR)IDC_CONNECT, NULL, NULL);
    SendMessageW(G.btn_connect, WM_SETFONT, (WPARAM)G.f_btn, TRUE);
    reg_btn(G.btn_connect);
    y += SC(52);

    G.lbl_status = mk(wnd, L"STATIC", L"", WS_VISIBLE, x, y, w, SC(20),
                      IDC_CONNECT_STATUS);
    SendMessageW(G.lbl_status, WM_SETFONT, (WPARAM)G.f_small, TRUE);

    /* 页面登记（连接按钮/状态也随页切换） */
    G.page_conn[G.n_conn++] = G.btn_connect;
    G.page_conn[G.n_conn++] = G.lbl_status;
}

/* ── 工作区（卡片分区，无 GROUPBOX）── */
static void build_ws_page(HWND wnd)
{
    int x = SC(16), w = SC(600) - SC(32);
    int y = SC(14);

    /* 连接状态行 */
    G.lbl_ws_status = mk(wnd, L"STATIC", L"● 未连接", WS_VISIBLE,
                         x, y + SC(5), w - SC(120), SC(22), IDC_WS_STATUS);
    SendMessageW(G.lbl_ws_status, WM_SETFONT, (WPARAM)G.f_body, TRUE);
    wchar_t dis[16]; u16("断开连接", dis, 16);
    G.btn_disconnect = CreateWindowExW(0, L"BUTTON", dis,
                                       WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
                                       x + w - SC(104), y, SC(104), SC(32), wnd,
                                       (HMENU)(INT_PTR)IDC_WS_DISCONNECT, NULL, NULL);
    SendMessageW(G.btn_disconnect, WM_SETFONT, (WPARAM)G.f_body, TRUE);
    reg_btn(G.btn_disconnect);
    y += SC(46);

    /* ── 卡片：TRANSPONDER ── */
    G.rc_card_xp = (RECT){ x, y, x + w, y + SC(176) };
    HWND lbl;
    wchar_t code_l[8]; u16("代码", code_l, 8);
    lbl = mk_label(wnd, code_l, x + SC(16), y + SC(46), SC(40));
    G.page_ws[G.n_ws++] = lbl;
    G.ed_xpdr = CreateWindowExW(0, L"EDIT", L"1200",
                                WS_CHILD | WS_VISIBLE | ES_CENTER | ES_AUTOHSCROLL,
                                x + SC(16), y + SC(66), SC(150), SC(40), wnd,
                                (HMENU)(INT_PTR)IDC_XPDR_CODE, NULL, NULL);
    SendMessageW(G.ed_xpdr, WM_SETFONT, (WPARAM)G.f_code, TRUE);
    const wchar_t *qtexts[5] = { L"1200", L"7000", L"7700", L"7600", L"7500" };
    const int qids[5] = { IDC_Q1200, IDC_Q7000, IDC_Q7700, IDC_Q7600, IDC_Q7500 };
    for (int i = 0; i < 5; i++) {
        G.btn_q[i] = CreateWindowExW(0, L"BUTTON", qtexts[i],
                                     WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
                                     x + SC(186) + i * SC(74), y + SC(72),
                                     SC(64), SC(30), wnd,
                                     (HMENU)(INT_PTR)qids[i], NULL, NULL);
        SendMessageW(G.btn_q[i], WM_SETFONT, (WPARAM)G.f_body, TRUE);
        reg_btn(G.btn_q[i]);
    }
    /* 分段控件 STBY | ALT（容器轨道由父窗口绘制） */
    G.btn_stby = CreateWindowExW(0, L"BUTTON", L"STBY",
                                 WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
                                 x + SC(16), y + SC(120), SC(110), SC(36), wnd,
                                 (HMENU)(INT_PTR)IDC_STBY, NULL, NULL);
    SendMessageW(G.btn_stby, WM_SETFONT, (WPARAM)G.f_btn, TRUE);
    reg_btn(G.btn_stby);
    G.btn_alt = CreateWindowExW(0, L"BUTTON", L"ALT",
                                WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
                                x + SC(16) + SC(110), y + SC(120),
                                SC(110), SC(36), wnd,
                                (HMENU)(INT_PTR)IDC_ALT, NULL, NULL);
    SendMessageW(G.btn_alt, WM_SETFONT, (WPARAM)G.f_btn, TRUE);
    reg_btn(G.btn_alt);
    wchar_t idt_t[8]; u16("IDENT", idt_t, 8);
    G.btn_ident = CreateWindowExW(0, L"BUTTON", idt_t,
                                  WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
                                  x + SC(260), y + SC(120), SC(110), SC(36), wnd,
                                  (HMENU)(INT_PTR)IDC_IDENT, NULL, NULL);
    SendMessageW(G.btn_ident, WM_SETFONT, (WPARAM)G.f_btn, TRUE);
    reg_btn(G.btn_ident);
    G.lbl_warning = CreateWindowExW(0, L"STATIC", L"", WS_CHILD,
                                    x + SC(390), y + SC(114), w - SC(390) - SC(16),
                                    SC(56), wnd,
                                    (HMENU)(INT_PTR)IDC_XPDR_WARNING, NULL, NULL);
    SendMessageW(G.lbl_warning, WM_SETFONT, (WPARAM)G.f_small, TRUE);
    y += SC(192);

    /* ── 卡片：FLIGHT PLAN（3×3 网格 + 航路/备注/提交行）── */
    G.rc_card_fp = (RECT){ x, y, x + w, y + SC(270) };
    int cw = (w - SC(32) - SC(32)) / 3;
    struct { const char *label; HWND *out; HWND *lbl_out; int id; } grid[] = {
        { "机型", &G.ed_ac,      NULL,        IDC_FP_AIRCRAFT },
        { "TAS",  &G.ed_tas,     NULL,        IDC_FP_TAS },
        { "尾流", &G.cb_wake,    NULL,        IDC_FP_WAKE },
        { "起飞", &G.ed_dep,     NULL,        IDC_FP_DEP },
        { "降落", &G.ed_dest,    NULL,        IDC_FP_DEST },
        { "备降", &G.ed_altn,    NULL,        IDC_FP_ALTN },
        { "巡航", &G.ed_cruise,  NULL,        IDC_FP_CRUISE },
    };
    for (int i = 0; i < 7; i++) {
        int cx = x + SC(16) + (i % 3) * (cw + SC(16));
        int cy = y + SC(40) + (i / 3) * SC(56);
        wchar_t lblt[16];
        u16(grid[i].label, lblt, 16);
        HWND lb = mk_label(wnd, lblt, cx, cy, cw);
        G.page_ws[G.n_ws++] = lb;
        wchar_t *empty = L"";
        if (grid[i].id == IDC_FP_WAKE) {
            *grid[i].out = mk(wnd, L"COMBOBOX", L"",
                              WS_VISIBLE | CBS_DROPDOWN,
                              cx, cy + SC(20), cw, SC(120), IDC_FP_WAKE);
            ComboBox_AddString(G.cb_wake, L"Light");
            ComboBox_AddString(G.cb_wake, L"Medium");
            ComboBox_AddString(G.cb_wake, L"Heavy");
            ComboBox_AddString(G.cb_wake, L"Super");
            ComboBox_SetCurSel(G.cb_wake, 1);
        } else {
            *grid[i].out = mk_edit(wnd, grid[i].id, cx, cy + SC(20), cw, SC(30));
        }
        (void)empty;
    }
    /* 航路（第 3 行跨 2 列） */
    int r3y = y + SC(152);
    wchar_t route_l[8]; u16("航路", route_l, 8);
    lbl = mk_label(wnd, route_l, x + SC(16) + cw + SC(16), r3y, cw * 2 + SC(16));
    G.page_ws[G.n_ws++] = lbl;
    G.ed_route = mk_edit(wnd, IDC_FP_ROUTE, x + SC(16) + cw + SC(16),
                         r3y + SC(20), cw * 2 + SC(16), SC(30));
    /* 备注（第 4 行跨 2 列）+ 提交按钮 */
    int r4y = y + SC(208);
    wchar_t rmk_l[8]; u16("备注", rmk_l, 8);
    lbl = mk_label(wnd, rmk_l, x + SC(16), r4y, cw * 2 + SC(16));
    G.page_ws[G.n_ws++] = lbl;
    G.ed_remarks = mk_edit(wnd, IDC_FP_REMARKS, x + SC(16),
                           r4y + SC(20), cw * 2 + SC(16), SC(30));
    wchar_t fpb_t[20]; u16("提交飞行计划", fpb_t, 20);
    G.btn_fp = CreateWindowExW(0, L"BUTTON", fpb_t,
                               WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
                               x + SC(16) + cw * 2 + SC(32), r4y + SC(20),
                               cw, SC(30), wnd,
                               (HMENU)(INT_PTR)IDC_FP_SUBMIT, NULL, NULL);
    SendMessageW(G.btn_fp, WM_SETFONT, (WPARAM)G.f_body, TRUE);
    reg_btn(G.btn_fp);
    G.lbl_fp_status = mk(wnd, L"STATIC", L"", WS_VISIBLE,
                         x + SC(16) + cw * 2 + SC(32), r4y + SC(54),
                         cw, SC(18), IDC_FP_STATUS);
    SendMessageW(G.lbl_fp_status, WM_SETFONT, (WPARAM)G.f_small, TRUE);
    y += SC(286);

    /* ── 卡片：ATC MESSAGES ── */
    G.rc_card_log = (RECT){ x, y, x + w, y + SC(212) };
    G.ed_log = CreateWindowExW(0, L"EDIT", L"",
                               WS_CHILD | WS_VISIBLE | ES_MULTILINE |
                               ES_READONLY | ES_AUTOVSCROLL | WS_VSCROLL,
                               x + SC(16), y + SC(40), w - SC(32), SC(110), wnd,
                               (HMENU)(INT_PTR)IDC_LOG, NULL, NULL);
    SendMessageW(G.ed_log, WM_SETFONT, (WPARAM)G.f_log, TRUE);
    G.ed_msg = CreateWindowExW(0, L"EDIT", L"",
                               WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
                               x + SC(16), y + SC(162), w - SC(32) - SC(90),
                               SC(32), wnd, (HMENU)(INT_PTR)IDC_MSG, NULL, NULL);
    SendMessageW(G.ed_msg, WM_SETFONT, (WPARAM)G.f_body, TRUE);
    wchar_t send_t[8]; u16("发送", send_t, 8);
    G.btn_send = CreateWindowExW(0, L"BUTTON", send_t,
                                 WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
                                 x + w - SC(16) - SC(74), y + SC(162),
                                 SC(74), SC(32), wnd,
                                 (HMENU)(INT_PTR)IDC_SEND, NULL, NULL);
    SendMessageW(G.btn_send, WM_SETFONT, (WPARAM)G.f_body, TRUE);
    reg_btn(G.btn_send);

    /* 工作区控件登记（页面切换） */
    HWND *ws = G.page_ws;
    int *n = &G.n_ws;
    ws[(*n)++] = G.lbl_ws_status;  ws[(*n)++] = G.btn_disconnect;
    ws[(*n)++] = G.ed_xpdr;        ws[(*n)++] = G.lbl_warning;
    for (int i = 0; i < 5; i++) ws[(*n)++] = G.btn_q[i];
    ws[(*n)++] = G.btn_stby;       ws[(*n)++] = G.btn_alt;
    ws[(*n)++] = G.btn_ident;      ws[(*n)++] = G.btn_fp;
    ws[(*n)++] = G.ed_ac;          ws[(*n)++] = G.cb_wake;
    ws[(*n)++] = G.ed_tas;         ws[(*n)++] = G.ed_dep;
    ws[(*n)++] = G.ed_dest;        ws[(*n)++] = G.ed_altn;
    ws[(*n)++] = G.ed_cruise;      ws[(*n)++] = G.ed_route;
    ws[(*n)++] = G.ed_remarks;     ws[(*n)++] = G.lbl_fp_status;
    ws[(*n)++] = G.ed_log;         ws[(*n)++] = G.ed_msg;
    ws[(*n)++] = G.btn_send;
}

/* ── 页面切换 ── */
static void set_connected_ui(bool on)
{
    for (int i = 0; i < G.n_conn; i++)
        ShowWindow(G.page_conn[i], on ? SW_HIDE : SW_SHOW);
    for (int i = 0; i < G.n_ws; i++)
        ShowWindow(G.page_ws[i], on ? SW_SHOW : SW_HIDE);
}

static int g_page_shown = -1;
static void sync_pages(void)
{
    int now = (G.app.sess.state != SESS_DISCONNECTED) ? 1 : 0;
    if (now != g_page_shown) {
        g_page_shown = now;
        set_connected_ui(now);
    }
}

/* ── 配置 ↔ UI ── */
static void ui_from_cfg(void)
{
    wchar_t w[256];
    cfg_t *c = &G.app.cfg;

    u16(c->callsign, w, 256); SetWindowTextW(G.ed_callsign, w);
    u16(c->cid, w, 256); SetWindowTextW(G.ed_cid, w);
    u16(c->realname, w, 256); SetWindowTextW(G.ed_realname, w);
    u16(c->model, w, 256); SetWindowTextW(G.ed_model, w);
    for (size_t i = 0; i < c->nservers; i++) {
        wchar_t wsrv[128];
        u16(c->servers[i], wsrv, 128);
        ComboBox_AddString(G.cb_server, wsrv);
    }
    char full[160];
    _snprintf(full, sizeof(full) - 1, "%s:%d", c->server, c->port);
    full[sizeof(full) - 1] = '\0';
    u16(full, w, 256);
    SetWindowTextW(G.cb_server, w);

    u16(c->fp_aircraft, w, 256); SetWindowTextW(G.ed_ac, w);
    u16(c->fp_tas, w, 256); SetWindowTextW(G.ed_tas, w);
    u16(c->fp_dep, w, 256); SetWindowTextW(G.ed_dep, w);
    u16(c->fp_dest, w, 256); SetWindowTextW(G.ed_dest, w);
    u16(c->fp_altn, w, 256); SetWindowTextW(G.ed_altn, w);
    u16(c->fp_cruise, w, 256); SetWindowTextW(G.ed_cruise, w);
    u16(c->fp_route, w, 256); SetWindowTextW(G.ed_route, w);
    u16(c->fp_remarks, w, 256); SetWindowTextW(G.ed_remarks, w);
    const char *wake_names[] = { "Light", "Medium", "Heavy", "Super" };
    for (int i = 0; i < 4; i++)
        if (strcmp(c->fp_wake, wake_names[i]) == 0)
            ComboBox_SetCurSel(G.cb_wake, i);
    int eco_sel = strcmp(c->eco, "vatsim") == 0 ? 0
                : strcmp(c->eco, "legacy") == 0 ? 2 : 1;
    ComboBox_SetCurSel(G.cb_eco, eco_sel);
}

static void cfg_from_ui(void)
{
    cfg_t *c = &G.app.cfg;
    wchar_t w[256];
    char s[256];

#define GETTEXT(hwnd, dst, cap) do { \
    GetWindowTextW(hwnd, w, 256); u8(w, s, 256); \
    strncpy(dst, s, (cap) - 1); dst[(cap) - 1] = '\0'; } while (0)

    GETTEXT(G.ed_callsign, c->callsign, sizeof(c->callsign));
    for (char *p = c->callsign; *p; p++)
        *p = (char)(*p >= 'a' && *p <= 'z' ? *p - 32 : *p);
    GETTEXT(G.ed_cid, c->cid, sizeof(c->cid));
    GETTEXT(G.ed_realname, c->realname, sizeof(c->realname));
    GETTEXT(G.ed_model, c->model, sizeof(c->model));
    GETTEXT(G.cb_server, s, 256);
    {
        char host[128] = "";
        int port = 6809;
        if (sscanf(s, "%127[^:]:%d", host, &port) >= 1) {
            strncpy(c->server, host, sizeof(c->server) - 1);
            c->server[sizeof(c->server) - 1] = '\0';
            c->port = port;
        }
    }
    GETTEXT(G.ed_ac, c->fp_aircraft, sizeof(c->fp_aircraft));
    GETTEXT(G.ed_tas, c->fp_tas, sizeof(c->fp_tas));
    GETTEXT(G.ed_dep, c->fp_dep, sizeof(c->fp_dep));
    GETTEXT(G.ed_dest, c->fp_dest, sizeof(c->fp_dest));
    GETTEXT(G.ed_altn, c->fp_altn, sizeof(c->fp_altn));
    GETTEXT(G.ed_cruise, c->fp_cruise, sizeof(c->fp_cruise));
    GETTEXT(G.ed_route, c->fp_route, sizeof(c->fp_route));
    GETTEXT(G.ed_remarks, c->fp_remarks, sizeof(c->fp_remarks));
#undef GETTEXT

    GetWindowTextW(G.cb_wake, w, 256);
    u8(w, s, 256);
    {
        const char *wake_names[] = { "Light", "Medium", "Heavy", "Super" };
        int wk = 1;   /* 缺省 Medium */
        for (int i = 0; i < 4; i++)
            if (strcmp(s, wake_names[i]) == 0)
                wk = i;
        strncpy(c->fp_wake, wake_names[wk], sizeof(c->fp_wake) - 1);
    }

    for (size_t i = 0; i < c->nservers; i++)
        if (strcmp(c->servers[i], s) == 0)
            return;
    if (c->nservers < CFG_MAX_SERVERS && s[0]) {
        strncpy(c->servers[c->nservers], s, JSN_STR_CAP - 1);
        c->nservers++;
    }
}

static void apply_eco_type(void)
{
    int eco = ComboBox_GetCurSel(G.cb_eco);
    if (eco < 0) {
        wchar_t we[32];
        char se[32];
        GetWindowTextW(G.cb_eco, we, 32);
        u8(we, se, 32);
        eco = strstr(se, "VATSIM") ? 0 : strstr(se, "legacy") ? 2 : 1;
        ComboBox_SetCurSel(G.cb_eco, eco);
    }
    int type;
    switch (eco) {
    case 0:  type = 0; ComboBox_Enable(G.cb_type, FALSE); break;
    case 2:  type = 1; ComboBox_Enable(G.cb_type, FALSE); break;
    default: type = ComboBox_GetCurSel(G.cb_type) < 0 ? 0
                     : ComboBox_GetCurSel(G.cb_type);
             ComboBox_Enable(G.cb_type, TRUE); break;
    }
    ComboBox_SetCurSel(G.cb_type, type);
}

/* ── owner-draw 绘制 ── */

static void fill_round(HDC dc, RECT rc, COLORREF c, int r)
{
    HBRUSH br = CreateSolidBrush(c);
    HBRUSH old = SelectObject(dc, br);
    HPEN pen = CreatePen(PS_NULL, 0, 0);
    HPEN open_ = SelectObject(dc, pen);
    RoundRect(dc, rc.left, rc.top, rc.right + 1, rc.bottom + 1, r, r);
    SelectObject(dc, old);
    SelectObject(dc, open_);
    DeleteObject(br);
    DeleteObject(pen);
}

static void frame_round(HDC dc, RECT rc, COLORREF c, int r)
{
    HPEN pen = CreatePen(PS_SOLID, 1, c);
    HPEN open_ = SelectObject(dc, pen);
    HBRUSH br = GetStockObject(NULL_BRUSH);
    HBRUSH old = SelectObject(dc, br);
    RoundRect(dc, rc.left, rc.top, rc.right, rc.bottom, r, r);
    SelectObject(dc, old);
    SelectObject(dc, open_);
    DeleteObject(pen);
}

static void draw_center_text(HDC dc, RECT rc, const wchar_t *text,
                             COLORREF c, HFONT f)
{
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, c);
    HFONT old = SelectObject(dc, f);
    DrawTextW(dc, text, -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    SelectObject(dc, old);
}

static COLORREF lerp_color(COLORREF a, COLORREF b, double t)
{
    return RGB((int)(GetRValue(a) + (GetRValue(b) - GetRValue(a)) * t),
               (int)(GetGValue(a) + (GetGValue(b) - GetGValue(a)) * t),
               (int)(GetBValue(a) + (GetBValue(b) - GetBValue(a)) * t));
}

/* 按钮分类绘制（Apple 语义：主操作实心胶囊 / 次操作着色 / 分段 / 开关） */
static void draw_btn_item(DRAWITEMSTRUCT *d)
{
    HDC dc = d->hDC;
    RECT rc = d->rcItem;
    int id = (int)d->CtlID;
    bool hover = false, pressed = (d->itemState & ODS_SELECTED) != 0;
    for (int i = 0; i < G.nob; i++)
        if (G.ob[i].h == d->hwndItem) hover = G.ob[i].hover;

    /* 先以所在区域底色清底：圆角外的四角不露默认白底 */
    bool on_card = (id != IDC_CONNECT && id != IDC_MOCK);
    RECT full = rc;
    HBRUSH clearbr = CreateSolidBrush(on_card ? C_CARD : C_BG);
    FillRect(dc, &full, clearbr);
    DeleteObject(clearbr);

    if (id == IDC_CONNECT) {
        /* 主操作：胶囊实心，白字 */
        COLORREF fill = pressed ? C_ACC_PRS : hover ? C_ACC_HOV : C_ACCENT;
        int r = rc.bottom - rc.top;
        fill_round(dc, rc, fill, r);
        draw_center_text(dc, rc, L"连 接 服 务 器", RGB(255, 255, 255), G.f_btn);
        return;
    }
    if (id == IDC_STBY || id == IDC_ALT) {
        /* 分段控件：选中段实心强调色，未选中透明灰字 */
        bool sel = (id == IDC_ALT) ? G.app.xpdr.alt_mode
                                   : !G.app.xpdr.alt_mode;
        if (sel) {
            fill_round(dc, rc, pressed ? C_ACC_PRS : C_ACCENT,
                       SC(17));
            draw_center_text(dc, rc, L"STBY", RGB(255, 255, 255), G.f_btn);
            if (id == IDC_ALT)
                draw_center_text(dc, rc, L"ALT", RGB(255, 255, 255), G.f_btn);
        } else {
            fill_round(dc, rc, hover ? C_HOVER : C_BG, SC(17));
            draw_center_text(dc, rc, id == IDC_ALT ? L"ALT" : L"STBY",
                             C_DIM, G.f_btn);
        }
        return;
    }
    if (id == IDC_IDENT) {
        /* 警示语义色；激活期橙色呼吸 */
        if (G.ident_cooldown) {
            double t = 0.5 + 0.5 * sin(net_now() * 5.0);
            COLORREF fill = lerp_color(C_ORANGE,
                                       RGB(0xFF, 0xC4, 0x6B), t);
            fill_round(dc, rc, fill, SC(10));
            draw_center_text(dc, rc, L"IDENT", RGB(255, 255, 255), G.f_btn);
        } else {
            fill_round(dc, rc, pressed ? C_HOVER
                                       : hover ? RGB(0x45, 0x45, 0x48) : C_CARD,
                       SC(10));
            draw_center_text(dc, rc, L"IDENT", C_ORANGE, G.f_btn);
        }
        return;
    }
    if (id == IDC_MOCK) {
        /* Apple 开关：轨道 + 圆形旋钮 */
        bool on = G.app.mock_on;
        RECT track = rc;
        track.right = track.left + (rc.bottom - rc.top) * 2 - SC(2);
        int r = track.bottom - track.top;
        fill_round(dc, track, on ? C_ACCENT : C_HOVER, r);
        int kn = r - SC(6);
        RECT knob = { 0, 0, kn, kn };
        if (on)
            OffsetRect(&knob, track.right - r + SC(3), track.top + SC(3));
        else
            OffsetRect(&knob, track.left + SC(3), track.top + SC(3));
        HBRUSH br = CreateSolidBrush(RGB(255, 255, 255));
        HBRUSH old = SelectObject(dc, br);
        HPEN pen = CreatePen(PS_NULL, 0, 0);
        HPEN open_ = SelectObject(dc, pen);
        Ellipse(dc, knob.left, knob.top, knob.right + 1, knob.bottom + 1);
        SelectObject(dc, old);
        SelectObject(dc, open_);
        DeleteObject(br);
        DeleteObject(pen);
        return;
    }

    /* 次操作：卡片底 + 着色文字 */
    COLORREF text = C_ACCENT;
    if (id == IDC_WS_DISCONNECT) text = C_RED;
    else if (id == IDC_Q7700 || id == IDC_Q7500) text = C_RED;
    else if (id == IDC_Q7600) text = C_ORANGE;
    fill_round(dc, rc, pressed ? C_HOVER : hover ? RGB(0x36, 0x36, 0x38)
                                                 : C_CARD,
               SC(8));
    wchar_t txt[32];
    GetWindowTextW(d->hwndItem, txt, 32);
    draw_center_text(dc, rc, txt, text, G.f_body);
}

/* ── 父窗口绘制：背景 / 卡片 / 分节标题 / 聚焦描边 ── */

static void draw_section_label(HDC dc, int x, int y, const wchar_t *text)
{
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, C_DIM);
    HFONT old = SelectObject(dc, G.f_section);
    TextOutW(dc, x, y, text, (int)wcslen(text));
    SelectObject(dc, old);
}

static void on_paint(HWND wnd)
{
    PAINTSTRUCT ps;
    HDC dc = BeginPaint(wnd, &ps);
    RECT rc;
    GetClientRect(wnd, &rc);
    FillRect(dc, &rc, G.br_bg);

    if (g_page_shown == 1) {
        /* 三张卡片 + 分节标题 */
        fill_round(dc, G.rc_card_xp, C_CARD, SC(12));
        fill_round(dc, G.rc_card_fp, C_CARD, SC(12));
        fill_round(dc, G.rc_card_log, C_CARD, SC(12));
        draw_section_label(dc, G.rc_card_xp.left + SC(16),
                           G.rc_card_xp.top + SC(12), L"TRANSPONDER");
        draw_section_label(dc, G.rc_card_fp.left + SC(16),
                           G.rc_card_fp.top + SC(12), L"FLIGHT PLAN");
        draw_section_label(dc, G.rc_card_log.left + SC(16),
                           G.rc_card_log.top + SC(12), L"ATC MESSAGES");

        /* 分段控件容器轨道 */
        RECT seg = { G.rc_card_xp.left + SC(15), G.rc_card_xp.top + SC(119),
                     G.rc_card_xp.left + SC(15) + SC(221),
                     G.rc_card_xp.top + SC(119) + SC(38) };
        fill_round(dc, seg, C_BG, SC(18));
    }

    /* 聚焦输入框 1px 蓝描边 */
    if (G.focus_edit && IsWindowVisible(G.focus_edit)) {
        RECT er;
        GetWindowRect(G.focus_edit, &er);
        POINT pt = { er.left, er.top };
        ScreenToClient(wnd, &pt);
        RECT br = { pt.x - SC(2), pt.y - SC(2),
                    pt.x + (er.right - er.left) + SC(2),
                    pt.y + (er.bottom - er.top) + SC(2) };
        frame_round(dc, br, C_ACCENT, SC(8));
    }

    /* 状态栏分隔线 */
    RECT sb;
    GetClientRect(wnd, &sb);
    sb.top = sb.bottom - SC(44);
    sb.bottom = sb.top + 1;
    FillRect(dc, &sb, G.br_input);
    EndPaint(wnd, &ps);
}

/* ── 静态文字颜色：按控件语义映射 ── */
static LRESULT on_ctlcolor_static(HWND ctrl, HDC dc)
{
    SetBkMode(dc, TRANSPARENT);
    int id = GetDlgCtrlID(ctrl);
    COLORREF text = C_DIM;
    HBRUSH bg = G.br_bg;

    if (id == IDC_WS_STATUS || id == IDC_FP_STATUS) {
        text = C_TEXT;
        bg = (id == IDC_FP_STATUS) ? G.br_card : G.br_bg;
    } else if (id == IDC_XPDR_WARNING) {
        text = C_ORANGE;
        bg = G.br_card;
    } else if (id == IDC_CONNECT_STATUS) {
        text = C_TEXT;
    } else {
        /* 卡片上的小标签：机型/TAS/尾流/起飞/降落/备降/航路/备注/代码 */
        POINT pt;
        RECT r;
        GetWindowRect(ctrl, &r);
        pt.x = r.left;
        pt.y = r.top;
        ScreenToClient(G.wnd, &pt);
        POINT sz = { r.right - r.left, r.bottom - r.top };
        RECT test = { pt.x, pt.y, pt.x + sz.x, pt.y + sz.y };
        RECT inter;
        if (IntersectRect(&inter, &test, &G.rc_card_xp)
            || IntersectRect(&inter, &test, &G.rc_card_fp)
            || IntersectRect(&inter, &test, &G.rc_card_log))
            bg = G.br_card;
    }
    SetTextColor(dc, text);
    SetBkColor(dc, bg == G.br_card ? C_CARD : C_BG);
    return (LRESULT)bg;
}

static LRESULT on_ctlcolor_edit(HWND ctrl, HDC dc)
{
    int id = GetDlgCtrlID(ctrl);
    SetBkMode(dc, TRANSPARENT);
    if (id == IDC_XPDR_CODE) {
        SetTextColor(dc, C_GREEN);
        SetBkColor(dc, C_INPUT);
        return (LRESULT)G.br_input;
    }
    SetTextColor(dc, C_TEXT);
    SetBkColor(dc, (id == IDC_LOG) ? C_LOGBG : C_INPUT);
    return (id == IDC_LOG) ? (LRESULT)G.br_logbg : (LRESULT)G.br_input;
}

/* ── 定时器 ── */
static void on_timer(HWND wnd)
{
    app_poll(&G.app, 0);
    sync_pages();

    if (G.warn_until && net_now() > G.warn_until) {
        ShowWindow(G.lbl_warning, SW_HIDE);
        G.warn_until = 0;
    }
    if (G.ident_cooldown && net_now() > G.ident_until) {
        G.ident_cooldown = false;
        InvalidateRect(wnd, NULL, FALSE);
    }
    if (G.ident_cooldown)
        InvalidateRect(wnd, NULL, FALSE);   /* IDENT 呼吸动画 */

    char conn[64] = "", xpdr[64] = "", flight[64] = "", dll[64] = "";
    app_status_text(&G.app, conn, sizeof(conn), xpdr, sizeof(xpdr),
                    flight, sizeof(flight), dll, sizeof(dll));
    wchar_t w[128];
    u16(conn, w, 128); SetWindowTextW(G.sb_conn, w);
    u16(xpdr, w, 128); SetWindowTextW(G.sb_xpdr, w);
    u16(dll, w, 128);  SetWindowTextW(G.sb_dll, w);

    int nearby = app_nearby_count(&G.app);
    char fl[96];
    if (nearby > 0)
        _snprintf(fl, sizeof(fl) - 1, "%s | 附近 %d 架", flight, nearby);
    else
        _snprintf(fl, sizeof(fl) - 1, "%s", flight);
    fl[sizeof(fl) - 1] = '\0';
    u16(fl, w, 128); SetWindowTextW(G.sb_flight, w);

    if (GetFocus() != G.ed_xpdr) {
        char cur[16];
        GetWindowTextA(G.ed_xpdr, cur, sizeof(cur));
        if (strcmp(cur, G.app.xpdr.squawk) != 0)
            SetWindowTextA(G.ed_xpdr, G.app.xpdr.squawk);
    }
}

/* ── 动作 ── */
static void do_connect(HWND wnd)
{
    cfg_from_ui();
    if (!G.app.cfg.callsign[0] || !G.app.cfg.cid[0]) {
        MessageBoxW(wnd, L"请填写呼号和 CID", L"配置不完整", MB_ICONWARNING);
        return;
    }
    cfg_t *c = &G.app.cfg;
    strncpy(G.app.sess.callsign, c->callsign, sizeof(G.app.sess.callsign) - 1);
    strncpy(G.app.sess.cid, c->cid, sizeof(G.app.sess.cid) - 1);
    strncpy(G.app.sess.realname, c->realname, sizeof(G.app.sess.realname) - 1);
    strncpy(G.app.sess.server, c->server, sizeof(G.app.sess.server) - 1);
    G.app.sess.port = c->port;
    G.app.sess.rating = c->rating;
    G.app.sess.vatsim = (ComboBox_GetCurSel(G.cb_type) == 0);

    /* 密码仅内存传递（ADR 0003） */
    {
        wchar_t wpw[64];
        GetWindowTextW(G.ed_password, wpw, 64);
        u8(wpw, G.password, sizeof(G.password));
        app_set_password(&G.app, G.password);
    }

    char path[MAX_PATH];
    cfg_default_path(path, sizeof(path));
    app_connect_fsd(&G.app);
    cfg_save(&G.app.cfg, path);
}

static void do_submit_fp(HWND wnd)
{
    cfg_from_ui();
    cfg_t *c = &G.app.cfg;
#define FPCOPY(field, key) do { \
    strncpy(G.app.field, c->key, sizeof(G.app.field) - 1); \
    G.app.field[sizeof(G.app.field) - 1] = '\0'; } while (0)
    strcpy(G.app.fp_type, "I");
    FPCOPY(fp_aircraft_buf, fp_aircraft); FPCOPY(fp_wake_buf, fp_wake);
    FPCOPY(fp_tas_buf, fp_tas);           FPCOPY(fp_dep_buf, fp_dep);
    FPCOPY(fp_dest_buf, fp_dest);         FPCOPY(fp_altn_buf, fp_altn);
    FPCOPY(fp_cruise_buf, fp_cruise);     FPCOPY(fp_route_buf, fp_route);
    FPCOPY(fp_remarks_buf, fp_remarks);   FPCOPY(fp_eet_buf, fp_eet);
    FPCOPY(fp_endur_buf, fp_endur);
    strcpy(G.app.fp_dep_time_buf, "0");
    strcpy(G.app.fp_act_buf, "0");
#undef FPCOPY
    if (!G.app.fp_aircraft_buf[0] || !G.app.fp_tas_buf[0]
        || !G.app.fp_dep_buf[0] || !G.app.fp_dest_buf[0]) {
        SetWindowTextW(G.lbl_fp_status, L"机型 / TAS / 起降机场为必填");
        return;
    }
    if (app_submit_flightplan(&G.app) == 0)
        SetWindowTextW(G.lbl_fp_status, L"飞行计划已提交");
    else
        SetWindowTextW(G.lbl_fp_status, L"提交失败（未连接？）");
    (void)wnd;
}

static void on_command(HWND wnd, int id, int code)
{
    wchar_t w[512];
    char s[512];

    switch (id) {
    case IDC_CONNECT:       do_connect(wnd); break;
    case IDC_WS_DISCONNECT: app_disconnect_fsd(&G.app); break;
    case IDC_ECO:           if (code == CBN_SELCHANGE) apply_eco_type(); break;
    case IDC_STBY:          app_set_xpdr_mode(&G.app, false); break;
    case IDC_ALT:           app_set_xpdr_mode(&G.app, true); break;
    case IDC_IDENT:
        app_ident(&G.app);
        G.ident_cooldown = true;
        G.ident_until = net_now() + IDENT_SECS;
        InvalidateRect(wnd, NULL, FALSE);
        break;
    case IDC_FP_SUBMIT:     do_submit_fp(wnd); break;
    case IDC_MOCK: {
        bool want = !G.app.mock_on;
        const char *err = app_toggle_mock(&G.app, want);
        if (err) {
            wchar_t msg[256];
            u16(err, msg, 256);
            MessageBoxW(wnd, msg, L"模拟DLL", MB_ICONWARNING);
        }
        InvalidateRect(wnd, NULL, FALSE);
        break;
    }
    case IDC_SEND: {
        GetWindowTextW(G.ed_msg, w, 512);
        u8(w, s, 512);
        if (s[0] && app_send_chat(&G.app, s) == 0)
            SetWindowTextW(G.ed_msg, L"");
        break;
    }
    case IDC_Q1200: case IDC_Q7000: case IDC_Q7700:
    case IDC_Q7600: case IDC_Q7500: {
        static const char *codes[5] = { "1200", "7000", "7700", "7600", "7500" };
        app_set_xpdr_code(&G.app, codes[id - IDC_Q1200]);
        break;
    }
    default:
        /* 输入框聚焦描边：焦点进出即重绘 */
        if (code == EN_SETFOCUS) {
            G.focus_edit = GetDlgItem(wnd, id);
            InvalidateRect(wnd, NULL, FALSE);
        } else if (code == EN_KILLFOCUS) {
            G.focus_edit = NULL;
            InvalidateRect(wnd, NULL, FALSE);
        }
        break;
    }
}

/* ── 输入框 Enter（子类化：消息发送 / 代码确认）── */
static LRESULT CALLBACK msg_edit_proc(HWND h, UINT m, WPARAM wp, LPARAM lp)
{
    if (m == WM_KEYDOWN && wp == VK_RETURN) {
        wchar_t w[512];
        char s[512];
        GetWindowTextW(h, w, 512);
        u8(w, s, 512);
        if (s[0] && app_send_chat(&G.app, s) == 0)
            SetWindowTextW(h, L"");
        return 0;
    }
    return CallWindowProcW(G.old_msg_proc, h, m, wp, lp);
}

static LRESULT CALLBACK xpdr_edit_proc(HWND h, UINT m, WPARAM wp, LPARAM lp)
{
    if (m == WM_KEYDOWN && wp == VK_RETURN) {
        wchar_t w[16];
        char s[16];
        GetWindowTextW(h, w, 16);
        u8(w, s, 16);
        app_set_xpdr_code(&G.app, s);
        return 0;
    }
    return CallWindowProcW(G.old_xpdr_proc, h, m, wp, lp);
}

/* ── 主窗口过程 ── */
static LRESULT CALLBACK wnd_proc(HWND wnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_CREATE: {
        G.wnd = wnd;
        G.dpi = GetDpiForWindow(wnd);
        if (G.dpi <= 0)
            G.dpi = 96;

        /* 按 DPI 校准客户区 SC(600)×SC(760)（DPI 在创建后才可知） */
        {
            DWORD fstyle = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
            RECT frame = { 0, 0, SC(600), SC(800) };
            AdjustWindowRectExForDpi(&frame, fstyle, FALSE, 0, (UINT)G.dpi);
            SetWindowPos(wnd, NULL, 0, 0,
                         frame.right - frame.left, frame.bottom - frame.top,
                         SWP_NOMOVE | SWP_NOZORDER);
        }

        /* 字体层级：标题 24 / 正文 14 / 按钮 14 半粗 / 小字 12 / 代码 24 等宽 */
        G.f_title  = mkfont(SC(24), FW_SEMIBOLD, L"Segoe UI");
        G.f_section = mkfont(SC(12), FW_SEMIBOLD, L"Segoe UI");
        G.f_body   = mkfont(SC(14), FW_NORMAL, L"Segoe UI");
        G.f_small  = mkfont(SC(12), FW_NORMAL, L"Segoe UI");
        G.f_btn    = mkfont(SC(14), FW_SEMIBOLD, L"Segoe UI");
        G.f_code   = mkfont(SC(24), FW_BOLD, L"Consolas");
        G.f_log    = mkfont(SC(13), FW_NORMAL, L"Consolas");

        G.br_bg = CreateSolidBrush(C_BG);
        G.br_card = CreateSolidBrush(C_CARD);
        G.br_input = CreateSolidBrush(C_INPUT);
        G.br_logbg = CreateSolidBrush(C_LOGBG);

        /* DWM：深色标题栏 + 圆角窗口（Win11；旧系统静默忽略） */
        BOOL dark = TRUE;
        DwmSetWindowAttribute(wnd, 20, &dark, sizeof(dark));   /* IMMERSIVE_DARK_MODE */
        INT round_pref = 2;                                    /* DWMWCP_ROUND */
        DwmSetWindowAttribute(wnd, 33, &round_pref, sizeof(round_pref));

        build_conn_page(wnd);
        build_ws_page(wnd);

        /* 输入框 Enter 子类化 */
        G.old_msg_proc = (WNDPROC)SetWindowLongPtrW(
            G.ed_msg, GWLP_WNDPROC, (LONG_PTR)msg_edit_proc);
        G.old_xpdr_proc = (WNDPROC)SetWindowLongPtrW(
            G.ed_xpdr, GWLP_WNDPROC, (LONG_PTR)xpdr_edit_proc);

        /* 暗色系统控件主题（下拉列表/滚动条），失败无妨 */
        SetWindowTheme(G.cb_eco, L"DarkMode_Explorer", NULL);
        SetWindowTheme(G.cb_type, L"DarkMode_Explorer", NULL);
        SetWindowTheme(G.cb_server, L"DarkMode_Explorer", NULL);
        SetWindowTheme(G.cb_wake, L"DarkMode_Explorer", NULL);
        SetWindowTheme(G.ed_log, L"DarkMode_Explorer", NULL);
        SetWindowTheme(G.ed_msg, L"DarkMode_Explorer", NULL);

        /* 状态栏 */
        int w = SC(600);
        int sb_y = SC(800) - SC(36);
        G.sb_conn = mk(wnd, L"STATIC", L"● 未连接", WS_VISIBLE,
                       SC(14), sb_y + SC(8), SC(110), SC(20), IDC_SB_CONN);
        SendMessageW(G.sb_conn, WM_SETFONT, (WPARAM)G.f_small, TRUE);
        G.sb_xpdr = mk(wnd, L"STATIC", L"应答机: ALT 1200", WS_VISIBLE,
                       SC(128), sb_y + SC(8), SC(150), SC(20), IDC_SB_XPDR);
        SendMessageW(G.sb_xpdr, WM_SETFONT, (WPARAM)G.f_small, TRUE);
        G.sb_flight = mk(wnd, L"STATIC", L"高度: ---  地速: ---", WS_VISIBLE,
                         SC(282), sb_y + SC(8), SC(164), SC(20), IDC_SB_FLIGHT);
        SendMessageW(G.sb_flight, WM_SETFONT, (WPARAM)G.f_small, TRUE);
        G.sb_dll = mk(wnd, L"STATIC", L"DLL: ○ 等待", WS_VISIBLE,
                      SC(450), sb_y + SC(8), SC(88), SC(20), IDC_SB_DLL);
        SendMessageW(G.sb_dll, WM_SETFONT, (WPARAM)G.f_small, TRUE);
        G.btn_mock = CreateWindowExW(0, L"BUTTON", L"",
                                     WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
                                     w - SC(66), sb_y + SC(4), SC(52), SC(28),
                                     wnd, (HMENU)(INT_PTR)IDC_MOCK, NULL, NULL);
        reg_btn(G.btn_mock);

        SetTimer(wnd, TIMER_APP, TIMER_MS, NULL);
        break;
    }
    case WM_PAINT:
        on_paint(wnd);
        return 0;
    case WM_ERASEBKGND:
        return 1;   /* 背景在 WM_PAINT 全量绘制，防闪烁 */
    case WM_CTLCOLORSTATIC:
        return on_ctlcolor_static((HWND)lp, (HDC)wp);
    case WM_CTLCOLOREDIT:
        return on_ctlcolor_edit((HWND)lp, (HDC)wp);
    case WM_CTLCOLORLISTBOX: {
        HDC dc = (HDC)wp;
        SetBkColor(dc, C_INPUT);
        SetTextColor(dc, C_TEXT);
        return (LRESULT)G.br_input;
    }
    case WM_DRAWITEM:
        draw_btn_item((DRAWITEMSTRUCT *)lp);
        return TRUE;
    case WM_COMMAND:
        on_command(wnd, LOWORD(wp), HIWORD(wp));
        break;
    case WM_TIMER:
        if (wp == TIMER_APP)
            on_timer(wnd);
        break;
    case WM_DESTROY:
        KillTimer(wnd, TIMER_APP);
        PostQuitMessage(0);
        break;
    default:
        return DefWindowProcW(wnd, msg, wp, lp);
    }
    return 0;
}

int gui_main(HINSTANCE hInst, int show)
{
    if (net_init() != 0) {
        MessageBoxW(NULL, L"WinSock 初始化失败", L"Aerofly Link", MB_ICONERROR);
        return 1;
    }

    cfg_t cfg;
    cfg_defaults(&cfg);
    char path[MAX_PATH];
    cfg_default_path(path, sizeof(path));
    cfg_load(&cfg, path);

    app_init(&G.app, &cfg);
    G.app.on_log = cb_log;
    G.app.on_status = cb_status;
    G.app.on_warning = cb_warning;
    G.app.on_xpdr = cb_xpdr;
    memset(G.password, 0, sizeof(G.password));

    WNDCLASSW wc;
    memset(&wc, 0, sizeof(wc));
    wc.lpfnWndProc = wnd_proc;
    wc.hInstance = hInst;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = NULL;   /* 背景由 WM_PAINT 全量绘制 */
    wc.lpszClassName = L"AeroflyLinkMain";
    RegisterClassW(&wc);

    INITCOMMONCONTROLSEX icc = { sizeof(icc), ICC_STANDARD_CLASSES };
    InitCommonControlsEx(&icc);

    /* 初始尺寸任意；WM_CREATE 拿到真实 DPI 后按客户区 SC(600)×SC(760) 校准 */
    DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_CLIPCHILDREN;

    HWND wnd = CreateWindowExW(0, L"AeroflyLinkMain",
                               L"Aerofly Link - Aerofly FS 4 联机客户端",
                               style, CW_USEDEFAULT, CW_USEDEFAULT,
                               CW_USEDEFAULT, CW_USEDEFAULT,
                               NULL, NULL, hInst, NULL);
    ui_from_cfg();
    apply_eco_type();
    g_page_shown = -1;
    sync_pages();          /* 初始隐藏工作区，仅显示连接页 */
    ShowWindow(wnd, show);
    UpdateWindow(wnd);

    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    app_disconnect_fsd(&G.app);
    app_toggle_mock(&G.app, false);
    cfg_from_ui();
    cfg_save(&G.app.cfg, path);
    net_cleanup();
    return (int)msg.wParam;
}

int WINAPI WinMain(HINSTANCE hInst, HINSTANCE prev, LPSTR cmd, int show)
{
    (void)prev; (void)cmd;
    return gui_main(hInst, show);
}
