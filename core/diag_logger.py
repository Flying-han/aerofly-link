# -*- coding: utf-8 -*-
"""
诊断日志模块
============
统一管理 %APPDATA%/AeroflyLink/diag.log 的写入，
避免在多个文件中重复定义 _diag() 函数。

线程安全：使用 threading.Lock 保护文件句柄。
"""
import os
import threading
from pathlib import Path
from datetime import datetime

_lock = threading.Lock()
_file_handle = None  # type: ignore


def _ensure_handle():
    """延迟打开日志文件（线程安全）。"""
    global _file_handle
    if _file_handle is None:
        try:
            log_dir = Path(
                os.environ.get("APPDATA", Path.home() / "AppData" / "Roaming")
            ) / "AeroflyLink"
            log_dir.mkdir(parents=True, exist_ok=True)
            _file_handle = open(
                str(log_dir / "diag.log"), "a", encoding="utf-8", buffering=1
            )
        except Exception:
            _file_handle = False  # type: ignore


def diag(msg: str) -> None:
    """写入一条诊断日志。线程安全，失败时静默忽略。"""
    with _lock:
        _ensure_handle()
        if _file_handle:
            ts = datetime.now().strftime("%H:%M:%S.%f")[:12]
            _file_handle.write(f"[{ts}] {msg}\n")
            _file_handle.flush()
