# -*- coding: utf-8 -*-
"""枚举 AeroflyLinkMain 的子窗口与可见性（GUI 调试用）"""
import ctypes
import subprocess
import time
import os

u32 = ctypes.windll.user32
EnumChildProc = ctypes.WINFUNCTYPE(ctypes.c_bool, ctypes.c_void_p, ctypes.c_void_p)
rows = []


def cb(hwnd, lp):
    buf = ctypes.create_unicode_buffer(64)
    u32.GetClassNameW(hwnd, buf, 64)
    txt = ctypes.create_unicode_buffer(64)
    u32.GetWindowTextW(hwnd, txt, 64)
    vis = u32.IsWindowVisible(hwnd)
    r = ctypes.wintypes.RECT() if hasattr(ctypes, "wintypes") else None
    import ctypes.wintypes as wt
    rr = wt.RECT()
    u32.GetWindowRect(hwnd, ctypes.byref(rr))
    rows.append((hwnd, buf.value, txt.value, bool(vis),
                 rr.left, rr.top, rr.right - rr.left, rr.bottom - rr.top))
    return True


try:
    ctypes.windll.shcore.SetProcessDpiAwareness(2)
except Exception:
    pass

proc = subprocess.Popen([os.path.abspath(r"client-c\build\aeroflylink.exe")])
time.sleep(2.5)
hwnd = u32.FindWindowW("AeroflyLinkMain", None)
print("top hwnd:", hwnd, "visible:", bool(u32.IsWindowVisible(hwnd)))
u32.EnumChildWindows(hwnd, EnumChildProc(cb), 0)
for hwnd_, cls, txt, vis, x, y, w, h in rows:
    print(f"{'V' if vis else 'H'} {cls:<12} id? pos=({x},{y}) size={w}x{h} text={txt[:24]!r}")
proc.terminate()
proc.wait()
