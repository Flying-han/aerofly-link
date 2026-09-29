# -*- coding: utf-8 -*-
"""
C GUI 冒烟测试（完整视觉验证）
==============================
流程：备份用户 settings.json → 写测试配置 → 启动 Mock FSD → 启动 GUI →
截连接页 → 自动点击「连接服务器」→ 等待工作区+遥测 → 截工作区 →
WM_CLOSE 优雅退出 → 恢复用户 settings.json。
"""
import ctypes
import ctypes.wintypes as wt
import json
import os
import subprocess
import sys
import time
import urllib.error
import urllib.request

import PIL.Image as _I

try:
    ctypes.windll.shcore.SetProcessDpiAwareness(2)
except Exception:
    ctypes.windll.user32.SetProcessDPIAware()

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
EXE = os.environ.get(
    "AEROFLYLINK_GUI_EXE", os.path.join(ROOT, r"client-c\build\aeroflylink.exe"))
OUT_DIR = os.environ.get(
    "AEROFLYLINK_SMOKE_OUTPUT_DIR",
    os.path.join(os.environ["TEMP"], "aerofly_link_c_smoke"))
CFG_PATH = os.path.join(os.environ["APPDATA"], "AeroflyLink", "settings.json")
CFG_BAK = CFG_PATH + f".smokebak.{os.getpid()}"
CALLSIGN = os.environ.get("AEROFLYLINK_SMOKE_CALLSIGN", "TST123")
CID = os.environ.get("AEROFLYLINK_SMOKE_CID", "1111111")
PASSWORD = os.environ.get("AEROFLYLINK_SMOKE_PASSWORD", "")
REALNAME = os.environ.get("AEROFLYLINK_SMOKE_REALNAME", "Aerofly Link E2E")
FSD_HOST = os.environ.get("AEROFLYLINK_SMOKE_FSD_HOST", "127.0.0.1")
FSD_PORT = int(os.environ.get("AEROFLYLINK_SMOKE_FSD_PORT", "16809"))
FSD_TYPE = os.environ.get("AEROFLYLINK_SMOKE_FSD_TYPE", "legacy")
FSD_HTTP_URL = os.environ.get("AEROFLYLINK_SMOKE_FSD_HTTP_URL", "")
JWT_URL = os.environ.get(
    "AEROFLYLINK_SMOKE_JWT_URL", "https://api.skeet.top/api/fsd-jwt")
EXTERNAL_FSD = os.environ.get("AEROFLYLINK_SMOKE_EXTERNAL_FSD", "0") == "1"

os.makedirs(OUT_DIR, exist_ok=True)
if os.path.exists(CFG_BAK):
    raise RuntimeError(f"refusing to overwrite existing settings backup: {CFG_BAK}")

# ── 备份/写入测试配置 ──
had_cfg = os.path.exists(CFG_PATH)
if had_cfg:
    os.replace(CFG_PATH, CFG_BAK)
os.makedirs(os.path.dirname(CFG_PATH), exist_ok=True)
smoke_settings = {
    "callsign": CALLSIGN, "cid": CID, "realname": REALNAME,
    "server": FSD_HOST, "port": FSD_PORT, "rating": 2,
    "eco": "private", "type": FSD_TYPE, "jwt_url": JWT_URL,
    "mock_lat": "31.1434", "mock_lon": "121.8082", "mock_alt": "3500",
    "aircraft": "B738", "wake_category": "Medium", "tas": "450",
    "dep_airport": "ZSPD", "dest_airport": "ZBAA", "cruise_alt": "F350",
    "route": "CDY G212", "remarks": "",
}
with open(CFG_PATH, "w", encoding="utf-8") as f:
    json.dump(smoke_settings, f)
print(f"[smoke] config callsign={CALLSIGN} cid_set={bool(CID)} "
      f"server={FSD_HOST}:{FSD_PORT} type={FSD_TYPE} appdata={os.environ['APPDATA']}")

# ── 启动开发 Mock；外部 FSD E2E 复用已运行的隔离服务 ──
fsd = None
if not EXTERNAL_FSD:
    fsd = subprocess.Popen(
        [sys.executable, os.path.join(ROOT, "tools", "mock_fsd_server.py"),
         "--port", str(FSD_PORT),
         "--log", os.path.join(OUT_DIR, "fsd_mock.log")],
        stdout=subprocess.DEVNULL)
    time.sleep(1.5)


def shot(hwnd, name):
    r = wt.RECT()
    ctypes.windll.user32.GetWindowRect(hwnd, ctypes.byref(r))
    w, h = r.right - r.left, r.bottom - r.top
    hdc = ctypes.windll.user32.GetWindowDC(hwnd)
    mem = ctypes.windll.gdi32.CreateCompatibleDC(hdc)
    bmp = ctypes.windll.gdi32.CreateCompatibleBitmap(hdc, w, h)
    ctypes.windll.gdi32.SelectObject(mem, bmp)
    ctypes.windll.user32.PrintWindow(hwnd, mem, 2)  # PW_RENDERFULLCONTENT

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
    bmi = BMIH(ctypes.sizeof(BMIH), w, -h, 1, 32, 0, 0, 0, 0, 0, 0)
    buf = ctypes.create_string_buffer(w * h * 4)
    ctypes.windll.gdi32.GetDIBits(mem, bmp, 0, h, buf, ctypes.byref(bmi), 0)
    img = _I.frombytes("RGBA", (w, h), buf.raw, "raw", "BGRA").convert("RGB")
    p = os.path.join(OUT_DIR, name)
    img.save(p)
    print(f"[smoke] saved {p}")


