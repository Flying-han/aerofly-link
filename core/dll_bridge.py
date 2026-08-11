"""
DLL Bridge 客户端 — 对接 AeroflyBridge.dll
==============================================
与 AeroflyBridge.dll (jlgabriel/Aerofly-FS4-Bridge) 的 TCP 端口通信：
  - localhost:12345  遥测数据输出（DLL → 客户端，JSON 流，~50Hz）
  - localhost:12346  控制命令输入（客户端 → DLL，短连接请求/响应）

AeroflyBridge.dll 协议（v0.3.1+）:
  遥测格式：{"Aircraft.Altitude":1200.5, "Aircraft.Pitch":0.05, ...}（扁平命名）
  命令格式：{"variable":"Communication.TransponderCode", "value":7000}\n

本模块提供向后兼容的 send_command() 接口，内部将旧命令名
（set_xpdr_code 等）映射为 AeroflyBridge 的变量名和值。
"""

from __future__ import annotations

import asyncio
import json
import logging
import math
import os
import socket
import time
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any, Callable, Optional

logger = logging.getLogger("aerofly_link.dll_bridge")

# ── 诊断日志（使用共享模块）─────────────────────────────────────
from core.diag_logger import diag as _diag

# ── 默认端口 ────────────────────────────────────────────────────────
DEFAULT_TELEMETRY_PORT = 12345
DEFAULT_COMMAND_PORT = 12346
DEFAULT_HOST = "127.0.0.1"


# ── 航向单位/约定转换 ────────────────────────────────────────────────
# AeroflyBridge.dll 输出的航向为 **弧度、数学约定**（East=0, 逆时针, x=East,
# North=π/2）。这与 Aerofly TMD 仪器接口一致（参考 Aerofly wiki: "MagneticHeading
# in radiant ... East = 0.0 North = 1.57"），也是 AeroflyBridge 官方教程
# tutorial_python_maps_bridge.md 中消费方使用的约定。
#
# FSD / 航空航向为 **罗盘约定**（North=0, 顺时针）。
# 转换: 数学度 = rad * RAD_TO_DEG;  罗盘度 = (90 - 数学度) % 360
def aerofly_heading_to_compass(v: Any) -> float:
    """
    将 AeroflyBridge 输出的航向（弧度或度，数学约定 East=0 逆时针）
    转换为 FSD 罗盘航向（度, North=0 顺时针）。

    AeroflyBridge.dll 默认输出弧度；若某些构建输出已为度（|v|>6.5），
    则按度处理。两种情况下都先归一化为数学度，再翻转为罗盘度。
    """
    val = float(v)
    if abs(val) <= 6.5:
        math_deg = (val * RAD_TO_DEG) % 360.0      # 弧度 → 数学度
    else:
        math_deg = val % 360.0                       # 已为度（数学约定）
    return (90.0 - math_deg) % 360.0                 # 数学度 → 罗盘度


# ════════════════════════════════════════════════════════════════════
#  单位转换常量
# ════════════════════════════════════════════════════════════════════

RAD_TO_DEG = 180.0 / math.pi
MPS_TO_KTS = 1.94384
MPS_TO_FPM = 196.8504


# ════════════════════════════════════════════════════════════════════
#  AeroflyBridge 变量名 → Telemetry 字段映射 + 单位转换
#  值格式：(字段名, 转换函数 or None=直接赋值)
# ════════════════════════════════════════════════════════════════════

