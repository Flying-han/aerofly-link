/*
 * gui_pages.c —— 连接页/工作区构建、配置↔UI、动作
 * ================================================================
 * 共享状态与工厂函数见 gui_internal.h；绘制与窗口管线在 gui.c。
 */
#include "gui_internal.h"

#include <commctrl.h>
#include <windowsx.h>
#include <richedit.h>

#include <stdio.h>
#include <string.h>

/* ── 连接页（标签在输入框上方，8pt 网格）── */
void build_conn_page(HWND wnd)
{
    int x = SC(24), w = SC(WIN_W) - SC(48);
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

    /* 服务器（combo 缩窄，右侧 +/− 管理记录） */
    wchar_t srv_l[16]; u16("SERVER", srv_l, 16);
    G.page_conn[G.n_conn++] = mk_label(wnd, srv_l, x, y, w);
    int srv_w = w - SC(116);
    G.cb_server = CreateWindowExW(0, L"COMBOBOX", L"",
                                  WS_CHILD | WS_VISIBLE | CBS_DROPDOWN,
                                  x, y + SC(22), srv_w, SC(200), wnd,
                                  (HMENU)(INT_PTR)IDC_SERVER, NULL, NULL);
    SendMessageW(G.cb_server, WM_SETFONT, (WPARAM)G.f_body, TRUE);
    G.page_conn[G.n_conn++] = G.cb_server;
    const wchar_t *srv_btn[2] = { L"+", L"-" };
    const int srv_ids[2] = { IDC_SRV_ADD, IDC_SRV_DEL };
    for (int i = 0; i < 2; i++) {
        G.page_conn[G.n_conn++] = CreateWindowExW(
            0, L"BUTTON", srv_btn[i],
            WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
            x + srv_w + SC(6) + i * SC(56), y + SC(22), SC(50), SC(34), wnd,
            (HMENU)(INT_PTR)srv_ids[i], NULL, NULL);
        SendMessageW(G.page_conn[G.n_conn - 1], WM_SETFONT,
                     (WPARAM)G.f_body, TRUE);
        reg_btn(G.page_conn[G.n_conn - 1]);
    }
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
void build_ws_page(HWND wnd)
{
    int x = SC(16), w = SC(WIN_W) - SC(32);
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

    /* ── 卡片：ATC MESSAGES（owner-draw ListBox：时间戳/按类着色）── */
    G.rc_card_log = (RECT){ x, y, x + w, y + SC(212) };
    G.ed_log = CreateWindowExW(
        0, L"LISTBOX", L"",
        WS_CHILD | WS_VISIBLE | WS_VSCROLL | LBS_OWNERDRAWFIXED |
            LBS_NOINTEGRALHEIGHT | LBS_DISABLENOSCROLL,
        x + SC(16), y + SC(40), w - SC(32), SC(110), wnd,
        (HMENU)(INT_PTR)IDC_LOG, NULL, NULL);
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

void sync_pages(void)
{
    int now = (G.app.sess.state != SESS_DISCONNECTED) ? 1 : 0;
    if (now != G.page_shown) {
        G.page_shown = now;
        set_connected_ui(now);
        if (!now)
            reset_fp_fields();   /* 断开重置（Python main_window reset_fields） */
    }
}

/* ── 配置 ↔ UI ── */
void ui_from_cfg(void)
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

/* 连接字段 UI → cfg（退出保存路径只调此，避免覆盖已重置的 FP 字段） */
void cfg_from_ui_conn(void)
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
#undef GETTEXT
    {
        char host[128] = "";
        int port = 6809;
        if (sscanf(s, "%127[^:]:%d", host, &port) >= 1) {
            strncpy(c->server, host, sizeof(c->server) - 1);
            c->server[sizeof(c->server) - 1] = '\0';
            c->port = port;
        }
    }

    for (size_t i = 0; i < c->nservers; i++)
        if (strcmp(c->servers[i], s) == 0)
            return;
    if (c->nservers < CFG_MAX_SERVERS && s[0]) {
        strncpy(c->servers[c->nservers], s, JSN_STR_CAP - 1);
        c->nservers++;
    }
}

/* 飞行计划字段 UI → cfg（连接/提交时保存；起飞时间等临时字段不落盘） */
void cfg_from_ui_fp(void)
{
    cfg_t *c = &G.app.cfg;
    wchar_t w[256];
    char s[256];

#define GETTEXT(hwnd, dst, cap) do { \
    GetWindowTextW(hwnd, w, 256); u8(w, s, 256); \
    strncpy(dst, s, (cap) - 1); dst[(cap) - 1] = '\0'; } while (0)

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
}

/* 断开重置 FP 面板（基准 Python flightplan_panel.reset_fields） */
void reset_fp_fields(void)
{
    SetWindowTextW(G.ed_ac, L"");
    SetWindowTextW(G.ed_tas, L"");
    SetWindowTextW(G.ed_dep, L"");
    SetWindowTextW(G.ed_dest, L"");
    SetWindowTextW(G.ed_altn, L"");
    SetWindowTextW(G.ed_cruise, L"");
    SetWindowTextW(G.ed_route, L"");
    SetWindowTextW(G.ed_remarks, L"");
    ComboBox_SetCurSel(G.cb_wake, 1);   /* Medium */
    SetWindowTextW(G.lbl_fp_status, L"请先连接到服务器");
}

void apply_eco_type(void)
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

/* ── 动作 ── */
void do_connect(HWND wnd)
{
    cfg_from_ui_conn();
    cfg_from_ui_fp();
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

void do_submit_fp(HWND wnd)
{
    cfg_from_ui_fp();
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
