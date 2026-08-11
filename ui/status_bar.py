# -*- coding: utf-8 -*-
"""
状态栏组件
==========
从 main_window.py 中拆出的底部状态栏，
集中管理连接状态、应答机、高度/地速、呼号、DLL 状态等显示。
"""
from PyQt6.QtWidgets import QStatusBar, QLabel, QPushButton, QWidget, QSizePolicy


class StatusBar(QStatusBar):
    """底部状态栏 — 显示连接、应答机、飞行数据、DLL 状态。"""

    def __init__(self, parent=None):
        super().__init__(parent)
        self.setMinimumHeight(26)
        self.setStyleSheet(
            "QStatusBar { background-color: #1a1a1a; color: #e0e0e0; font-size: 12px; }"
        )

        # 连接状态
        self.lbl_connection = QLabel("● 未连接")
        self.lbl_connection.setStyleSheet("color: gray; padding: 0 10px;")
        self.addWidget(self.lbl_connection)

        self._add_separator()

        # 应答机状态
        self.lbl_xpdr = QLabel("应答机: STBY 7000")
        self.lbl_xpdr.setStyleSheet("color: #e0e0e0; padding: 0 5px;")
        self.addWidget(self.lbl_xpdr)

        self._add_separator()

        # 飞行数据
        self.lbl_flight = QLabel("高度: ---ft  地速: ---kts")
        self.lbl_flight.setStyleSheet("color: #e0e0e0; padding: 0 5px;")
        self.addWidget(self.lbl_flight)

        self._add_separator()

        # 呼号
        self.lbl_callsign = QLabel("呼号: ---")
        self.lbl_callsign.setStyleSheet("color: #ff9800; padding: 0 5px;")
        self.addWidget(self.lbl_callsign)

        # 弹性间隔
        spacer = QWidget()
        spacer.setSizePolicy(QSizePolicy.Policy.Expanding, QSizePolicy.Policy.Preferred)
        self.addWidget(spacer)

        # DLL 状态
        self.lbl_dll = QLabel("DLL: 检测中...")
        self.lbl_dll.setStyleSheet("color: gray; padding: 0 10px;")
        self.addWidget(self.lbl_dll)

        # Mock DLL 开关
        self.btn_mock = QPushButton("模拟DLL")
        self.btn_mock.setMinimumHeight(20)
        self.btn_mock.setCheckable(True)
        self.btn_mock.setStyleSheet(
            "QPushButton {"
            "  background-color: #333; color: #888; border: 1px solid #555;"
            "  border-radius: 3px; padding: 0 8px; font-size: 11px;"
            "}"
            "QPushButton:checked {"
            "  background-color: #1b5e20; color: #4CAF50; border-color: #4CAF50;"
            "}"
        )
        self.btn_mock.setToolTip(
            "非正版 AF4 不支持 external DLL API 时，可开启此模拟服务器"
        )
        self.addWidget(self.btn_mock)

    def _add_separator(self):
        sep = QLabel("|")
        sep.setStyleSheet("color: #444; padding: 0 5px;")
        self.addWidget(sep)

    # ── 状态更新接口 ──────────────────────────────────────────

    def set_connection_status(self, text: str, color: str = "gray"):
        self.lbl_connection.setText(text)
        self.lbl_connection.setStyleSheet(f"color: {color}; padding: 0 10px;")

    def set_xpdr_status(self, text: str):
        self.lbl_xpdr.setText(text)

    def set_flight_data(self, text: str):
        self.lbl_flight.setText(text)

    def set_callsign(self, text: str):
        self.lbl_callsign.setText(text)

    def set_dll_status(self, text: str, color: str = "gray", tooltip: str = ""):
        self.lbl_dll.setText(text)
        self.lbl_dll.setStyleSheet(f"color: {color}; padding: 0 10px;")
        if tooltip:
            self.lbl_dll.setToolTip(tooltip)

    def reset_flight_data(self):
        self.lbl_flight.setText("高度: ---ft  地速: ---kts")

    def reset_callsign(self):
        self.lbl_callsign.setText("呼号: ---")
