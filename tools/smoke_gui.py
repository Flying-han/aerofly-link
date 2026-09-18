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

import PIL.Image as _I

try:
    ctypes.windll.shcore.SetProcessDpiAwareness(2)
except Exception:
    ctypes.windll.user32.SetProcessDPIAware()

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
EXE = os.path.join(ROOT, r"client-c\build\aeroflylink.exe")
OUT_DIR = os.path.join(os.environ["TEMP"], "aerofly_link_c_smoke")
CFG_PATH = os.path.join(os.environ["APPDATA"], "AeroflyLink", "settings.json")
CFG_BAK = CFG_PATH + ".smokebak"

os.makedirs(OUT_DIR, exist_ok=True)

# ── 备份/写入测试配置 ──
had_cfg = os.path.exists(CFG_PATH)
if had_cfg:
    os.replace(CFG_PATH, CFG_BAK)
os.makedirs(os.path.dirname(CFG_PATH), exist_ok=True)
with open(CFG_PATH, "w", encoding="utf-8") as f:
    json.dump({
        "callsign": "TST123", "cid": "1111111", "realname": "Smoke Tester",
        "server": "127.0.0.1", "port": 16809, "rating": 2,
        "eco": "private", "type": "legacy",
        "mock_lat": "31.1434", "mock_lon": "121.8082", "mock_alt": "3500",
        "aircraft": "B738", "wake_category": "Medium", "tas": "450",
        "dep_airport": "ZSPD", "dest_airport": "ZBAA",
        "cruise_alt": "F350", "route": "CDY G212", "remarks": "",
    }, f)

# ── 启动 Mock FSD ──
fsd = subprocess.Popen(
    [sys.executable, os.path.join(ROOT, "tools", "mock_fsd_server.py"),
     "--port", "16809",
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
proc = subprocess.Popen([EXE])
code = 0
try:
    time.sleep(2.5)
    hwnd = ctypes.windll.user32.FindWindowW("AeroflyLinkMain", None)
    if not hwnd:
        print("[smoke] FAIL: window not found")
        sys.exit(1)
    shot(hwnd, "conn_page.png")

    # 自动点击「连接服务器」(IDC_CONNECT=108, BN_CLICKED=0)
    ctypes.windll.user32.PostMessageW(hwnd, 0x0111, 108, 0)
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
finally:
    if proc.poll() is None:
        proc.kill()
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