# 遥测变量名映射：(telemetry_field, transform_fn)
# transform_fn 接收 AeroflyBridge 原始值，返回 Telemetry 字段值
VAR_MAP: dict[str, tuple[str, Callable[[Any], Any]]] = {
    # ── 位置（AFS4 输出弧度 → 我们存度）──
    "Aircraft.Latitude":  ("lat", lambda v: float(v) * RAD_TO_DEG),
    # AFS4 经度为 0~2π 弧度 → 度，再归一化到 [-180, 180]
    "Aircraft.Longitude": ("lon", lambda v: (float(v) * RAD_TO_DEG + 180.0) % 360.0 - 180.0),
    "Aircraft.Altitude":  ("alt_m", lambda v: float(v)),             # 米
    "Aircraft.HeightAboveGround": ("agl_m", lambda v: float(v)),     # 米

    # ── 姿态与速度 ──
    # 航向: AeroflyBridge 输出弧度、数学约定(East=0 逆时针) → 转换为罗盘度(North=0 顺时针)
    "Aircraft.TrueHeading":      ("hdg_true", aerofly_heading_to_compass),
    "Aircraft.MagneticHeading":  ("hdg_mag",  aerofly_heading_to_compass),
    "Aircraft.IndicatedAirspeed":("ias_kts",  lambda v: float(v) * MPS_TO_KTS),
    "Aircraft.GroundSpeed":      ("gs_kts",   lambda v: float(v) * MPS_TO_KTS),
    "Aircraft.VerticalSpeed":    ("vs_fpm",   lambda v: float(v) * MPS_TO_FPM),
    "Aircraft.MachNumber":       ("mach",     lambda v: float(v)),
    "Aircraft.AngleOfAttack":    ("aoa_deg",  lambda v: float(v) * RAD_TO_DEG),
    "Aircraft.RateOfTurn":       ("rate_of_turn", lambda v: float(v)),

    # ── 无线电 ──
    "Communication.COM1Frequency": ("com1_freq", lambda v: int(float(v) / 1000)),
    "Communication.COM2Frequency": ("com2_freq", lambda v: int(float(v) / 1000)),
    "Communication.TransponderCode": ("xpdr_code", lambda v: str(int(float(v))).zfill(4)),

    # ── 飞机状态 ──
    "Aircraft.Gear":     ("gear_pos",  lambda v: float(v)),
    "Aircraft.Flaps":    ("flaps_pos", lambda v: float(v)),
    "Aircraft.OnGround": ("on_ground", lambda v: bool(int(float(v)))),
    "Aircraft.OnRunway": ("on_runway", lambda v: bool(int(float(v)))),

    # ── 机身信息 ──
    "Aircraft.Name":     ("model_icao", lambda v: str(v)),
    "Aircraft.Pitch":    ("pitch_deg",  lambda v: float(v) * RAD_TO_DEG),
    "Aircraft.Bank":     ("bank_deg",   lambda v: float(v) * RAD_TO_DEG),

    # ── 自动驾驶（部分常用）──
    "Autopilot.Master":              ("ap_master",     lambda v: bool(int(float(v)))),
    "Autopilot.SelectedAltitude":    ("ap_alt",        lambda v: float(v)),
    "Autopilot.SelectedHeading":     ("ap_hdg",        lambda v: float(v) * RAD_TO_DEG),
    "Autopilot.SelectedVerticalSpeed": ("ap_vs",       lambda v: float(v)),

    # ── 引擎 ──
    "Aircraft.EngineRunning1": ("eng1_running", lambda v: bool(int(float(v)))),
    "Aircraft.EngineRunning2": ("eng2_running", lambda v: bool(int(float(v)))),
}


# ════════════════════════════════════════════════════════════════════
#  命令映射：旧命令名 → AeroflyBridge 变量 + 值转换
# ════════════════════════════════════════════════════════════════════

# 键为旧 send_command(command, params) 中的 command
# 值为 (variable_name, transform_fn(params) → value)
# transform_fn 返回 None 表示不支持
COMMAND_MAP: dict[str, tuple[str, Callable[[dict], Any]]] = {
    "set_xpdr_code": (
        "Communication.TransponderCode",
        lambda p: int(p.get("code", "7000")),
    ),
    "set_com1_freq": (
        "Communication.COM1Frequency",
        lambda p: int(float(p.get("freq", 122800))) * 1000,
    ),
    "set_com2_freq": (
        "Communication.COM2Frequency",
        lambda p: int(float(p.get("freq", 119500))) * 1000,
    ),
    # set_xpdr_mode — AFS4/AeroflyBridge 不支持外部写入应答机模式
    # ident — 暂未在 AeroflyBridge 中找到对应变量
}


# ════════════════════════════════════════════════════════════════════
#  数据结构
# ════════════════════════════════════════════════════════════════════

