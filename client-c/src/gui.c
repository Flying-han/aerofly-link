/*
 * gui.c —— Aerofly Link Win32 GUI（P3）
 * ================================================================
 * 功能对齐 Python/PyQt 版的两阶段布局（连接页 → 工作区）：
 *   连接页：呼号/CID/密码/姓名/机型/Eco/Type/服务器 历史 + 连接
 *   工作区：连接状态/断开、应答机（代码/快捷码/STBY/ALT/IDENT）、
 *           飞行计划表单 + 提交、通讯日志 + 发送
 *   状态栏：连接 | 应答机 | 高度/DLL | 模拟DLL 开关
 *
 * 线程模型：单线程——WM_TIMER(100ms) 内驱动 app_poll(0)，全部 socket
 * 非阻塞，UI 永不阻塞（与 C 重写架构文档 §4 一致）。
 * 文本：源码 UTF-8，经 u16() 转 UTF-16 后使用 W 系列 API。
 */
#include "link/app.h"

#include <stdio.h>
#include <string.h>
#include <windows.h>
#include <commctrl.h>
#include <windowsx.h>

/* ── 控件 ID ── */
enum {
    /* 连接页 */
    IDC_CALLSIGN = 100, IDC_CID, IDC_PASSWORD, IDC_REALNAME, IDC_MODEL,
    IDC_ECO, IDC_TYPE, IDC_SERVER, IDC_CONNECT, IDC_CONNECT_STATUS,
    /* 工作区 */
    IDC_WS_DISCONNECT = 200, IDC_WS_STATUS,
    IDC_XPDR_CODE, IDC_Q1200, IDC_Q7000, IDC_Q7700, IDC_Q7600, IDC_Q7500,
    IDC_STBY, IDC_ALT, IDC_IDENT, IDC_XPDR_WARNING,
    IDC_FP_AIRCRAFT, IDC_FP_WAKE, IDC_FP_TAS, IDC_FP_DEP, IDC_FP_DEST,
    IDC_FP_ALTN, IDC_FP_CRUISE, IDC_FP_ROUTE, IDC_FP_REMARKS,
    IDC_FP_SUBMIT, IDC_FP_STATUS,
    IDC_LOG, IDC_MSG, IDC_SEND,
    /* 状态栏 */
    IDC_SB_CONN = 300, IDC_SB_XPDR, IDC_SB_FLIGHT, IDC_SB_DLL, IDC_MOCK
};

#define TIMER_APP  1
#define TIMER_MS   100
#define WARN_HIDE_SECS 5.0

typedef struct {
    app_t   app;
    HWND    wnd;
    HFONT   font, font_big, font_code;
    double  warn_until;          /* 警告标签自动隐藏 */
    bool    ident_cooldown;      /* IDENT 按钮冷却 */
    double  ident_until;
    /* 连接页控件 */
    HWND    ed_callsign, ed_cid, ed_password, ed_realname, ed_model;
    HWND    cb_eco, cb_type, cb_server;
    HWND    btn_connect, lbl_status;
    /* 工作区控件 */
    HWND    ws_panel, conn_panel;
    HWND    lbl_ws_status, btn_disconnect;
    HWND    ed_xpdr, btn_q[5], rad_stby, rad_alt, btn_ident, lbl_warning;
    HWND    ed_ac, cb_wake, ed_tas, ed_dep, ed_dest, ed_altn, ed_cruise;
    HWND    ed_route, ed_remarks, btn_fp, lbl_fp_status;
    HWND    ed_log, ed_msg, btn_send;
    HWND    sb_conn, sb_xpdr, sb_flight, sb_dll, btn_mock;
    char    password[64];
} gui_t;

static gui_t G;

/* ── UTF-8 → UTF-16 ── */
static int u16(const char *s, wchar_t *w, int wcap)
{
    return MultiByteToWideChar(CP_UTF8, 0, s, -1, w, wcap);
}
static int u8(const wchar_t *w, char *s, int cap)
{
    return WideCharToMultiByte(CP_UTF8, 0, w, -1, s, cap, NULL, NULL);
}

