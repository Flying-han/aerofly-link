# -*- coding: utf-8 -*-
"""
工作区侧边栏
============
从 main_window.py 中拆出的 Page 1 工作区面板，
包含连接状态条、应答机、飞行计划、通讯日志。
"""
from PyQt6.QtWidgets import (
    QWidget, QVBoxLayout, QHBoxLayout, QFrame, QLabel, QPushButton
)


class WorkspaceSidebar(QWidget):
    """工作区侧边栏（Page 1）：连接状态 + 应答机 + 飞行计划 + 日志。"""

    def __init__(self, parent=None):
        super().__init__(parent)
        self.setStyleSheet("background-color: #1a1a1a;")
        self._init_ui()

    def _init_ui(self):
        layout = QVBoxLayout(self)
        layout.setContentsMargins(8, 8, 8, 8)
        layout.setSpacing(6)

        # ── 连接状态条 ──
        conn_bar = QFrame()
        conn_bar.setStyleSheet("""
            QFrame#connBar {
                background-color: #262626;
                border: 1px solid #3a3a3a;
                border-radius: 6px;
                padding: 8px;
            }
        """)
        conn_bar.setObjectName("connBar")
        conn_layout = QVBoxLayout(conn_bar)
        conn_layout.setContentsMargins(12, 10, 12, 10)
        conn_layout.setSpacing(8)

        info_row = QHBoxLayout()
        info_row.setSpacing(8)

        self.conn_status_icon = QLabel("●")
        self.conn_status_icon.setStyleSheet("color: #4CAF50; font-size: 14px;")
        info_row.addWidget(self.conn_status_icon)

        self.conn_status_label = QLabel("已连接")
        self.conn_status_label.setStyleSheet(
            "color: #e0e0e0; font-size: 13px; font-weight: bold;"
        )
        info_row.addWidget(self.conn_status_label)

        info_row.addStretch()

        self.conn_server_label = QLabel("")
        self.conn_server_label.setStyleSheet("color: #777; font-size: 11px;")
        info_row.addWidget(self.conn_server_label)

        conn_layout.addLayout(info_row)

        self.btn_disconnect = QPushButton("断开连接")
        self.btn_disconnect.setStyleSheet("""
            QPushButton {
                background-color: #c0392b;
                color: white;
                padding: 6px;
                font-size: 12px;
                font-weight: bold;
                border: none;
                border-radius: 4px;
            }
            QPushButton:hover {
                background-color: #e74c3c;
            }
        """)
        conn_layout.addWidget(self.btn_disconnect)

        layout.addWidget(conn_bar)

        # ── 应答机面板（延迟导入避免循环依赖）──
        from ui.transponder_panel import TransponderPanel
        self.transponder_panel = TransponderPanel()
        layout.addWidget(self.transponder_panel)

        # ── 飞行计划面板 ──
        from ui.flightplan_panel import FlightPlanPanel
        self.flightplan_panel = FlightPlanPanel()
        layout.addWidget(self.flightplan_panel)

        # ── 通讯日志面板（可拉伸）──
        from ui.log_panel import LogPanel
        self.log_panel = LogPanel()
        layout.addWidget(self.log_panel, stretch=1)

    def set_connected_display(self, connected: bool, server: str = ""):
        """更新连接状态显示。"""
        if connected:
            self.conn_status_icon.setStyleSheet("color: #4CAF50; font-size: 14px;")
            self.conn_status_label.setText("已连接")
            self.conn_status_label.setStyleSheet(
                "color: #e0e0e0; font-size: 13px; font-weight: bold;"
            )
            self.conn_server_label.setText(server)
        else:
            self.conn_status_icon.setStyleSheet("color: gray; font-size: 14px;")
            self.conn_status_label.setText("未连接")
            self.conn_server_label.setText("")
