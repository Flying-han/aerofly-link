# -*- coding: utf-8 -*-
"""
Aerofly Link - Aerofly FS 4 第三方联机客户端
主入口文件，初始化 Qt 应用和主窗口
"""
import sys
import os
from pathlib import Path

# PyInstaller 打包后资源路径
if getattr(sys, 'frozen', False):
    os.chdir(sys._MEIPASS)

# 确保项目根目录在 Python 路径中
project_root = Path(__file__).parent
if str(project_root) not in sys.path:
    sys.path.insert(0, str(project_root))

# Windows 下 asyncio 兼容性：使用 SelectorEventLoop
import asyncio
import platform
if platform.system() == 'Windows':
    try:
        from asyncio import WindowsSelectorEventLoopPolicy
        asyncio.set_event_loop_policy(WindowsSelectorEventLoopPolicy())
    except ImportError:
        pass

from PyQt6.QtWidgets import QApplication
from PyQt6.QtCore import Qt
from main_window import MainWindow
from ui.styles import APP_QSS

# ── 全局异常捕获 ──────────────────────────────────
CRASH_LOG = Path(__file__).parent.parent / "crash.log" if not getattr(sys, 'frozen', False) else Path(os.environ.get("APPDATA", Path.home())) / "AeroflyLink" / "crash.log"

def _excepthook(exc_type, exc_value, exc_tb):
    """捕获所有未处理的异常，写入 crash.log"""
    import traceback
    import datetime
    crash_log = CRASH_LOG
    crash_log.parent.mkdir(parents=True, exist_ok=True)
    with open(crash_log, "a", encoding="utf-8") as f:
        f.write(f"\n{'='*60}\n")
        f.write(f"CRASH: {datetime.datetime.now().isoformat()}\n")
        f.write(f"Type: {exc_type.__name__}\n")
        f.write(f"Message: {exc_value}\n")
        traceback.print_exception(exc_type, exc_value, exc_tb, file=f)
        f.write(f"{'='*60}\n")
    # 调用默认处理（打印到 stderr）
    sys.__excepthook__(exc_type, exc_value, exc_tb)

sys.excepthook = _excepthook


def main():
    # 高 DPI 支持
    QApplication.setHighDpiScaleFactorRoundingPolicy(
        Qt.HighDpiScaleFactorRoundingPolicy.PassThrough
    )

    app = QApplication(sys.argv)
    app.setApplicationName("Aerofly Link")
    app.setApplicationVersion("1.0.0")

    # 全局深色主题样式（使用共享样式模块）
    app.setStyleSheet(APP_QSS)

    # 创建主窗口
    window = MainWindow()
    window.show()

    # 启动事件循环
    sys.exit(app.exec())


if __name__ == "__main__":
    main()
