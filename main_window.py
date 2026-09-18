# -*- coding: utf-8 -*-
"""
Aerofly Link 主窗口类
=====================
整合：连接页（首页） → 工作区（侧边栏）
两阶段布局：连接前只显示连接页，连接成功后才解锁操作面板。

重构要点：
  - AsyncWorker → core/async_worker.py
  - 诊断日志 → core/diag_logger.py
  - 状态栏 → ui/status_bar.py
  - 工作区侧边栏 → ui/workspace.py
"""
import json
import asyncio
import os
from pathlib import Path
from typing import Optional

from core.diag_logger import diag as _diag
from core.async_worker import AsyncWorker
from core.dll_bridge import DLLBridge, Telemetry
from core.transponder_controller import TransponderController
from core.mock_server import MockServer
from core.fsd_protocol import distance_nm

from PyQt6.QtWidgets import (
    QMainWindow, QWidget, QHBoxLayout,
    QMessageBox, QStackedWidget
)
from PyQt6.QtCore import QTimer, pyqtSignal


class MainWindow(QMainWindow):
    """
    主窗口 — 两阶段布局
    Page 0: ConnectPage（宽敞连接表单）
    Page 1: Workspace（侧边栏面板 + 地图）
    """

    # === 信号定义 ===
    telemetry_updated = pyqtSignal(dict)
    connection_status_changed = pyqtSignal(str, str)
    atc_message_received = pyqtSignal(str, str, str)
    traffic_updated = pyqtSignal(list)
    transponder_status_changed = pyqtSignal(str, str, bool)

    def __init__(self):
        super().__init__()

        self.setWindowTitle("Aerofly Link - Aerofly FS 4 联机客户端")
        # 灵活最小尺寸：不再卡死 1200x800
        self.setMinimumSize(300, 480)

        # === 初始化核心模块 ===
        self.dll_bridge = DLLBridge()
        self.transponder = TransponderController(self.dll_bridge)
        self.transponder.on_state_change = self._on_transponder_state_change
        self.fsd_client = None
        self._fsd_generation = 0
        self._user_disconnect = False

        # === Mock 服务器 ===
        self._mock_server: Optional[MockServer] = None
        self._mock_enabled = False

        # === 初始化 UI ===
        self._init_ui()
        self._init_timers()
        self._connect_signals()

        # === 启动后台异步线程 ===
        self._async_worker = AsyncWorker(self)
        self._async_worker.start()

        # === 连接 DLL ===
        AsyncWorker.run_async(self.dll_bridge.connect())

        # === 加载用户配置 ===
        self._load_settings()

    # ──────────────────────────────────────────────
    # 界面初始化
    # ──────────────────────────────────────────────

    def _init_ui(self):
        """初始化布局 — QStackedWidget 占据全部空间，无地图。"""
        central = QWidget()
        self.setCentralWidget(central)
        outer = QHBoxLayout(central)
        outer.setContentsMargins(0, 0, 0, 0)
        outer.setSpacing(0)

        # QStackedWidget（两页切换，填满窗口）
        self.left_stack = QStackedWidget()

        # ── Page 0: 连接页 ──
        from ui.connect_page import ConnectPage
        self.connect_page = ConnectPage()
        self.left_stack.addWidget(self.connect_page)  # index 0

        # ── Page 1: 工作侧边栏 ──
        from ui.workspace import WorkspaceSidebar
        self.workspace = WorkspaceSidebar()
        self.left_stack.addWidget(self.workspace)  # index 1

        self.left_stack.setCurrentIndex(0)

        outer.addWidget(self.left_stack)

        # === 底部状态栏 ===
        from ui.status_bar import StatusBar
        self.status_bar = StatusBar()
        self.status_bar.btn_mock.clicked.connect(self._toggle_mock_server)
        self.setStatusBar(self.status_bar)

    def _init_timers(self):
        """初始化所有定时器"""
        # 4Hz 轮询足够驱动状态栏文字显示（遥测本身在 DLL 接收线程持续更新）
        self.telemetry_timer = QTimer(self)
        self.telemetry_timer.timeout.connect(self._read_telemetry)
        self.telemetry_timer.start(250)

        self.position_report_timer = QTimer(self)
        self.position_report_timer.timeout.connect(self._send_position_report)

        self.sync_check_timer = QTimer(self)
        self.sync_check_timer.timeout.connect(self._check_transponder_sync)

        self.dll_health_timer = QTimer(self)
        self.dll_health_timer.timeout.connect(self._check_dll_health)
        self.dll_health_timer.start(1000)

    # ──────────────────────────────────────────────
    # 信号连接
    # ──────────────────────────────────────────────

    def _connect_signals(self):
        """连接所有信号槽"""
        cp = self.connect_page
        ws = self.workspace

        # 连接页信号
        cp.connect_clicked.connect(self._on_connect)
        cp.disconnect_clicked.connect(self._on_disconnect)

        # 工作区侧边栏信号
        ws.btn_disconnect.clicked.connect(self._on_disconnect)

        # 应答机面板信号
        ws.transponder_panel.mode_changed.connect(self._on_xpdr_mode_change)
        ws.transponder_panel.code_changed.connect(self._on_xpdr_code_change)
        ws.transponder_panel.ident_clicked.connect(self._on_ident)

        # 飞行计划面板信号
        ws.flightplan_panel.flight_plan_submitted.connect(self._on_flight_plan_submit)

        # 通讯日志面板信号
        ws.log_panel.message_send_requested.connect(self._on_send_text_message)

        # Callsign + Realname 同步到飞行计划面板
        cp.input_callsign_widget.textChanged.connect(
            ws.flightplan_panel.set_callsign
        )
        cp.input_realname_widget.textChanged.connect(
            ws.flightplan_panel.set_pilot_name
        )

        # 自身信号
        self.telemetry_updated.connect(self._on_telemetry_update)
        self.connection_status_changed.connect(self._on_connection_status_change)
        self.atc_message_received.connect(self._on_atc_message)
        self.traffic_updated.connect(self._on_traffic_update)
        self.transponder_status_changed.connect(self._on_transponder_status_change)

    # ──────────────────────────────────────────────
    # 连接管理
    # ──────────────────────────────────────────────

    def _on_connect(self):
        """用户点击 Connect"""
        config = self.connect_page.get_config()

        if not config["callsign"] or not config["cid"]:
            QMessageBox.warning(self, "配置不完整", "请填写呼号和 CID")
            return
        if not config["server"]:
            QMessageBox.warning(self, "配置不完整", "请填写服务器地址")
            return

        self._user_disconnect = False
        self._save_settings()

        self._fsd_generation += 1
        gen = self._fsd_generation

        from core.fsd_client import FSDClient
        try:
            config["init_lat"] = float(config.get("mock_lat") or "51.4775")
            config["init_lon"] = float(config.get("mock_lon") or "-0.4614")
        except ValueError:
            config["init_lat"] = 51.4775
            config["init_lon"] = -0.4614
        try:
            config["init_alt"] = float(config.get("mock_alt") or "3500")
        except ValueError:
            config["init_alt"] = 3500.0

        self.fsd_client = FSDClient(config)
        self.fsd_client.message_received.connect(self.atc_message_received.emit)
        self.fsd_client.traffic_updated.connect(self.traffic_updated.emit)
        self.fsd_client.status_changed.connect(
            lambda s, m, g=gen: self._on_connection_status_change_if_current(s, m, g)
        )
        # 协议原始报文只在显式开启调试时显示（发布版默认安静，不向界面暴露协议细节）
        if os.environ.get("AEROFLYLINK_DEBUG"):
            self.fsd_client.debug_line.connect(
                lambda line: self.workspace.log_panel.add_message("FSD", "DEBUG", line, kind="system")
            )

        self.transponder.fsd_client = self.fsd_client
        AsyncWorker.run_async(self.fsd_client.connect())

        self.connect_page.set_connecting(True)

        self.position_report_timer.start(1000)
        self.sync_check_timer.start(5000)

        self.workspace.flightplan_panel.set_callsign(config.get("callsign", ""))
        self.workspace.flightplan_panel.set_pilot_name(config.get("realname", ""))

        self.status_bar.set_connection_status("● 连接中...", "orange")

    def _on_disconnect(self):
        """用户点击 Disconnect"""
        self._user_disconnect = True
        self._fsd_generation += 1

        if self.fsd_client:
            AsyncWorker.run_async(self.fsd_client.disconnect())
            self.fsd_client = None
            self.transponder.fsd_client = None

        self.position_report_timer.stop()
        self.sync_check_timer.stop()

        self.workspace.flightplan_panel.reset_fields()
        self.connect_page.set_connected(False)
        self.connect_page.set_connecting(False)

        self.left_stack.setCurrentIndex(0)
        self.status_bar.set_connection_status("● 未连接", "gray")
        self.status_bar.reset_flight_data()
        self.status_bar.reset_callsign()

    def _cleanup_fsd_connection(self):
        if self.fsd_client:
            self.fsd_client = None
            self.transponder.fsd_client = None
        self.position_report_timer.stop()
        self.sync_check_timer.stop()

    # ──────────────────────────────────────────────
    # 定时器回调
    # ──────────────────────────────────────────────

    def _read_telemetry(self):
        try:
            t = self.dll_bridge.latest_telemetry
            if not t and self.fsd_client:
                t = Telemetry(
                    lat=self.fsd_client._init_lat,
                    lon=self.fsd_client._init_lon,
                    alt_m=self.fsd_client._init_alt_m,
                    hdg_true=0.0, gs_kts=0.0, on_ground=True,
                )
            if t:
                data = {
                    "lat": t.lat, "lon": t.lon, "alt_m": t.alt_m,
                    "hdg_true": t.hdg_true, "gs_kts": t.gs_kts,
                }
                # 数据无变化时跳过信号发射（停靠/未连接时避免空转刷新）
                if data != getattr(self, "_last_telemetry_data", None):
                    self._last_telemetry_data = data
                    self.telemetry_updated.emit(data)
        except Exception as e:
            _diag(f"_read_telemetry ERROR: {e}")

    def _send_position_report(self):
        if not self.fsd_client:
            return
        t = self.dll_bridge.latest_telemetry
        if not t:
            t = Telemetry(
                lat=self.fsd_client._init_lat,
                lon=self.fsd_client._init_lon,
                alt_m=self.fsd_client._init_alt_m,
                hdg_true=0.0, gs_kts=0.0, on_ground=True,
            )
        xpdr_code = self.transponder.get_current_xpdr_for_ap()
        if xpdr_code == "0000":
            return
        AsyncWorker.run_async(
            self.fsd_client.send_position_report(
                lat=t.lat, lon=t.lon, alt_ft=t.alt_m * 3.28084,
                gs_kts=t.gs_kts, hdg=t.hdg_true,
                xpdr=xpdr_code, xpdr_mode=self.transponder.virtual_mode.value
            )
        )

    def _check_transponder_sync(self):
        AsyncWorker.run_async(self._do_sync_check())

    async def _do_sync_check(self):
        result = await self.transponder.sync_check()
        if not result.get("synced", True):
            for warning in result.get("warnings", []):
                # 去重：同一警告持续存在时不重复弹（面板 5s 自动隐藏，重复弹等于常驻骚扰）
                if warning != getattr(self, "_last_sync_warning", None):
                    self._last_sync_warning = warning
                    self.workspace.transponder_panel.show_warning(warning)
        else:
            self._last_sync_warning = None

    def _check_dll_health(self):
        """刷新状态栏 DLL 状态（完全依据 dll_bridge 的连接状态，不做任何阻塞探测）。"""
        conn = self.dll_bridge.is_telemetry_connected
        if conn:
            if self._mock_enabled:
                self.status_bar.set_dll_status(
                    "Mock: ● 已连接", "#4CAF50",
                    f"模拟 DLL 模式 — 应答机: {self.transponder.mode.value} {self.transponder.squawk}"
                )
            elif self.dll_bridge.latest_telemetry is None:
                self.status_bar.set_dll_status("DLL: ⚠ 无数据", "orange",
                    "DLL 端口已连接，但未收到遥测数据\n可能：AF4 飞机未初始化 / 非正版 DLL\n建议：重启 AF4 / 重新进入驾驶舱 / 点击「模拟DLL」")
            else:
                self.status_bar.set_dll_status("DLL: ● 已连接", "green",
                    f"AeroflyBridge.dll 正常\n应答机: {self.transponder.mode.value} {self.transponder.squawk}")
        elif self._mock_enabled and self._mock_server is not None:
            self.status_bar.set_dll_status("Mock: ○ 启动中...", "orange", "模拟 DLL 服务器正在启动...")
        else:
            # dll_bridge 内部维护自动重连（指数退避），无需在 UI 线程探测端口
            self.status_bar.set_dll_status(
                "DLL: ○ 等待游戏", "gray",
                "尚未连接遥测端口 12345 — dll_bridge 正在后台自动重连\n"
                "请确认：\n"
                "1. 启动 Aerofly FS 4 并进入驾驶舱\n"
                "2. AeroflyBridge.dll 已放入 external_dll 文件夹\n"
                "3. 非正版游戏可能不支持 external DLL API\n"
                "提示: 先启动游戏再启动 Aerofly Link（连接后状态自动刷新）"
            )

    def _toggle_mock_server(self, checked: bool):
        if checked:
            self._start_mock_server()
        else:
            self._stop_mock_server()

    def _start_mock_server(self):
        self._mock_enabled = True
        self.status_bar.btn_mock.setChecked(True)
        config = self.connect_page.get_config()
        try:
            mock_lat = float(config.get("mock_lat") or "51.4775")
        except ValueError:
            mock_lat = 51.4775
        try:
            mock_lon = float(config.get("mock_lon") or "-0.4614")
        except ValueError:
            mock_lon = -0.4614
        try:
            mock_alt = float(config.get("mock_alt") or "3500")
        except ValueError:
            mock_alt = 3500.0

        async def _start():
            try:
                if self.dll_bridge.is_connected:
                    await self.dll_bridge.disconnect()
                    await asyncio.sleep(0.5)
                self._mock_server = MockServer(
                    center_lat=mock_lat, center_lon=mock_lon, alt_m=mock_alt
                )
                ok = await self._mock_server.start()
                if ok:
                    await self.dll_bridge.connect()
                else:
                    self._mock_enabled = False
                    self.status_bar.btn_mock.setChecked(False)
                    if not self.dll_bridge.is_connected:
                        AsyncWorker.run_async(self.dll_bridge.connect())
            except Exception as e:
                _diag(f"mock: start exception: {e}")
                self._mock_enabled = False
                self.status_bar.btn_mock.setChecked(False)
                if not self.dll_bridge.is_connected:
                    AsyncWorker.run_async(self.dll_bridge.connect())

        AsyncWorker.run_async(_start())

    def _stop_mock_server(self):
        self._mock_enabled = False
        self.status_bar.btn_mock.setChecked(False)

        async def _stop():
            if self._mock_server:
                if self.dll_bridge.is_connected:
                    await self.dll_bridge.disconnect()
                await self._mock_server.stop()
                self._mock_server = None
                AsyncWorker.run_async(self.dll_bridge.connect())

        AsyncWorker.run_async(_stop())

    # ──────────────────────────────────────────────
    # 信号槽处理
    # ──────────────────────────────────────────────

    def _on_telemetry_update(self, data: dict):
        alt_ft = data.get("alt_m", 0) * 3.28084
        gs = data.get("gs_kts", 0)
        self.status_bar.set_flight_data(f"高度: {alt_ft:.0f}ft  地速: {gs:.0f}kts")

    def _on_connection_status_change_if_current(self, status: str, message: str, generation: int):
        if generation != self._fsd_generation:
            return
        self._on_connection_status_change(status, message)

    def _on_connection_status_change(self, status: str, message: str):
        ws = self.workspace
        if status == "connected":
            self.status_bar.set_connection_status("● 已连接", "green")
            ws.set_connected_display(True, self.connect_page.get_config().get("server", ""))
            self.status_bar.set_callsign(f"呼号: {self.connect_page.get_config().get('callsign', '---')}")
            self.connect_page.set_connected(True)
            self.left_stack.setCurrentIndex(1)
            ws.log_panel.add_message("Aerofly Link", "SYSTEM", message, kind="system")

        elif status == "disconnected":
            self.status_bar.set_connection_status("● 未连接", "gray")
            self.status_bar.reset_flight_data()
            self.status_bar.reset_callsign()
            self._cleanup_fsd_connection()
            self.connect_page.set_connected(False)
            self.connect_page.set_connecting(False)
            ws.set_connected_display(False)
            self.left_stack.setCurrentIndex(0)
            ws.log_panel.add_message("Aerofly Link", "SYSTEM", message, kind="system")
            if not self._user_disconnect:
                QMessageBox.warning(self, "连接已断开", f"与服务器的连接意外断开：\n\n{message}")
            self._user_disconnect = False

        elif status == "error":
            self.status_bar.set_connection_status("● 错误", "red")
            self.status_bar.reset_flight_data()
            self.status_bar.reset_callsign()
            self._cleanup_fsd_connection()
            self.connect_page.set_connected(False)
            self.connect_page.set_connecting(False)
            ws.set_connected_display(False)
            self.left_stack.setCurrentIndex(0)
            ws.log_panel.add_message("FSD", "ERROR", message, kind="system")
            QMessageBox.critical(self, "连接失败", f"无法连接到服务器：\n\n{message}")

        elif status == "connecting":
            self.status_bar.set_connection_status("● 连接中...", "orange")
            ws.log_panel.add_message("Aerofly Link", "SYSTEM", message, kind="system")

    def _on_atc_message(self, source: str, dest: str, message: str):
        self.workspace.log_panel.add_message(source, dest, message, kind="in")

    def _on_send_text_message(self, text: str):
        """用户在通讯日志面板发送消息。支持「@目标 消息」语法，缺省目标为 UNICOM。"""
        if not self.fsd_client:
            self.workspace.log_panel.add_message(
                "Aerofly Link", "SYSTEM", "未连接到服务器，无法发送消息", kind="system")
            return
        text = text.strip()
        if text.startswith("@"):
            body = text[1:]
            dest, _, content = body.partition(" ")
            if not content:
                dest, content = "UNICOM", body
        else:
            dest, content = "UNICOM", text
        AsyncWorker.run_async(self._do_send_text_message(dest, content))

    async def _do_send_text_message(self, dest: str, content: str):
        try:
            ok = await self.fsd_client.send_text_message(dest, content)
            if ok:
                self.workspace.log_panel.add_message("me", dest, content, kind="out")
            else:
                self.workspace.log_panel.add_message(
                    "Aerofly Link", "SYSTEM", "消息发送失败", kind="system")
        except Exception as e:
            self.workspace.log_panel.add_message(
                "Aerofly Link", "SYSTEM", f"消息发送失败: {e}", kind="system")

    def _on_traffic_update(self, traffic: list):
        own = self.dll_bridge.latest_telemetry
        if not own or (own.lat == 0.0 and own.lon == 0.0):
            return
        nearby = 0
        for ac in traffic:
            try:
                if distance_nm(own.lat, own.lon, float(ac["lat"]), float(ac["lon"])) < 10:
                    nearby += 1
            except (KeyError, TypeError, ValueError):
                continue
        # 仅在数量变化时提示，避免高频重置状态栏消息计时器
        if nearby != getattr(self, "_last_nearby_count", -1):
            self._last_nearby_count = nearby
            if nearby:
                self.statusBar().showMessage(f"附近 {nearby} 架飞机", 3000)

    def _on_transponder_status_change(self, mode: str, code: str, ident: bool):
        ident_str = " IDENT" if ident else ""
        self.status_bar.set_xpdr_status(f"应答机: {mode} {code}{ident_str}")
        self.workspace.transponder_panel.update_display(mode, code, ident)

    def _on_xpdr_mode_change(self, mode: str):
        AsyncWorker.run_async(self.transponder.set_mode(mode))

    def _on_xpdr_code_change(self, code: str):
        AsyncWorker.run_async(self.transponder.set_squawk(code))

    def _on_ident(self):
        AsyncWorker.run_async(self.transponder.trigger_ident())

    def _on_transponder_state_change(self, state: dict):
        mode = state.get("mode", "STBY")
        code = state.get("squawk", "7000")
        ident = state.get("ident_active", False)
        self.transponder_status_changed.emit(mode, code, ident)

    def _on_flight_plan_submit(self, plan: dict):
        if not self.fsd_client:
            self.workspace.flightplan_panel.on_submit_result(False, "未连接到服务器")
            return
        if not plan.get("callsign"):
            plan["callsign"] = self.connect_page.get_config().get("callsign", "")
        if not plan.get("pilot"):
            plan["pilot"] = self.connect_page.get_config().get("realname", "")
        AsyncWorker.run_async(self._do_submit_flight_plan(plan))

    async def _do_submit_flight_plan(self, plan: dict):
        try:
            success = await self.fsd_client.send_flight_plan(plan)
            self.workspace.flightplan_panel.on_submit_result(success, "" if success else "发送失败")
        except Exception as e:
            self.workspace.flightplan_panel.on_submit_result(False, str(e))

    # ──────────────────────────────────────────────
    # 配置持久化
    # ──────────────────────────────────────────────

    def _load_settings(self):
        from core.resource_utils import get_config_path
        config_path = get_config_path()
        if config_path.exists():
            try:
                with open(config_path, "r", encoding="utf-8") as f:
                    settings = json.load(f)
                self.connect_page.load_settings(settings)
                self.workspace.flightplan_panel.load_settings(settings)
                self.workspace.flightplan_panel.set_callsign(settings.get("callsign", ""))
                self.workspace.flightplan_panel.set_pilot_name(settings.get("realname", ""))
            except (json.JSONDecodeError, IOError):
                pass

    def _save_settings(self):
        from core.resource_utils import get_config_path
        settings = self.connect_page.get_config()
        # 安全策略：密码只在本次运行内使用，绝不写入磁盘（见 docs/adr/0003-credential-handling.md）
        settings.pop("password", None)
        fp = self.workspace.flightplan_panel.get_flight_plan()
        settings.update({
            "aircraft": fp.get("aircraft", ""),
            "wake_category": fp.get("wake_category", "Medium"),
            "tas": fp.get("tas", ""),
            "dep_airport": fp.get("dep_airport", ""),
            "dest_airport": fp.get("dest_airport", ""),
            "alt_airport": fp.get("alt_airport", ""),
            "cruise_alt": fp.get("cruise_alt", ""),
            "route": fp.get("route", ""),
            "remarks": fp.get("remarks", ""),
            "eet": fp.get("eet", ""),
            "endurance": fp.get("endurance", ""),
        })
        config_path = get_config_path()
        with open(config_path, "w", encoding="utf-8") as f:
            json.dump(settings, f, ensure_ascii=False, indent=2)

    async def _graceful_shutdown(self):
        """在后台事件循环内执行的停机序列（供 closeEvent 同步等待）。"""
        self._mock_enabled = False
        if self._mock_server:
            if self.dll_bridge.is_connected:
                await self.dll_bridge.disconnect()
            await self._mock_server.stop()
            self._mock_server = None
        if self.fsd_client:
            await self.fsd_client.disconnect()
            self.fsd_client = None
            self.transponder.fsd_client = None
        # 最后断开 DLL 遥测（停止接收循环，避免退出时 executor 已关闭的报错）
        await self.dll_bridge.disconnect()

    def closeEvent(self, event):
        self._save_settings()
        worker = self._async_worker
        loop = worker._loop
        if loop and loop.is_running():
            # 同步等待停机序列完成（上限 3 秒），确保后台任务先于事件循环退出
            try:
                asyncio.run_coroutine_threadsafe(
                    self._graceful_shutdown(), loop).result(timeout=3.0)
            except Exception as e:
                _diag(f"shutdown: graceful shutdown incomplete: {e}")
        worker.stop()
        event.accept()
