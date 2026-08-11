# -*- coding: utf-8 -*-
"""
连接页面 — 应用启动时的首页
简洁表单，字段随窗口大小自适应缩放（类似 Swift）
"""
from PyQt6.QtWidgets import (
    QWidget, QVBoxLayout, QHBoxLayout,
    QLineEdit, QComboBox, QPushButton, QLabel,
    QInputDialog, QMessageBox, QFrame, QScrollArea
)
from PyQt6.QtCore import pyqtSignal, Qt

from ui.styles import (
    INPUT_CSS_LARGE as _INPUT_CSS,
    COMBO_CSS_LARGE as _COMBO_CSS,
    LABEL_CSS as _LABEL_CSS,
    BTN_SMALL_CSS as _BTN_SMALL_CSS,
    BTN_PRIMARY_CSS,
    BTN_DANGER_CSS,
)

# 按 Eco. 区分的服务器预设（对应 Swift 中的服务器环境）
SERVERS_BY_ECO = {
    "vatsim": [
        "cert.vatsim.net",
        "usa-s1.vatsim.net",
        "europe-s1.vatsim.net",
        "asia-s1.vatsim.net",
    ],
    "private": [
        "127.0.0.1:6809",
    ],
    "legacy": [
        "127.0.0.1:6809",
    ],
}

ECO_LABELS = {
    "vatsim": "VATSIM",
    "private": "FSD (private)",
    "legacy": "FSD (legacy)",
}

TYPE_LABELS = {
    "vatsim": "FSD [VATSIM]",
    "legacy": "FSD (Legacy)",
}