/* ── 日志追加（超长截半，防无限内存）── */
static void log_append(const char *line)
{
    wchar_t wline[600];
    u16(line, wline, 600);

    int len = GetWindowTextLengthW(G.ed_log);
    if (len > 48000) {                       /* ~200KB 上限，对齐 ADR 0005 精神 */
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

static void cb_xpdr(void *ud) { (void)ud; }   /* 状态栏定时刷新即可 */

/* ── 输入框 Enter 键（子类化：消息发送 / 应答机代码确认）── */
static WNDPROC old_msg_proc, old_xpdr_proc;

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
    return CallWindowProcW(old_msg_proc, h, m, wp, lp);
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
    return CallWindowProcW(old_xpdr_proc, h, m, wp, lp);
}

/* ── 布局 ── */

static void build_conn_page(HWND wnd)
{
    int x = 24, y = 16, w = 512, lh = 20, gap = 6, fw = 180;
    wchar_t t[128];

    u16("Aerofly Link —— Aerofly FS 4 联机客户端", t, 128);
    HWND title = CreateWindowExW(0, L"STATIC", t, WS_CHILD | WS_VISIBLE,
                                 x, y, w, 30, wnd, NULL, NULL, NULL);
    SendMessageW(title, WM_SETFONT, (WPARAM)G.font_big, TRUE);

    y += 40;
    struct { const char *label; HWND *out; int id; } fields[] = {
        { "呼号", &G.ed_callsign, IDC_CALLSIGN },
        { "CID", &G.ed_cid, IDC_CID },
        { "密码", &G.ed_password, IDC_PASSWORD },
        { "姓名", &G.ed_realname, IDC_REALNAME },
        { "机型", &G.ed_model, IDC_MODEL },
    };
    for (int i = 0; i < 5; i++) {
        wchar_t lbl[32];
        u16(fields[i].label, lbl, 32);
        CreateWindowExW(0, L"STATIC", lbl, WS_CHILD | WS_VISIBLE,
                        x, y + 3, 60, lh, wnd, NULL, NULL, NULL);
        DWORD es = ES_AUTOHSCROLL;
        if (i == 2) es |= ES_PASSWORD;
        *fields[i].out = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
                                         WS_CHILD | WS_VISIBLE | es,
                                         x + 64, y, fw, 24, wnd,
                                         (HMENU)(INT_PTR)fields[i].id, NULL, NULL);
        SendMessageW(*fields[i].out, WM_SETFONT, (WPARAM)G.font, TRUE);
        y += 24 + gap;
    }

    /* Eco + Type 并排 */
    wchar_t eco_l[8]; u16("Eco.", eco_l, 8);
    CreateWindowExW(0, L"STATIC", eco_l, WS_CHILD | WS_VISIBLE,
                    x, y + 3, 60, lh, wnd, NULL, NULL, NULL);
    G.cb_eco = CreateWindowExW(0, L"COMBOBOX", L"",
                               WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST,
                               x + 64, y, fw, 100, wnd,
                               (HMENU)(INT_PTR)IDC_ECO, NULL, NULL);
    SendMessageW(G.cb_eco, WM_SETFONT, (WPARAM)G.font, TRUE);
    ComboBox_AddString(G.cb_eco, L"VATSIM");
    ComboBox_AddString(G.cb_eco, L"FSD (private)");
    ComboBox_AddString(G.cb_eco, L"FSD (legacy)");
    ComboBox_SetCurSel(G.cb_eco, 1);

    wchar_t type_l[8]; u16("Type", type_l, 8);
    CreateWindowExW(0, L"STATIC", type_l, WS_CHILD | WS_VISIBLE,
                    x + 260, y + 3, 60, lh, wnd, NULL, NULL, NULL);
    G.cb_type = CreateWindowExW(0, L"COMBOBOX", L"",
                                WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST,
                                x + 260 + 64, y, fw, 100, wnd,
                                (HMENU)(INT_PTR)IDC_TYPE, NULL, NULL);
    SendMessageW(G.cb_type, WM_SETFONT, (WPARAM)G.font, TRUE);
    ComboBox_AddString(G.cb_type, L"FSD [VATSIM]");
    ComboBox_AddString(G.cb_type, L"FSD (Legacy)");
    ComboBox_SetCurSel(G.cb_type, 1);
    y += 24 + gap;

    /* 服务器 */
    wchar_t srv_l[16]; u16("服务器", srv_l, 16);
    CreateWindowExW(0, L"STATIC", srv_l, WS_CHILD | WS_VISIBLE,
                    x, y + 3, 60, lh, wnd, NULL, NULL, NULL);
    G.cb_server = CreateWindowExW(WS_EX_CLIENTEDGE, L"COMBOBOX", L"",
                                  WS_CHILD | WS_VISIBLE | CBS_DROPDOWN,
                                  x + 64, y, w - 64, 300, wnd,
                                  (HMENU)(INT_PTR)IDC_SERVER, NULL, NULL);
    SendMessageW(G.cb_server, WM_SETFONT, (WPARAM)G.font, TRUE);
    y += 28 + gap;

    /* 连接按钮 + 状态 */
    wchar_t btn_t[16]; u16("连 接 服 务 器", btn_t, 16);
    G.btn_connect = CreateWindowExW(0, L"BUTTON", btn_t,
                                    WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON,
                                    x, y, w, 34, wnd,
                                    (HMENU)(INT_PTR)IDC_CONNECT, NULL, NULL);
    SendMessageW(G.btn_connect, WM_SETFONT, (WPARAM)G.font_big, TRUE);
    y += 42;
    G.lbl_status = CreateWindowExW(0, L"STATIC", L"", WS_CHILD | WS_VISIBLE,
                                   x, y, w, lh, wnd,
                                   (HMENU)(INT_PTR)IDC_CONNECT_STATUS, NULL, NULL);
}

