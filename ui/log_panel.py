# -*- coding: utf-8 -*-
"""
通讯日志面板 - 左侧底部，可拉伸
类似 Swift Pilot Client 的 Text 通讯窗口
"""
from datetime import datetime

from PyQt6.QtWidgets import (
    QWidget, QVBoxLayout, QTextEdit, QLineEdit,
    QPushButton, QHBoxLayout, QGroupBox
)

from ui.styles import INPUT_CSS, BTN_INFO_CSS


class LogPanel(QGroupBox):
    """ATC 通讯日志面板"""

    def __init__(self, parent=None):
        super().__init__("通讯日志 (ATC Messages)", parent)
        self._init_ui()

    def _init_ui(self):
        layout = QVBoxLayout(self)
        layout.setSpacing(6)

        # 消息显示区（只读）
        self.txt_log = QTextEdit()
        self.txt_log.setReadOnly(True)
        self.txt_log.setStyleSheet("""
            QTextEdit {
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
        self.input_msg.setPlaceholderText("输入消息 (如: @ZGGG_TWR 请求放行)")
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

    def add_message(self, source: str, dest: str, message: str):
        """添加一条消息到日志"""
        timestamp = self._get_timestamp()

        if source.startswith("@"):
            # ATC 发来的消息（绿色）
            html = (
                f"<span style='color:#888'>[{timestamp}]</span> "
                f"<span style='color:#4CAF50'>{source}</span>"
                f"<span style='color:#ccc'>: {message}</span><br>"
            )
        elif source == "系统":
            # 系统消息（灰色）
            html = (
                f"<span style='color:#888'>[{timestamp}]</span> "
                f"<span style='color:#888'>[系统] {message}</span><br>"
            )
        else:
            # 自己发送的消息（白色）
            html = (
                f"<span style='color:#888'>[{timestamp}]</span> "
                f"<span style='color:#e0e0e0'>我 → {dest}: {message}</span><br>"
            )

        self.txt_log.insertHtml(html)
        # 自动滚动到底部
        self.txt_log.verticalScrollBar().setValue(
            self.txt_log.verticalScrollBar().maximum()
        )

    # ──────────────────────────────────────────────
    # 内部方法
    # ──────────────────────────────────────────────

    def _on_send(self):
        """发送消息"""
        text = self.input_msg.text().strip()
        if not text:
            return

        # 将消息显示在自己的日志中
        self.add_message("我", "频率", text)
        self.input_msg.clear()

        # TODO: 连接 FSDClient 发送 #TM 消息
        # 格式: @频率 消息内容 或 @管制员呼号 消息内容

    def _get_timestamp(self) -> str:
        """获取当前时间 HH:MM:SS"""
        return datetime.now().strftime("%H:%M:%S")
