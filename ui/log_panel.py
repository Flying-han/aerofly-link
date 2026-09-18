# -*- coding: utf-8 -*-
"""
通讯日志面板 - 左侧底部，可拉伸
类似 Swift Pilot Client 的 Text 通讯窗口

消息方向（kind）:
  - "in"     来自 ATC / 服务器的消息（绿色，来源呼号）
  - "out"    本机发送的消息（白色，"我 → 目标"）
  - "system" 系统状态消息（灰色，[系统]）
"""
from datetime import datetime

from PyQt6.QtWidgets import (
    QWidget, QVBoxLayout, QPlainTextEdit, QLineEdit,
    QPushButton, QHBoxLayout, QGroupBox
)
from PyQt6.QtCore import pyqtSignal

from ui.styles import INPUT_CSS, BTN_INFO_CSS

# 日志块数上限：QPlainTextEdit 自动丢弃最旧块，长时间联机内存不增长
MAX_LOG_BLOCKS = 500


class LogPanel(QGroupBox):
    """ATC 通讯日志面板"""

    message_send_requested = pyqtSignal(str)  # 用户请求发送消息

    def __init__(self, parent=None):
        super().__init__("通讯日志 (ATC Messages)", parent)
        self._init_ui()

    def _init_ui(self):
        layout = QVBoxLayout(self)
        layout.setSpacing(6)

        # 消息显示区（只读，块数封顶）
        self.txt_log = QPlainTextEdit()
        self.txt_log.setReadOnly(True)
        self.txt_log.setMaximumBlockCount(MAX_LOG_BLOCKS)
        self.txt_log.setStyleSheet("""
            QPlainTextEdit {
                background-color: #1a1a1a;
                color: #e0e0e0;
                font-family: 'Consolas', 'Courier New', monospace;
                font-size: 12px;
                border: 1px solid #333;
                border-radius: 4px;
                padding: 6px;
            }
            QScrollBar:vertical {
                background: #1a1a1a;
                width: 10px;
            }
            QScrollBar::handle:vertical {
                background: #444;
                border-radius: 5px;
            }
        """)
        layout.addWidget(self.txt_log)

        # 发送区
        send_layout = QHBoxLayout()

        self.input_msg = QLineEdit()
        self.input_msg.setPlaceholderText("发送到 UNICOM，或 @呼号 消息（如 @ZGGG_TWR 请求放行）")
        self.input_msg.setStyleSheet(INPUT_CSS)
        self.input_msg.returnPressed.connect(self._on_send)
        send_layout.addWidget(self.input_msg)

        self.btn_send = QPushButton("发送")
        self.btn_send.setStyleSheet(BTN_INFO_CSS)
        self.btn_send.clicked.connect(self._on_send)
        send_layout.addWidget(self.btn_send)

        layout.addLayout(send_layout)

    # ──────────────────────────────────────────────
    # 公共接口
    # ──────────────────────────────────────────────

    def add_message(self, source: str, dest: str, message: str, kind: str = "in"):
        """添加一条消息到日志。

        :param kind: "in"（收到的 ATC/服务器消息）/"out"（本机发送）/"system"（系统）
        """
        timestamp = datetime.now().strftime("%H:%M:%S")

        if kind == "out":
            html = (
                f"<span style='color:#888'>[{timestamp}]</span> "
                f"<span style='color:#e0e0e0'>我 → {dest}: {message}</span>"
            )
        elif kind == "system":
            html = (
                f"<span style='color:#888'>[{timestamp}]</span> "
                f"<span style='color:#888'>[系统] {message}</span>"
            )
        else:  # in — ATC / 服务器消息
            html = (
                f"<span style='color:#888'>[{timestamp}]</span> "
                f"<span style='color:#4CAF50'>{source}</span>"
                f"<span style='color:#ccc'>: {message}</span>"
            )

        self.txt_log.appendHtml(html)

    # ──────────────────────────────────────────────
    # 内部方法
    # ──────────────────────────────────────────────

    def _on_send(self):
        """用户按回车或点击发送 → 发出请求信号（实际发送由主窗口接线）"""
        text = self.input_msg.text().strip()
        if not text:
            return
        self.input_msg.clear()
        self.message_send_requested.emit(text)
