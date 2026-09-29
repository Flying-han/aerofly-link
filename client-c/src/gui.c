/*
 * Aerofly Link desktop interface: Nuklear widgets, one Win32/GDI window.
 * Network/session code stays in app.c; this file owns presentation and input.
 */
#define NK_INCLUDE_FIXED_TYPES
#define NK_INCLUDE_STANDARD_IO
#define NK_INCLUDE_STANDARD_VARARGS
#define NK_INCLUDE_DEFAULT_ALLOCATOR

#include "../vendor/nuklear/nuklear.h"
#include "../vendor/nuklear/nuklear_gdi.h"
#include "link/app.h"

#include <windows.h>
#include <windowsx.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>

#define APP_TIMER_ID       1
#define APP_TIMER_MS       50
#define IDC_PASSWORD       1101
#define IDC_CONNECT        1102
#define LOG_LINES          180
#define LOG_LINE_CAP       512
#define UI_TEXT_CAP        512
#ifdef AEROFLYLINK_E2E_GUI
#define E2E_QUERY_STATE    (WM_APP + 0x31)
#define E2E_SET_PAGE       (WM_APP + 0x32)
#define E2E_SET_LANGUAGE   (WM_APP + 0x33)
#define E2E_SET_THEME      (WM_APP + 0x34)
#endif

typedef enum { PAGE_CONNECT, PAGE_FLIGHT, PAGE_SETTINGS } ui_page_t;

typedef struct {
    app_log_kind_t kind;
    char text[LOG_LINE_CAP];
} log_line_t;

typedef struct {
    app_t app;
    HWND window;
    HWND password_edit;
    HFONT password_font;
    HBRUSH password_brush;
    WNDPROC old_password_proc;
    HDC window_dc;
    GdiFont *font_body;
    GdiFont *font_title;
    GdiFont *font_small;
    GdiFont *font_code;
    struct nk_context *nk;
    int width;
    int height;
    UINT dpi;
    float scale;
    ui_page_t page;
    char server_entry[160];
    char chat_entry[512];
    char xpdr_entry[8];
    char departure_time[8];
    char actual_time[8];
    char password[64];
    char status[UI_TEXT_CAP];
    char notice[UI_TEXT_CAP];
    char warning[UI_TEXT_CAP];
    log_line_t log[LOG_LINES];
    size_t log_head;
    size_t log_count;
    size_t selected_server;
    double warning_until;
    double config_dirty_at;
    bool config_dirty;
    bool password_visible;
    bool theme_dirty;
    bool settings_advanced;
    sess_state_t previous_state;
#ifdef AEROFLYLINK_E2E_GUI
    bool e2e_password_loaded;
#endif
} gui_t;

static gui_t G;

static float UI(float logical_pixels)
{
    return logical_pixels * (G.scale > 0.0f ? G.scale : 1.0f);
}
#define U(value) UI((float)(value))

static struct nk_color C_BG       = {  11,  18,  26, 255 };
static struct nk_color C_SURFACE  = {  19,  30,  40, 255 };
static struct nk_color C_RAISED   = {  27,  42,  54, 255 };
static struct nk_color C_LINE     = {  48,  67,  80, 255 };
static struct nk_color C_TEXT     = { 232, 240, 244, 255 };
static struct nk_color C_MUTED    = { 164, 181, 192, 255 };
static struct nk_color C_CYAN     = {  78, 207, 225, 255 };
static struct nk_color C_GREEN    = {  93, 211, 160, 255 };
static struct nk_color C_AMBER    = { 255, 186,  92, 255 };
static struct nk_color C_RED      = { 238, 112, 119, 255 };
static const struct nk_color C_DARKTEXT = {  10,  22,  29, 255 };

static const char *tr3(const char *language, const char *sc,
                       const char *hk, const char *en)
{
    if (language && strcmp(language, "zh-HK") == 0)
        return hk;
    if (language && strcmp(language, "en-US") == 0)
        return en;
    return sc;
}

#define T(sc, hk, en) tr3(G.app.cfg.language, sc, hk, en)

static const wchar_t *localized_window_title(void)
{
    if (strcmp(G.app.cfg.language, "zh-HK") == 0)
        return L"Aerofly Link — 飛行連線";
    if (strcmp(G.app.cfg.language, "en-US") == 0)
        return L"Aerofly Link — Flight Network";
    return L"Aerofly Link — 飞行联机";
}

static void update_window_title(void)
{
    if (G.window)
        SetWindowTextW(G.window, localized_window_title());
}

static void mark_config_dirty(void)
{
    G.config_dirty = true;
    G.config_dirty_at = net_now();
}

static void save_config(void)
{
    char path[MAX_PATH];
    cfg_default_path(path, sizeof(path));
    if (cfg_save(&G.app.cfg, path) == 0) {
        G.config_dirty = false;
        G.status[0] = '\0';
    } else {
        strncpy(G.status, T("无法保存设置。请检查用户配置目录的写入权限。",
                            "無法儲存設定。請檢查使用者設定目錄的寫入權限。",
                            "Settings could not be saved. Check write access to your user config folder."),
                sizeof(G.status) - 1);
        G.status[sizeof(G.status) - 1] = '\0';
    }
}

static void cb_log(void *ud, app_log_kind_t kind, const char *line)
{
    (void)ud;
    if (!line)
        return;
    size_t slot;
    if (G.log_count < LOG_LINES) {
        slot = (G.log_head + G.log_count++) % LOG_LINES;
    } else {
        slot = G.log_head;
        G.log_head = (G.log_head + 1) % LOG_LINES;
    }
    G.log[slot].kind = kind;
    SYSTEMTIME now;
    GetLocalTime(&now);
    _snprintf(G.log[slot].text, sizeof(G.log[slot].text) - 1,
              "[%02u:%02u:%02u] %.*s", now.wHour, now.wMinute,
              now.wSecond, 430, line);
    G.log[slot].text[sizeof(G.log[slot].text) - 1] = '\0';
}

static void cb_debug(void *ud, const char *line)
{
    cb_log(ud, APP_LOG_SYS, line);
}

static void cb_status(void *ud, const char *line)
{
    (void)ud;
    if (!line)
        return;
    strncpy(G.status, line, sizeof(G.status) - 1);
    G.status[sizeof(G.status) - 1] = '\0';
}

static void cb_warning(void *ud, const char *line)
{
    (void)ud;
    if (!line)
        return;
    const char *translated = line;
    if (strcmp(line, "AFS4 不支持外部控制应答机模式（已降级为虚拟状态）") == 0)
        translated = T("AFS4 不支持外部控制应答机模式（已降级为虚拟状态）",
                       "AFS4 不支援外部控制應答機模式（已降級為虛擬狀態）",
                       "Aerofly FS 4 does not support external transponder-mode control. The client will use virtual mode.");
    else if (strcmp(line, "AFS4 不支持外部控制应答机模式，请手动调整游戏内面板") == 0)
        translated = T("AFS4 不支持外部控制应答机模式，请手动调整游戏内面板",
                       "AFS4 不支援外部控制應答機模式，請手動調整遊戲內面板",
                       "Aerofly FS 4 does not support external transponder-mode control. Change the in-sim panel manually.");
    strncpy(G.warning, translated, sizeof(G.warning) - 1);
    G.warning[sizeof(G.warning) - 1] = '\0';
    G.warning_until = net_now() + 7.0;
    cb_log(NULL, APP_LOG_SYS, translated);
}

static void cb_xpdr(void *ud)
{
    (void)ud;
}

static bool is_online(void)
{
    return G.app.sess.state == SESS_ONLINE;
}

static bool is_training_host(const char *host)
{
    return host && (strstr(host, "sweatbox") != NULL
                    || strstr(host, "SWEATBOX") != NULL);
}

static bool is_vatsim_host(const char *host)
{
    if (!host)
        return false;
    size_t n = strlen(host);
    const char suffix[] = ".vatsim.net";
    size_t suffix_n = sizeof(suffix) - 1;
    if (n < suffix_n)
        return false;
    for (size_t i = 0; i < suffix_n; ++i) {
        if (tolower((unsigned char)host[n - suffix_n + i]) != suffix[i])
            return false;
    }
    return true;
}

static bool parse_server_address(const char *value, char *host,
                                 size_t host_cap, int *port)
{
    if (!value || !value[0] || !host || host_cap < 2 || !port)
        return false;

    const char *colon = strchr(value, ':');
    size_t host_len = colon ? (size_t)(colon - value) : strlen(value);
    if (host_len == 0 || host_len >= host_cap)
        return false;
    if (colon && strchr(colon + 1, ':'))
        return false; /* This client transport is IPv4-only. */

    for (size_t i = 0; i < host_len; ++i) {
        unsigned char c = (unsigned char)value[i];
        if (!(isalnum(c) || c == '.' || c == '-'))
            return false;
    }

    int parsed_port = 6809;
    if (colon) {
        if (!colon[1])
            return false;
        parsed_port = 0;
        for (const char *p = colon + 1; *p; ++p) {
            if (*p < '0' || *p > '9')
                return false;
            parsed_port = parsed_port * 10 + (*p - '0');
            if (parsed_port > 65535)
                return false;
        }
        if (parsed_port == 0)
            return false;
    }

    memcpy(host, value, host_len);
    host[host_len] = '\0';
    *port = parsed_port;
    return true;
}

static void current_server_address(char *out, size_t cap)
{
    _snprintf(out, cap - 1, "%s:%d", G.app.cfg.server, G.app.cfg.port);
    out[cap - 1] = '\0';
}

static bool address_is_asc(const char *host, int port)
{
    return host && port == 6809 && _stricmp(host, "flight.skeet.top") == 0;
}

static void set_notice(const char *sc, const char *hk, const char *en)
{
    const char *text = T(sc, hk, en);
    strncpy(G.notice, text, sizeof(G.notice) - 1);
    G.notice[sizeof(G.notice) - 1] = '\0';
}

static const char *display_status(void)
{
    const char *s = G.status;
    if (!s[0])
        return "";
    if (strstr(s, "TCP 连接中") || strstr(s, "连接中 | TCP"))
        return T("正在连接社区 FSD 服务器……", "正在連線社群 FSD 伺服器……",
                 "Connecting to the community FSD server…");
    if (strstr(s, "已连接并认证"))
        return T("服务器认证成功。", "伺服器認證成功。", "Server authentication succeeded.");
    if (strstr(s, "JWT 获取失败"))
        return T("无法获取登录令牌。检查 HTTPS 地址和账号信息。",
                 "無法取得登入權杖。請檢查 HTTPS 地址及帳號資料。",
                 "Could not obtain a login token. Check the HTTPS endpoint and account details.");
    if (strstr(s, "认证超时"))
        return T("认证超时。请检查账号信息和服务器状态。",
                 "認證逾時。請檢查帳號資料及伺服器狀態。",
                 "Authentication timed out. Check your account details and server status.");
    if (strstr(s, "连接失败"))
        return T("无法连接服务器。检查地址、端口和网络连接。",
                 "無法連線伺服器。請檢查地址、連接埠及網絡連線。",
                 "Could not reach the server. Check the host, port, and network connection.");
    return s; /* Server-supplied protocol errors remain verbatim. */
}