class ConnectPage(QFrame):
    """连接页 — 简洁表单，字段随窗口自适应"""

    connect_clicked = pyqtSignal()
    disconnect_clicked = pyqtSignal()

    def __init__(self, parent=None):
        super().__init__(parent)
        self.setObjectName("connectPage")
        self.setStyleSheet("""
            #connectPage {
                background-color: #1e1e1e;
                border: none;
            }
        """)
        self._connected = False
        self._servers_by_eco = {
            eco: list(items) for eco, items in SERVERS_BY_ECO.items()
        }
        self._init_ui()

    def _init_ui(self):
        outer = QVBoxLayout(self)
        outer.setContentsMargins(0, 0, 0, 0)
        outer.setSpacing(0)

        # 顶部标题
        header = QVBoxLayout()
        header.setContentsMargins(0, 12, 0, 8)
        header.setSpacing(2)

        title = QLabel("Aerofly Link")
        title.setAlignment(Qt.AlignmentFlag.AlignCenter)
        title.setStyleSheet(
            "color: #4CAF50; font-size: 18px; font-weight: bold; letter-spacing: 1px;"
        )
        header.addWidget(title)

        subtitle = QLabel("Aerofly FS 4 联机客户端")
        subtitle.setAlignment(Qt.AlignmentFlag.AlignCenter)
        subtitle.setStyleSheet("color: #888; font-size: 10px;")
        header.addWidget(subtitle)

        outer.addLayout(header)

        # 表单区 — QScrollArea 包裹，窗口过短时可滚动
        scroll = QScrollArea()
        scroll.setWidgetResizable(True)
        scroll.setFrameShape(QFrame.Shape.NoFrame)
        scroll.setStyleSheet("QScrollArea { background-color: transparent; border: none; }")

        form_widget = QWidget()
        form_widget.setStyleSheet("background-color: transparent;")
        form_layout = QVBoxLayout(form_widget)
        form_layout.setContentsMargins(32, 8, 32, 16)
        form_layout.setSpacing(14)

        # 表单标题
        form_title = QLabel("连接服务器")
        form_title.setStyleSheet(
            "color: #e0e0e0; font-size: 17px; font-weight: bold; padding-bottom: 6px;"
        )
        form_layout.addWidget(form_title)

        def add_field(label_text, widget):
            lbl = QLabel(label_text)
            lbl.setStyleSheet(_LABEL_CSS)
            form_layout.addWidget(lbl)
            if isinstance(widget, QLineEdit):
                widget.setStyleSheet(_INPUT_CSS)
            else:
                widget.setStyleSheet(_COMBO_CSS)
            form_layout.addWidget(widget)

        # 呼号
        self.input_callsign = QLineEdit()
        self.input_callsign.setPlaceholderText("如: CES2101")
        self.input_callsign.setMaxLength(7)
        add_field("呼号", self.input_callsign)

        # CID
        self.input_cid = QLineEdit()
        self.input_cid.setPlaceholderText("如: 1234567")
        self.input_cid.setMaxLength(10)
        add_field("CID", self.input_cid)

        # 密码
        self.input_password = QLineEdit()
        self.input_password.setEchoMode(QLineEdit.EchoMode.Password)
        self.input_password.setPlaceholderText("输入密码")
        add_field("密码", self.input_password)

        # 姓名
        self.input_realname = QLineEdit()
        self.input_realname.setPlaceholderText("真实姓名（用于飞行计划）")
        add_field("姓名", self.input_realname)

        # 机型
        self.input_model = QLineEdit()
        self.input_model.setPlaceholderText("如: B738, A320")
        self.input_model.setMaxLength(4)
        add_field("机型", self.input_model)

        # Eco. + Type 并排
        type_row = QHBoxLayout()
        type_row.setSpacing(12)

        eco_col = QVBoxLayout()
        eco_col.setSpacing(4)
        lbl_eco = QLabel("Eco.")
        lbl_eco.setStyleSheet(_LABEL_CSS)
        eco_col.addWidget(lbl_eco)
        self.combo_eco = QComboBox()
        self.combo_eco.setStyleSheet(_COMBO_CSS)
        self.combo_eco.addItem(ECO_LABELS["vatsim"], "vatsim")
        self.combo_eco.addItem(ECO_LABELS["private"], "private")
        self.combo_eco.addItem(ECO_LABELS["legacy"], "legacy")
        self.combo_eco.currentIndexChanged.connect(self._on_eco_changed)
        eco_col.addWidget(self.combo_eco)
        type_row.addLayout(eco_col, 1)

        type_col = QVBoxLayout()
        type_col.setSpacing(4)
        lbl_type = QLabel("Type")
        lbl_type.setStyleSheet(_LABEL_CSS)
        type_col.addWidget(lbl_type)
        self.combo_type = QComboBox()
        self.combo_type.setStyleSheet(_COMBO_CSS)
        self.combo_type.addItem(TYPE_LABELS["vatsim"], "vatsim")
        self.combo_type.addItem(TYPE_LABELS["legacy"], "legacy")
        type_col.addWidget(self.combo_type)
        type_row.addLayout(type_col, 1)

        form_layout.addLayout(type_row)

        # 服务器
        lbl_srv = QLabel("服务器")
        lbl_srv.setStyleSheet(_LABEL_CSS)
        form_layout.addWidget(lbl_srv)

        server_row = QHBoxLayout()
        server_row.setSpacing(8)

        self.combo_server = QComboBox()
        self.combo_server.setEditable(True)
        self.combo_server.setStyleSheet(_COMBO_CSS)
        server_row.addWidget(self.combo_server, 1)

        self.btn_add = QPushButton("+")
        self.btn_add.setToolTip("添加服务器")
        self.btn_add.setStyleSheet(_BTN_SMALL_CSS)
        self.btn_add.clicked.connect(self._on_add_server)
        server_row.addWidget(self.btn_add)

        self.btn_del = QPushButton("\u2212")
        self.btn_del.setToolTip("删除当前选中的服务器")
        self.btn_del.setStyleSheet(_BTN_SMALL_CSS)
        self.btn_del.clicked.connect(self._on_del_server)
        server_row.addWidget(self.btn_del)

        form_layout.addLayout(server_row)

        # 连接 / 断开 按钮
        self.btn_connect = QPushButton("连 接 服 务 器")
        self.btn_connect.setStyleSheet(BTN_PRIMARY_CSS)
        self.btn_connect.clicked.connect(self._on_connect)
        form_layout.addWidget(self.btn_connect)

        self.btn_disconnect = QPushButton("断 开 连 接")
        self.btn_disconnect.setStyleSheet(BTN_DANGER_CSS)
        self.btn_disconnect.clicked.connect(self._on_disconnect)
        self.btn_disconnect.hide()
        form_layout.addWidget(self.btn_disconnect)

        # 状态提示
        self.lbl_status = QLabel("")
        self.lbl_status.setAlignment(Qt.AlignmentFlag.AlignCenter)
        self.lbl_status.setStyleSheet("color: #888; font-size: 13px;")
        self.lbl_status.setWordWrap(True)
        form_layout.addWidget(self.lbl_status)

        form_layout.addStretch()

        scroll.setWidget(form_widget)
        outer.addWidget(scroll, 1)

        # 底部提示
        hint = QLabel("请确保已在 Documents\\Aerofly FS 4\\external_dll\\ 放置 AeroflyBridge.dll")
        hint.setAlignment(Qt.AlignmentFlag.AlignCenter)
        hint.setStyleSheet("color: #555; font-size: 12px; padding: 16px;")
        outer.addWidget(hint)

    # ── Eco. 切换 ─────────────────────────────────────

    def _on_eco_changed(self):
        """切换 Eco. 时更新服务器列表，并设置合理的默认 Type"""
        eco = self.combo_eco.currentData()
        current_server = self.combo_server.currentText()

        servers = self._servers_by_eco.get(eco, [])
        self.combo_server.clear()
        self.combo_server.addItems(servers)

        idx = self.combo_server.findText(current_server)
        if idx >= 0:
            self.combo_server.setCurrentIndex(idx)

        # 根据 Eco. 联动 Type：VATSIM/legacy 强制匹配，private 保持用户选择
        if eco == "vatsim":
            self._set_type("vatsim")
            self.combo_type.setEnabled(False)
        elif eco == "legacy":
            self._set_type("legacy")
            self.combo_type.setEnabled(False)
        else:  # private
            self.combo_type.setEnabled(True)
            if self.combo_type.currentData() == "legacy":
                self._set_type("vatsim")

    def _set_type(self, type_key: str):
        for i in range(self.combo_type.count()):
            if self.combo_type.itemData(i) == type_key:
                self.combo_type.setCurrentIndex(i)
                return

    # ── 服务器增删 ──────────────────────────────────────

    def _on_add_server(self):
        text, ok = QInputDialog.getText(
            self, "添加服务器",
            "请输入服务器地址（如 host:port 或 host）:"
        )
        if ok and text.strip():
            server = text.strip()
            for i in range(self.combo_server.count()):
                if self.combo_server.itemText(i) == server:
                    self.combo_server.setCurrentIndex(i)
                    return
            self.combo_server.addItem(server)
            self.combo_server.setCurrentText(server)
            self._save_current_servers()

    def _on_del_server(self):
        if self.combo_server.count() <= 1:
            QMessageBox.warning(self, "无法删除", "至少保留一个服务器地址。")
            return
        idx = self.combo_server.currentIndex()
        name = self.combo_server.currentText()
        reply = QMessageBox.question(
            self, "确认删除",
            f"确定要删除服务器 \"{name}\" 吗？",
            QMessageBox.StandardButton.Yes | QMessageBox.StandardButton.No
        )
        if reply == QMessageBox.StandardButton.Yes:
            self.combo_server.removeItem(idx)
            self._save_current_servers()

    def _save_current_servers(self):
        servers = [self.combo_server.itemText(i) for i in range(self.combo_server.count())]
        eco = self.combo_eco.currentData()
        self._servers_by_eco[eco] = servers

    def _get_servers(self) -> dict:
        return {eco: list(items) for eco, items in self._servers_by_eco.items()}

    def _set_servers(self, servers):
        if isinstance(servers, dict):
            if any(k in servers for k in ("vatsim", "private", "legacy")):
                for eco in ("vatsim", "private", "legacy"):
                    if eco in servers:
                        self._servers_by_eco[eco] = list(servers[eco])
            elif "legacy" in servers or "vatsim" in servers:
                if "legacy" in servers:
                    self._servers_by_eco["legacy"] = list(servers["legacy"])
                    self._servers_by_eco["private"] = list(servers["legacy"])
                if "vatsim" in servers:
                    self._servers_by_eco["vatsim"] = list(servers["vatsim"])
        elif isinstance(servers, list):
            self._servers_by_eco["private"] = list(servers)
            self._servers_by_eco["legacy"] = list(servers)

    def _populate_server_combo(self):
        eco = self.combo_eco.currentData()
        servers = self._servers_by_eco.get(eco, [])
        current = self.combo_server.currentText()
        self.combo_server.clear()
        self.combo_server.addItems(servers)
        idx = self.combo_server.findText(current)
        if idx >= 0:
            self.combo_server.setCurrentIndex(idx)

    # ── 连接/断开 ───────────────────────────────────────

    def _on_connect(self):
        callsign = self.input_callsign.text().strip()
        cid = self.input_cid.text().strip()
        if not callsign or not cid:
            QMessageBox.warning(self, "配置不完整", "请填写呼号和 CID")
            return
        self.connect_clicked.emit()

    def _on_disconnect(self):
        self.disconnect_clicked.emit()

    # ── 公共接口 ────────────────────────────────────────

    def get_config(self) -> dict:
        server_text = self.combo_server.currentText()
        if ":" in server_text:
            host, port_str = server_text.rsplit(":", 1)
            try:
                port = int(port_str)
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
            "server": host,
            "port": port,
            "eco": self.combo_eco.currentData(),
            "type": self.combo_type.currentData(),
            "model": self.input_model.text().upper().strip(),
            "servers": self._get_servers()
        }

    def set_connected(self, connected: bool):
        self._connected = connected
        self.btn_connect.setVisible(not connected)
        self.btn_disconnect.setVisible(connected)

        self.input_callsign.setEnabled(not connected)
        self.input_cid.setEnabled(not connected)
        self.input_password.setEnabled(not connected)
        self.input_realname.setEnabled(not connected)
        self.input_model.setEnabled(not connected)
        self.combo_server.setEnabled(not connected)
        self.combo_eco.setEnabled(not connected)
        self.combo_type.setEnabled(not connected and self.combo_eco.currentData() == "private")
        self.btn_add.setEnabled(not connected)
        self.btn_del.setEnabled(not connected)

        if connected:
            callsign = self.input_callsign.text().upper().strip()
            self.lbl_status.setText(f"● 已连接到服务器 — {callsign}")
            self.lbl_status.setStyleSheet("color: #4CAF50; font-size: 14px; padding-top: 8px;")
        else:
            self.lbl_status.setText("")
            self.lbl_status.setStyleSheet("color: #888; font-size: 13px; padding-top: 4px;")

    def set_connecting(self, connecting: bool):
        if connecting:
            self.btn_connect.setVisible(False)
            self.btn_disconnect.setVisible(True)
            self.btn_disconnect.setEnabled(False)

            self.input_callsign.setEnabled(False)
            self.input_cid.setEnabled(False)
            self.input_password.setEnabled(False)
            self.input_realname.setEnabled(False)
            self.input_model.setEnabled(False)
            self.combo_server.setEnabled(False)
            self.combo_eco.setEnabled(False)
            self.combo_type.setEnabled(False)
            self.btn_add.setEnabled(False)
            self.btn_del.setEnabled(False)

            self.lbl_status.setText("● 正在连接服务器...")
            self.lbl_status.setStyleSheet("color: orange; font-size: 14px; padding-top: 8px;")
        else:
            self.btn_disconnect.setEnabled(True)
            self.lbl_status.setText("")

    def show_error(self, message: str):
        self.lbl_status.setText(f"\u2715 {message}")
        self.lbl_status.setStyleSheet("color: #ff4444; font-size: 14px; padding-top: 8px;")

    def load_settings(self, settings: dict):
        self.input_callsign.setText(settings.get("callsign", ""))
        self.input_cid.setText(settings.get("cid", ""))
        self.input_realname.setText(settings.get("realname", ""))
        self.input_model.setText(settings.get("model", ""))

        saved_servers = settings.get("servers")
        self._set_servers(saved_servers if saved_servers else SERVERS_BY_ECO)

        eco = settings.get("eco")
        ctype = settings.get("type")

        if not eco and "mode" in settings:
            old_mode = settings.get("mode", "legacy")
            if old_mode == "vatsim":
                eco = "vatsim"
                ctype = "vatsim"
            else:
                eco = "private"
                ctype = "legacy"

        eco = eco or "private"
        ctype = ctype or ("vatsim" if eco == "vatsim" else "legacy")

        for i in range(self.combo_eco.count()):
            if self.combo_eco.itemData(i) == eco:
                self.combo_eco.setCurrentIndex(i)
                break

        self._on_eco_changed()
        self._set_type(ctype)
        self._populate_server_combo()

        server = settings.get("server", "")
        port = settings.get("port", 6809)
        if server:
            full = f"{server}:{port}"
            idx = self.combo_server.findText(full)
            if idx >= 0:
                self.combo_server.setCurrentIndex(idx)
            else:
                self.combo_server.setCurrentText(full)

    @property
    def input_realname_widget(self):
        return self.input_realname

    @property
    def input_callsign_widget(self):
        return self.input_callsign
