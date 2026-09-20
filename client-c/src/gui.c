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
 * 本文件承载 Win32 管线（窗口/绘制/定时器/回调/工厂）；
 * 页面构建与动作见 gui_pages.c，共享声明见 gui_internal.h。
 */
#include "gui_internal.h"

#include <math.h>
#include <stdio.h>
#include <string.h>
#include <windows.h>
#include <commctrl.h>
#include <windowsx.h>
#include <uxtheme.h>
#include <dwmapi.h>

gui_t G;

/* ── 缩放与文本 ── */
int SC(int v) { return v * G.dpi / 96; }

int u16(const char *s, wchar_t *w, int wcap)
{
    return MultiByteToWideChar(CP_UTF8, 0, s, -1, w, wcap);
}
int u8(const wchar_t *w, char *s, int cap)
{
    return WideCharToMultiByte(CP_UTF8, 0, w, -1, s, cap, NULL, NULL);
}

/* ── 日志追加（超长截半，ADR 0005 内存上限）── */
void log_append(const char *line)
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

static void cb_debug(void *ud, const char *line)
{
    (void)ud;
    char tagged[600];
    _snprintf(tagged, sizeof(tagged) - 1, "[trace] %s", line);
    tagged[sizeof(tagged) - 1] = '\0';
    log_append(tagged);
}

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

void reg_btn(HWND h)
{
    if (G.nob >= 24 || !h)
        return;
    G.ob[G.nob].h = h;
    G.ob[G.nob].hover = false;
    G.ob[G.nob].old = (WNDPROC)GetWindowLongPtrW(h, GWLP_WNDPROC);
    G.nob++;
    SetWindowLongPtrW(h, GWLP_WNDPROC, (LONG_PTR)btn_proc);
}

HFONT mkfont(int h_px, int weight, const wchar_t *face)
{
    return CreateFontW(-h_px, 0, 0, 0, weight, 0, 0, 0,
                       DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY,
                       0, face);
}

/* 控件工厂：默认挂 body 字体 */
HWND mk(HWND parent, const wchar_t *cls, const wchar_t *text,
        DWORD style, int x, int y, int w, int h, int id)
{
    HWND c = CreateWindowExW(0, cls, text, WS_CHILD | style,
                             x, y, w, h, parent,
                             (HMENU)(INT_PTR)id, NULL, NULL);
    SendMessageW(c, WM_SETFONT, (WPARAM)G.f_body, TRUE);
    return c;
}

HWND mk_label(HWND parent, const wchar_t *text, int x, int y, int w)
{
    return mk(parent, L"STATIC", text, WS_VISIBLE, x, y, w, SC(18), 0);
}

HWND mk_edit(HWND parent, int id, int x, int y, int w, int h)
{
    return mk(parent, L"EDIT", L"", WS_VISIBLE | ES_AUTOHSCROLL,
              x, y, w, h, id);
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

    if (G.page_shown == 1) {
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

/* ── 动作（页面外）── */
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

        /* 按 DPI 校准客户区 SC(WIN_W)×SC(WIN_H)（DPI 在创建后才可知） */
        {
            DWORD fstyle = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
            RECT frame = { 0, 0, SC(WIN_W), SC(WIN_H) };
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
        int w = SC(WIN_W);
        int sb_y = SC(WIN_H) - SC(36);
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
    G.app.on_debug = cb_debug;
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

    /* 初始尺寸任意；WM_CREATE 拿到真实 DPI 后按客户区 SC(WIN_W)×SC(WIN_H) 校准 */
    DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_CLIPCHILDREN;

    HWND wnd = CreateWindowExW(0, L"AeroflyLinkMain",
                               L"Aerofly Link - Aerofly FS 4 联机客户端",
                               style, CW_USEDEFAULT, CW_USEDEFAULT,
                               CW_USEDEFAULT, CW_USEDEFAULT,
                               NULL, NULL, hInst, NULL);
    ui_from_cfg();
    apply_eco_type();
    G.page_shown = -1;
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