static void draw_heading(const char *text)
{
    nk_gdi_set_font(G.font_title);
    nk_layout_row_dynamic(G.nk, U(30), 1);
    nk_label(G.nk, text, NK_TEXT_LEFT);
    nk_gdi_set_font(G.font_body);
}

static void draw_section(const char *text)
{
    nk_gdi_set_font(G.font_body);
    nk_layout_row_dynamic(G.nk, U(27), 1);
    nk_label(G.nk, text, NK_TEXT_LEFT);
    nk_gdi_set_font(G.font_body);
}

static void draw_help(const char *text)
{
    nk_layout_row_dynamic(G.nk, U(37), 1);
    nk_label_colored(G.nk, text, NK_TEXT_LEFT, C_MUTED);
}

static void draw_field_label(const char *text)
{
    nk_layout_row_dynamic(G.nk, U(22), 1);
    nk_label_colored(G.nk, text, NK_TEXT_LEFT, C_MUTED);
}

static bool edit_input(char *buffer, size_t cap)
{
    char before[UI_TEXT_CAP];
    if (cap > sizeof(before))
        cap = sizeof(before);
    strncpy(before, buffer, cap - 1);
    before[cap - 1] = '\0';

    nk_edit_string_zero_terminated(G.nk, NK_EDIT_FIELD, buffer,
                                   (int)cap, nk_filter_default);
    if (strcmp(before, buffer) != 0) {
        mark_config_dirty();
        return true;
    }
    return false;
}

static bool edit_field(const char *label, char *buffer, size_t cap)
{
    draw_field_label(label);
    nk_layout_row_dynamic(G.nk, U(34), 1);
    return edit_input(buffer, cap);
}

static bool primary_button(const char *label)
{
    bool light = strcmp(G.app.cfg.theme, "light") == 0;
    struct nk_style_button saved = G.nk->style.button;
    G.nk->style.button.normal = nk_style_item_color(C_CYAN);
    G.nk->style.button.hover = nk_style_item_color(
        light ? nk_rgb(16, 89, 108) : nk_rgb(111, 222, 235));
    G.nk->style.button.active = nk_style_item_color(
        light ? nk_rgb(12, 72, 88) : nk_rgb(50, 175, 194));
    G.nk->style.button.border_color = C_CYAN;
    struct nk_color text = light ? nk_rgb(255, 255, 255) : C_DARKTEXT;
    G.nk->style.button.text_normal = text;
    G.nk->style.button.text_hover = text;
    G.nk->style.button.text_active = text;
    G.nk->style.button.rounding = U(5);
    bool pressed = nk_button_label(G.nk, label) != 0;
    G.nk->style.button = saved;
    return pressed;
}

static bool secondary_button(const char *label)
{
    return nk_button_label(G.nk, label) != 0;
}

static bool caution_button(const char *label)
{
    bool light = strcmp(G.app.cfg.theme, "light") == 0;
    struct nk_style_button saved = G.nk->style.button;
    G.nk->style.button.normal = nk_style_item_color(C_RED);
    G.nk->style.button.hover = nk_style_item_color(
        light ? nk_rgb(145, 39, 46) : nk_rgb(245, 137, 143));
    G.nk->style.button.active = nk_style_item_color(
        light ? nk_rgb(120, 28, 35) : nk_rgb(205, 81, 89));
    G.nk->style.button.border_color = C_RED;
    struct nk_color text = light ? nk_rgb(255, 255, 255) : C_DARKTEXT;
    G.nk->style.button.text_normal = text;
    G.nk->style.button.text_hover = text;
    G.nk->style.button.text_active = text;
    bool pressed = nk_button_label(G.nk, label) != 0;
    G.nk->style.button = saved;
    return pressed;
}

static void update_palette(void)
{
    if (strcmp(G.app.cfg.theme, "light") == 0) {
        C_BG      = (struct nk_color){ 231, 238, 241, 255 };
        C_SURFACE = (struct nk_color){ 247, 250, 251, 255 };
        C_RAISED  = (struct nk_color){ 215, 227, 232, 255 };
        C_LINE    = (struct nk_color){ 180, 196, 203, 255 };
        C_TEXT    = (struct nk_color){  27,  41,  49, 255 };
        C_MUTED   = (struct nk_color){  75,  94, 103, 255 };
        C_CYAN    = (struct nk_color){  23, 105, 128, 255 };
        C_GREEN   = (struct nk_color){  27, 112,  75, 255 };
        C_AMBER   = (struct nk_color){ 145,  75,  10, 255 };
        C_RED     = (struct nk_color){ 166,  47,  56, 255 };
    } else {
        C_BG      = (struct nk_color){  11,  18,  26, 255 };
        C_SURFACE = (struct nk_color){  19,  30,  40, 255 };
        C_RAISED  = (struct nk_color){  27,  42,  54, 255 };
        C_LINE    = (struct nk_color){  48,  67,  80, 255 };
        C_TEXT    = (struct nk_color){ 232, 240, 244, 255 };
        C_MUTED   = (struct nk_color){ 164, 181, 192, 255 };
        C_CYAN    = (struct nk_color){  78, 207, 225, 255 };
        C_GREEN   = (struct nk_color){  93, 211, 160, 255 };
        C_AMBER   = (struct nk_color){ 255, 186,  92, 255 };
        C_RED     = (struct nk_color){ 238, 112, 119, 255 };
    }
}

static void apply_theme(void)
{
    update_palette();
    struct nk_color colors[NK_COLOR_COUNT];
    for (int i = 0; i < NK_COLOR_COUNT; ++i)
        colors[i] = C_SURFACE;

    const bool light = strcmp(G.app.cfg.theme, "light") == 0;
    if (light) {
        colors[NK_COLOR_TEXT] = C_TEXT;
        colors[NK_COLOR_WINDOW] = C_BG;
        colors[NK_COLOR_HEADER] = C_SURFACE;
        colors[NK_COLOR_BORDER] = C_LINE;
        colors[NK_COLOR_BUTTON] = C_RAISED;
        colors[NK_COLOR_BUTTON_HOVER] = nk_rgb(197, 216, 224);
        colors[NK_COLOR_BUTTON_ACTIVE] = nk_rgb(174, 201, 211);
        colors[NK_COLOR_TOGGLE] = C_RAISED;
        colors[NK_COLOR_TOGGLE_HOVER] = nk_rgb(184, 206, 215);
        colors[NK_COLOR_TOGGLE_CURSOR] = C_CYAN;
        colors[NK_COLOR_SELECT] = C_SURFACE;
        colors[NK_COLOR_SELECT_ACTIVE] = C_RAISED;
        colors[NK_COLOR_SLIDER] = C_RAISED;
        colors[NK_COLOR_SLIDER_CURSOR] = C_CYAN;
        colors[NK_COLOR_SLIDER_CURSOR_HOVER] = nk_rgb(42, 143, 164);
        colors[NK_COLOR_SLIDER_CURSOR_ACTIVE] = nk_rgb(26, 118, 138);
        colors[NK_COLOR_PROPERTY] = C_SURFACE;
        colors[NK_COLOR_EDIT] = C_SURFACE;
        colors[NK_COLOR_EDIT_CURSOR] = C_TEXT;
        colors[NK_COLOR_COMBO] = C_SURFACE;
        colors[NK_COLOR_CHART] = C_LINE;
    } else {
        colors[NK_COLOR_TEXT] = C_TEXT;
        colors[NK_COLOR_WINDOW] = C_BG;
        colors[NK_COLOR_HEADER] = C_SURFACE;
        colors[NK_COLOR_BORDER] = C_LINE;
        colors[NK_COLOR_BUTTON] = C_RAISED;
        colors[NK_COLOR_BUTTON_HOVER] = nk_rgb(42, 62, 76);
        colors[NK_COLOR_BUTTON_ACTIVE] = nk_rgb(53, 83, 98);
        colors[NK_COLOR_TOGGLE] = C_RAISED;
        colors[NK_COLOR_TOGGLE_HOVER] = nk_rgb(45, 67, 81);
        colors[NK_COLOR_TOGGLE_CURSOR] = C_CYAN;
        colors[NK_COLOR_SELECT] = C_SURFACE;
        colors[NK_COLOR_SELECT_ACTIVE] = C_RAISED;
        colors[NK_COLOR_SLIDER] = C_SURFACE;
        colors[NK_COLOR_SLIDER_CURSOR] = C_CYAN;
        colors[NK_COLOR_SLIDER_CURSOR_HOVER] = nk_rgb(111, 222, 235);
        colors[NK_COLOR_SLIDER_CURSOR_ACTIVE] = nk_rgb(50, 175, 194);
        colors[NK_COLOR_PROPERTY] = C_SURFACE;
        colors[NK_COLOR_EDIT] = C_SURFACE;
        colors[NK_COLOR_EDIT_CURSOR] = C_TEXT;
        colors[NK_COLOR_COMBO] = C_SURFACE;
        colors[NK_COLOR_CHART] = C_LINE;
    }
    nk_style_from_table(G.nk, colors);
    G.nk->style.window.spacing = nk_vec2(U(10), U(8));
    G.nk->style.window.padding = nk_vec2(U(14), U(12));
    G.nk->style.window.border = U(1);
    G.nk->style.window.rounding = U(7);
    G.nk->style.button.rounding = U(5);
    G.nk->style.button.padding = nk_vec2(U(10), U(7));
    G.nk->style.edit.border = U(1);
    G.nk->style.edit.rounding = U(4);
    G.nk->style.edit.padding = nk_vec2(U(8), U(6));
    if (G.password_brush) {
        HBRUSH replacement = CreateSolidBrush(
            RGB(C_SURFACE.r, C_SURFACE.g, C_SURFACE.b));
        if (replacement) {
            HBRUSH previous = G.password_brush;
            G.password_brush = replacement;
            DeleteObject(previous);
        }
    }
    G.theme_dirty = false;
}

static UINT system_dpi(void)
{
    HMODULE user32 = GetModuleHandleW(L"user32.dll");
    typedef UINT (WINAPI *get_dpi_for_system_fn)(void);
    get_dpi_for_system_fn get_dpi = NULL;
    FARPROC get_dpi_proc = user32
        ? GetProcAddress(user32, "GetDpiForSystem") : NULL;
    if (get_dpi_proc && sizeof(get_dpi) == sizeof(get_dpi_proc))
        memcpy(&get_dpi, &get_dpi_proc, sizeof(get_dpi));
    if (get_dpi) {
        UINT dpi = get_dpi();
        if (dpi)
            return dpi;
    }
    HDC dc = GetDC(NULL);
    if (!dc)
        return 96;
    int dpi = GetDeviceCaps(dc, LOGPIXELSX);
    ReleaseDC(NULL, dc);
    return dpi > 0 ? (UINT)dpi : 96;
}

