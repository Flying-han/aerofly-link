# -*- coding: utf-8 -*-
"""
连接配置面板 - 左侧顶部
类似 Swift Pilot Client 的连接配置区域
支持服务器列表增删持久化
"""
from PyQt6.QtWidgets import (
    QWidget, QVBoxLayout, QHBoxLayout, QFormLayout, QLineEdit,
    QComboBox, QPushButton, QGroupBox, QLabel, QInputDialog, QMessageBox
)
from PyQt6.QtCore import pyqtSignal

from ui.styles import INPUT_CSS, COMBO_CSS, BTN_SMALL_CSS

# 默认服务器列表
DEFAULT_SERVERS = [
    "sweatbox.vatsim.net",
    "fsd.vatsim.net",
    "127.0.0.1:6809"
]


class ConnectionPanel(QGroupBox):
    """连接配置面板，类似 Swift 的左侧连接区"""

    connect_clicked = pyqtSignal()
    disconnect_clicked = pyqtSignal()

    def __init__(self, parent=None):
        super().__init__("连接配置", parent)
        self._init_ui()

    def _init_ui(self):
        layout = QVBoxLayout(self)

        form = QFormLayout()
        form.setVerticalSpacing(8)

        # 呼号
        self.input_callsign = QLineEdit()
        self.input_callsign.setPlaceholderText("如: CES2101")
        self.input_callsign.setMaxLength(7)
        self.input_callsign.setStyleSheet(INPUT_CSS)
        form.addRow("呼号:", self.input_callsign)

        # CID
        self.input_cid = QLineEdit()
        self.input_cid.setPlaceholderText("CID（如: 1234567）")
        self.input_cid.setMaxLength(10)
        self.input_cid.setStyleSheet(INPUT_CSS)
        form.addRow("CID:", self.input_cid)

        # 密码
        self.input_password = QLineEdit()
        self.input_password.setEchoMode(QLineEdit.EchoMode.Password)
        self.input_password.setPlaceholderText("密码")
        self.input_password.setStyleSheet(INPUT_CSS)
        form.addRow("密码:", self.input_password)

        # 服务器选择 —— 水平布局 = ComboBox + [+] + [-]
        self.combo_server = QComboBox()
        self.combo_server.setEditable(True)
        self.combo_server.setStyleSheet(COMBO_CSS)

        self.btn_add_server = QPushButton("+")
        self.btn_add_server.setToolTip("添加服务器")
        self.btn_add_server.setStyleSheet(BTN_SMALL_CSS)
        self.btn_add_server.clicked.connect(self._on_add_server)

        self.btn_del_server = QPushButton("-")
        self.btn_del_server.setToolTip("删除当前选中的服务器")
        self.btn_del_server.setStyleSheet(BTN_SMALL_CSS)
        self.btn_del_server.clicked.connect(self._on_del_server)

        server_row = QHBoxLayout()
        server_row.setSpacing(4)
        server_row.addWidget(self.combo_server, 1)
        server_row.addWidget(self.btn_add_server)
        server_row.addWidget(self.btn_del_server)
        form.addRow("服务器:", server_row)

        # 真实姓名
        self.input_realname = QLineEdit()
        self.input_realname.setPlaceholderText("真实姓名（用于飞行计划）")
        self.input_realname.setStyleSheet(INPUT_CSS)
        form.addRow("姓名:", self.input_realname)

        # 飞行员等级（服务器验证 CID 对应的等级，过高会被拒绝）
        self.combo_rating = QComboBox()
        self.combo_rating.setStyleSheet(COMBO_CSS)
        self.combo_rating.addItem("OBS (观察员)", 1)
        self.combo_rating.addItem("S1 (学生)", 2)
        self.combo_rating.addItem("S2 (学生2)", 3)
        self.combo_rating.addItem("S3 (学生3)", 4)
        self.combo_rating.setCurrentIndex(0)  # 默认 OBS
        form.addRow("等级:", self.combo_rating)

        # 机型 ICAO
        self.input_model = QLineEdit()
        self.input_model.setPlaceholderText("如: B738, A320")
        self.input_model.setMaxLength(4)
        self.input_model.setStyleSheet(INPUT_CSS)
        form.addRow("机型:", self.input_model)

        # Mock DLL 位置配置
        mock_label = QLabel("模拟 DLL 位置（非正版使用）:")
        mock_label.setStyleSheet("color: #888; font-size: 11px; margin-top: 8px;")
        layout.addWidget(mock_label)

        mock_form = QFormLayout()
        mock_form.setVerticalSpacing(6)

        self.input_mock_lat = QLineEdit()
        self.input_mock_lat.setPlaceholderText("如: 51.4775")
        self.input_mock_lat.setStyleSheet(INPUT_CSS)
        mock_form.addRow("纬度:", self.input_mock_lat)

        self.input_mock_lon = QLineEdit()
        self.input_mock_lon.setPlaceholderText("如: -0.4614（伦敦希思罗）")
        self.input_mock_lon.setStyleSheet(INPUT_CSS)
        mock_form.addRow("经度:", self.input_mock_lon)

        self.input_mock_alt = QLineEdit()
        self.input_mock_alt.setPlaceholderText("如: 3500 (米)")
        self.input_mock_alt.setStyleSheet(INPUT_CSS)
        mock_form.addRow("高度:", self.input_mock_alt)

        layout.addLayout(mock_form)

        layout.addLayout(form)

        # 连接按钮
        self.btn_connect = QPushButton("Connect")
        self.btn_connect.setStyleSheet("""
            QPushButton {
                background-color: #4CAF50;
                color: white;
                padding: 10px;
                font-size: 14px;
                font-weight: bold;
                border: none;
                border-radius: 4px;
            }
            QPushButton:hover {
                background-color: #45a049;
            }
            QPushButton:disabled {
                background-color: #333;
                color: #666;
            }
        """)
        self.btn_connect.clicked.connect(self.connect_clicked.emit)
        layout.addWidget(self.btn_connect)

        # 断开按钮
        self.btn_disconnect = QPushButton("Disconnect")
        self.btn_disconnect.setStyleSheet("""
            QPushButton {
                background-color: #f44336;
                color: white;
                padding: 10px;
                font-size: 14px;
                font-weight: bold;
                border: none;
                border-radius: 4px;
            }
            QPushButton:hover {
                background-color: #da190b;
            }
            QPushButton:disabled {
                background-color: #333;
                color: #666;
            }
        """)
        self.btn_disconnect.clicked.connect(self.disconnect_clicked.emit)
        self.btn_disconnect.setEnabled(False)
        layout.addWidget(self.btn_disconnect)

        # 提示信息
        self.lbl_hint = QLabel("先启动 AFS4 再点击 Connect")
        self.lbl_hint.setStyleSheet("color: gray; font-size: 11px; padding-top: 5px;")
        self.lbl_hint.setWordWrap(True)
        layout.addWidget(self.lbl_hint)

    # ──────────────────────────────────────────────
    # 服务器增删
    # ──────────────────────────────────────────────

    def _on_add_server(self):
        """弹出对话框，输入新服务器地址并加入列表"""
        text, ok = QInputDialog.getText(
            self, "添加服务器",
            "请输入服务器地址（如 host:port 或 host）:"
        )
        if ok and text.strip():
            server = text.strip()
            # 避免重复
            for i in range(self.combo_server.count()):
                if self.combo_server.itemText(i) == server:
                    self.combo_server.setCurrentIndex(i)
                    return
            self.combo_server.addItem(server)
            self.combo_server.setCurrentText(server)

    def _on_del_server(self):
        """删除当前选中的服务器（至少保留一个）"""
        if self.combo_server.count() <= 1:
            QMessageBox.warning(self, "无法删除", "至少保留一个服务器地址。")
            return

        idx = self.combo_server.currentIndex()
        server_name = self.combo_server.currentText()
        reply = QMessageBox.question(
            self, "确认删除",
            f"确定要删除服务器 \"{server_name}\" 吗？",
            QMessageBox.StandardButton.Yes | QMessageBox.StandardButton.No
        )
        if reply == QMessageBox.StandardButton.Yes:
            self.combo_server.removeItem(idx)

    def _get_servers(self) -> list:
        """获取当前 combo 中所有服务器地址"""
        return [self.combo_server.itemText(i) for i in range(self.combo_server.count())]

    def _set_servers(self, servers: list):
        """用列表批量设置服务器（保留当前选中项不动）"""
        current = self.combo_server.currentText()
        self.combo_server.clear()
        if servers:
            self.combo_server.addItems(servers)
            idx = self.combo_server.findText(current)
            if idx >= 0:
                self.combo_server.setCurrentIndex(idx)

    # ──────────────────────────────────────────────
    # 公共接口
    # ──────────────────────────────────────────────

    def get_config(self) -> dict:
        """获取当前配置，含完整服务器列表"""
        server_text = self.combo_server.currentText()
        if ":" in server_text:
            host, port = server_text.rsplit(":", 1)
            try:
                port = int(port)
            except ValueError:
                port = 6809
        else:
            host = server_text
            port = 6809

        return {
            "callsign": self.input_callsign.text().upper().strip(),
            "cid": self.input_cid.text().strip(),
            "password": self.input_password.text(),
            "realname": self.input_realname.text().strip(),
            "rating": self.combo_rating.currentData(),
            "server": host,
            "port": port,
            "model": self.input_model.text().upper().strip(),
            "servers": self._get_servers(),
            "mock_lat": self.input_mock_lat.text().strip(),
            "mock_lon": self.input_mock_lon.text().strip(),
            "mock_alt": self.input_mock_alt.text().strip(),
        }

    def set_connected(self, connected: bool):
        """设置连接状态，更新按钮可用性"""
        self.btn_connect.setEnabled(not connected)
        self.btn_disconnect.setEnabled(connected)
        self.input_callsign.setEnabled(not connected)
        self.input_cid.setEnabled(not connected)
        self.input_password.setEnabled(not connected)
        self.input_realname.setEnabled(not connected)
        self.combo_rating.setEnabled(not connected)
        self.combo_server.setEnabled(not connected)
        self.btn_add_server.setEnabled(not connected)
        self.btn_del_server.setEnabled(not connected)
        self.input_model.setEnabled(not connected)
        self.input_mock_lat.setEnabled(not connected)
        self.input_mock_lon.setEnabled(not connected)
        self.input_mock_alt.setEnabled(not connected)

        if connected:
            self.lbl_hint.setText("已连接到服务器")
            self.lbl_hint.setStyleSheet("color: green; font-size: 11px; padding-top: 5px;")
        else:
            self.lbl_hint.setText("先启动 AFS4 再点击 Connect")
            self.lbl_hint.setStyleSheet("color: gray; font-size: 11px; padding-top: 5px;")

    def load_settings(self, settings: dict):
        """加载保存的配置"""
        self.input_callsign.setText(settings.get("callsign", ""))
        self.input_cid.setText(settings.get("cid", ""))
        self.input_realname.setText(settings.get("realname", ""))
        self.input_model.setText(settings.get("model", ""))

        # 飞行员等级
        saved_rating = settings.get("rating", 1)
        for i in range(self.combo_rating.count()):
            if self.combo_rating.itemData(i) == saved_rating:
                self.combo_rating.setCurrentIndex(i)
                break

        # Mock 位置
        self.input_mock_lat.setText(str(settings.get("mock_lat", "")))
        self.input_mock_lon.setText(str(settings.get("mock_lon", "")))
        self.input_mock_alt.setText(str(settings.get("mock_alt", "")))

        # 恢复服务器列表
        saved_servers = settings.get("servers")
        self._set_servers(saved_servers if saved_servers else DEFAULT_SERVERS)

        # 选中上次的服务器
        server = settings.get("server", "")
        port = settings.get("port", 6809)
        if server:
            full = f"{server}:{port}"
            idx = self.combo_server.findText(full)
            if idx >= 0:
                self.combo_server.setCurrentIndex(idx)
            else:
                self.combo_server.setCurrentText(full)
