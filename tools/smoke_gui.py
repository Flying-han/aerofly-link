# -*- coding: utf-8 -*-
"""Local GUI + FSD E2E driver. Uses a per-run APPDATA profile, never the user's."""
import ctypes
import ctypes.wintypes as wt
import json
import os
import subprocess
import sys
import tempfile
import time
import urllib.error
import urllib.request

import PIL.Image as Image

try:
    ctypes.windll.shcore.SetProcessDpiAwareness(2)
except Exception:
    ctypes.windll.user32.SetProcessDPIAware()

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
EXE = os.environ.get(
    "AEROFLYLINK_GUI_EXE", os.path.join(ROOT, r"client-c\build\aeroflylink-e2e.exe"))
OUT_DIR = os.environ.get(
    "AEROFLYLINK_SMOKE_OUTPUT_DIR",
    os.path.join(ROOT, r"client-c\build\e2e-smoke"))
CALLSIGN = os.environ.get("AEROFLYLINK_SMOKE_CALLSIGN", "TST123")
CID = os.environ.get("AEROFLYLINK_SMOKE_CID", "1111111")
PASSWORD = os.environ.get(
    "AEROFLYLINK_SMOKE_PASSWORD", "Local-E2E-Only-Not-A-Real-Password")
REALNAME = os.environ.get("AEROFLYLINK_SMOKE_REALNAME", "Aerofly Link E2E")
FSD_HOST = os.environ.get("AEROFLYLINK_SMOKE_FSD_HOST", "127.0.0.1")
FSD_PORT = int(os.environ.get("AEROFLYLINK_SMOKE_FSD_PORT", "16809"))
FSD_TYPE = os.environ.get("AEROFLYLINK_SMOKE_FSD_TYPE", "legacy")
FSD_HTTP_URL = os.environ.get("AEROFLYLINK_SMOKE_FSD_HTTP_URL", "")
JWT_URL = os.environ.get(
    "AEROFLYLINK_SMOKE_JWT_URL", "https://api.skeet.top/api/fsd-jwt")
EXTERNAL_FSD = os.environ.get("AEROFLYLINK_SMOKE_EXTERNAL_FSD", "0") == "1"

os.makedirs(OUT_DIR, exist_ok=True)
PROFILE_ROOT = tempfile.mkdtemp(prefix="profile_", dir=OUT_DIR)
CFG_PATH = os.path.join(PROFILE_ROOT, "AeroflyLink", "settings.json")
os.makedirs(os.path.dirname(CFG_PATH), exist_ok=True)

smoke_settings = {
    "callsign": CALLSIGN,
    "cid": CID,
    "realname": REALNAME,
    "server": FSD_HOST,
    "port": FSD_PORT,
    "rating": 2,
    "eco": "private",
    "auth_mode": "legacy" if FSD_TYPE == "legacy" else "fsd-jwt",
    "language": "zh-CN",
    "theme": "dark",
    "jwt_url": JWT_URL,
    "jwt_proxy": "",
    "jwt_proxy_bypass": "localhost;127.0.0.1;::1",
    "mock_lat": "31.1434",
    "mock_lon": "121.8082",
    "mock_alt": "3500",
    "aircraft": "B738",
    "flight_rules": "IFR",
    "wake_category": "Medium",
    "tas": "450",
    "dep_airport": "ZSPD",
    "dest_airport": "ZBAA",
    "cruise_alt": "F350",
    "route": "CDY G212",
    "remarks": "",
    "servers": ["flight.skeet.top:6809", f"{FSD_HOST}:{FSD_PORT}"],
}
with open(CFG_PATH, "w", encoding="utf-8") as config_file:
    json.dump(smoke_settings, config_file, ensure_ascii=False)
print(f"[smoke] profile={PROFILE_ROOT} server={FSD_HOST}:{FSD_PORT} "
      f"type={FSD_TYPE} external_fsd={EXTERNAL_FSD}")