static void enable_dpi_awareness(void)
{
    HMODULE user32 = GetModuleHandleW(L"user32.dll");
    typedef BOOL (WINAPI *set_dpi_context_fn)(DPI_AWARENESS_CONTEXT);
    set_dpi_context_fn set_context = NULL;
    FARPROC set_context_proc = user32
        ? GetProcAddress(user32, "SetProcessDpiAwarenessContext") : NULL;
    if (set_context_proc && sizeof(set_context) == sizeof(set_context_proc))
        memcpy(&set_context, &set_context_proc, sizeof(set_context));
    if (!set_context || !set_context(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2))
        SetProcessDPIAware();
}

static void adjust_window_rect_for_dpi(RECT *rect, DWORD style, UINT dpi)
{
    RECT original = *rect;
    HMODULE user32 = GetModuleHandleW(L"user32.dll");
    typedef BOOL (WINAPI *adjust_rect_for_dpi_fn)(LPRECT, DWORD, BOOL,
                                                  DWORD, UINT);
    adjust_rect_for_dpi_fn adjust = NULL;
    FARPROC adjust_proc = user32
        ? GetProcAddress(user32, "AdjustWindowRectExForDpi") : NULL;
    if (adjust_proc && sizeof(adjust) == sizeof(adjust_proc))
        memcpy(&adjust, &adjust_proc, sizeof(adjust));
    if (!adjust || !adjust(rect, style, FALSE, 0, dpi)) {
        *rect = original;
        AdjustWindowRectEx(rect, style, FALSE, 0);
    }
}

static bool recreate_dpi_fonts(void)
{
    GdiFont *body = nk_gdifont_create("Segoe UI", (int)U(15));
    GdiFont *title = nk_gdifont_create("Segoe UI", (int)U(21));
    GdiFont *small = nk_gdifont_create("Segoe UI", (int)U(12));
    GdiFont *code = nk_gdifont_create("Consolas", (int)U(27));
    HFONT password_font = CreateFontW(-(int)U(16), 0, 0, 0, FW_NORMAL,
        FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
        CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
    if (!body || !title || !small || !code || !password_font) {
        if (body) nk_gdifont_del(body);
        if (title) nk_gdifont_del(title);
        if (small) nk_gdifont_del(small);
        if (code) nk_gdifont_del(code);
        if (password_font) DeleteObject(password_font);
        return false;
    }

    GdiFont *old_body = G.font_body;
    GdiFont *old_title = G.font_title;
    GdiFont *old_small = G.font_small;
    GdiFont *old_code = G.font_code;
    HFONT old_password_font = G.password_font;
    G.font_body = body;
    G.font_title = title;
    G.font_small = small;
    G.font_code = code;
    G.password_font = password_font;
    nk_gdi_set_font(G.font_body);
    apply_theme();
    if (G.password_edit)
        SendMessageW(G.password_edit, WM_SETFONT, (WPARAM)G.password_font, TRUE);
    if (old_password_font) DeleteObject(old_password_font);
    if (old_code) nk_gdifont_del(old_code);
    if (old_small) nk_gdifont_del(old_small);
    if (old_title) nk_gdifont_del(old_title);
    if (old_body) nk_gdifont_del(old_body);
    return true;
}

static void draw_status_footer(void)
{
    const char *state;
    struct nk_color color;
    switch (G.app.sess.state) {
    case SESS_CONNECTING:
    case SESS_HANDSHAKE:
        state = T("正在连接", "正在連線", "Connecting");
        color = C_AMBER;
        break;
    case SESS_ONLINE:
        state = T("已连接", "已連線", "Connected");
        color = C_GREEN;
        break;
    default:
        state = T("未连接", "未連線", "Disconnected");
        color = C_MUTED;
        break;
    }

    nk_layout_row_begin(G.nk, NK_DYNAMIC, U(28), 2);
    nk_layout_row_push(G.nk, 0.25f);
    nk_label_colored(G.nk, state, NK_TEXT_LEFT, color);
    nk_layout_row_push(G.nk, 0.75f);
    const char *message = G.notice[0] ? G.notice : display_status();
    struct nk_color message_color = G.notice[0] ? C_AMBER : C_MUTED;
    char telemetry_state[UI_TEXT_CAP];
    if (G.app.sess.state == SESS_ONLINE && !G.notice[0]) {
        if (G.app.mock_on) {
            message = T("模拟遥测运行中", "模擬遙測運行中", "Simulated telemetry active");
            message_color = C_CYAN;
        } else if (G.app.bridge.has_telem) {
            message = T("模拟器遥测正常", "模擬器遙測正常", "Simulator telemetry live");
            message_color = C_GREEN;
        } else {
            strncpy(telemetry_state,
                    T("等待 AeroflyBridge 遥测", "等待 AeroflyBridge 遙測",
                      "Waiting for AeroflyBridge telemetry"),
                    sizeof(telemetry_state) - 1);
            telemetry_state[sizeof(telemetry_state) - 1] = '\0';
            message = telemetry_state;
            message_color = C_AMBER;
        }
    }
    nk_label_colored(G.nk, message, NK_TEXT_RIGHT, message_color);
    nk_layout_row_end(G.nk);
}

static void select_server_address(const char *address)
{
    char host[128];
    int port;
    if (!parse_server_address(address, host, sizeof(host), &port))
        return;
    strncpy(G.server_entry, address, sizeof(G.server_entry) - 1);
    G.server_entry[sizeof(G.server_entry) - 1] = '\0';
    strncpy(G.app.cfg.server, host, sizeof(G.app.cfg.server) - 1);
    G.app.cfg.server[sizeof(G.app.cfg.server) - 1] = '\0';
    G.app.cfg.port = port;
    G.selected_server = 0;
    for (size_t i = 0; i < G.app.cfg.nservers; ++i) {
        if (strcmp(G.app.cfg.servers[i], address) == 0) {
            G.selected_server = i;
            break;
        }
    }
    mark_config_dirty();
}

static bool save_current_server(void)
{
    char host[128];
    int port;
    if (!parse_server_address(G.server_entry, host, sizeof(host), &port)) {
        set_notice("地址格式应为 host:port，例如 flight.skeet.top:6809。",
                   "地址格式應為 host:port，例如 flight.skeet.top:6809。",
                   "Use host:port format, for example flight.skeet.top:6809.");
        return false;
    }

    for (size_t i = 0; i < G.app.cfg.nservers; ++i) {
        if (strcmp(G.app.cfg.servers[i], G.server_entry) == 0) {
            select_server_address(G.server_entry);
            set_notice("服务器地址已保存。", "伺服器地址已儲存。", "Server address saved.");
            save_config();
            return true;
        }
    }
    if (G.app.cfg.nservers >= CFG_MAX_SERVERS) {
        set_notice("服务器列表已满，请先移除一条记录。",
                   "伺服器清單已滿，請先移除一筆記錄。",
                   "The server list is full. Remove an entry first.");
        return false;
    }

    strncpy(G.app.cfg.servers[G.app.cfg.nservers], G.server_entry,
            JSN_STR_CAP - 1);
    G.app.cfg.servers[G.app.cfg.nservers][JSN_STR_CAP - 1] = '\0';
    G.app.cfg.nservers++;
    select_server_address(G.server_entry);
    set_notice("服务器地址已保存。", "伺服器地址已儲存。", "Server address saved.");
    save_config();
    return true;
}

static void draw_server_combo(void)
{
    char selected[JSN_STR_CAP];
    current_server_address(selected, sizeof(selected));
    nk_layout_row_dynamic(G.nk, U(22), 1);
    nk_label_colored(G.nk, T("已保存的服务器", "已儲存的伺服器", "Saved servers"),
                     NK_TEXT_LEFT, C_MUTED);
    nk_layout_row_dynamic(G.nk, U(34), 1);
    if (nk_combo_begin_label(G.nk, selected, nk_vec2(U(340), U(210)))) {
        nk_layout_row_dynamic(G.nk, U(29), 1);
        for (size_t i = 0; i < G.app.cfg.nservers; ++i) {
            if (nk_combo_item_label(G.nk, G.app.cfg.servers[i], NK_TEXT_LEFT))
                select_server_address(G.app.cfg.servers[i]);
        }
        nk_combo_end(G.nk);
    }
}

static void draw_connection_page(void)
{
    cfg_t *c = &G.app.cfg;
    draw_heading(T("连接到飞行网络", "連線至飛行網絡", "Connect to a flight network"));
    draw_help(T("输入网络账号信息，确认服务器地址，然后连接。密码只在本次运行期间保留。",
                "輸入網絡帳號資料，確認伺服器地址，然後連線。密碼只會在本次執行期間保留。",
                "Enter your network account, confirm the server, then connect. Your password stays in memory for this session only."));
    if (G.notice[0] || (G.status[0] && !is_online())) {
        nk_layout_row_dynamic(G.nk, U(36), 1);
        nk_label_colored(G.nk, G.notice[0] ? G.notice : display_status(),
                         NK_TEXT_LEFT, G.notice[0] ? C_AMBER : C_MUTED);
    }

    draw_section(T("飞行员信息", "飛行員資料", "Pilot details"));
    if (G.width / G.scale >= 850) {
        nk_layout_row_dynamic(G.nk, U(22), 2);
        nk_label_colored(G.nk, T("呼号", "呼號", "Callsign"), NK_TEXT_LEFT, C_MUTED);
        nk_label_colored(G.nk, T("网络账号 ID", "網絡帳號 ID", "Network account ID"), NK_TEXT_LEFT, C_MUTED);
        nk_layout_row_dynamic(G.nk, U(34), 2);
        edit_input(c->callsign, sizeof(c->callsign));
        edit_input(c->cid, sizeof(c->cid));
    } else {
        edit_field(T("呼号", "呼號", "Callsign"), c->callsign, sizeof(c->callsign));
        edit_field(T("网络账号 ID", "網絡帳號 ID", "Network account ID"), c->cid, sizeof(c->cid));
    }

    if (G.width / G.scale >= 850) {
        nk_layout_row_dynamic(G.nk, U(22), 2);
        nk_label_colored(G.nk, T("显示姓名", "顯示姓名", "Display name"), NK_TEXT_LEFT, C_MUTED);
        nk_label_colored(G.nk, T("模拟器", "模擬器", "Simulator"), NK_TEXT_LEFT, C_MUTED);
        nk_layout_row_dynamic(G.nk, U(34), 2);
        edit_input(c->realname, sizeof(c->realname));
        nk_label(G.nk, "Aerofly FS 4", NK_TEXT_LEFT);
    } else {
        edit_field(T("显示姓名", "顯示姓名", "Display name"), c->realname, sizeof(c->realname));
        draw_field_label(T("模拟器", "模擬器", "Simulator"));
        nk_layout_row_dynamic(G.nk, U(30), 1);
        nk_label(G.nk, "Aerofly FS 4", NK_TEXT_LEFT);
    }

    draw_field_label(T("密码", "密碼", "Password"));
    nk_layout_row_dynamic(G.nk, U(35), 1);
    static char empty_password_widget[1] = "";
    struct nk_rect password_bounds = nk_widget_bounds(G.nk);
    nk_edit_string_zero_terminated(G.nk, NK_EDIT_FIELD | NK_EDIT_READ_ONLY,
                                   empty_password_widget,
                                   (int)sizeof(empty_password_widget),
                                   nk_filter_default);
    if (G.password_edit) {
        MoveWindow(G.password_edit, (int)password_bounds.x + 2,
                   (int)password_bounds.y + 2,
                   (int)password_bounds.w - 4,
                   (int)password_bounds.h - 4, TRUE);
        ShowWindow(G.password_edit, SW_SHOW);
    }
    draw_help(T("密码不会写入 settings.json。", "密碼不會寫入 settings.json。",
                "The password is never written to settings.json."));

    draw_section(T("服务器", "伺服器", "Server"));
    edit_field(T("服务器地址（host:port）", "伺服器地址（host:port）",
                  "Server address (host:port)"),
               G.server_entry, sizeof(G.server_entry));
    draw_server_combo();
    nk_layout_row_dynamic(G.nk, U(36), 1);
    if (secondary_button(T("保存当前地址", "儲存目前地址", "Save this address")))
        save_current_server();

    char host[128];
    int port = 6809;
    bool valid_address = parse_server_address(G.server_entry, host, sizeof(host), &port);
    if (valid_address && address_is_asc(host, port)) {
        nk_layout_row_dynamic(G.nk, U(27), 1);
        nk_label_colored(G.nk,
                         T("Aviation Simulation Community · 默认服务器",
                           "Aviation Simulation Community · 預設伺服器",
                           "Aviation Simulation Community · default server"),
                         NK_TEXT_LEFT, C_GREEN);
    } else if (valid_address && is_training_host(host)) {
        nk_layout_row_dynamic(G.nk, U(39), 1);
        nk_label_colored(G.nk,
                         T("此地址是 VATSIM Sweatbox 训练服务器，不是普通连飞入口。",
                           "此地址是 VATSIM Sweatbox 訓練伺服器，並非一般連飛入口。",
                           "This is a VATSIM Sweatbox training server, not a regular flight-network endpoint."),
                         NK_TEXT_LEFT, C_AMBER);
    } else {
        draw_help(T("社区服务器可使用自己的 FSD 地址和认证要求。",
                    "社群伺服器可使用自己的 FSD 地址及認證要求。",
                    "Community servers can use their own FSD address and authentication requirements."));
    }

    nk_layout_row_dynamic(G.nk, U(44), 1);
    const char *connect_label = (valid_address && address_is_asc(host, port))
        ? T("连接到 ASC 社区", "連線至 ASC 社群", "Connect to ASC Community")
        : T("连接到所选服务器", "連線至所選伺服器", "Connect to selected server");
    if (primary_button(connect_label))
        SendMessageW(G.window, WM_COMMAND, IDC_CONNECT, 0);

    nk_layout_row_dynamic(G.nk, U(34), 1);
    nk_label_colored(G.nk,
        T("新手流程：填入账号 → 确认服务器 → 连接后提交飞行计划并设置应答机。",
          "新手流程：輸入帳號 → 確認伺服器 → 連線後提交飛行計劃並設定應答機。",
          "Quick start: enter your account → confirm the server → file a plan and set your transponder after connecting."),
        NK_TEXT_LEFT, C_MUTED);
}

static void copy_to_clipboard(const char *utf8)
{
    int chars = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
                                    utf8, -1, NULL, 0);
    if (chars <= 0 || !OpenClipboard(G.window))
        return;
    EmptyClipboard();
    HGLOBAL mem = GlobalAlloc(GMEM_MOVEABLE, (SIZE_T)chars * sizeof(wchar_t));
    if (mem) {
        wchar_t *wide = (wchar_t *)GlobalLock(mem);
        if (wide) {
            MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
                                utf8, -1, wide, chars);
            GlobalUnlock(mem);
            if (!SetClipboardData(CF_UNICODETEXT, mem))
                GlobalFree(mem);
        } else {
            GlobalFree(mem);
        }
    }
    CloseClipboard();
}