@dataclass
class Telemetry:
    """AFS4 遥测数据快照。"""

    timestamp: float = 0.0
    lat: float = 0.0
    lon: float = 0.0
    alt_m: float = 0.0
    agl_m: float = 0.0
    hdg_true: float = 0.0
    hdg_mag: float = 0.0
    ias_kts: float = 0.0
    gs_kts: float = 0.0
    vs_fpm: float = 0.0
    mach: float = 0.0
    aoa_deg: float = 0.0
    rate_of_turn: float = 0.0
    pitch_deg: float = 0.0
    bank_deg: float = 0.0
    com1_freq: int = 0          # kHz*10 (例: 122800)
    com2_freq: int = 0
    xpdr_code: str = "0000"
    xpdr_mode: str = "SBY"      # 注：AeroflyBridge 不暴露模式，虚拟模式始终为 SBY
    gear_pos: float = 0.0
    flaps_pos: float = 0.0
    on_ground: bool = True
    on_runway: bool = False
    model_icao: str = ""
    ap_master: bool = False
    ap_alt: float = 0.0
    ap_hdg: float = 0.0
    ap_vs: float = 0.0
    eng1_running: bool = False
    eng2_running: bool = False
    # 原始数据
    raw: dict = field(default_factory=dict)

    @classmethod
    def from_aerofly_bridge(cls, raw_json: dict) -> "Telemetry":
        """从 AeroflyBridge 扁平 JSON 构造 Telemetry。"""
        kwargs: dict[str, Any] = {"raw": raw_json}

        for var_name, (field, transform) in VAR_MAP.items():
            if var_name in raw_json:
                try:
                    kwargs[field] = transform(raw_json[var_name])
                except (ValueError, TypeError, KeyError):
                    pass

        kwargs["timestamp"] = time.time()
        return cls(**kwargs)


# ════════════════════════════════════════════════════════════════════
#  DLLBridge
# ════════════════════════════════════════════════════════════════════