static void build_workspace_page(HWND wnd)
{
    int x = 12, y = 8, w = 536;

    /* 连接状态行 */
    G.lbl_ws_status = CreateWindowExW(0, L"STATIC", L"● 未连接",
                                      WS_CHILD | WS_VISIBLE,
                                      x, y, w - 110, 20, wnd,
                                      (HMENU)(INT_PTR)IDC_WS_STATUS, NULL, NULL);
    wchar_t dis[16]; u16("断开连接", dis, 16);
    G.btn_disconnect = CreateWindowExW(0, L"BUTTON", dis,
                                       WS_CHILD | WS_VISIBLE,
                                       x + w - 100, y - 3, 100, 26, wnd,
                                       (HMENU)(INT_PTR)IDC_WS_DISCONNECT, NULL, NULL);
    y += 30;

    /* ── 应答机 ── */
    wchar_t xpdr_t[32]; u16("应答机 (Transponder)", xpdr_t, 32);
    CreateWindowExW(0, L"BUTTON", xpdr_t,
                    WS_CHILD | WS_VISIBLE | BS_GROUPBOX,
                    x, y, w, 118, wnd, NULL, NULL, NULL);
    int ix = x + 12, iy = y + 22;

    wchar_t code_l[12]; u16("代码", code_l, 12);
    CreateWindowExW(0, L"STATIC", code_l, WS_CHILD | WS_VISIBLE,
                    ix, iy + 4, 34, 20, wnd, NULL, NULL, NULL);
    G.ed_xpdr = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"1200",
                                WS_CHILD | WS_VISIBLE | ES_CENTER | ES_AUTOHSCROLL,
                                ix + 38, iy, 84, 24, wnd,
                                (HMENU)(INT_PTR)IDC_XPDR_CODE, NULL, NULL);
    SendMessageW(G.ed_xpdr, WM_SETFONT, (WPARAM)G.font_code, TRUE);

    const wchar_t *qtexts[5] = { L"1200", L"7000", L"7700", L"7600", L"7500" };
    const int qids[5] = { IDC_Q1200, IDC_Q7000, IDC_Q7700, IDC_Q7600, IDC_Q7500 };
    for (int i = 0; i < 5; i++) {
        G.btn_q[i] = CreateWindowExW(0, L"BUTTON", qtexts[i],
                                     WS_CHILD | WS_VISIBLE,
                                     ix + 140 + i * 62, iy, 58, 24, wnd,
                                     (HMENU)(INT_PTR)qids[i], NULL, NULL);
    }
    iy += 34;

    wchar_t stby_t[8], alt_t[8], ident_t[8];
    u16("STBY", stby_t, 8); u16("ALT", alt_t, 8); u16("IDENT", ident_t, 8);
    G.rad_stby = CreateWindowExW(0, L"BUTTON", stby_t,
                                 WS_CHILD | WS_VISIBLE | BS_AUTORADIOBUTTON,
                                 ix, iy, 100, 26, wnd,
                                 (HMENU)(INT_PTR)IDC_STBY, NULL, NULL);
    G.rad_alt = CreateWindowExW(0, L"BUTTON", alt_t,
                                WS_CHILD | WS_VISIBLE | BS_AUTORADIOBUTTON,
                                ix + 110, iy, 100, 26, wnd,
                                (HMENU)(INT_PTR)IDC_ALT, NULL, NULL);
    SendMessageW(G.rad_alt, BM_SETCHECK, BST_CHECKED, 0);
    G.btn_ident = CreateWindowExW(0, L"BUTTON", ident_t,
                                  WS_CHILD | WS_VISIBLE,
                                  ix + 240, iy, 100, 26, wnd,
                                  (HMENU)(INT_PTR)IDC_IDENT, NULL, NULL);
    G.lbl_warning = CreateWindowExW(0, L"STATIC", L"", WS_CHILD,
                                    ix + 350, iy, w - ix - 350 - 12, 40, wnd,
                                    (HMENU)(INT_PTR)IDC_XPDR_WARNING, NULL, NULL);
    y += 126;

    /* ── 飞行计划 ── */
    wchar_t fp_t[16]; u16("飞行计划", fp_t, 16);
    CreateWindowExW(0, L"BUTTON", fp_t, WS_CHILD | WS_VISIBLE | BS_GROUPBOX,
                    x, y, w, 168, wnd, NULL, NULL, NULL);
    int fx = x + 12, fy = y + 22;
    struct { const char *label; HWND *out; int id; int w; } ffields[] = {
        { "机型", &G.ed_ac, IDC_FP_AIRCRAFT, 90 },
        { "TAS", &G.ed_tas, IDC_FP_TAS, 70 },
        { "起", &G.ed_dep, IDC_FP_DEP, 80 },
        { "落", &G.ed_dest, IDC_FP_DEST, 80 },
        { "备", &G.ed_altn, IDC_FP_ALTN, 80 },
        { "高度", &G.ed_cruise, IDC_FP_CRUISE, 80 },
    };
    int col = 0;
    for (int i = 0; i < 6; i++) {
        int cx = fx + (col % 3) * 172;
        int cy = fy + (col / 3) * 28;
        wchar_t lbl[12]; u16(ffields[i].label, lbl, 12);
        CreateWindowExW(0, L"STATIC", lbl, WS_CHILD | WS_VISIBLE,
                        cx, cy + 4, 32, 18, wnd, NULL, NULL, NULL);
        *ffields[i].out = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
                                          WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
                                          cx + 34, cy, ffields[i].w - 34, 22, wnd,
                                          (HMENU)(INT_PTR)ffields[i].id, NULL, NULL);
        SendMessageW(*ffields[i].out, WM_SETFONT, (WPARAM)G.font, TRUE);
        col++;
    }
    wchar_t wake_l[12]; u16("尾流", wake_l, 12);
    CreateWindowExW(0, L"STATIC", wake_l, WS_CHILD | WS_VISIBLE,
                    fx + 172 * 2, fy + 32 + 4, 32, 18, wnd, NULL, NULL, NULL);
    G.cb_wake = CreateWindowExW(0, L"COMBOBOX", L"",
                                WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST,
                                fx + 172 * 2 + 34, fy + 32, 80 - 34, 100, wnd,
                                (HMENU)(INT_PTR)IDC_FP_WAKE, NULL, NULL);
    ComboBox_AddString(G.cb_wake, L"Light");
    ComboBox_AddString(G.cb_wake, L"Medium");
    ComboBox_AddString(G.cb_wake, L"Heavy");
    ComboBox_AddString(G.cb_wake, L"Super");
    ComboBox_SetCurSel(G.cb_wake, 1);

    fy += 56;
    wchar_t route_l[12]; u16("航路", route_l, 12);
    CreateWindowExW(0, L"STATIC", route_l, WS_CHILD | WS_VISIBLE,
                    fx, fy + 4, 32, 18, wnd, NULL, NULL, NULL);
    G.ed_route = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
                                 WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
                                 fx + 34, fy, w - 34 - 24, 22, wnd,
                                 (HMENU)(INT_PTR)IDC_FP_ROUTE, NULL, NULL);
    fy += 28;
    wchar_t rmk_l[12]; u16("备注", rmk_l, 12);
    CreateWindowExW(0, L"STATIC", rmk_l, WS_CHILD | WS_VISIBLE,
                    fx, fy + 4, 32, 18, wnd, NULL, NULL, NULL);
    G.ed_remarks = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
                                   WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
                                   fx + 34, fy, w - 34 - 24 - 110, 22, wnd,
                                   (HMENU)(INT_PTR)IDC_FP_REMARKS, NULL, NULL);
    wchar_t fpb_t[20]; u16("提交飞行计划", fpb_t, 20);
    G.btn_fp = CreateWindowExW(0, L"BUTTON", fpb_t, WS_CHILD | WS_VISIBLE,
                               x + w - 24 - 100, fy - 1, 100, 26, wnd,
                               (HMENU)(INT_PTR)IDC_FP_SUBMIT, NULL, NULL);
    G.lbl_fp_status = CreateWindowExW(0, L"STATIC", L"", WS_CHILD | WS_VISIBLE,
                                      fx + 34, fy + 26, w - 60, 16, wnd,
                                      (HMENU)(INT_PTR)IDC_FP_STATUS, NULL, NULL);
    y += 176;

    /* ── 通讯日志 ── */
    wchar_t log_t[32]; u16("通讯日志 (ATC Messages)", log_t, 32);
    CreateWindowExW(0, L"BUTTON", log_t, WS_CHILD | WS_VISIBLE | BS_GROUPBOX,
                    x, y, w, 190, wnd, NULL, NULL, NULL);
    G.ed_log = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
                               WS_CHILD | WS_VISIBLE | ES_MULTILINE |
                               ES_READONLY | ES_AUTOVSCROLL | WS_VSCROLL,
                               x + 12, y + 22, w - 24, 118, wnd,
                               (HMENU)(INT_PTR)IDC_LOG, NULL, NULL);
    SendMessageW(G.ed_log, WM_SETFONT, (WPARAM)G.font_code, TRUE);
    G.ed_msg = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
                               WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
                               x + 12, y + 148, w - 24 - 70, 24, wnd,
                               (HMENU)(INT_PTR)IDC_MSG, NULL, NULL);
    wchar_t send_t[8]; u16("发送", send_t, 8);
    G.btn_send = CreateWindowExW(0, L"BUTTON", send_t, WS_CHILD | WS_VISIBLE,
                                 x + w - 12 - 60, y + 147, 60, 26, wnd,
                                 (HMENU)(INT_PTR)IDC_SEND, NULL, NULL);
}