static void set_auth_mode(bool jwt)
{
    const char *value = jwt ? "vatsim" : "legacy";
    if (strcmp(G.app.cfg.type, value) != 0) {
        strncpy(G.app.cfg.type, value, sizeof(G.app.cfg.type) - 1);
        G.app.cfg.type[sizeof(G.app.cfg.type) - 1] = '\0';
        mark_config_dirty();
    }
}

static void remove_selected_server(void)
{
    cfg_t *c = &G.app.cfg;
    if (G.selected_server >= c->nservers)
        return;
    if (strcmp(c->servers[G.selected_server], "flight.skeet.top:6809") == 0) {
        set_notice("ASC 默认服务器不能从列表中移除。",
                   "ASC 預設伺服器不能從清單中移除。",
                   "The ASC default server cannot be removed.");
        return;
    }
    memmove(&c->servers[G.selected_server],
            &c->servers[G.selected_server + 1],
            (c->nservers - G.selected_server - 1) * sizeof(c->servers[0]));
    c->nservers--;
    if (G.selected_server >= c->nservers && c->nservers)
        G.selected_server = c->nservers - 1;
    if (c->nservers)
        select_server_address(c->servers[G.selected_server]);
    mark_config_dirty();
    save_config();
}

static void draw_settings_page(void)
{
    cfg_t *c = &G.app.cfg;
    draw_heading(T("设置", "設定", "Settings"));

    draw_section(T("语言", "語言", "Language"));
    nk_layout_row_dynamic(G.nk, U(36), 3);
    if (nk_option_label(G.nk, "简体中文", strcmp(c->language, "zh-CN") == 0)) {
        strcpy(c->language, "zh-CN"); update_window_title(); mark_config_dirty();
    }
    if (nk_option_label(G.nk, "繁體中文", strcmp(c->language, "zh-HK") == 0)) {
        strcpy(c->language, "zh-HK"); update_window_title(); mark_config_dirty();
    }
    if (nk_option_label(G.nk, "English (US)", strcmp(c->language, "en-US") == 0)) {
        strcpy(c->language, "en-US"); update_window_title(); mark_config_dirty();
    }

    draw_section(T("外观", "外觀", "Appearance"));
    nk_layout_row_dynamic(G.nk, U(36), 2);
    if (nk_option_label(G.nk, T("深色航行情境", "深色航行情境", "Dark flight deck"),
                        strcmp(c->theme, "dark") == 0)) {
        strcpy(c->theme, "dark"); G.theme_dirty = true; mark_config_dirty();
    }
    if (nk_option_label(G.nk, T("浅色", "淺色", "Light"),
                        strcmp(c->theme, "light") == 0)) {
        strcpy(c->theme, "light"); G.theme_dirty = true; mark_config_dirty();
    }

    draw_section(T("服务器列表", "伺服器清單", "Server list"));
    draw_help(T("在连接页编辑服务器地址，再保存到列表。ASC 默认服务器始终保留。",
                "在連線頁編輯伺服器地址，再儲存至清單。ASC 預設伺服器會保留。",
                "Edit an address on the Connection page, then save it here. The ASC default stays in the list."));
    nk_layout_row_dynamic(G.nk, U(34), 1);
    char selection[JSN_STR_CAP] = "";
    if (G.selected_server < c->nservers)
        strncpy(selection, c->servers[G.selected_server], sizeof(selection) - 1);
    if (nk_combo_begin_label(G.nk,
                             selection[0] ? selection : T("选择服务器", "選擇伺服器", "Select a server"),
                             nk_vec2((float)G.width - U(100), U(190)))) {
        nk_layout_row_dynamic(G.nk, U(30), 1);
        for (size_t i = 0; i < c->nservers; ++i) {
            if (nk_combo_item_label(G.nk, c->servers[i], NK_TEXT_LEFT)) {
                G.selected_server = i;
                select_server_address(c->servers[i]);
            }
        }
        nk_combo_end(G.nk);
    }
    nk_layout_row_dynamic(G.nk, U(36), 2);
    if (secondary_button(T("保存连接页地址", "儲存連線頁地址", "Save address from Connection")))
        save_current_server();
    if (secondary_button(T("移除选中地址", "移除所選地址", "Remove selected address")))
        remove_selected_server();

    nk_layout_row_dynamic(G.nk, U(24), 1);
    nk_label_colored(G.nk,
        T("VATSIM AUTOMATIC 官方地址（仅供参考）",
          "VATSIM AUTOMATIC 官方地址（僅供參考）",
          "VATSIM AUTOMATIC official address (reference only)"),
        NK_TEXT_LEFT, C_AMBER);
    nk_layout_row_dynamic(G.nk, U(30), 1);
    nk_label(G.nk, "fsd.connect.vatsim.net:6809", NK_TEXT_LEFT);
    nk_layout_row_dynamic(G.nk, U(34), 2);
    if (secondary_button(T("复制官方地址", "複製官方地址", "Copy official address"))) {
        copy_to_clipboard("fsd.connect.vatsim.net:6809");
        set_notice("已复制服务器地址。", "已複製伺服器地址。", "Server address copied.");
    }
    nk_label_colored(G.nk,
        T("Aerofly Link 未列入 VATSIM 获批客户端。请使用获批软件连接。",
          "Aerofly Link 未列入 VATSIM 認可用戶端。請使用認可軟件連線。",
          "Aerofly Link is not listed as VATSIM-approved. Use an approved client to connect."),
        NK_TEXT_LEFT, C_AMBER);
    nk_layout_row_dynamic(G.nk, U(25), 1);
    nk_label_colored(G.nk, "vatsim.net/docs/policy/approved-software",
                     NK_TEXT_LEFT, C_MUTED);
    nk_layout_row_dynamic(G.nk, U(25), 1);
    nk_label_colored(G.nk,
        T("官方状态源：status.vatsim.net/status.json（普通网络与 Sweatbox 分列）",
          "官方狀態來源：status.vatsim.net/status.json（一般網絡與 Sweatbox 分開）",
          "Official status feed: status.vatsim.net/status.json (network and Sweatbox lists are separate)"),
        NK_TEXT_LEFT, C_MUTED);

    draw_section(T("认证方式", "認證方式", "Authentication"));
    nk_layout_row_dynamic(G.nk, U(37), 2);
    if (nk_option_label(G.nk,
                        T("FSD-JWT 令牌（推荐）", "FSD-JWT 權杖（建議）",
                          "FSD-JWT token (recommended)"),
                        strcmp(c->type, "vatsim") == 0))
        set_auth_mode(true);
    if (nk_option_label(G.nk,
                        T("旧版密码协议", "舊版密碼協定", "Legacy password protocol"),
                        strcmp(c->type, "legacy") == 0))
        set_auth_mode(false);
    draw_help(T("FSD-JWT 使用 revision 100 和 HTTPS 令牌地址；旧版使用 revision 9。按服务器文档选择。",
                "FSD-JWT 使用 revision 100 及 HTTPS 權杖地址；舊版使用 revision 9。請按伺服器文件選擇。",
                "FSD-JWT uses revision 100 and an HTTPS token endpoint; legacy uses revision 9. Follow the server documentation."));
    nk_layout_row_dynamic(G.nk, U(36), 1);
    if (secondary_button(G.settings_advanced
        ? T("隐藏高级网络与遥测设置", "隱藏進階網絡與遙測設定",
            "Hide advanced network and telemetry settings")
        : T("高级网络与遥测设置", "進階網絡與遙測設定",
            "Advanced network and telemetry settings")))
        G.settings_advanced = !G.settings_advanced;

    if (G.settings_advanced) {
        if (strcmp(c->type, "vatsim") == 0) {
            edit_field(T("HTTPS 令牌地址", "HTTPS 權杖地址", "HTTPS token endpoint"),
                       c->jwt_url, sizeof(c->jwt_url));
            edit_field(T("应用代理（留空使用 Windows 系统代理）",
                         "應用程式 Proxy（留空使用 Windows 系統 Proxy）",
                         "Application proxy (blank uses Windows system proxy)"),
                       c->jwt_proxy, sizeof(c->jwt_proxy));
            edit_field(T("代理绕过列表", "Proxy 繞過清單", "Proxy bypass list"),
                       c->jwt_proxy_bypass, sizeof(c->jwt_proxy_bypass));
            draw_help(T("CID 和密码只会发送到此 HTTPS 地址。不要填写不可信的令牌服务。",
                        "CID 和密碼只會傳送至此 HTTPS 地址。請勿填入不可信的權杖服務。",
                        "Your CID and password are sent only to this HTTPS endpoint. Use a token service you trust."));
        }

        draw_section(T("模拟器遥测", "模擬器遙測", "Simulator telemetry"));
        if (G.width / G.scale >= 850) {
            nk_layout_row_dynamic(G.nk, U(22), 3);
            nk_label_colored(G.nk, T("纬度", "緯度", "Latitude"), NK_TEXT_LEFT, C_MUTED);
            nk_label_colored(G.nk, T("经度", "經度", "Longitude"), NK_TEXT_LEFT, C_MUTED);
            nk_label_colored(G.nk, T("高度（米）", "高度（米）", "Altitude (m)"), NK_TEXT_LEFT, C_MUTED);
            nk_layout_row_dynamic(G.nk, U(34), 3);
            edit_input(c->mock_lat, sizeof(c->mock_lat));
            edit_input(c->mock_lon, sizeof(c->mock_lon));
            edit_input(c->mock_alt, sizeof(c->mock_alt));
        } else {
            edit_field(T("纬度", "緯度", "Latitude"), c->mock_lat, sizeof(c->mock_lat));
            edit_field(T("经度", "經度", "Longitude"), c->mock_lon, sizeof(c->mock_lon));
            edit_field(T("高度（米）", "高度（米）", "Altitude (m)"), c->mock_alt, sizeof(c->mock_alt));
        }
        nk_layout_row_dynamic(G.nk, U(38), 1);
        if (secondary_button(G.app.mock_on
            ? T("停止模拟遥测", "停止模擬遙測", "Stop simulated telemetry")
            : T("启动模拟遥测", "啟動模擬遙測", "Start simulated telemetry"))) {
            const char *error = app_toggle_mock(&G.app, !G.app.mock_on);
            if (error) {
                strncpy(G.status, error, sizeof(G.status) - 1);
                G.status[sizeof(G.status) - 1] = '\0';
            }
        }
    }
    if (primary_button(T("保存设置", "儲存設定", "Save settings")))
        save_config();
}