fsd = None
if not EXTERNAL_FSD:
    fsd = subprocess.Popen(
        [sys.executable, os.path.join(ROOT, "tools", "mock_fsd_server.py"),
         "--port", str(FSD_PORT), "--log", os.path.join(OUT_DIR, "fsd_mock.log")],
        stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL, creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))
    time.sleep(1.0)


def shot(hwnd, name):
    rect = wt.RECT()
    ctypes.windll.user32.GetWindowRect(hwnd, ctypes.byref(rect))
    width, height = rect.right - rect.left, rect.bottom - rect.top
    hdc = ctypes.windll.user32.GetWindowDC(hwnd)
    memory_dc = ctypes.windll.gdi32.CreateCompatibleDC(hdc)
    bitmap = ctypes.windll.gdi32.CreateCompatibleBitmap(hdc, width, height)
    old_bitmap = ctypes.windll.gdi32.SelectObject(memory_dc, bitmap)
    ctypes.windll.user32.PrintWindow(hwnd, memory_dc, 2)  # PW_RENDERFULLCONTENT

    class BMIH(ctypes.Structure):
        _fields_ = [("biSize", ctypes.c_uint32), ("biWidth", ctypes.c_int32),
                    ("biHeight", ctypes.c_int32), ("biPlanes", ctypes.c_uint16),
                    ("biBitCount", ctypes.c_uint16),
                    ("biCompression", ctypes.c_uint32),
                    ("biSizeImage", ctypes.c_uint32),
                    ("biXPelsPerMeter", ctypes.c_int32),
                    ("biYPelsPerMeter", ctypes.c_int32),
                    ("biClrUsed", ctypes.c_uint32),
                    ("biClrImportant", ctypes.c_uint32)]

    bmi = BMIH(ctypes.sizeof(BMIH), width, -height, 1, 32, 0, 0, 0, 0, 0, 0)
    buffer = ctypes.create_string_buffer(width * height * 4)
    ctypes.windll.gdi32.GetDIBits(memory_dc, bitmap, 0, height, buffer,
                                  ctypes.byref(bmi), 0)
    image = Image.frombytes("RGBA", (width, height), buffer.raw,
                            "raw", "BGRA").convert("RGB")
    output_path = os.path.join(OUT_DIR, name)
    image.save(output_path)
    print(f"[smoke] saved {output_path}")
    ctypes.windll.gdi32.SelectObject(memory_dc, old_bitmap)
    ctypes.windll.gdi32.DeleteObject(bitmap)
    ctypes.windll.gdi32.DeleteDC(memory_dc)
    ctypes.windll.user32.ReleaseDC(hwnd, hdc)