static void set_connected_ui(bool on)
{
    ShowWindow(G.conn_panel, on ? SW_HIDE : SW_SHOW);
    ShowWindow(G.ws_panel, on ? SW_SHOW : SW_HIDE);
}

/* 页面随会话状态自动切换（连接/断开均为异步结果） */
static void sync_pages(void)
{
    bool online = (G.app.sess.state != SESS_DISCONNECTED);
    static int last = -1;
    int now = online ? 1 : 0;
    if (now != last) {
        last = now;
        set_connected_ui(online);
    }
}

/* ── 配置 ↔ UI 同步 ── */

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

    /* 飞行计划 */
    u16(c->fp_aircraft, w, 256); SetWindowTextW(G.ed_ac, w);
    u16(c->fp_tas, w, 256); SetWindowTextW(G.ed_tas, w);
    u16(c->fp_dep, w, 256); SetWindowTextW(G.ed_dep, w);
    u16(c->fp_dest, w, 256); SetWindowTextW(G.ed_dest, w);
    u16(c->fp_altn, w, 256); SetWindowTextW(G.ed_altn, w);
    u16(c->fp_cruise, w, 256); SetWindowTextW(G.ed_cruise, w);
    u16(c->fp_route, w, 256); SetWindowTextW(G.ed_route, w);
    u16(c->fp_remarks, w, 256); SetWindowTextW(G.ed_remarks, w);
    const char *wake_names[] = { "Light", "Medium", "Heavy", "Super" };
    for (int i = 0; i < 4; i++) {
        if (strcmp(c->fp_wake, wake_names[i]) == 0) {
            ComboBox_SetCurSel(G.cb_wake, i);
            break;
        }
    }

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
    /* 大写呼号（与 Python 一致） */
    for (char *p = c->callsign; *p; p++) *p = (char)(*p >= 'a' && *p <= 'z' ? *p - 32 : *p);
    GETTEXT(G.ed_cid, c->cid, sizeof(c->cid));
    GETTEXT(G.ed_realname, c->realname, sizeof(c->realname));
    GETTEXT(G.ed_model, c->model, sizeof(c->model));
    GETTEXT(G.cb_server, s, 256);
    char host[128] = "";
    int port = 6809;
    if (sscanf(s, "%127[^:]:%d", host, &port) >= 1) {
        strncpy(c->server, host, sizeof(c->server) - 1);
        c->server[sizeof(c->server) - 1] = '\0';
        c->port = port;
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

    int wake = ComboBox_GetCurSel(G.cb_wake);
    const char *wake_names[] = { "Light", "Medium", "Heavy", "Super" };
    strncpy(c->fp_wake, wake_names[wake < 0 ? 1 : wake], sizeof(c->fp_wake) - 1);

    /* 服务器历史去重追加 */
    for (size_t i = 0; i < c->nservers; i++) {
        if (strcmp(c->servers[i], s) == 0)
            return;
    }
    if (c->nservers < CFG_MAX_SERVERS && s[0]) {
        strncpy(c->servers[c->nservers], s, JSN_STR_CAP - 1);
        c->nservers++;
    }
}