static void set_xpdr_code(const char *code)
{
    if (!xpdr_code_valid(code)) {
        set_notice("应答机代码必须是四位八进制数字。",
                   "應答機代碼必須是四位八進制數字。",
                   "The transponder code must contain four octal digits.");
        return;
    }
    app_set_xpdr_code(&G.app, code);
    strncpy(G.xpdr_entry, code, sizeof(G.xpdr_entry) - 1);
    G.xpdr_entry[sizeof(G.xpdr_entry) - 1] = '\0';
    G.notice[0] = '\0';
}

static void draw_transponder(void)
{
    draw_section(T("应答机", "應答機", "Transponder"));
    nk_layout_row_begin(G.nk, NK_DYNAMIC, U(76), 2);
    nk_layout_row_push(G.nk, 0.36f);
    nk_gdi_set_font(G.font_small);
    nk_label_colored(G.nk, T("当前代码", "目前代碼", "Current code"), NK_TEXT_LEFT, C_MUTED);
    nk_gdi_set_font(G.font_code);
    nk_label(G.nk, G.app.xpdr.squawk, NK_TEXT_LEFT);
    nk_gdi_set_font(G.font_body);
    nk_layout_row_push(G.nk, 0.64f);
    if (G.app.xpdr.alt_mode)
        nk_label_colored(G.nk, T("高度上报已启用", "高度上報已啟用", "Altitude reporting active"), NK_TEXT_LEFT, C_GREEN);
    else
        nk_label_colored(G.nk, T("待机：位置上报暂停", "待機：位置上報已暫停", "Standby: position reports paused"), NK_TEXT_LEFT, C_AMBER);
    nk_layout_row_end(G.nk);

    nk_layout_row_dynamic(G.nk, U(37), 3);
    if (nk_option_label(G.nk, T("待机", "待機", "Standby"), !G.app.xpdr.alt_mode))
        app_set_xpdr_mode(&G.app, false);
    if (nk_option_label(G.nk, T("高度上报", "高度上報", "Altitude reporting"), G.app.xpdr.alt_mode))
        app_set_xpdr_mode(&G.app, true);
    if (secondary_button(T("发送 IDENT", "發送 IDENT", "Send IDENT")))
        app_ident(&G.app);

    draw_field_label(T("设置代码（0–7）", "設定代碼（0–7）", "Set code (0–7)"));
    nk_layout_row_dynamic(G.nk, U(34), 2);
    nk_edit_string_zero_terminated(G.nk, NK_EDIT_FIELD, G.xpdr_entry,
                                   sizeof(G.xpdr_entry), nk_filter_default);
    if (secondary_button(T("应用代码", "套用代碼", "Apply code")))
        set_xpdr_code(G.xpdr_entry);
    nk_layout_row_dynamic(G.nk, U(34), 5);
    static const char *codes[] = { "1200", "7000", "7700", "7600", "7500" };
    for (size_t i = 0; i < 5; ++i) {
        bool pressed = i == 2 ? caution_button(codes[i])
                              : secondary_button(codes[i]);
        if (pressed)
            set_xpdr_code(codes[i]);
    }
    draw_help(T("7700 紧急；7600 无线电失效；7500 遭劫持。只有在适用时使用。",
                "7700 緊急；7600 無線電失效；7500 遭劫持。只在適用時使用。",
                "7700 emergency; 7600 radio failure; 7500 unlawful interference. Use only when applicable."));
}

static void set_fp_type_from_config(void)
{
    const char *type = G.app.cfg.fp_type;
    if (strcmp(type, "VFR") == 0) G.app.fp_type[0] = 'V';
    else if (strcmp(type, "SVFR") == 0) G.app.fp_type[0] = 'S';
    else if (strcmp(type, "DVFR") == 0) G.app.fp_type[0] = 'D';
    else G.app.fp_type[0] = 'I';
    G.app.fp_type[1] = '\0';
}

static void draw_flightplan(void)
{
    cfg_t *c = &G.app.cfg;
    draw_section(T("飞行计划", "飛行計劃", "Flight plan"));
    if (G.width / G.scale >= 900) {
        nk_layout_row_dynamic(G.nk, U(22), 3);
        nk_label_colored(G.nk, T("航空器", "航空器", "Aircraft"), NK_TEXT_LEFT, C_MUTED);
        nk_label_colored(G.nk, T("真空速（节）", "真空速（節）", "True airspeed (kt)"), NK_TEXT_LEFT, C_MUTED);
        nk_label_colored(G.nk, T("尾流类别", "尾流類別", "Wake category"), NK_TEXT_LEFT, C_MUTED);
        nk_layout_row_dynamic(G.nk, U(34), 3);
        edit_input(c->fp_aircraft, sizeof(c->fp_aircraft));
        edit_input(c->fp_tas, sizeof(c->fp_tas));
        static const char *wake[] = { "Light", "Medium", "Heavy", "Super" };
        char *wake_value = c->fp_wake;
        if (nk_combo_begin_label(G.nk, wake_value[0] ? wake_value : "Medium",
                                 nk_vec2(U(220), U(135)))) {
            nk_layout_row_dynamic(G.nk, U(28), 1);
            for (size_t i = 0; i < 4; ++i)
                if (nk_combo_item_label(G.nk, wake[i], NK_TEXT_LEFT)) {
                    strncpy(wake_value, wake[i], sizeof(c->fp_wake) - 1);
                    wake_value[sizeof(c->fp_wake) - 1] = '\0';
                    mark_config_dirty();
                }
            nk_combo_end(G.nk);
        }
    } else {
        edit_field(T("航空器", "航空器", "Aircraft"), c->fp_aircraft, sizeof(c->fp_aircraft));
        edit_field(T("真空速（节）", "真空速（節）", "True airspeed (kt)"), c->fp_tas, sizeof(c->fp_tas));
        edit_field(T("尾流类别", "尾流類別", "Wake category"), c->fp_wake, sizeof(c->fp_wake));
    }

    if (G.width / G.scale >= 900) {
        nk_layout_row_dynamic(G.nk, U(22), 3);
        nk_label_colored(G.nk, T("起飞机场 ICAO", "起飛機場 ICAO", "Departure ICAO"), NK_TEXT_LEFT, C_MUTED);
        nk_label_colored(G.nk, T("目的机场 ICAO", "目的機場 ICAO", "Destination ICAO"), NK_TEXT_LEFT, C_MUTED);
        nk_label_colored(G.nk, T("备降机场 ICAO", "備降機場 ICAO", "Alternate ICAO"), NK_TEXT_LEFT, C_MUTED);
        nk_layout_row_dynamic(G.nk, U(34), 3);
        edit_input(c->fp_dep, sizeof(c->fp_dep));
        edit_input(c->fp_dest, sizeof(c->fp_dest));
        edit_input(c->fp_altn, sizeof(c->fp_altn));
        nk_layout_row_dynamic(G.nk, U(22), 3);
        nk_label_colored(G.nk, T("巡航高度", "巡航高度", "Cruise altitude"), NK_TEXT_LEFT, C_MUTED);
        nk_label_colored(G.nk, T("计划起飞（UTC）", "計劃起飛（UTC）", "Departure time (UTC)"), NK_TEXT_LEFT, C_MUTED);
        nk_label_colored(G.nk, T("预计用时", "預計用時", "Estimated time en route"), NK_TEXT_LEFT, C_MUTED);
        nk_layout_row_dynamic(G.nk, U(34), 3);
        edit_input(c->fp_cruise, sizeof(c->fp_cruise));
        nk_edit_string_zero_terminated(G.nk, NK_EDIT_FIELD, G.departure_time,
                                       sizeof(G.departure_time), nk_filter_default);
        edit_input(c->fp_eet, sizeof(c->fp_eet));
    } else {
        edit_field(T("起飞机场 ICAO", "起飛機場 ICAO", "Departure ICAO"), c->fp_dep, sizeof(c->fp_dep));
        edit_field(T("目的机场 ICAO", "目的機場 ICAO", "Destination ICAO"), c->fp_dest, sizeof(c->fp_dest));
        edit_field(T("备降机场 ICAO", "備降機場 ICAO", "Alternate ICAO"), c->fp_altn, sizeof(c->fp_altn));
        edit_field(T("巡航高度", "巡航高度", "Cruise altitude"), c->fp_cruise, sizeof(c->fp_cruise));
    }

    draw_field_label(T("飞行规则", "飛行規則", "Flight rules"));
    nk_layout_row_dynamic(G.nk, U(34), 4);
    static const char *types[] = { "IFR", "VFR", "SVFR", "DVFR" };
    for (size_t i = 0; i < 4; ++i) {
        if (nk_option_label(G.nk, types[i], strcmp(c->fp_type, types[i]) == 0)) {
            strncpy(c->fp_type, types[i], sizeof(c->fp_type) - 1);
            c->fp_type[sizeof(c->fp_type) - 1] = '\0';
            mark_config_dirty();
        }
    }

    edit_field(T("航路", "航路", "Route"), c->fp_route, sizeof(c->fp_route));
    edit_field(T("备注", "備註", "Remarks"), c->fp_remarks, sizeof(c->fp_remarks));
    nk_layout_row_dynamic(G.nk, U(39), 1);
    if (primary_button(T("提交飞行计划", "提交飛行計劃", "Submit flight plan"))) {
        set_fp_type_from_config();
        strncpy(G.app.fp_aircraft_buf, c->fp_aircraft, sizeof(G.app.fp_aircraft_buf) - 1);
        strncpy(G.app.fp_wake_buf, c->fp_wake, sizeof(G.app.fp_wake_buf) - 1);
        strncpy(G.app.fp_tas_buf, c->fp_tas, sizeof(G.app.fp_tas_buf) - 1);
        strncpy(G.app.fp_dep_buf, c->fp_dep, sizeof(G.app.fp_dep_buf) - 1);
        strncpy(G.app.fp_dest_buf, c->fp_dest, sizeof(G.app.fp_dest_buf) - 1);
        strncpy(G.app.fp_altn_buf, c->fp_altn, sizeof(G.app.fp_altn_buf) - 1);
        strncpy(G.app.fp_cruise_buf, c->fp_cruise, sizeof(G.app.fp_cruise_buf) - 1);
        strncpy(G.app.fp_route_buf, c->fp_route, sizeof(G.app.fp_route_buf) - 1);
        strncpy(G.app.fp_remarks_buf, c->fp_remarks, sizeof(G.app.fp_remarks_buf) - 1);
        strncpy(G.app.fp_eet_buf, c->fp_eet, sizeof(G.app.fp_eet_buf) - 1);
        strncpy(G.app.fp_endur_buf, c->fp_endur, sizeof(G.app.fp_endur_buf) - 1);
        G.app.fp_dep_time_buf[0] = '\0';
        G.app.fp_act_buf[0] = '\0';
        if (app_submit_flightplan(&G.app) == 0)
            set_notice("飞行计划已提交。", "飛行計劃已提交。", "Flight plan submitted.");
        else
            set_notice("提交失败：请确认已连接且字段完整。",
                       "提交失敗：請確認已連線並填妥欄位。",
                       "Plan submission failed. Check the connection and required fields.");
        mark_config_dirty();
        save_config();
    }
}