# ── 启动 GUI ──
print(f"[smoke] exe={EXE} external_fsd={EXTERNAL_FSD}")
proc = subprocess.Popen([EXE])
code = 0
try:
    if fsd is not None and fsd.poll() is not None:
        print(f"[smoke] FAIL: Mock FSD exited during startup ({fsd.returncode})")
        sys.exit(1)
    time.sleep(2.5)
    if proc.poll() is not None:
        print(f"[smoke] FAIL: GUI exited during startup ({proc.returncode})")
        sys.exit(1)
    hwnd = ctypes.windll.user32.FindWindowW("AeroflyLinkMain", None)
    if not hwnd:
        print("[smoke] FAIL: window not found")
        sys.exit(1)
    user32 = ctypes.windll.user32
    user32.GetDlgItem.argtypes = [wt.HWND, ctypes.c_int]
    user32.GetDlgItem.restype = wt.HWND
    user32.SetWindowTextW.argtypes = [wt.HWND, wt.LPCWSTR]
    user32.SetWindowTextW.restype = wt.BOOL
    user32.GetWindowTextW.argtypes = [wt.HWND, wt.LPWSTR, ctypes.c_int]
    user32.GetWindowTextW.restype = ctypes.c_int
    user32.SendMessageW.argtypes = [wt.HWND, wt.UINT, wt.WPARAM, wt.LPARAM]
    user32.SendMessageW.restype = ctypes.c_ssize_t
    shot(hwnd, "conn_page.png")
    user32.SetForegroundWindow(hwnd)

    # External-FSD runs use a test-only GUI build that preloads controls inside
    # the client process; cross-process GetWindowText is not a reliable check.
    if EXTERNAL_FSD:
        fixture_status = ctypes.create_unicode_buffer(128)
        user32.GetWindowTextW(user32.GetDlgItem(hwnd, 109), fixture_status, 128)
        print(f"[smoke] fixture_status={fixture_status.value!r}")
        if fixture_status.value != "E2E cfg6/7 ui6/7 id100/101 pwd1 type0 eco1":
            print("[smoke] FAIL: GUI config prefill or VATSIM selection mismatch")
            sys.exit(1)
    shot(hwnd, "conn_page_filled.png")

    if os.environ.get("AEROFLYLINK_SMOKE_SKIP_CONNECT", "0") == "1":
        ctypes.windll.user32.PostMessageW(hwnd, 0x0010, 0, 0)  # WM_CLOSE
        time.sleep(1)
        if proc.poll() is None:
            proc.terminate()
            code = 1
        sys.exit(code)

    # 发送标准 BN_CLICKED 命令；异步投递，避免配置错误对话框卡住 runner。
    ctypes.windll.user32.PostMessageW(hwnd, 0x0111, 108, 0)
    if EXTERNAL_FSD and FSD_HTTP_URL:
        opener = urllib.request.build_opener(urllib.request.ProxyHandler({}))
        connected = False
        for _ in range(12):
            try:
                with opener.open(FSD_HTTP_URL.rstrip("/") + "/api/clients", timeout=2) as response:
                    clients = json.load(response)
                if any(p.get("callsign") == CALLSIGN for p in clients.get("pilots", [])):
                    connected = True
                    break
            except (OSError, urllib.error.URLError):
                pass
            time.sleep(1)
        if not connected:
            status_buffer = ctypes.create_unicode_buffer(256)
            user32.GetWindowTextW(user32.GetDlgItem(hwnd, 109), status_buffer, 256)
            shot(hwnd, "workspace-failed.png")
            print(f"[smoke] FAIL: external FSD did not register the GUI pilot; "
                  f"connect_status={status_buffer.value!r}")
            sys.exit(1)
    else:
        time.sleep(5)   # 登录 + Mock 遥测接入 + 若干 1Hz 上报
    shot(hwnd, "workspace.png")

    # 关闭
    ctypes.windll.user32.PostMessageW(hwnd, 0x0010, 0, 0)  # WM_CLOSE
    time.sleep(1.5)
    if proc.poll() is None:
        proc.terminate()
        print("[smoke] WARN: no graceful exit")
        code = 1
    else:
        print(f"[smoke] exit code: {proc.returncode}")
    if not EXTERNAL_FSD:
        with open(os.path.join(OUT_DIR, "fsd_mock.log"), "r", encoding="utf-8") as log_file:
            mock_log = log_file.read()
        required_packets = (f"<<< #AP{CALLSIGN}", f"<<< $FP{CALLSIGN}",
                           f"<<< @", f"<<< #DP{CALLSIGN}")
        missing = [packet for packet in required_packets if packet not in mock_log]
        if missing:
            print(f"[smoke] FAIL: Mock FSD did not observe required packets: {missing}")
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
    # 恢复用户配置
    if had_cfg:
        os.replace(CFG_BAK, CFG_PATH)
    else:
        if os.path.exists(CFG_PATH):
            os.remove(CFG_PATH)

sys.exit(code)
