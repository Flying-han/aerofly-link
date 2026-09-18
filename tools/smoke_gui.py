# -*- coding: utf-8 -*-
"""
GUI 冒烟截图工具：启动 aeroflylink.exe，窗口定位截图后关闭。
用法: python tools/smoke_gui.py [exe路径] [输出png]
"""
import ctypes
import ctypes.wintypes as wt
import os
import subprocess
import sys
import time

import PIL.ImageGrab

# DPI 感知：否则 GetWindowRect 与物理像素错位，截图被裁剪
try:
    ctypes.windll.shcore.SetProcessDpiAwareness(2)
except Exception:
    ctypes.windll.user32.SetProcessDPIAware()

exe = sys.argv[1] if len(sys.argv) > 1 else r"client-c\build\aeroflylink.exe"
out = sys.argv[2] if len(sys.argv) > 2 else os.path.join(
    os.environ["TEMP"], "aerofly_link_c_gui.png")

proc = subprocess.Popen([os.path.abspath(exe)])
try:
    time.sleep(3.0)

    hwnd = ctypes.windll.user32.FindWindowW("AeroflyLinkMain", None)
    if not hwnd:
        print("[smoke-gui] FAIL: window not found")
        sys.exit(1)

    rect = wt.RECT()
    ctypes.windll.user32.GetWindowRect(hwnd, ctypes.byref(rect))
    ctypes.windll.user32.SetForegroundWindow(hwnd)
    time.sleep(0.8)

    img = PIL.ImageGrab.grab(bbox=(rect.left, rect.top, rect.right, rect.bottom))
    img.save(out)
    print(f"[smoke-gui] saved {out}")

    # 关闭（WM_CLOSE → 优雅退出路径）
    ctypes.windll.user32.PostMessageW(hwnd, 0x0010, 0, 0)  # WM_CLOSE
    time.sleep(1.5)
    if proc.poll() is None:
        proc.terminate()
        print("[smoke-gui] WARN: did not exit on WM_CLOSE")
    else:
        print(f"[smoke-gui] exit code: {proc.returncode}")
finally:
    if proc.poll() is None:
        proc.kill()