static void draw_telemetry(void)
{
    const telem_t *t = &G.app.bridge.last;
    char value[96];
    draw_section(T("飞行状态", "飛行狀態", "Flight status"));
    if (!G.app.bridge.has_telem) {
        draw_help(T("尚未收到模拟器遥测。启动 Aerofly FS 4 和 AeroflyBridge，或在设置中启用模拟遥测。",
                    "尚未收到模擬器遙測。請啟動 Aerofly FS 4 和 AeroflyBridge，或在設定中啟用模擬遙測。",
                    "No simulator telemetry yet. Start Aerofly FS 4 with AeroflyBridge, or enable simulated telemetry in Settings."));
        return;
    }
    bool wide = G.width / G.scale >= 900;
    int columns = wide ? 4 : 2;
    nk_layout_row_dynamic(G.nk, U(22), columns);
    nk_label_colored(G.nk, T("高度", "高度", "Altitude"), NK_TEXT_LEFT, C_MUTED);
    nk_label_colored(G.nk, T("地速", "地速", "Ground speed"), NK_TEXT_LEFT, C_MUTED);
    if (wide) {
        nk_label_colored(G.nk, T("航向", "航向", "Heading"), NK_TEXT_LEFT, C_MUTED);
        nk_label_colored(G.nk, T("垂直速度", "垂直速度", "Vertical speed"), NK_TEXT_LEFT, C_MUTED);
    }
    nk_layout_row_dynamic(G.nk, U(31), columns);
    _snprintf(value, sizeof(value) - 1, "%.0f ft", t->alt_m * 3.28084);
    nk_label(G.nk, value, NK_TEXT_LEFT);
    _snprintf(value, sizeof(value) - 1, "%.0f kt", t->gs_kts);
    nk_label(G.nk, value, NK_TEXT_LEFT);
    if (wide) {
        _snprintf(value, sizeof(value) - 1, "%.0f°", t->hdg_true);
        nk_label(G.nk, value, NK_TEXT_LEFT);
        _snprintf(value, sizeof(value) - 1, "%.0f fpm", t->vs_fpm);
        nk_label(G.nk, value, NK_TEXT_LEFT);
    }
    if (!wide) {
        nk_layout_row_dynamic(G.nk, U(22), 2);
        nk_label_colored(G.nk, T("航向", "航向", "Heading"), NK_TEXT_LEFT, C_MUTED);
        nk_label_colored(G.nk, T("垂直速度", "垂直速度", "Vertical speed"), NK_TEXT_LEFT, C_MUTED);
        nk_layout_row_dynamic(G.nk, U(31), 2);
        _snprintf(value, sizeof(value) - 1, "%.0f°", t->hdg_true);
        nk_label(G.nk, value, NK_TEXT_LEFT);
        _snprintf(value, sizeof(value) - 1, "%.0f fpm", t->vs_fpm);
        nk_label(G.nk, value, NK_TEXT_LEFT);
    }
    _snprintf(value, sizeof(value) - 1, "%.5f, %.5f", t->lat, t->lon);
    nk_layout_row_dynamic(G.nk, U(22), 1);
    nk_label_colored(G.nk, T("位置", "位置", "Position"), NK_TEXT_LEFT, C_MUTED);
    nk_layout_row_dynamic(G.nk, U(28), 1);
    nk_label(G.nk, value, NK_TEXT_LEFT);
}

static void draw_messages(void)
{
    draw_section(T("网络消息", "網絡訊息", "Network messages"));
    nk_layout_row_dynamic(G.nk, U(G.height / G.scale > 760 ? 190 : 125), 1);
    if (nk_group_begin(G.nk, "message-history", NK_WINDOW_BORDER)) {
        for (size_t i = 0; i < G.log_count; ++i) {
            size_t slot = (G.log_head + i) % LOG_LINES;
            struct nk_color color = G.log[slot].kind == APP_LOG_IN ? C_GREEN
                : G.log[slot].kind == APP_LOG_OUT ? C_TEXT : C_MUTED;
            nk_layout_row_dynamic(G.nk, U(22), 1);
            nk_label_colored(G.nk, G.log[slot].text, NK_TEXT_LEFT, color);
        }
        nk_group_end(G.nk);
    }
    nk_layout_row_dynamic(G.nk, U(22), 1);
    nk_label_colored(G.nk,
        T("可输入 @呼号 消息，或直接发送 UNICOM 消息。",
          "可輸入 @呼號 訊息，或直接傳送 UNICOM 訊息。",
          "Enter @callsign message to address a pilot, or send a UNICOM message."),
        NK_TEXT_LEFT, C_MUTED);
    nk_layout_row_begin(G.nk, NK_DYNAMIC, U(36), 2);
    nk_layout_row_push(G.nk, 0.78f);
    nk_edit_string_zero_terminated(G.nk, NK_EDIT_FIELD, G.chat_entry,
                                   sizeof(G.chat_entry), nk_filter_default);
    nk_layout_row_push(G.nk, 0.22f);
    if (secondary_button(T("发送", "傳送", "Send")) && G.chat_entry[0]) {
        if (app_send_chat(&G.app, G.chat_entry) == 0) {
            G.chat_entry[0] = '\0';
            G.notice[0] = '\0';
        }
    }
    nk_layout_row_end(G.nk);
}

static void draw_flight_page(void)
{
    if (!is_online()) {
        draw_heading(T("尚未连接", "尚未連線", "Not connected"));
        draw_help(T("先在“连接”页选择服务器并完成登录。",
                    "請先在「連線」頁選擇伺服器並完成登入。",
                    "Choose a server and sign in on the Connection page first."));
        nk_layout_row_dynamic(G.nk, U(40), 1);
        if (primary_button(T("返回连接", "返回連線", "Go to Connection")))
            G.page = PAGE_CONNECT;
        return;
    }

    char server[160];
    current_server_address(server, sizeof(server));
    nk_layout_row_begin(G.nk, NK_DYNAMIC, U(42), 2);
    nk_layout_row_push(G.nk, 0.70f);
    nk_gdi_set_font(G.font_title);
    nk_label(G.nk, T("飞行工作区", "飛行工作區", "Flight deck"), NK_TEXT_LEFT);
    nk_gdi_set_font(G.font_body);
    nk_layout_row_push(G.nk, 0.30f);
    if (secondary_button(T("断开连接", "中斷連線", "Disconnect")))
        SendMessageW(G.window, WM_COMMAND, IDC_CONNECT + 1, 0);
    nk_layout_row_end(G.nk);

    nk_layout_row_dynamic(G.nk, U(27), 1);
    char connected_line[256];
    _snprintf(connected_line, sizeof(connected_line) - 1,
              "%s · %s", T("已连接至", "已連線至", "Connected to"), server);
    connected_line[sizeof(connected_line) - 1] = '\0';
    nk_label_colored(G.nk, connected_line, NK_TEXT_LEFT, C_GREEN);

    if (G.warning[0] && net_now() < G.warning_until) {
        nk_layout_row_dynamic(G.nk, U(34), 1);
        nk_label_colored(G.nk, G.warning, NK_TEXT_LEFT, C_AMBER);
    }

    int columns_height = G.height - (int)U(280);
    if (columns_height < (int)U(320))
        columns_height = (int)U(320);
    if (G.width / G.scale >= 1050) {
        nk_layout_row_begin(G.nk, NK_DYNAMIC, (float)columns_height, 2);
        nk_layout_row_push(G.nk, 0.43f);
        if (nk_group_begin(G.nk, "flight-controls", NK_WINDOW_BORDER)) {
            draw_telemetry();
            draw_transponder();
            nk_group_end(G.nk);
        }
        nk_layout_row_push(G.nk, 0.57f);
        if (nk_group_begin(G.nk, "flight-operations", NK_WINDOW_BORDER)) {
            draw_flightplan();
            draw_messages();
            nk_group_end(G.nk);
        }
        nk_layout_row_end(G.nk);
    } else {
        nk_layout_row_dynamic(G.nk, (float)columns_height, 1);
        if (nk_group_begin(G.nk, "flight-operations", NK_WINDOW_BORDER)) {
            draw_telemetry();
            draw_transponder();
            draw_flightplan();
            draw_messages();
            nk_group_end(G.nk);
        }
    }
}