print(f"[smoke] exe={EXE}")
gui_env = os.environ.copy()
gui_env["APPDATA"] = PROFILE_ROOT
gui_env["AEROFLYLINK_SMOKE_PASSWORD"] = PASSWORD
proc = subprocess.Popen([EXE], env=gui_env)
code = 0
try:
    if fsd is not None and fsd.poll() is not None:
        raise RuntimeError(f"Mock FSD exited during startup ({fsd.returncode})")
    time.sleep(1.5)
    if proc.poll() is not None:
        raise RuntimeError(f"GUI exited during startup ({proc.returncode})")

    user32 = ctypes.windll.user32
    hwnd = user32.FindWindowW("AeroflyLinkMain", None)
    if not hwnd:
        raise RuntimeError("Aerofly Link window was not found")
    user32.GetDlgItem.argtypes = [wt.HWND, ctypes.c_int]
    user32.GetDlgItem.restype = wt.HWND
    user32.SetWindowTextW.argtypes = [wt.HWND, wt.LPCWSTR]
    user32.SetWindowTextW.restype = wt.BOOL
    user32.GetWindowTextLengthW.argtypes = [wt.HWND]
    user32.GetWindowTextLengthW.restype = ctypes.c_int
    user32.GetWindowTextW.argtypes = [wt.HWND, wt.LPWSTR, ctypes.c_int]
    user32.GetWindowTextW.restype = ctypes.c_int
    user32.SendMessageW.argtypes = [wt.HWND, wt.UINT, wt.WPARAM, wt.LPARAM]
    user32.SendMessageW.restype = ctypes.c_ssize_t
    user32.PostMessageW.argtypes = [wt.HWND, wt.UINT, wt.WPARAM, wt.LPARAM]
    user32.PostMessageW.restype = wt.BOOL
    user32.GetWindowRect.argtypes = [wt.HWND, ctypes.POINTER(wt.RECT)]
    user32.GetWindowRect.restype = wt.BOOL
    user32.SetWindowPos.argtypes = [wt.HWND, wt.HWND, ctypes.c_int, ctypes.c_int,
                                    ctypes.c_int, ctypes.c_int, wt.UINT]
    user32.SetWindowPos.restype = wt.BOOL

    def assert_window_title(expected):
        title = ctypes.create_unicode_buffer(256)
        user32.GetWindowTextW(hwnd, title, len(title))
        if title.value != expected:
            raise RuntimeError(
                f"Window title mismatch: expected {expected!r}, got {title.value!r}")

    assert_window_title("Aerofly Link — 飞行联机")

    shot(hwnd, "connection.png")
    if os.environ.get("AEROFLYLINK_SMOKE_SKIP_CONNECT", "0") == "1":
        user32.PostMessageW(hwnd, 0x0010, 0, 0)  # WM_CLOSE
        time.sleep(1.0)
        sys.exit(0 if proc.poll() is not None else 1)

    # Exercise the same connection command as the visible Nuklear primary button.
    user32.SendMessageW(hwnd, 0x0111, 1102, 0)  # WM_COMMAND / IDC_CONNECT

    connected = False
    session_state = 0
    if EXTERNAL_FSD and FSD_HTTP_URL:
        opener = urllib.request.build_opener(urllib.request.ProxyHandler({}))
        for _ in range(18):
            session_state = user32.SendMessageW(hwnd, 0x8031, 0, 0)
            try:
                with opener.open(FSD_HTTP_URL.rstrip("/") + "/api/clients",
                                 timeout=2) as response:
                    clients = json.load(response)
                if (session_state == 3 and any(
                        p.get("callsign") == CALLSIGN
                        for p in clients.get("pilots", []))):
                    connected = True
                    break
            except (OSError, urllib.error.URLError, json.JSONDecodeError):
                pass
            time.sleep(1)
    else:
        log_path = os.path.join(OUT_DIR, "fsd_mock.log")
        for _ in range(18):
            session_state = user32.SendMessageW(hwnd, 0x8031, 0, 0)
            if fsd is not None and fsd.poll() is not None:
                break
            try:
                with open(log_path, "r", encoding="utf-8") as log_file:
                    mock_log = log_file.read()
                connected = (session_state == 3
                             and f"<<< #AP{CALLSIGN}" in mock_log)
                if connected:
                    break
            except OSError:
                pass
            time.sleep(1)

    if not connected:
        shot(hwnd, "connection-failed.png")
        raise RuntimeError(f"FSD E2E did not reach ONLINE (GUI state={session_state})")

    if not EXTERNAL_FSD:
        time.sleep(3.5)  # allow initial flight-plan and position messages
    shot(hwnd, "flight-deck.png")

    original_rect = wt.RECT()
    user32.GetWindowRect(hwnd, ctypes.byref(original_rect))
    user32.SetWindowPos(hwnd, None, 0, 0, 760,
                        original_rect.bottom - original_rect.top,
                        0x0002 | 0x0004 | 0x0010)  # NOMOVE|NOZORDER|NOACTIVATE
    time.sleep(0.2)
    shot(hwnd, "flight-deck-compact.png")
    user32.SetWindowPos(hwnd, None, 0, 0,
                        original_rect.right - original_rect.left,
                        original_rect.bottom - original_rect.top,
                        0x0002 | 0x0004 | 0x0010)
    time.sleep(0.2)

    # The test-only page switch captures settings without depending on a
    # hard-coded screen coordinate; the release interface uses the visible tab.
    user32.SendMessageW(hwnd, 0x8032, 2, 0)  # WM_APP + 0x32 / Settings
    time.sleep(0.2)
    shot(hwnd, "settings.png")
    user32.SendMessageW(hwnd, 0x8032, 0, 0)  # Connection
    user32.SendMessageW(hwnd, 0x8033, 1, 0)  # Traditional Chinese (Hong Kong)
    assert_window_title("Aerofly Link — 飛行連線")
    time.sleep(0.2)
    shot(hwnd, "connection-zh-hk.png")
    user32.SendMessageW(hwnd, 0x8032, 1, 0)  # Flight deck
    time.sleep(0.2)
    shot(hwnd, "flight-deck-zh-hk.png")
    user32.SendMessageW(hwnd, 0x8032, 2, 0)  # Settings
    time.sleep(0.2)
    shot(hwnd, "settings-zh-hk.png")
    user32.SendMessageW(hwnd, 0x8032, 0, 0)  # Connection
    user32.SendMessageW(hwnd, 0x8033, 2, 0)  # American English
    assert_window_title("Aerofly Link — Flight Network")
    time.sleep(0.2)
    shot(hwnd, "connection-en-us.png")
    user32.SendMessageW(hwnd, 0x8032, 1, 0)  # Flight deck
    time.sleep(0.2)
    shot(hwnd, "flight-deck-en-us.png")
    user32.SendMessageW(hwnd, 0x8032, 2, 0)  # Settings
    time.sleep(0.2)
    shot(hwnd, "settings-en-us.png")
    user32.SendMessageW(hwnd, 0x8034, 1, 0)  # light appearance
    time.sleep(0.2)
    shot(hwnd, "settings-light.png")
    user32.SendMessageW(hwnd, 0x8032, 1, 0)  # Flight deck
    time.sleep(0.2)
    shot(hwnd, "flight-deck-light.png")
    user32.SendMessageW(hwnd, 0x8033, 0, 0)  # restore test profile
    user32.SendMessageW(hwnd, 0x8034, 0, 0)
    assert_window_title("Aerofly Link — 飞行联机")

    user32.PostMessageW(hwnd, 0x0010, 0, 0)  # WM_CLOSE
    time.sleep(1.5)
    if proc.poll() is None:
        proc.terminate()
        code = 1
        print("[smoke] FAIL: the window did not close gracefully")
    else:
        print(f"[smoke] exit code: {proc.returncode}")

    if EXTERNAL_FSD and FSD_HTTP_URL and proc.returncode == 0:
        opener = urllib.request.build_opener(urllib.request.ProxyHandler({}))
        removed = False
        for _ in range(12):
            try:
                with opener.open(FSD_HTTP_URL.rstrip("/") + "/api/clients",
                                 timeout=2) as response:
                    clients = json.load(response)
                if not any(p.get("callsign") == CALLSIGN
                           for p in clients.get("pilots", [])):
                    removed = True
                    break
            except (OSError, urllib.error.URLError, json.JSONDecodeError):
                pass
            time.sleep(1)
        if not removed:
            print("[smoke] FAIL: GUI pilot remained in external FSD after WM_CLOSE")
            code = 1

    if not EXTERNAL_FSD:
        with open(os.path.join(OUT_DIR, "fsd_mock.log"), "r", encoding="utf-8") as log_file:
            mock_log = log_file.read()
        required = (f"<<< #AP{CALLSIGN}", f"<<< $FP{CALLSIGN}",
                    "<<< @", f"<<< #DP{CALLSIGN}")
        missing = [packet for packet in required if packet not in mock_log]
        if missing:
            print(f"[smoke] FAIL: Mock FSD did not observe packets: {missing}")
            code = 1
finally:
    if proc.poll() is None:
        proc.kill()
    if fsd is not None:
        fsd.terminate()
        try:
            fsd.wait(timeout=5)
        except Exception:
            fsd.kill()

sys.exit(code)