class DLLBridge:
    """
    AeroflyBridge.dll 异步通信桥。

    端口分工：
      12345 (telemetry)  ——  DLL 持续推送 JSON 行，客户端被动接收
      12346 (command)    ——  客户端短连接发送 JSON 命令

    使用方式::

        bridge = DLLBridge()
        await bridge.connect()
        telemetry = await bridge.get_telemetry()
        result = await bridge.send_command("set_xpdr_code", {"code": "7000"})
    """

    def __init__(
        self,
        host: str = DEFAULT_HOST,
        telemetry_port: int = DEFAULT_TELEMETRY_PORT,
        command_port: int = DEFAULT_COMMAND_PORT,
    ):
        self._host = host
        self._telemetry_port = telemetry_port
        self._command_port = command_port

        # 遥测连接（持久连接）
        self._telemetry_sock: Optional[socket.socket] = None
        self._telemetry_reader: Optional[asyncio.StreamReader] = None
        self._telemetry_writer: Optional[asyncio.StreamWriter] = None
        self._telemetry_task: Optional[asyncio.Task] = None

        # 最新遥测快照
        self._latest_telemetry: Optional[Telemetry] = None
        self._telemetry_updated = asyncio.Event()

        # 外部回调
        self.on_telemetry: Optional[Callable[[Telemetry], None]] = None

        # 连接状态
        self._telemetry_connected = False
        self._command_connected = False   # 命令端口是短连接，这里只标记端口可达
        self._shutdown = False            # 断线重连维护循环的控制标志
        self._first_heading_logged = False  # 航向诊断只打印一次

    # ── 属性 ──────────────────────────────────────────────────────

    @property
    def is_connected(self) -> bool:
        return self._telemetry_connected

    @property
    def is_telemetry_connected(self) -> bool:
        return self._telemetry_connected

    @property
    def is_command_connected(self) -> bool:
        """命令端口始终返回 True（短连接，不维持长连接）。"""
        return self._command_connected

    @property
    def latest_telemetry(self) -> Optional[Telemetry]:
        return self._latest_telemetry

    # ── 连接管理 ──────────────────────────────────────────────────

    async def connect(self, retry: bool = True, retry_interval: float = 2.0) -> None:
        """
        连接 DLL 的遥测端口并探测命令端口的可达性。

        :param retry: 若端口暂不可用，是否自动重试。
        :param retry_interval: 重试间隔（秒）。
        """
        self._shutdown = False
        await asyncio.gather(
            self._connect_telemetry(retry, retry_interval),
            self._probe_command_port(retry, retry_interval),
        )

    async def _connect_telemetry(self, retry: bool, interval: float) -> None:
        """连接遥测端口 12345 并维护接收循环（断线自动重连）。

        使用原始 socket（非 asyncio StreamReader），因为 Windows
        SelectorEventLoop 下 StreamReader 读取数据存在问题。

        连接建立并启动接收循环后，若 DLL 断开（EOF），会自动重连，
        直到 disconnect() 被调用（设置 _shutdown 标志）。这样无需手动
        开关 Mock DLL 即可在 AF4 重启 / DLL 重载后自动恢复遥测。
        """
        retry_count = 0
        _diag(f"telemetry connect: starting (raw socket), target={self._host}:{self._telemetry_port}")
        while not self._shutdown:
            try:
                sock = socket.create_connection(
                    (self._host, self._telemetry_port), timeout=5.0
                )
                sock.setblocking(True)
                sock.settimeout(10.0)  # 10s 读超时：检测 DLL 连接但不发数据的情况
                self._telemetry_sock = sock
                self._telemetry_connected = True
                logger.info("遥测端口 %s:%d 已连接（raw socket）", self._host, self._telemetry_port)
                _diag(f"telemetry connect: SUCCESS ({retry_count} retries, raw socket)")
                self._telemetry_task = asyncio.create_task(
                    self._telemetry_receive_loop()
                )
            except (ConnectionRefusedError, OSError, socket.timeout) as e:
                if not retry:
                    raise
                retry_count += 1
                if retry_count <= 3 or retry_count % 10 == 0:
                    _diag(f"telemetry connect: retry #{retry_count}, {self._host}:{self._telemetry_port} refused ({e})")
                logger.warning(
                    "遥测端口 %s:%d 暂不可用 (%s)，%.1fs 后重试",
                    self._host, self._telemetry_port, e, interval,
                )
                await asyncio.sleep(interval)
                continue

            # 等待接收循环结束（EOF 或 disconnect 取消）
            try:
                await self._telemetry_task
            except asyncio.CancelledError:
                # 被 disconnect() 取消 → 退出重连循环
                break

            # 接收循环正常结束（EOF，如 DLL 重启）→ 自动重连
            self._telemetry_connected = False
            if self._shutdown:
                break
            _diag(f"telemetry recv loop ended (EOF), reconnecting in {interval:.1f}s")
            logger.info("遥测连接断开，%.1fs 后自动重连", interval)
            await asyncio.sleep(interval)

        _diag("telemetry connect: maintenance loop exited (shutdown)")

    async def _probe_command_port(self, retry: bool, interval: float) -> None:
        """探测命令端口 12346 可达性（短连接测试）。"""
        while True:
            try:
                _, writer = await asyncio.wait_for(
                    asyncio.open_connection(self._host, self._command_port),
                    timeout=3.0,
                )
                writer.close()
                try:
                    await writer.wait_closed()
                except Exception:
                    pass
                self._command_connected = True
                logger.info("命令端口 %s:%d 可达", self._host, self._command_port)
                return
            except (ConnectionRefusedError, OSError, asyncio.TimeoutError) as e:
                if not retry:
                    self._command_connected = False
                    return
                logger.warning(
                    "命令端口 %s:%d 暂不可用 (%s)，%.1fs 后重试",
                    self._host, self._command_port, e, interval,
                )
                await asyncio.sleep(interval)

    async def disconnect(self) -> None:
        """断开所有连接。
        
        注意：不直接 await _telemetry_task，因为该 task 可能阻塞在
        run_in_executor 的 sock.recv 上，跨协程 await 已取消的 task
        会触发 "attached to a different loop" 错误。
        改为：cancel + close socket + 短暂等待让 task 自然退出。
        """
        self._shutdown = True

        # 先关闭 socket，让阻塞中的 sock.recv() 返回 EOF/错误
        if self._telemetry_sock:
            try:
                self._telemetry_sock.close()
            except Exception:
                pass
            self._telemetry_sock = None

        # 取消接收循环 task（不 await，避免跨协程 future 冲突）
        if self._telemetry_task and not self._telemetry_task.done():
            self._telemetry_task.cancel()

        # 短暂等待让 task 自然退出
        await asyncio.sleep(0.3)

        if self._telemetry_writer:
            self._telemetry_writer.close()
            try:
                await self._telemetry_writer.wait_closed()
            except Exception:
                pass

        self._telemetry_connected = False
        self._command_connected = False
        self._telemetry_writer = None
        self._telemetry_reader = None
        logger.info("DLL Bridge 已断开")

    # ── 遥测接收 ──────────────────────────────────────────────────

    async def _telemetry_receive_loop(self) -> None:
        """
        持续从 raw socket 读取 JSON 行（每帧约 13KB，~50Hz）。
        
        使用 loop.run_in_executor() + 阻塞 socket.recv() 在线程池中执行，
        彻底避开 Windows SelectorEventLoop 下 asyncio StreamReader 的 bug。
        """
        assert self._telemetry_sock is not None
        _diag("telemetry recv loop: STARTED (raw socket + run_in_executor)")
        loop = asyncio.get_event_loop()
        sock = self._telemetry_sock
        _first_frame_logged = False
        _frame_count = 0
        _line_buf = b""

        while True:
            try:
                chunk = await loop.run_in_executor(None, sock.recv, 8192)
            except asyncio.CancelledError:
                break
            except socket.timeout:
                # DLL TCP 连接建立但 10s 内无数据 → DLL 可能加载了但无法读取 AF4 飞机数据
                if _frame_count == 0:
                    _diag("telemetry recv loop: TIMEOUT — DLL connected but no data in 10s. "
                          "DLL may be loaded but AF4 aircraft data not available. "
                          "Try: 1) Restart AF4  2) Re-enter cockpit  3) Use Mock DLL button")
                    logger.warning("遥测端口 10s 内无数据 — DLL 可能无法读取 AF4 飞机数据")
                # 继续等待（不 break），数据可能在之后到来
                continue
            except Exception as e:
                logger.error("遥测接收异常: %s", e)
                _diag(f"telemetry recv loop: FATAL ERROR recv: {e}")
                import traceback; _diag(traceback.format_exc())
                self._telemetry_connected = False
                break

            if not chunk:  # EOF — DLL 关闭了连接
                _diag("telemetry recv loop: EOF (connection closed by DLL)")
                logger.warning("遥测端口连接已断开")
                self._telemetry_connected = False
                break

            _line_buf += chunk

            # 首次收到数据
            if _frame_count == 0 and _line_buf:
                _diag(f"telemetry recv loop: first recv, {len(chunk)} bytes in chunk, buffer={len(_line_buf)} bytes")
            elif _frame_count == 0 and not _line_buf:
                # chunk 已全部被处理（不太可能，chunk 通常包含多帧）
                pass

            # 拆分完整的 JSON 行（\n 分隔）
            while b"\n" in _line_buf:
                line, _line_buf = _line_buf.split(b"\n", 1)
                if not line.strip():
                    continue

                try:
                    msg = json.loads(line.decode("utf-8"))
                except json.JSONDecodeError as e:
                    _diag(f"telemetry recv loop: JSON decode error frame #{_frame_count+1}: {e}")
                    logger.debug("遥测 JSON 解析跳过: %s", e)
                    continue

                _frame_count += 1

                # 提取变量数据
                if "variables" in msg:
                    vars_data = msg["variables"]
                    if not msg.get("data_valid", 1):
                        if _frame_count <= 5:
                            _diag(f"telemetry recv loop: frame #{_frame_count} skipped (data_valid=0)")
                        continue
                else:
                    vars_data = msg

                # 首次有效帧：打印诊断
                if not _first_frame_logged:
                    _first_frame_logged = True
                    sample_keys = list(vars_data.keys())[:10]
                    logger.info("遥测首帧样本 keys: %s", sample_keys)
                    pos_keys = [k for k in vars_data if any(
                        t in k.lower() for t in ["lat", "lon", "alt", "pos", "heading", "speed"]
                    )]
                    if pos_keys:
                        for k in pos_keys[:8]:
                            logger.info("  首帧 %s = %s", k, vars_data.get(k))

                telemetry = Telemetry.from_aerofly_bridge(vars_data)
                self._latest_telemetry = telemetry
                self._telemetry_updated.set()

                # 一次性诊断：打印原始航向与转换后的罗盘航向，
                # 用于排查「服务器地图朝向朝北」问题（确认 DLL 是否真的发出航向）。
                if not getattr(self, "_first_heading_logged", False):
                    self._first_heading_logged = True
                    raw_true = vars_data.get("Aircraft.TrueHeading", "N/A")
                    raw_mag = vars_data.get("Aircraft.MagneticHeading", "N/A")
                    _diag(f"heading diag: raw TrueHeading={raw_true} MagneticHeading={raw_mag} "
                          f"-> hdg_true={telemetry.hdg_true:.1f}° (compass)")

                if not _first_frame_logged:
                    _first_frame_logged = True
                    _diag(f"telemetry recv loop: first valid frame, lat={telemetry.lat:.4f} lon={telemetry.lon:.4f} alt={telemetry.alt_m:.0f}m gs={telemetry.gs_kts:.0f}kt")

                if self.on_telemetry:
                    self.on_telemetry(telemetry)

            if _frame_count % 100 == 0 and _frame_count > 0:
                _diag(f"telemetry recv loop: frame #{_frame_count}, OK "
                      f"lat={telemetry.lat:.4f} lon={telemetry.lon:.4f} "
                      f"hdg_true={telemetry.hdg_true:.1f}° "
                      f"raw_TrueHdg={vars_data.get('Aircraft.TrueHeading', 'MISSING')}")

    async def get_telemetry(self, timeout: float = 5.0) -> Optional[Telemetry]:
        """获取最新遥测数据。"""
        if self._latest_telemetry is not None:
            return self._latest_telemetry
        if not self._telemetry_connected:
            return None
        try:
            await asyncio.wait_for(self._telemetry_updated.wait(), timeout)
        except asyncio.TimeoutError:
            logger.warning("等待遥测数据超时 (%.1fs)", timeout)
            return None
        return self._latest_telemetry

    # ── 命令发送（短连接，每次新建连接）──────────────────────────

    async def send_command(
        self, command: str, params: Optional[dict] = None, timeout: float = 3.0
    ) -> dict:
        """
        向 DLL 命令端口发送控制命令。

        兼容旧接口：内部将 set_xpdr_code 等旧命令映射为
        AeroflyBridge 格式 {"variable":"...","value":...}。

        注：命令使用短连接（与 master_control_panel.py 一致），
        每次调用独立建立 TCP 连接。

        :param command: 命令名 (set_xpdr_code / set_com1_freq / ...)
        :param params: 参数字典
        :param timeout: 超时（秒）
        :return: {"status":"ok"} 或 {"status":"unsupported","msg":"..."}
        """
        # ── 查找命令映射 ──
        if command not in COMMAND_MAP:
            return {"status": "unsupported",
                    "msg": f"命令 {command} 不在 AeroflyBridge 支持列表中"}

        var_name, transform = COMMAND_MAP[command]
        try:
            value = transform(params or {})
        except Exception as e:
            return {"status": "error", "msg": f"参数转换失败: {e}"}

        payload = json.dumps({"variable": var_name, "value": value},
                             ensure_ascii=False)
        data = (payload + "\n").encode("utf-8")

        try:
            reader, writer = await asyncio.wait_for(
                asyncio.open_connection(self._host, self._command_port),
                timeout=timeout,
            )
        except (ConnectionRefusedError, OSError) as e:
            raise ConnectionError(f"命令端口 {self._host}:{self._command_port} 不可达: {e}")
        except asyncio.TimeoutError:
            raise ConnectionError(f"命令端口连接超时")

        try:
            writer.write(data)
            await writer.drain()

            # 读取响应
            line = await asyncio.wait_for(reader.readline(), timeout)
            if not line:
                return {"status": "error", "msg": "命令端口无响应"}
            response = json.loads(line.decode("utf-8"))
            logger.debug("命令 %s (%s=%s) → %s", command, var_name, value, response)
            return response
        except asyncio.TimeoutError:
            logger.error("命令 %s 响应超时", command)
            return {"status": "error", "msg": "响应超时"}
        except Exception as e:
            logger.error("命令 %s 异常: %s", command, e)
            return {"status": "error", "msg": str(e)}
        finally:
            writer.close()
            try:
                await writer.wait_closed()
            except Exception:
                pass