static void draw_nav_button(ui_page_t page, const char *label)
{
    bool active = G.page == page;
    struct nk_style_button saved = G.nk->style.button;
    if (active) {
        G.nk->style.button.normal = nk_style_item_color(C_RAISED);
        G.nk->style.button.hover = nk_style_item_color(C_RAISED);
        G.nk->style.button.active = nk_style_item_color(C_RAISED);
        G.nk->style.button.text_normal = C_CYAN;
        G.nk->style.button.text_hover = C_CYAN;
        G.nk->style.button.text_active = C_CYAN;
        G.nk->style.button.border_color = C_CYAN;
    }
    if (nk_button_label(G.nk, label)) {
        G.page = page;
        G.notice[0] = '\0';
        if (page == PAGE_FLIGHT && !is_online())
            G.page = PAGE_CONNECT;
    }
    G.nk->style.button = saved;
}

static void draw_frame(void)
{
    if (G.theme_dirty)
        apply_theme();
    if (G.password_edit && G.page != PAGE_CONNECT)
        ShowWindow(G.password_edit, SW_HIDE);

    struct nk_rect bounds = nk_rect(0, 0,
                                    (float)(G.width > 0 ? G.width : 800),
                                    (float)(G.height > 0 ? G.height : 700));
    if (nk_begin(G.nk, "", bounds,
                 NK_WINDOW_BORDER | NK_WINDOW_NO_SCROLLBAR)) {
        nk_layout_row_begin(G.nk, NK_DYNAMIC, U(48), 4);
        nk_layout_row_push(G.nk, 0.43f);
        nk_gdi_set_font(G.font_title);
        nk_label(G.nk, "Aerofly Link", NK_TEXT_LEFT);
        nk_gdi_set_font(G.font_body);
        nk_layout_row_push(G.nk, 0.19f);
        draw_nav_button(PAGE_CONNECT, T("连接", "連線", "Connection"));
        nk_layout_row_push(G.nk, 0.19f);
        draw_nav_button(PAGE_FLIGHT, T("飞行工作区", "飛行工作區", "Flight deck"));
        nk_layout_row_push(G.nk, 0.19f);
        draw_nav_button(PAGE_SETTINGS, T("设置", "設定", "Settings"));
        nk_layout_row_end(G.nk);

        int body_height = G.height - (int)U(165);
        if (body_height < (int)U(360))
            body_height = (int)U(360);
        nk_layout_row_dynamic(G.nk, (float)body_height, 1);
        if (nk_group_begin(G.nk, "page-content", NK_WINDOW_BORDER)) {
            switch (G.page) {
            case PAGE_CONNECT:  draw_connection_page(); break;
            case PAGE_FLIGHT:   draw_flight_page(); break;
            case PAGE_SETTINGS: draw_settings_page(); break;
            }
            nk_group_end(G.nk);
        }

        nk_layout_row_dynamic(G.nk, U(28), 1);
        draw_status_footer();
    }
    nk_end(G.nk);
    nk_gdi_render(C_BG);
}

static void clear_password(void)
{
    if (G.password_edit)
        SetWindowTextW(G.password_edit, L"");
    SecureZeroMemory(G.password, sizeof(G.password));
    app_set_password(&G.app, "");
}

static void connect_to_server(void)
{
    cfg_t *c = &G.app.cfg;
    if (G.app.sess.state != SESS_DISCONNECTED) {
        set_notice("当前已有连接。请先断开，再更改服务器。",
                   "目前已有連線。請先中斷，再更改伺服器。",
                   "A session is already active. Disconnect before changing servers.");
        return;
    }

    if (!c->callsign[0] || !c->cid[0]) {
        set_notice("请填写呼号和网络账号 ID。", "請填寫呼號和網絡帳號 ID。",
                   "Enter your callsign and network account ID.");
        return;
    }

    char host[128];
    int port;
    if (!parse_server_address(G.server_entry, host, sizeof(host), &port)) {
        set_notice("服务器地址无效。请使用 host:port，例如 flight.skeet.top:6809。",
                   "伺服器地址無效。請使用 host:port，例如 flight.skeet.top:6809。",
                   "Invalid server address. Use host:port, such as flight.skeet.top:6809.");
        return;
    }
    if (is_vatsim_host(host)) {
        set_notice("Aerofly Link 未获 VATSIM 批准，不能用本程序连接 VATSIM。请使用获批客户端。",
                   "Aerofly Link 未獲 VATSIM 認可，不能用本程式連線 VATSIM。請使用認可用戶端。",
                   "Aerofly Link is not VATSIM-approved. Connect with an approved client instead.");
        return;
    }
    if (strcmp(c->type, "vatsim") == 0
        && strncmp(c->jwt_url, "https://", 8) != 0) {
        set_notice("FSD-JWT 令牌地址必须使用 HTTPS。", "FSD-JWT 權杖地址必須使用 HTTPS。",
                   "The FSD-JWT endpoint must use HTTPS.");
        return;
    }

    int password_chars = 0;
#ifdef AEROFLYLINK_E2E_GUI
    if (G.e2e_password_loaded)
        password_chars = (int)strlen(G.password);
    else
#endif
    {
        wchar_t wide_password[64];
        password_chars = (int)SendMessageW(
            G.password_edit, WM_GETTEXT,
            (WPARAM)(sizeof(wide_password) / sizeof(wide_password[0])),
            (LPARAM)wide_password);
        int password_bytes = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS,
                                                  wide_password, -1,
                                                  G.password, sizeof(G.password),
                                                  NULL, NULL);
        SecureZeroMemory(wide_password, sizeof(wide_password));
        if (password_bytes <= 1)
            password_chars = 0;
    }
    if (password_chars <= 0) {
        set_notice("请输入密码。密码不会保存到磁盘。",
                   "請輸入密碼。密碼不會儲存至磁碟。",
                   "Enter your password. It will not be saved to disk.");
        return;
    }

    strncpy(c->server, host, sizeof(c->server) - 1);
    c->server[sizeof(c->server) - 1] = '\0';
    c->port = port;
    for (char *p = c->callsign; *p; ++p)
        *p = (char)toupper((unsigned char)*p);

    strncpy(G.app.sess.callsign, c->callsign, sizeof(G.app.sess.callsign) - 1);
    G.app.sess.callsign[sizeof(G.app.sess.callsign) - 1] = '\0';
    strncpy(G.app.sess.cid, c->cid, sizeof(G.app.sess.cid) - 1);
    G.app.sess.cid[sizeof(G.app.sess.cid) - 1] = '\0';
    strncpy(G.app.sess.realname, c->realname, sizeof(G.app.sess.realname) - 1);
    G.app.sess.realname[sizeof(G.app.sess.realname) - 1] = '\0';
    strncpy(G.app.sess.server, c->server, sizeof(G.app.sess.server) - 1);
    G.app.sess.server[sizeof(G.app.sess.server) - 1] = '\0';
    G.app.sess.port = c->port;
    G.app.sess.rating = c->rating;
    G.app.sess.vatsim = strcmp(c->type, "vatsim") == 0;
    strncpy(G.app.sess.jwt_url, c->jwt_url, sizeof(G.app.sess.jwt_url) - 1);
    G.app.sess.jwt_url[sizeof(G.app.sess.jwt_url) - 1] = '\0';
    strncpy(G.app.sess.jwt_proxy, c->jwt_proxy, sizeof(G.app.sess.jwt_proxy) - 1);
    G.app.sess.jwt_proxy[sizeof(G.app.sess.jwt_proxy) - 1] = '\0';
    strncpy(G.app.sess.jwt_proxy_bypass, c->jwt_proxy_bypass,
            sizeof(G.app.sess.jwt_proxy_bypass) - 1);
    G.app.sess.jwt_proxy_bypass[sizeof(G.app.sess.jwt_proxy_bypass) - 1] = '\0';
    app_set_password(&G.app, G.password);
    G.notice[0] = '\0';
    mark_config_dirty();
    save_config();
    app_connect_fsd(&G.app);
}

static void disconnect_from_server(void)
{
    app_disconnect_fsd(&G.app);
    clear_password();
    G.page = PAGE_CONNECT;
    G.notice[0] = '\0';
}

#ifdef AEROFLYLINK_E2E_GUI
static void gui_e2e_prepopulate(void)
{
    char synthetic_password[sizeof(G.password)];
    wchar_t wide_password[sizeof(G.password)];
    wchar_t readback[sizeof(G.password)];
    memset(synthetic_password, 0, sizeof(synthetic_password));
    memset(wide_password, 0, sizeof(wide_password));
    memset(readback, 0, sizeof(readback));
    DWORD length = GetEnvironmentVariableA("AEROFLYLINK_SMOKE_PASSWORD",
                                           synthetic_password,
                                           sizeof(synthetic_password));
    if (length > 0 && length < sizeof(synthetic_password)) {
        int wide_count = MultiByteToWideChar(
            CP_UTF8, MB_ERR_INVALID_CHARS, synthetic_password, -1,
            wide_password, (int)(sizeof(wide_password) / sizeof(wide_password[0])));
        if (wide_count > 0 && SetWindowTextW(G.password_edit, wide_password)) {
            int chars = (int)SendMessageW(
                G.password_edit, WM_GETTEXT,
                (WPARAM)(sizeof(readback) / sizeof(readback[0])),
                (LPARAM)readback);
            int bytes = chars > 0
                ? WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS,
                                      readback, -1, G.password,
                                      sizeof(G.password), NULL, NULL)
                : 0;
            G.e2e_password_loaded = bytes > 1;
        }
    }
    SecureZeroMemory(synthetic_password, sizeof(synthetic_password));
    SecureZeroMemory(wide_password, sizeof(wide_password));
    SecureZeroMemory(readback, sizeof(readback));
}
#endif

static LRESULT CALLBACK password_proc(HWND hwnd, UINT msg,
                                      WPARAM wp, LPARAM lp)
{
    if (msg == WM_KEYDOWN && wp == VK_RETURN) {
        PostMessageW(G.window, WM_COMMAND, IDC_CONNECT, 0);
        return 0;
    }
    if (msg == WM_KEYDOWN && wp == VK_TAB) {
        SetFocus(G.window);
        PostMessageW(G.window, WM_KEYDOWN, VK_TAB, lp);
        return 0;
    }
    return CallWindowProcW(G.old_password_proc, hwnd, msg, wp, lp);
}

