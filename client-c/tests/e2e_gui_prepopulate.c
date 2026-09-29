/* Test-only GUI control fixture; never compiled into the release executable. */
#include "gui_internal.h"

#include <commctrl.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>
#include <windowsx.h>

static bool set_test_edit(const char *env_name, HWND edit)
{
    char value[256];
    DWORD length = GetEnvironmentVariableA(env_name, value, sizeof(value));
    if (length == 0 || length >= sizeof(value))
        return false;
    wchar_t wide[256];
    if (MultiByteToWideChar(CP_UTF8, 0, value, -1, wide,
                            (int)(sizeof(wide) / sizeof(wide[0]))) <= 0)
        return false;
    if (!edit || SetWindowTextW(edit, wide) == FALSE)
        return false;
    wchar_t readback[256];
    int copied = GetWindowTextW(edit, readback,
                                (int)(sizeof(readback) / sizeof(readback[0])));
    return copied > 0 && wcscmp(readback, wide) == 0;
}

void gui_e2e_prepopulate(void)
{
    size_t config_callsign_len = strlen(G.app.cfg.callsign);
    size_t config_cid_len = strlen(G.app.cfg.cid);
    wchar_t callsign[64], cid[64];
    GetWindowTextW(G.ed_callsign, callsign, 64);
    GetWindowTextW(G.ed_cid, cid, 64);
    size_t ui_callsign_len = wcslen(callsign);
    size_t ui_cid_len = wcslen(cid);
    bool password = set_test_edit("AEROFLYLINK_SMOKE_PASSWORD", G.ed_password);

    char server[128];
    DWORD length = GetEnvironmentVariableA("AEROFLYLINK_SMOKE_SERVER",
                                           server, sizeof(server));
    if (length > 0 && length < sizeof(server)) {
        wchar_t wide[128];
        if (MultiByteToWideChar(CP_UTF8, 0, server, -1, wide, 128) > 0)
            SetWindowTextW(G.cb_server, wide);
    }

    wchar_t status[96];
    _snwprintf(status, 95,
               L"E2E cfg%u/%u ui%u/%u id%u/%u pwd%d type%d eco%d",
               (unsigned)config_callsign_len, (unsigned)config_cid_len,
               (unsigned)ui_callsign_len, (unsigned)ui_cid_len,
               (unsigned)GetDlgCtrlID(G.ed_callsign),
               (unsigned)GetDlgCtrlID(G.ed_cid),
               password, ComboBox_GetCurSel(G.cb_type),
               ComboBox_GetCurSel(G.cb_eco));
    status[95] = L'\0';
    SetWindowTextW(G.lbl_status, status);
}
