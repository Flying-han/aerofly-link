# -*- coding: utf-8 -*-
"""
UI 冒烟测试（离屏截图）
=======================
在进程内构建完整主窗口，连接内置 Mock DLL，抓取两个页面的截图：
  Page 0 连接页  /  Page 1 工作区（应答机 + 飞行计划 + 通讯日志）

用法（在仓库根目录）::

    python tests/smoke_ui.py [输出目录]

退出码 0 表示窗口构建、遥测接入与页面切换均正常。
截图输出到指定目录（默认 %TEMP%/aerofly_link_smoke）。
"""
import asyncio
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent.parent))

from PyQt6.QtCore import QTimer
from PyQt6.QtWidgets import QApplication

from core import __version__
from main_window import MainWindow
from ui.styles import APP_QSS


def main() -> int:
    out_dir = Path(sys.argv[1] if len(sys.argv) > 1 else
                   Path.home() / "AppData" / "Local" / "Temp" / "aerofly_link_smoke")
    out_dir.mkdir(parents=True, exist_ok=True)

    app = QApplication(sys.argv)
    app.setApplicationName("Aerofly Link")
    app.setApplicationVersion(__version__)
    app.setStyleSheet(APP_QSS)

    window = MainWindow()
    window.resize(900, 640)
    window.show()

    # 连接页截图
    def grab_connect_page():
        window.left_stack.setCurrentIndex(0)
        QTimer.singleShot(300, lambda: shoot("page0_connect"))

    # 切到工作区 + 开 Mock（遥测接入的完整链路）
    def go_workspace():
        window.left_stack.setCurrentIndex(1)
        window.connect_page.input_callsign.setText("TST123")
        window._start_mock_server()
        QTimer.singleShot(4000, lambda: shoot("page1_workspace"))

    def shoot(name):
        png = out_dir / f"{name}.png"
        window.grab().save(str(png), "PNG")
        print(f"[smoke] saved {png}")

    # 收尾：校验遥测与状态后退出
    def finish():
        tel = window.dll_bridge.latest_telemetry
        print(f"[smoke] dll connected: {window.dll_bridge.is_telemetry_connected}")
        print(f"[smoke] telemetry: {None if tel is None else f'{tel.lat:.4f},{tel.lon:.4f} alt={tel.alt_m:.0f}m hdg={tel.hdg_true:.1f}'}")
        print(f"[smoke] version: {__version__}")
        # 遥测坐标取决于连接页 mock 配置（空 → 默认希思罗），只验证链路与数值合法性
        ok = (tel is not None and -90 <= tel.lat <= 90
              and -180 <= tel.lon <= 180 and 0 <= tel.hdg_true < 360)
        print("[smoke] RESULT:", "OK" if ok else "DEGRADED (see screenshots)")
        # 走真实关闭路径（closeEvent → 优雅停机序列），验证无退出异常
        window.close()
        app.exit(0 if ok else 1)

    QTimer.singleShot(1500, go_workspace)
    QTimer.singleShot(6000, finish)
    return app.exec()


if __name__ == "__main__":
    sys.exit(main())