static LRESULT CALLBACK window_proc(HWND window, UINT message,
                                    WPARAM wparam, LPARAM lparam)
{
#ifdef AEROFLYLINK_E2E_GUI
    if (message == E2E_QUERY_STATE)
        return (LRESULT)G.app.sess.state;
    if (message == E2E_SET_PAGE) {
        ui_page_t requested = (ui_page_t)wparam;
        if (requested >= PAGE_CONNECT && requested <= PAGE_SETTINGS
            && (requested != PAGE_FLIGHT || is_online()))
            G.page = requested;
        return 1;
    }
    if (message == E2E_SET_LANGUAGE) {
        static const char *const languages[] = { "zh-CN", "zh-HK", "en-US" };
        if (wparam < sizeof(languages) / sizeof(languages[0])) {
            strncpy(G.app.cfg.language, languages[wparam],
                    sizeof(G.app.cfg.language) - 1);
            G.app.cfg.language[sizeof(G.app.cfg.language) - 1] = '\0';
            update_window_title();
            mark_config_dirty();
        }
        return 1;
    }
    if (message == E2E_SET_THEME) {
        if (wparam <= 1) {
            strcpy(G.app.cfg.theme, wparam ? "light" : "dark");
            G.theme_dirty = true;
            mark_config_dirty();
        }
        return 1;
    }
#endif
    if (message == WM_DPICHANGED) {
        UINT dpi = HIWORD(wparam);
        G.dpi = dpi ? dpi : 96;
        G.scale = (float)G.dpi / 96.0f;
        const RECT *suggested = (const RECT *)lparam;
        SetWindowPos(window, NULL, suggested->left, suggested->top,
                     suggested->right - suggested->left,
                     suggested->bottom - suggested->top,
                     SWP_NOZORDER | SWP_NOACTIVATE);
        if (G.nk)
            recreate_dpi_fonts();
        return 0;
    }
    if (message == WM_SIZE) {
        G.width = LOWORD(lparam);
        G.height = HIWORD(lparam);
    }
    if (G.nk && nk_gdi_handle_event(window, message, wparam, lparam))
        return 0;

    switch (message) {
    case WM_CREATE:
        G.window = window;
        return 0;
    case WM_GETMINMAXINFO: {
        MINMAXINFO *info = (MINMAXINFO *)lparam;
        info->ptMinTrackSize.x = (LONG)U(700);
        info->ptMinTrackSize.y = (LONG)U(620);
        return 0;
    }
    case WM_TIMER:
        if (wparam == APP_TIMER_ID)
            return 0;
        break;
    case WM_COMMAND:
        switch (LOWORD(wparam)) {
        case IDC_CONNECT:     connect_to_server(); return 0;
        case IDC_CONNECT + 1: disconnect_from_server(); return 0;
        default: break;
        }
        break;
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLOREDIT: {
        HDC dc = (HDC)wparam;
        SetBkColor(dc, C_SURFACE.r | (C_SURFACE.g << 8) | (C_SURFACE.b << 16));
        SetTextColor(dc, C_TEXT.r | (C_TEXT.g << 8) | (C_TEXT.b << 16));
        SetBkMode(dc, OPAQUE);
        return (LRESULT)G.password_brush;
    }
    case WM_CLOSE:
        DestroyWindow(window);
        return 0;
    case WM_DESTROY:
        KillTimer(window, APP_TIMER_ID);
        PostQuitMessage(0);
        return 0;
    default:
        break;
    }
    return DefWindowProcW(window, message, wparam, lparam);
}

int WINAPI WinMain(HINSTANCE instance, HINSTANCE previous,
                   LPSTR command_line, int show)
{
    (void)previous;
    (void)command_line;
    memset(&G, 0, sizeof(G));
    enable_dpi_awareness();
    G.dpi = system_dpi();
    G.scale = (float)G.dpi / 96.0f;
    if (G.scale <= 0.0f)
        G.scale = 1.0f;
    G.width = (int)U(1120);
    G.height = (int)U(900);
    G.page = PAGE_CONNECT;
    G.selected_server = 0;

    if (net_init() != 0) {
        MessageBoxW(NULL, L"Network initialization failed.", L"Aerofly Link",
                    MB_ICONERROR | MB_OK);
        return 1;
    }

    cfg_t cfg;
    cfg_defaults(&cfg);
    char config_path[MAX_PATH];
    cfg_default_path(config_path, sizeof(config_path));
    cfg_load(&cfg, config_path);
    if (strcmp(cfg.language, "zh-CN") != 0
        && strcmp(cfg.language, "zh-HK") != 0
        && strcmp(cfg.language, "en-US") != 0)
        strcpy(cfg.language, "zh-CN");
    if (strcmp(cfg.theme, "light") != 0 && strcmp(cfg.theme, "dark") != 0)
        strcpy(cfg.theme, "dark");

    app_init(&G.app, &cfg);
    G.app.on_log = cb_log;
    G.app.on_debug = cb_debug;
    G.app.on_status = cb_status;
    G.app.on_warning = cb_warning;
    G.app.on_xpdr = cb_xpdr;
    current_server_address(G.server_entry, sizeof(G.server_entry));
    strncpy(G.xpdr_entry, G.app.xpdr.squawk, sizeof(G.xpdr_entry) - 1);
    G.xpdr_entry[sizeof(G.xpdr_entry) - 1] = '\0';

    G.font_body = nk_gdifont_create("Segoe UI", (int)U(15));
    G.font_title = nk_gdifont_create("Segoe UI", (int)U(21));
    G.font_small = nk_gdifont_create("Segoe UI", (int)U(12));
    G.font_code = nk_gdifont_create("Consolas", (int)U(27));
    G.password_font = CreateFontW(-(int)U(16), 0, 0, 0, FW_NORMAL, FALSE, FALSE,
                                  FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                                  CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                                  DEFAULT_PITCH, L"Segoe UI");
    G.password_brush = CreateSolidBrush(RGB(C_SURFACE.r, C_SURFACE.g, C_SURFACE.b));
    if (!G.font_body || !G.font_title || !G.font_small || !G.font_code
        || !G.password_font || !G.password_brush) {
        MessageBoxW(NULL, L"The interface could not initialize its fonts.",
                    L"Aerofly Link", MB_ICONERROR | MB_OK);
        goto cleanup_fonts;
    }

    WNDCLASSW wc;
    memset(&wc, 0, sizeof(wc));
    wc.lpfnWndProc = window_proc;
    wc.hInstance = instance;
    wc.hCursor = LoadCursorW(NULL, MAKEINTRESOURCEW(32512));
    wc.hIcon = LoadIconW(NULL, MAKEINTRESOURCEW(32512));
    wc.hbrBackground = NULL;
    wc.lpszClassName = L"AeroflyLinkMain";
    if (!RegisterClassW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        MessageBoxW(NULL, L"The application window could not be created.",
                    L"Aerofly Link", MB_ICONERROR | MB_OK);
        goto cleanup_fonts;
    }

    RECT frame = { 0, 0, G.width, G.height };
    DWORD style = WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN;
    adjust_window_rect_for_dpi(&frame, style, G.dpi);
    G.window = CreateWindowExW(0, wc.lpszClassName,
        localized_window_title(),
        style, CW_USEDEFAULT, CW_USEDEFAULT,
        frame.right - frame.left, frame.bottom - frame.top,
        NULL, NULL, instance, NULL);
    if (!G.window) {
        MessageBoxW(NULL, L"The application window could not be created.",
                    L"Aerofly Link", MB_ICONERROR | MB_OK);
        goto cleanup_window_class;
    }

    G.window_dc = GetDC(G.window);
    G.nk = nk_gdi_init(G.font_body, G.window_dc,
                       (unsigned int)G.width, (unsigned int)G.height);
    if (!G.nk) {
        MessageBoxW(G.window, L"The interface renderer could not start.",
                    L"Aerofly Link", MB_ICONERROR | MB_OK);
        goto cleanup_window;
    }
    apply_theme();

    G.password_edit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
        WS_CHILD | ES_PASSWORD | ES_AUTOHSCROLL | WS_TABSTOP,
        0, 0, 1, 1, G.window, (HMENU)(INT_PTR)IDC_PASSWORD,
        instance, NULL);
    if (G.password_edit) {
        SendMessageW(G.password_edit, WM_SETFONT, (WPARAM)G.password_font, TRUE);
        SendMessageW(G.password_edit, EM_SETPASSWORDCHAR, L'*', 0);
        G.old_password_proc = (WNDPROC)GetWindowLongPtrW(G.password_edit,
                                                         GWLP_WNDPROC);
        SetWindowLongPtrW(G.password_edit, GWLP_WNDPROC,
                          (LONG_PTR)password_proc);
    }

#ifdef AEROFLYLINK_E2E_GUI
    gui_e2e_prepopulate();
#endif

    SetTimer(G.window, APP_TIMER_ID, APP_TIMER_MS, NULL);
    ShowWindow(G.window, show);
    UpdateWindow(G.window);
    MSG msg;
    int message_result;
    while ((message_result = GetMessageW(&msg, NULL, 0, 0)) > 0) {
        nk_input_begin(G.nk);
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
        while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) {
                message_result = 0;
                break;
            }
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        nk_input_end(G.nk);

        app_poll(&G.app, 0);
        sess_state_t current_state = G.app.sess.state;
        if (current_state == SESS_ONLINE
            && G.previous_state != SESS_ONLINE) {
            G.page = PAGE_FLIGHT;
            G.notice[0] = '\0';
            clear_password(); /* Credentials are no longer needed after login. */
        } else if (current_state == SESS_DISCONNECTED
                   && G.previous_state != SESS_DISCONNECTED) {
            clear_password();
            if (G.page == PAGE_FLIGHT)
                G.page = PAGE_CONNECT;
        }
        G.previous_state = current_state;

        if (G.config_dirty && net_now() - G.config_dirty_at >= 1.0)
            save_config();
        draw_frame();
        if (message_result == 0)
            break;
    }

    app_disconnect_fsd(&G.app);
    app_toggle_mock(&G.app, false);
    clear_password();
    if (G.config_dirty)
        save_config();

cleanup_window:
    if (G.password_edit)
        DestroyWindow(G.password_edit);
    if (G.nk)
        nk_gdi_shutdown();
    if (G.window_dc && G.window)
        ReleaseDC(G.window, G.window_dc);
    if (G.window)
        DestroyWindow(G.window);
cleanup_window_class:
    UnregisterClassW(wc.lpszClassName, instance);
cleanup_fonts:
    if (G.password_brush) DeleteObject(G.password_brush);
    if (G.password_font) DeleteObject(G.password_font);
    if (G.font_code) nk_gdifont_del(G.font_code);
    if (G.font_small) nk_gdifont_del(G.font_small);
    if (G.font_title) nk_gdifont_del(G.font_title);
    if (G.font_body) nk_gdifont_del(G.font_body);
    net_cleanup();
    return 0;
}