/* eco/type 联动（对齐 Python ConnectPage） */
static void apply_eco_type(void)
{
    int eco = ComboBox_GetCurSel(G.cb_eco);   /* 0 vatsim 1 private 2 legacy */
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

/* ── 主循环 tick ── */

static void on_timer(void)
{
    app_poll(&G.app, 0);
    sync_pages();

    if (G.warn_until && net_now() > G.warn_until) {
        ShowWindow(G.lbl_warning, SW_HIDE);
        G.warn_until = 0;
    }
    if (G.ident_cooldown && net_now() > G.ident_until) {
        G.ident_cooldown = false;
        EnableWindow(G.btn_ident, TRUE);
    }

    char conn[64] = "", xpdr[64] = "", flight[64] = "", dll[64] = "";
    app_status_text(&G.app, conn, sizeof(conn), xpdr, sizeof(xpdr),
                    flight, sizeof(flight), dll, sizeof(dll));
    wchar_t w[128];

    u16(conn, w, 128); SetWindowTextW(G.sb_conn, w);
    u16(xpdr, w, 128); SetWindowTextW(G.sb_xpdr, w);
    u16(dll, w, 128);  SetWindowTextW(G.sb_dll, w);

    /* 飞行数据 + 附近飞机合并到同一格 */
    int nearby = app_nearby_count(&G.app);
    char fl[96];
    if (nearby > 0)
        _snprintf(fl, sizeof(fl) - 1, "%s | 附近 %d 架", flight, nearby);
    else
        _snprintf(fl, sizeof(fl) - 1, "%s", flight);
    fl[sizeof(fl) - 1] = '\0';
    u16(fl, w, 128); SetWindowTextW(G.sb_flight, w);

    /* 应答机代码输入框与虚拟状态同步（未聚焦时） */
    if (GetFocus() != G.ed_xpdr) {
        char cur[16];
        GetWindowTextA(G.ed_xpdr, cur, sizeof(cur));
        if (strcmp(cur, G.app.xpdr.squawk) != 0)
            SetWindowTextA(G.ed_xpdr, G.app.xpdr.squawk);
    }
}

/* ── 事件处理 ── */

static void do_connect(HWND wnd)
{
    cfg_from_ui();
    if (!G.app.cfg.callsign[0] || !G.app.cfg.cid[0]) {
        MessageBoxW(wnd, L"请填写呼号和 CID", L"配置不完整", MB_ICONWARNING);
        return;
    }
    /* 重新初始化会话配置（呼号/CID/服务器可能已改） */
    cfg_t *c = &G.app.cfg;
    strncpy(G.app.sess.callsign, c->callsign, sizeof(G.app.sess.callsign) - 1);
    strncpy(G.app.sess.cid, c->cid, sizeof(G.app.sess.cid) - 1);
    strncpy(G.app.sess.realname, c->realname, sizeof(G.app.sess.realname) - 1);
    strncpy(G.app.sess.server, c->server, sizeof(G.app.sess.server) - 1);
    G.app.sess.port = c->port;
    G.app.sess.rating = c->rating;
    G.app.sess.vatsim = (ComboBox_GetCurSel(G.cb_type) == 0);

    /* 密码仅内存传递（ADR 0003）：输入框 → 会话，绝不落盘 */
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
    set_connected_ui(true);
}

static void do_disconnect(void)
{
    app_disconnect_fsd(&G.app);
    set_connected_ui(false);
}

static void do_ident(HWND wnd)
{
    app_ident(&G.app);
    G.ident_cooldown = true;
    G.ident_until = net_now() + 5.0;
    EnableWindow(G.btn_ident, FALSE);
    (void)wnd;
}

static void do_submit_fp(HWND wnd)
{
    cfg_from_ui();
    /* 配置 → 飞行计划暂存缓冲（sess_send_flightplan 引用的就是这些缓冲） */
    cfg_t *c = &G.app.cfg;
    #define FPCOPY(field, key) do {         strncpy(G.app.field, c->key, sizeof(G.app.field) - 1);         G.app.field[sizeof(G.app.field) - 1] = '\0'; } while (0)
    FPCOPY(fp_type, eco);   /* 占位，下行覆盖 */
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
    wchar_t w[8];
    GetWindowTextW(G.ed_ac, w, 8);
    char ac[16]; u8(w, ac, 16);
    if (!ac[0] || !G.app.cfg.fp_dep[0] || !G.app.cfg.fp_dest[0]
        || !G.app.cfg.fp_tas[0]) {
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
    case IDC_CONNECT:    do_connect(wnd); break;
    case IDC_WS_DISCONNECT: do_disconnect(); break;
    case IDC_ECO:        if (code == CBN_SELCHANGE) apply_eco_type(); break;
    case IDC_STBY:       if (code) app_set_xpdr_mode(&G.app, false); break;
    case IDC_ALT:        if (code) app_set_xpdr_mode(&G.app, true); break;
    case IDC_IDENT:      do_ident(wnd); break;
    case IDC_FP_SUBMIT:  do_submit_fp(wnd); break;
    case IDC_MOCK: {
        bool on = (SendMessageW(G.btn_mock, BM_GETCHECK, 0, 0) == BST_CHECKED);
        const char *err = app_toggle_mock(&G.app, on);
        if (err) {
            wchar_t msg[256];
            u16(err, msg, 256);
            MessageBoxW(wnd, msg, L"模拟DLL", MB_ICONWARNING);
            SendMessageW(G.btn_mock, BM_SETCHECK,
                         on ? BST_UNCHECKED : BST_CHECKED, 0);
        }
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
        static const char *codes[5] =
            { "1200", "7000", "7700", "7600", "7500" };
        int idx = id - IDC_Q1200;
        app_set_xpdr_code(&G.app, codes[idx]);
        break;
    }
    default: break;
    }
}

static LRESULT CALLBACK wnd_proc(HWND wnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_CREATE: {
        G.wnd = wnd;
        G.font = CreateFontW(-13, 0, 0, 0, FW_NORMAL, 0, 0, 0,
                             DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY,
                             0, L"Segoe UI");
        G.font_big = CreateFontW(-20, 0, 0, 0, FW_SEMIBOLD, 0, 0, 0,
                                 DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY,
                                 0, L"Segoe UI");
        G.font_code = CreateFontW(-18, 0, 0, 0, FW_BOLD, 0, 0, 0,
                                  DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY,
                                  FIXED_PITCH | FF_MODERN, L"Consolas");

        /* 两页面板容器 */
        G.conn_panel = CreateWindowExW(0, L"STATIC", L"",
                                       WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN,
                                       0, 0, 560, 560, wnd, NULL, NULL, NULL);
        G.ws_panel = CreateWindowExW(0, L"STATIC", L"",
                                     WS_CHILD | WS_CLIPCHILDREN,
                                     0, 0, 560, 560, wnd, NULL, NULL, NULL);
        HWND save_wnd = G.wnd;
        /* 控件建在对应容器上：临时改 parent 上下文 */
        build_conn_page(G.conn_panel);
        build_workspace_page(G.ws_panel);

        /* 输入框 Enter 键子类化（消息发送 / 应答机代码确认） */
        old_msg_proc = (WNDPROC)SetWindowLongPtrW(
            G.ed_msg, GWLP_WNDPROC, (LONG_PTR)msg_edit_proc);
        old_xpdr_proc = (WNDPROC)SetWindowLongPtrW(
            G.ed_xpdr, GWLP_WNDPROC, (LONG_PTR)xpdr_edit_proc);
        G.wnd = save_wnd;

        /* 状态栏 */
        G.sb_conn = CreateWindowExW(0, L"STATIC", L"● 未连接",
                                    WS_CHILD | WS_VISIBLE, 8, 562, 130, 20, wnd,
                                    (HMENU)(INT_PTR)IDC_SB_CONN, NULL, NULL);
        G.sb_xpdr = CreateWindowExW(0, L"STATIC", L"应答机: ALT 1200",
                                    WS_CHILD | WS_VISIBLE, 142, 562, 170, 20, wnd,
                                    (HMENU)(INT_PTR)IDC_SB_XPDR, NULL, NULL);
        G.sb_flight = CreateWindowExW(0, L"STATIC", L"高度: ---  地速: ---",
                                      WS_CHILD | WS_VISIBLE, 316, 562, 180, 20, wnd,
                                      (HMENU)(INT_PTR)IDC_SB_FLIGHT, NULL, NULL);
        G.sb_dll = CreateWindowExW(0, L"STATIC", L"DLL: ○ 等待游戏",
                                   WS_CHILD | WS_VISIBLE, 498, 562, 130, 20, wnd,
                                   (HMENU)(INT_PTR)IDC_SB_DLL, NULL, NULL);
        wchar_t mock_t[12]; u16("模拟DLL", mock_t, 12);
        G.btn_mock = CreateWindowExW(0, L"BUTTON", mock_t,
                                     WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
                                     632, 560, 90, 24, wnd,
                                     (HMENU)(INT_PTR)IDC_MOCK, NULL, NULL);
        SendMessageW(G.btn_mock, WM_SETFONT, (WPARAM)G.font, TRUE);
        SendMessageW(G.sb_conn, WM_SETFONT, (WPARAM)G.font, TRUE);
        SendMessageW(G.sb_xpdr, WM_SETFONT, (WPARAM)G.font, TRUE);
        SendMessageW(G.sb_flight, WM_SETFONT, (WPARAM)G.font, TRUE);
        SendMessageW(G.sb_dll, WM_SETFONT, (WPARAM)G.font, TRUE);

        SetTimer(wnd, TIMER_APP, TIMER_MS, NULL);
        break;
    }
    case WM_COMMAND:
        on_command(wnd, LOWORD(wp), HIWORD(wp));
        break;
    case WM_TIMER:
        if (wp == TIMER_APP)
            on_timer();
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
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszClassName = L"AeroflyLinkMain";
    RegisterClassW(&wc);

    INITCOMMONCONTROLSEX icc = { sizeof(icc), ICC_STANDARD_CLASSES };
    InitCommonControlsEx(&icc);

    HWND wnd = CreateWindowExW(WS_EX_CONTROLPARENT, L"AeroflyLinkMain",
                               L"Aerofly Link - Aerofly FS 4 联机客户端",
                               WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU
                               | WS_MINIMIZEBOX,
                               CW_USEDEFAULT, CW_USEDEFAULT, 578, 640,
                               NULL, NULL, hInst, NULL);
    ui_from_cfg();
    apply_eco_type();
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
