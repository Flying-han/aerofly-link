# -*- coding: utf-8 -*-
"""
应答机控制面板 - PyQt6 版本，左侧中部
类似 Swift Pilot Client 的 XPDR 控制区域
"""
from PyQt6.QtWidgets import (
    QWidget, QVBoxLayout, QHBoxLayout, QLineEdit,
    QPushButton, QGroupBox, QLabel, QMessageBox
)
from PyQt6.QtCore import pyqtSignal, QTimer, Qt
from PyQt6.QtGui import QIntValidator

from ui.styles import BTN_INFO_CSS


class TransponderPanel(QGroupBox):
    """应答机控制面板"""

    mode_changed = pyqtSignal(str)     # "STBY" | "ALT"
    code_changed = pyqtSignal(str)     # 4 位八进制代码
    ident_clicked = pyqtSignal()

    def __init__(self, parent=None):
        super().__init__("应答机控制 (Transponder)", parent)
        self._ident_timer = QTimer(self)
        self._ident_timer.timeout.connect(self._clear_ident)
        self._init_ui()

    def _init_ui(self):
        layout = QVBoxLayout(self)
        layout.setSpacing(8)

        # --- 模式指示灯 ---
        indicator_layout = QHBoxLayout()
        self.indicator_stby = QLabel("●")
        self.indicator_stby.setStyleSheet("color: #333; font-size: 14px;")
        self.indicator_stby.setToolTip("STBY 模式")
        indicator_layout.addWidget(QLabel("STBY"))
        indicator_layout.addWidget(self.indicator_stby)

        indicator_layout.addSpacing(20)

        self.indicator_alt = QLabel("●")
        self.indicator_alt.setStyleSheet("color: #4CAF50; font-size: 14px;")
        self.indicator_alt.setToolTip("ALT 模式")
        indicator_layout.addWidget(QLabel("ALT"))
        indicator_layout.addWidget(self.indicator_alt)

        indicator_layout.addStretch()
        layout.addLayout(indicator_layout)

        # --- Squawk Code 输入 ---
        code_layout = QHBoxLayout()
        code_layout.addWidget(QLabel("代码:"))
        self.input_code = QLineEdit("1200")
        self.input_code.setMaxLength(4)
        self.input_code.setAlignment(Qt.AlignmentFlag.AlignCenter)
        self.input_code.setStyleSheet("""
            QLineEdit {
                font-size: 20px;
                font-family: 'Consolas', 'Courier New', monospace;
                background-color: #1a1a1a;
                color: #00ff00;
                border: 2px solid #333;
                border-radius: 4px;
                padding: 6px;
                letter-spacing: 6px;
            }
            QLineEdit:focus {
                border: 2px solid #4CAF50;
            }
        """)
        self.input_code.returnPressed.connect(self._on_code_submit)
        self.input_code.editingFinished.connect(self._on_code_submit)
        code_layout.addWidget(self.input_code)
        layout.addLayout(code_layout)

        # --- VFR / 快捷按钮 ---
        quick_layout = QHBoxLayout()
        quick_btn_style = """
            QPushButton {
                background-color: #2a2a2a;
                color: #e0e0e0;
                padding: 6px 12px;
                border: 1px solid #444;
                border-radius: 3px;
                font-size: 12px;
                font-family: 'Consolas', monospace;
            }
            QPushButton:hover {
                background-color: #3a3a3a;
                border: 1px solid #4CAF50;
            }
        """

        self.btn_vfr = QPushButton("1200")
        self.btn_vfr.setStyleSheet(quick_btn_style)
        self.btn_vfr.clicked.connect(lambda: self._set_code("1200"))
        quick_layout.addWidget(self.btn_vfr)

        self.btn_std = QPushButton("7000")
        self.btn_std.setStyleSheet(quick_btn_style)
        self.btn_std.clicked.connect(lambda: self._set_code("7000"))
        quick_layout.addWidget(self.btn_std)

        # 紧急代码
        emg_style = """
            QPushButton {
                background-color: #2a2a2a;
                color: #e0e0e0;
                padding: 6px 10px;
                border: 1px solid #444;
                border-radius: 3px;
                font-size: 11px;
                font-family: 'Consolas', monospace;
            }
            QPushButton:hover {
                background-color: #3a3a3a;
            }
        """
        self.btn_7700 = QPushButton("7700")
        self.btn_7700.setStyleSheet(emg_style + "\nQPushButton:hover { border: 1px solid #f44336; }")
        self.btn_7700.clicked.connect(lambda: self._set_code("7700"))
        quick_layout.addWidget(self.btn_7700)

        self.btn_7600 = QPushButton("7600")
        self.btn_7600.setStyleSheet(emg_style + "\nQPushButton:hover { border: 1px solid #ff9800; }")
        self.btn_7600.clicked.connect(lambda: self._set_code("7600"))
        quick_layout.addWidget(self.btn_7600)

        self.btn_7500 = QPushButton("7500")
        self.btn_7500.setStyleSheet(emg_style + "\nQPushButton:hover { border: 1px solid #f44336; }")
        self.btn_7500.clicked.connect(lambda: self._set_code("7500"))
        quick_layout.addWidget(self.btn_7500)

        layout.addLayout(quick_layout)

        # --- 模式切换按钮（互斥）---
        mode_layout = QHBoxLayout()

        mode_btn_base = """
            QPushButton {
                color: white;
                padding: 12px;
                font-size: 14px;
                font-weight: bold;
                border: none;
                border-radius: 4px;
            }
        """

        self.btn_stby = QPushButton("STBY")
        self.btn_stby.setCheckable(True)
        self.btn_stby.setStyleSheet(mode_btn_base + """
            QPushButton { background-color: #555; }
            QPushButton:hover { background-color: #666; }
            QPushButton:checked { background-color: #ff9800; }
        """)
        self.btn_stby.clicked.connect(lambda: self._on_mode("STBY"))
        mode_layout.addWidget(self.btn_stby)

        self.btn_alt = QPushButton("ALT")
        self.btn_alt.setCheckable(True)
        self.btn_alt.setChecked(True)
        self.btn_alt.setStyleSheet(mode_btn_base + """
            QPushButton { background-color: #555; }
            QPushButton:hover { background-color: #666; }
            QPushButton:checked { background-color: #4CAF50; }
        """)
        self.btn_alt.clicked.connect(lambda: self._on_mode("ALT"))
        mode_layout.addWidget(self.btn_alt)

        layout.addLayout(mode_layout)

        # --- IDENT 按钮 ---
        self.btn_ident = QPushButton("IDENT")
        self.btn_ident.setStyleSheet(BTN_INFO_CSS)
        self.btn_ident.clicked.connect(self._on_ident_click)
        layout.addWidget(self.btn_ident)

        # --- 警告标签 ---
        self.lbl_warning = QLabel("")
        self.lbl_warning.setStyleSheet("""
            color: #ff9800;
            font-size: 11px;
            background-color: #332200;
            border: 1px solid #664400;
            border-radius: 3px;
            padding: 6px;
        """)
        self.lbl_warning.setWordWrap(True)
        self.lbl_warning.hide()
        layout.addWidget(self.lbl_warning)

        # --- DLL 写入能力状态 ---
        self.lbl_dll_status = QLabel("DLL: 检测中...")
        self.lbl_dll_status.setStyleSheet("color: gray; font-size: 10px;")
        layout.addWidget(self.lbl_dll_status)

        layout.addStretch()

    # ──────────────────────────────────────────────
    # 内部逻辑
    # ──────────────────────────────────────────────

    def _on_mode(self, mode: str):
        """模式切换（互斥）"""
        if mode == "STBY":
            self.btn_stby.setChecked(True)
            self.btn_alt.setChecked(False)
            self.indicator_stby.setStyleSheet("color: #ff9800; font-size: 14px;")
            self.indicator_alt.setStyleSheet("color: #333; font-size: 14px;")
        else:
            self.btn_stby.setChecked(False)
            self.btn_alt.setChecked(True)
            self.indicator_stby.setStyleSheet("color: #333; font-size: 14px;")
            self.indicator_alt.setStyleSheet("color: #4CAF50; font-size: 14px;")

        self.mode_changed.emit(mode)

    def _on_code_submit(self):
        """用户按回车或失焦确认代码"""
        code = self.input_code.text()
        # 防重入：若代码未变化则跳过（editingFinished + returnPressed 可能连续触发）
        if getattr(self, '_last_submitted_code', None) == code:
            return
        if self._validate_squawk(code):
            self._last_submitted_code = code
            self.code_changed.emit(code)
        else:
            QMessageBox.warning(self, "无效代码", "应答机代码必须是 4 位八进制数字 (0-7)")

    def _set_code(self, code: str):
        """设置代码并触发变更"""
        self.input_code.setText(code)
        self.code_changed.emit(code)

    def _on_ident_click(self):
        """用户点击 IDENT 按钮"""
        self.btn_ident.setEnabled(False)
        self.btn_ident.setText("IDENT 发送中... (5s)")
        self.btn_ident.setStyleSheet("""
            QPushButton {
                background-color: #4CAF50;
                color: white;
                padding: 10px;
                font-size: 13px;
                font-weight: bold;
                border: none;
                border-radius: 4px;
            }
        """)
        self._ident_timer.start(5000)
        self.ident_clicked.emit()

    def _clear_ident(self):
        """IDENT 5 秒后自动恢复"""
        self.btn_ident.setEnabled(True)
        self.btn_ident.setText("IDENT")
        self.btn_ident.setStyleSheet(BTN_INFO_CSS)
        self._ident_timer.stop()

    def _validate_squawk(self, code: str) -> bool:
        """验证 Squawk 代码格式"""
        if len(code) != 4:
            return False
        return all(c in "01234567" for c in code)

    # ──────────────────────────────────────────────
    # 公共接口
    # ──────────────────────────────────────────────

    def update_display(self, mode: str, code: str, ident: bool):
        """更新面板显示（来自核心模块的反馈）"""
        self.input_code.setText(code)

        if mode == "STBY":
            self.btn_stby.setChecked(True)
            self.btn_alt.setChecked(False)
            self.indicator_stby.setStyleSheet("color: #ff9800; font-size: 14px;")
            self.indicator_alt.setStyleSheet("color: #333; font-size: 14px;")
        else:
            self.btn_stby.setChecked(False)
            self.btn_alt.setChecked(True)
            self.indicator_stby.setStyleSheet("color: #333; font-size: 14px;")
            self.indicator_alt.setStyleSheet("color: #4CAF50; font-size: 14px;")

        if ident:
            self.btn_ident.setEnabled(False)
            self.btn_ident.setText("IDENT 发送中...")
            self._ident_timer.start(5000)
        else:
            self.btn_ident.setEnabled(True)
            self.btn_ident.setText("IDENT")

    def show_warning(self, message: str):
        """显示同步警告"""
        self.lbl_warning.setText(f"⚠ {message}")
        self.lbl_warning.show()
        # 5 秒后自动清除
        QTimer.singleShot(5000, self.lbl_warning.hide)

    def update_dll_status(self, can_write_mode: bool | None):
        """更新 DLL 写入能力状态"""
        if can_write_mode is None:
            self.lbl_dll_status.setText("DLL: 检测中...")
            self.lbl_dll_status.setStyleSheet("color: gray; font-size: 10px;")
        elif can_write_mode:
            self.lbl_dll_status.setText("DLL: ✓ 已连接，支持模式写入")
            self.lbl_dll_status.setStyleSheet("color: green; font-size: 10px;")
        else:
            self.lbl_dll_status.setText("DLL: ✗ 不支持模式写入（已降级）")
            self.lbl_dll_status.setStyleSheet("color: #ff9800; font-size: 10px;")
