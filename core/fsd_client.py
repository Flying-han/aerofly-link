"""
FSD 协议客户端
==============
VATSIM FSD 协议 TCP 客户端，负责：
  - #AA  服务器认证 / 客户端鉴权
  - #AP  飞机位置报告（5 秒间隔）
  - #TM  ATC 文字消息收发
  - #SB  服务器广播接收
  - #DP  断开连接
  - #PC  在线人数

基于 asyncio TCP，所有 I/O 在后台线程运行。
通过 PyQt6 pyqtSignal 将事件派发到 UI 线程。

协议常量与工具函数已拆分到 core/fsd_protocol.py。
"""

from __future__ import annotations

import asyncio
import logging
import time
from typing import Optional

from PyQt6.QtCore import QObject, pyqtSignal

# 从协议模块导入常量和工具函数
from core.fsd_protocol import (
    PROTOCOL_VERSION_LEGACY,
    PROTOCOL_VERSION_VATSIM,
    CLIENT_NAME,
    CLIENT_VERSION,
    SIMULATOR_TYPE,
    SIM_TYPE_CODE,
    DEFAULT_RATING,
    CLIENT_ID_HEX,
    pack_pbh,
    unpack_pbh,
    xpdr_mode_to_fsd_letter,
    parse_coord,
    decimal_to_packed_coord,
)

logger = logging.getLogger("aerofly_link.fsd_client")


class FSDClient(QObject):
    """
    FSD 协议客户端。

    信号：
      - status_changed(str, str)     状态变更 (status, message)
      - message_received(str, str, str) 文本消息 (source, dest, text)
      - traffic_updated(list)         其他飞机列表
      - telemetry_updated(dict)       自身遥测（转发 DLL 数据，用于地图）
    """

    # === PyQt 信号 ===
    status_changed = pyqtSignal(str, str)         # status, message
    message_received = pyqtSignal(str, str, str)  # source, dest, text
    traffic_updated = pyqtSignal(list)             # list of aircraft dicts
    telemetry_updated = pyqtSignal(dict)           # 自身飞行数据
    debug_line = pyqtSignal(str)                   # 协议调试输出（>>>/<<<）

    def __init__(self, config: dict):
        """
        :param config: {
            "callsign": str,   # 呼号，如 CES2101
            "cid": str,        # VATSIM CID
            "password": str,   # VATSIM 密码
            "server": str,     # FSD 服务器地址
            "port": int,       # 端口（默认 6809）
            "model": str = "", # 机型 ICAO
            "eco":  str = "private",  # 服务器环境: "vatsim" | "private" | "legacy"
            "type": str = "vatsim",   # 客户端协议: "vatsim" | "legacy"
            "mode": str = "legacy",   # 旧配置兼容（"legacy" | "vatsim"）
        }
        """
        QObject.__init__(self)

        self.callsign = config.get("callsign", "")
        self.cid = config.get("cid", "")
        self.password = config.get("password", "")
        self.realname = config.get("realname", "")
        self.server = config.get("server", "sweatbox.vatsim.net")
        self.port = config.get("port", 6809)
        self.model = config.get("model", "")

        # 新配置：eco / type；兼容旧配置：mode
        self.eco = config.get("eco", "")
        self.type = config.get("type", "")
        if not self.eco or not self.type:
            old_mode = config.get("mode", "legacy")
            if old_mode == "vatsim":
                self.eco = "vatsim"
                self.type = "vatsim"
            else:
                self.eco = "private"
                self.type = "legacy"
        # 保留 mode 字段供旧代码读取
        self.mode = self.type

        # 初始位置（从 Mock 或 DLL 遥测获取前使用，默认希思罗）
        self._init_lat: float = float(config.get("init_lat", 51.4775))
        self._init_lon: float = float(config.get("init_lon", -0.4614))
        self._init_alt_m: float = float(config.get("init_alt", 3500.0))

        # 飞行员等级（从连接面板配置，默认 OBS=1）
        # 服务器会验证 rating 是否匹配 CID，过高会被拒绝
        self._rating: int = int(config.get("rating", DEFAULT_RATING))

        # 运行时状态
        self._reader: Optional[asyncio.StreamReader] = None
        self._writer: Optional[asyncio.StreamWriter] = None
        self._connected = False
        self._auth_ok = False

        # 应答机关联状态（由 TransponderController 写入）
        self._reporting_enabled = True   # ALT 模式为 True
        self._ident_active = False       # SPI/IDENT 脉冲激活

        # 在线飞机列表
        self._traffic: dict[str, dict] = {}

        # 接收缓冲
        self._recv_buffer = b""

        # Keepalive 心跳（FSD 服务器会关闭空闲连接）
        self._keepalive_task: Optional[asyncio.Task] = None
        self._keepalive_interval: float = 30.0  # 每 30 秒发一次心跳
        self._read_timeout: float = 90.0  # 接收超时（3 倍心跳间隔）

        # 最后有效位置缓存（避免 keepalive 用 0,0 重置位置）
        self._last_valid_lat: float = 0.0
        self._last_valid_lon: float = 0.0
        self._last_valid_position_set: bool = False

        # 最后发送的位置报告数据缓存（keepalive 用此数据而非全 0，避免地图数据跳变）
        self._last_sent_alt: int = 0
        self._last_sent_gs: int = 0
        self._last_sent_pbh: int = 0
        self._last_sent_xpdr: str = "1200"
        self._last_sent_mode: str = "N"
        self._has_sent_position: bool = False

    # ── 连接管理 ──────────────────────────────────────────────────

    async def connect(self) -> bool:
        """
        连接到 FSD 服务器并完成认证。
        返回 True 表示连接+认证成功。
        """
        if self._connected:
            logger.warning("Already connected")
            return False

        self.status_changed.emit("connecting",
            f"[{self.eco.upper()}/{self.type.upper()}] 正在连接 {self.server}:{self.port}...")

        try:
            self._reader, self._writer = await asyncio.wait_for(
                asyncio.open_connection(self.server, self.port),
                timeout=10.0
            )
        except asyncio.TimeoutError:
            msg = f"连接超时: {self.server}:{self.port}"
            self.status_changed.emit("error", msg)
            logger.error(msg)
            return False
        except OSError as e:
            msg = f"连接失败 [{self.server}:{self.port}]: {e}"
            self.status_changed.emit("error", msg)
            logger.error(msg)
            return False

        self._connected = True
        logger.info("TCP connected to %s:%s", self.server, self.port)

        # ── 阶段 1：读取服务器问候 ──
        # Swift FSD (Private) 发送 $DI 标识行，VATSIM 发送 #AA/#TM
        # 部分服务器会在标识行后跟 MOTD / 欢迎消息
        greeting_seen = False
        di_seen = False  # 是否收到 $DI（Swift FSD 私有服务器标识）
        for i in range(5):  # 最多读 5 行问候
            greeting = await self._read_line(timeout=3.0 if i == 0 else 1.0)
            if not greeting:
                if i == 0 and self.type == "vatsim":
                    logger.warning("VATSIM mode: no server greeting (unusual)")
                break  # 没更多数据了

            self.debug_line.emit(f"<<< {greeting[:200]}")
            logger.info("Server [%s] line %d: %s", self.type, i+1, greeting[:120])
            greeting_seen = True

            # 检查各种可能的服务器标识
            if greeting.startswith("#ER"):
                error_parts = greeting.split(":", 1)
                error_msg = error_parts[1] if len(error_parts) > 1 else greeting
                self.status_changed.emit("error", f"服务器拒绝: {error_msg}")
                await self._close_connection()
                return False

            if greeting.startswith("$DI"):
                logger.info("Server identified via $DI (FSD Private / Swift)")
                di_seen = True
                continue  # 继续读后续行（MOTD 等）

            if greeting.startswith("#TM"):
                logger.info("Server sent welcome/motd: %s", greeting[:60])
                continue  # 欢迎消息，继续读

            if greeting.startswith("#AA"):
                logger.info("Server identified via #AA (VATSIM-style)")
                # VATSIM 服务器通常在 #AA 后没有更多行
                break

            # 其他行：可能是服务器 MOTD 或自定义问候
            logger.info("Server greeting line: %s", greeting[:60])

        if not greeting_seen and self.type == "vatsim":
            logger.warning("No server greeting received in VATSIM mode")

        # ── 阶段 2：发送客户端 ident + 认证 ──
        # VATSIM 协议要求在 #AP 登录前先发送 $ID ident 包
        # 若服务器发送了 $DI（Swift FSD 私有服务器），也必须先发 $ID
        if self.type == "vatsim" or di_seen:
            ident_line = self._build_ident_line()
            self.debug_line.emit(f">>> {ident_line}")
            logger.info(">>> [%s ident] %s", "VATSIM" if self.type == "vatsim" else "Swift-private", ident_line)
            await self._send_line(ident_line)

        auth_line = self._build_auth_line()
        self.debug_line.emit(f">>> {auth_line}")
        logger.info(">>> [%s] %s", self.type, auth_line)
        await self._send_line(auth_line)

        # ── 阶段 3：等待认证结果 ──
        response = await self._read_line(timeout=10.0)
        if response:
            self.debug_line.emit(f"<<< {response[:200]}")
            logger.info("<<< [%s] %s", self.type, response[:200])
        else:
            logger.info("<<< (no response within 10s)")

        # ── 分析响应 ──
        if response is None:
            # 服务器在 10 秒内没有返回任何数据
            if self.type == "legacy":
                # Legacy/Private 模式：部分服务器认证通过后不发确认行
                # 探活确认连接是否还活着
                await asyncio.sleep(1.0)
                try:
                    probe = await self._read_line(timeout=2.0)
                    if probe:
                        self.debug_line.emit(f"<<< {probe[:200]}")
                        logger.info("Post-auth data (no explicit OK): %s", probe[:120])
                        self._dispatch(probe)
                    else:
                        logger.info("No auth response but connection alive (legacy mode)")
                except Exception as e:
                    msg = f"认证后连接断开，服务器可能拒绝了连接 ({e})"
                    self.status_changed.emit("error", msg)
                    await self._close_connection()
                    return False
                # 探活通过 → 跳到认证成功
            else:
                # VATSIM 模式必须收到明确响应
                msg = "服务器无响应：发送认证后 10 秒内未收到回复，可能协议不兼容"
                self.status_changed.emit("error", msg)
                logger.warning("No auth response (VATSIM mode)")
                await self._close_connection()
                return False

        elif response.startswith("#ER"):
            # 认证被拒绝 — 解析错误原因
            error_parts = response.split(":", 1)
            error_msg = error_parts[1] if len(error_parts) > 1 else response
            self.status_changed.emit("error", f"认证失败: {error_msg}")
            logger.error("Auth rejected: %s", error_msg)
            await self._close_connection()
            return False

        elif response.startswith("#TM"):
            # 服务器发 #TM 欢迎/提示 → 认证通过
            logger.info("Got #TM welcome/motd, auth OK")

        elif response.startswith("$DI") or response.startswith("#AA") or response.startswith("#SB"):
            # 服务器发标识/广播 → 认证通过
            logger.info("Server message after auth (normal): %s", response[:120])

        elif response.startswith("$CR") or response.startswith("#PC"):
            # 某些 FSD 实现直接发 $CR（Client Response）或 #PC（人数统计）
            logger.info("Server sent client data after auth: %s", response[:120])

        else:
            # 未知响应格式 — 在 legacy 模式下尝试接受，VATSIM 则拒绝
            logger.warning("Unexpected auth response [%s]: %s", self.type, response[:200])
            if self.type != "legacy":
                msg = f"服务器返回了无法识别的响应: {response[:80]}"
                self.status_changed.emit("error", msg)
                await self._close_connection()
                return False

        # ── 认证成功 ──
        self._auth_ok = True
        self.status_changed.emit("connected", "已连接并认证")
        logger.info("FSD authentication OK, callsign=%s", self.callsign)

        # 声明客户端类型（FSD V3.000 draft 9 必须）
        await self.send_sb_client_type()

        # 自动发送最小飞行计划（部分服务器要求有 $FP 才会将飞行员加入广播列表）
        await self._send_minimal_flight_plan()

        # 发送初始位置（@ 格式，用 0 值占位，等第一帧遥测）
        await self._send_initial_position()

        # 启动 keepalive 心跳（防止服务器因空闲而断开连接）
        self._keepalive_task = asyncio.create_task(self._keepalive_loop())

        # 启动接收循环
        asyncio.create_task(self._recv_loop())

        return True

    async def disconnect(self):
        """断开 FSD 连接"""
        if not self._connected:
            return

        # 发送断开通知（属于 #PI 类型，或直接 #DP）
        try:
            plan = f"#DP{self.callsign}"
            await self._send_line(plan)
        except Exception:
            pass

        await self._close_connection()
        self.status_changed.emit("disconnected", "已断开连接")
        logger.info("FSD disconnected")

    # ── FSDClientProtocol 接口（供 TransponderController 调用）──

    def set_reporting_enabled(self, enabled: bool):
        """应答机模式变更通知：ALT=True 发送 #AP，STBY=False 停止发送"""
        self._reporting_enabled = enabled
        logger.debug("Reporting %s", "ON" if enabled else "OFF")

    def set_ident_flag(self, active: bool):
        """应答机 IDENT/SPI 脉冲标记"""
        self._ident_active = active

    # ── 客户端类型声明 ────────────────────────────────────────────

    async def send_sb_client_type(self):
        """
        声明为 SquawkBox 客户端类型。
        格式: #SB<callsign>:SERVER
        FSD V3.000 draft 9 私有服务器需要此声明，
        否则服务器不知道如何路由位置更新包。
        """
        if not self._connected or not self._auth_ok:
            return
        sb_line = f"#SB{self.callsign}:SERVER"
        self.debug_line.emit(f">>> {sb_line}")
        await self._send_line(sb_line)
        logger.info("Client type declared: SquawkBox")

    async def _send_minimal_flight_plan(self):
        """
        连接成功后自动发送最小飞行计划。
        
        部分私有 FSD 服务器要求飞行员有 $FP 飞行计划才会将其加入
        广播列表（其他客户端才能收到该飞机的 @ 位置更新）。
        如果用户稍后手动提交了完整飞行计划，会覆盖此最小版本。
        """
        if not self._connected or not self._auth_ok:
            return
        rname = self.realname if self.realname else self.callsign
        # 最小飞行计划: IFR, 未知机型, 0 TAS, ZZZZ 起降, 0 时间
        fp_line = (
            f"$FP{self.callsign}:SERVER:"
            f"I:B738/L:0:"           # type:aircraft(tas)
            f"ZZZZ:0:0:FL000:ZZZZ:"  # dep:deptime:actdeptime:alt:dest
            f"0:0:0:0:"              # hrsenroute:minenroute:hrsfuel:minfuel
            f"ZZZZ:OPR/{rname}:"     # altn:remarks
            f"NOFP"                  # route
        )
        self.debug_line.emit(f">>> {fp_line}")
        await self._send_line(fp_line)
        logger.info("Minimal flight plan sent (auto)")

    # ── 位置报告 ──────────────────────────────────────────────────

    async def send_position_report(
        self,
        lat: float,
        lon: float,
        alt_ft: float,
        gs_kts: float,
        hdg: float,
        xpdr: str,
        xpdr_mode: str = "ALT"
    ):
        """
        发送 @ 飞行员位置更新（2 秒间隔）。

        格式与 Swift PilotDataUpdate::toTokens() 完全一致：
            @<mode>:<callsign>:<squawk>:<rating>:<lat>:<lon>:<alt>:<gs>:<pbh>:<alt_diff>

        - mode: S=STBY, N=ModeC/normal(ALT), Y=IDENT (FSD serializer 约定，非 modeAsShortString)
        - lat/lon: 十进制 5 位小数（与 Swift toTokens() 的 QString::number(lat,'f',5) 一致）
          ⚠️ 注意：Swift 服务器 fromTokens() 直接 toDouble()，不做 packed 解包。
          发 packed 格式（如 3954.252）会被服务器当作越界纬度丢弃 → 飞机不显示。
        - pbh: 32-bit packed pitch/bank/heading/onGround
        - alt_diff: pressure_altitude - true_altitude（我们传 0）

        若 _reporting_enabled 为 False（STBY 模式），跳过。
        """
        if not self._connected or not self._auth_ok:
            return
        if not self._reporting_enabled:
            return

        # ── 坐标有效性校验 ──
        is_zero_coord = (abs(lat) < 0.0001 and abs(lon) < 0.0001)
        if is_zero_coord:
            if self._last_valid_position_set:
                lat = self._last_valid_lat
                lon = self._last_valid_lon
                logger.debug("Telemetry returned (0,0), using last valid position: %.4f, %.4f", lat, lon)
            else:
                logger.debug("Telemetry returned (0,0) and no valid position cached, skipping report")
                self.debug_line.emit("<< DLL 遥测坐标仍为 (0,0)，游戏可能未加载完成")
                return

        # ── 更新位置缓存 ──
        if not is_zero_coord:
            if not self._last_valid_position_set:
                logger.info("First valid position from DLL: %.5f, %.5f", lat, lon)
                self.debug_line.emit(f"<< DLL 获取到有效位置: {lat:.5f}, {lon:.5f}")
            self._last_valid_lat = lat
            self._last_valid_lon = lon
            self._last_valid_position_set = True

        # ── 构建 @ 位置更新包（Swift 兼容格式）──
        # mode letter: N=normal (ALT), S=standby, Y=ident
        mode_letter = xpdr_mode_to_fsd_letter(xpdr_mode)
        if self._ident_active:
            mode_letter = "Y"

        # Squawk code
        xpdr_code = xpdr or "7000"

        # FSD 坐标：十进制 5 位小数（与 Swift PilotDataUpdate::toTokens() 完全一致）
        # 关键：Swift 服务器 fromTokens() 直接 toDouble()，不做 packed 解包！
        # 发送 packed 格式（如 3954.252）会被服务器当作越界纬度而丢弃 → 飞机不显示。
        lat_str = f"{lat:.5f}"
        lon_str = f"{lon:.5f}"

        # 高度（英尺）、地速（节）
        alt_int = int(alt_ft)
        gs_int = int(gs_kts)

        # PBH: 打包 pitch/bank/heading/onGround
        # 我们没有真实的 pitch/bank，传 0（大多数服务器不校验此值）
        pbh = pack_pbh(0.0, 0.0, hdg, False)

        # alt_diff: pressure_alt - true_alt（我们不区分，传 0）
        alt_diff = 0

        at_line = (
            f"@{mode_letter}:{self.callsign}:"
            f"{xpdr_code}:{self._rating}:"
            f"{lat_str}:{lon_str}:"
            f"{alt_int}:{gs_int}:"
            f"{pbh}:{alt_diff}"
        )

        await self._send_line(at_line)

        # ── 缓存本次发送的数据（keepalive 使用，避免 alt=0/gs=0 导致地图数据跳变）──
        self._last_sent_alt = alt_int
        self._last_sent_gs = gs_int
        self._last_sent_pbh = pbh
        self._last_sent_xpdr = xpdr_code
        self._last_sent_mode = mode_letter
        self._has_sent_position = True

        # ── FSD 包诊断日志 ──
        # 每 10 次位置报告打印一次完整 @ 包，含 PBH 解码航向，便于排查朝向/可见性问题
        if not hasattr(self, "_pos_rpt_count"):
            self._pos_rpt_count = 0
        self._pos_rpt_count += 1
        if self._pos_rpt_count <= 3 or self._pos_rpt_count % 10 == 0:
            try:
                _p, _b, _h, _og = unpack_pbh(pbh)
                import os as _os
                from pathlib import Path as _P
                _log_dir = _P(_os.environ.get("APPDATA", _P.home() / "AppData" / "Roaming")) / "AeroflyLink"
                _log_dir.mkdir(parents=True, exist_ok=True)
                with open(str(_log_dir / "fsd_packets.log"), "a", encoding="utf-8") as _f:
                    from datetime import datetime as _dt
                    _f.write(f"[{_dt.now().strftime('%H:%M:%S')}] #{self._pos_rpt_count} "
                             f"hdg_in={hdg:.1f}° pbh={pbh} -> decoded_hdg={_h:.1f}° "
                             f"alt={alt_int}ft gs={gs_int}kt "
                             f"lat={lat_str} lon={lon_str} mode={mode_letter}\n")
            except Exception:
                pass

    # ── 飞行计划 ──────────────────────────────────────────────────

    async def send_flight_plan(self, plan: dict) -> bool:
        """
        发送 $FP 飞行计划报文到 FSD 服务器。

        兼容 Swift 客户端使用的 FSD V3.000 draft 9 / VATSIM FSD 格式。
        Swift 对私有 FSD 服务器使用 FAA 机型格式 ``H/B772/L``，
        对 VATSIM 使用 ICAO 格式 ``B772/H-SDE2E3FGHIRWXY/LB1``。

        :param plan: {
            "type": str,          # IFR|VFR|SVFR|DVFR
            "aircraft": str,      # 机型 ICAO，如 B738
            "wake_category": str, # Light|Medium|Heavy|Super
            "tas": str,           # 巡航 TAS，如 "450"
            "dep_airport": str,   # 起飞机场 ICAO
            "dep_time": str,      # 起飞时间 HHMM UTC
            "cruise_alt": str,    # 巡航高度，如 F350
            "route": str,         # 航线
            "dest_airport": str,  # 降落机场 ICAO
            "alt_airport": str,   # 备降机场 ICAO
            "remarks": str,       # 备注（RMK/...）
            "pilot": str = "",    # 飞行员真实姓名
            "eet": str = "",      # 航路时间 HH:MM
            "endurance": str = "",# 续航时间 HH:MM
        }
        :return: True 表示已发送
        """
        if not self._connected or not self._auth_ok:
            logger.warning("Cannot send flight plan: not connected")
            return False

        # ── FSD $FP 飞行计划 17 字段标准格式 ──
        # $FP<callsign>:SERVER:<type>:<aircraft>:<tascruise>:
        #   <dep>:<deptime>:<actdeptime>:<alt>:<dest>:
        #   <hrsenroute>:<minenroute>:<hrsfuel>:<minfuel>:
        #   <altn>:<remarks>:<route>
        # 参考: Swift src/core/fsd/flightplan.cpp

        fp_type = plan.get("type", "I")[0].upper()

        raw_aircraft = plan.get("aircraft", "").strip().upper()
        wake_cat = plan.get("wake_category", "").strip().upper()

        # 如果用户已经在 aircraft 字段里写了完整格式（如 H/B772/L），直接复用
        if "/" in raw_aircraft:
            aircraft_full = raw_aircraft
        elif self.type == "vatsim":
            # VATSIM ICAO 简化格式：B772/H（至少把尾流类别带上）
            wake_letter = {"LIGHT": "L", "MEDIUM": "M", "HEAVY": "H", "SUPER": "J"}.get(wake_cat, "M")
            aircraft_full = f"{raw_aircraft}/{wake_letter}"
        else:
            # 私有/传统 FSD 服务器 FAA 格式：H/B772/L
            wake_prefix = {"LIGHT": "", "MEDIUM": "", "HEAVY": "H/", "SUPER": "J/"}.get(wake_cat, "")
            # FAA 单字符设备码，Swift 默认用 L；这里固定 L 即可通过大多数服务器校验
            aircraft_full = f"{wake_prefix}{raw_aircraft}/L"

        # TAS：Swift 传纯整数节，不带 N 前缀
        tas = plan.get("tas", "").strip()
        tas = tas.upper().lstrip("N")

        dep = plan.get("dep_airport", "").strip().upper()
        dep_time = plan.get("dep_time", "").strip()
        # 未起飞时 Swift 传 0；如果用户没填也传 0，避免服务器解析异常
        actual_dep_raw = plan.get("actual_dep_time", "").strip()
        actual_dep = actual_dep_raw if actual_dep_raw else (dep_time if dep_time else "0")

        cruise_alt = plan.get("cruise_alt", "").strip()
        dest = plan.get("dest_airport", "").strip().upper()
        altn = plan.get("alt_airport", "").strip().upper()

        # 路由和备注不能包含冒号（FSD 分隔符）
        route = plan.get("route", "").replace(":", " ")
        remarks = plan.get("remarks", "").replace(":", " ")

        # 把飞行员姓名追加到备注
        pilot = plan.get("pilot", "").strip()
        if pilot and f"OPR/{pilot}" not in remarks:
            remarks = f"OPR/{pilot} {remarks}".strip() if remarks else f"OPR/{pilot}"

        # EET / Endurance：拆为 hours:minutes 两个字段（未知时填 0）
        def _split_hm(value: str):
            value = str(value).strip()
            if ":" in value:
                h, m = value.split(":", 1)
                return h or "0", m or "0"
            return "0", "0"

        eet_hrs, eet_min = _split_hm(plan.get("eet", ""))
        fuel_hrs, fuel_min = _split_hm(plan.get("endurance", ""))

        # 统一用纯数字，去掉前导零（Swift 行为）
        def _int_str(v):
            try:
                return str(int(v))
            except ValueError:
                return "0"

        dep_time = _int_str(dep_time) if dep_time else "0"
        actual_dep = _int_str(actual_dep) if actual_dep else "0"
        eet_hrs, eet_min = _int_str(eet_hrs), _int_str(eet_min)
        fuel_hrs, fuel_min = _int_str(fuel_hrs), _int_str(fuel_min)

        fields = [
            f"$FP{self.callsign}",  # 1
            "SERVER",               # 2
            fp_type,                # 3
            aircraft_full,          # 4
            tas,                    # 5
            dep,                    # 6
            dep_time,               # 7
            actual_dep,             # 8
            cruise_alt,             # 9
            dest,                   # 10
            eet_hrs,                # 11
            eet_min,                # 12
            fuel_hrs,               # 13
            fuel_min,               # 14
            altn,                   # 15
            remarks,                # 16
            route,                  # 17
        ]

        fp_line = ":".join(fields)

        self.debug_line.emit(f">>> {fp_line}")
        # 字段级调试，方便排查服务器解析问题
        logger.info("$FP fields: %s", " | ".join(f"[{i+1}]{v}" for i, v in enumerate(fields)))
        await self._send_line(fp_line)
        logger.info("Flight plan sent [%s]: %s -> %s (%s)",
                     self.callsign, dep, dest, aircraft_full)
        return True

    # ── 内部方法 ──────────────────────────────────────────────────

    def _build_ident_line(self) -> str:
        """
        构建 VATSIM 前置 ident 包（$ID）。
        格式（9 字段）：
            $ID<callsign>:SERVER:<client_id_hex>:<client_string>:<version>:
            <sim_type>:<os>:<reserved>:<challenge>
        challenge 留空即可跳过 VATSIM auth challenge。
        """
        return (
            f"$ID{self.callsign}:SERVER:"
            f"{CLIENT_ID_HEX}:{CLIENT_NAME}:"
            f"{CLIENT_VERSION}:{SIMULATOR_TYPE}:"
            f"WIN:0:"
        )

    def _build_auth_line(self) -> str:
        """
        构建飞行员客户端认证行（统一使用 #AP）。
        字段顺序与 Swift AddPilot::toTokens() 完全一致：
            #AP<callsign>:SERVER:<cid>:<password>:<rating>:<revision>:<simtype>:<realname>

        - revision: legacy=9, VATSIM=100
        - simtype: "0"=Unknown（Aerofly FS 4 无标准 SimType 编号）
        """
        rname = self.realname if self.realname else self.callsign

        # 确定协议修订号：VATSIM 官方/现代 dialect 用 100，legacy 用 9
        if self.eco == "legacy" or self.type == "legacy":
            revision = PROTOCOL_VERSION_LEGACY
        else:
            revision = PROTOCOL_VERSION_VATSIM

        # 统一格式：#AP<callsign>:SERVER:<cid>:<password>:<rating>:<revision>:<simtype>:<realname>
        # Swift AddPilot::fromTokens() 读取 tokens[5]=revision, tokens[6]=simType
        # 旧 legacy 格式将 sim_type 和 revision 放反了，导致服务器看到 revision=0
        return (
            f"#AP{self.callsign}:SERVER:"
            f"{self.cid}:{self.password}:"
            f"{self._rating}:{revision}:"
            f"{SIM_TYPE_CODE}:{rname}"
        )

    async def _send_initial_position(self):
        """发送初始位置（@ 格式），使用配置或默认坐标。
        真实遥测数据到达后会通过 send_position_report 覆盖。"""
        init_lat, init_lon = self._init_lat, self._init_lon
        init_alt_ft = int(self._init_alt_m * 3.28084)
        init_line = (
            f"@N:{self.callsign}:"              # N = ModeC (normal altitude reporting, FSD serializer convention)
            f"1200:{self._rating}:"           # squawk:rating（默认 1200，与 TransponderController 默认值一致）
            f"{init_lat:.5f}:{init_lon:.5f}:"
            f"{init_alt_ft}:0:"               # alt:gs（使用配置高度，非 0）
            f"0:0"                            # pbh:alt_diff (PBH=0 → heading=0°/North, 真实航向将在后续位置报告中更新)
        )
        self.debug_line.emit(f">>> {init_line}")
        await self._send_line(init_line)
        # 初始化位置缓存，避免 keepalive 在世界中心 (0,0) 空转
        self._last_valid_lat = init_lat
        self._last_valid_lon = init_lon
        self._last_valid_position_set = True
        # 初始化发送缓存（keepalive 使用）
        self._last_sent_alt = init_alt_ft
        self._last_sent_gs = 0
        self._last_sent_pbh = 0
        self._last_sent_xpdr = "1200"
        self._last_sent_mode = "N"
        self._has_sent_position = True
        logger.info("Initial position sent: %.5f, %.5f alt=%dft (PBH=0, heading will update with telemetry)",
                     init_lat, init_lon, init_alt_ft)

    # ── Keepalive 心跳 ──────────────────────────────────────────

    async def _keepalive_loop(self):
        """定期发送心跳保持连接，防止服务器因空闲而断开。

        ⚠️ 重要：keepalive 使用上次位置报告缓存的 alt/gs/pbh/xpdr 数据，
        绝不发送 alt=0/gs=0/pbh=0，否则连飞地图上飞机会周期性跳变到
        高度 0、速度 0、朝向 0°（朝北），造成数据不连贯。
        """
        while self._connected:
            try:
                await asyncio.sleep(self._keepalive_interval)
                if not self._connected:
                    break
                if self.type == "legacy":
                    keep_line = f"#TM{self.callsign}:SERVER:@"
                else:
                    # 使用缓存的最后有效位置坐标
                    if self._last_valid_position_set:
                        k_lat_str = f"{self._last_valid_lat:.5f}"
                        k_lon_str = f"{self._last_valid_lon:.5f}"
                    else:
                        k_lat_str = f"{self._init_lat:.5f}"
                        k_lon_str = f"{self._init_lon:.5f}"

                    # 使用上次位置报告的真实数据（alt/gs/pbh/xpdr/mode）
                    # 而非全 0，避免地图数据周期性跳变
                    if self._has_sent_position:
                        k_alt = self._last_sent_alt
                        k_gs = self._last_sent_gs
                        k_pbh = self._last_sent_pbh
                        k_xpdr = self._last_sent_xpdr
                        k_mode = self._last_sent_mode
                    else:
                        # 尚未发送过位置报告（DLL 未连接），用初始值
                        k_alt = 0
                        k_gs = 0
                        k_pbh = 0
                        k_xpdr = "1200"
                        k_mode = "N"

                    keep_line = (
                        f"@{k_mode}:{self.callsign}:{k_xpdr}:{self._rating}:"
                        f"{k_lat_str}:{k_lon_str}:"
                        f"{k_alt}:{k_gs}:{k_pbh}:0"
                    )
                await self._send_line(keep_line)
                logger.debug("Keepalive sent (alt=%d gs=%d pbh=%d)", k_alt, k_gs, k_pbh)
            except asyncio.CancelledError:
                break
            except Exception as e:
                logger.warning("Keepalive error: %s", e)

    # ── $CQ / $PI / $ZC 协议处理（Swift 兼容关键）────────────

    def _handle_cq_query(self, line: str):
        """
        处理 $CQ ClientQuery。
        Swift FSD 服务器通过此包查询客户端能力和状态。
        若不响应 CAPS 查询，服务器会判定为非合法框架而断开连接。

        格式: $CQ<sender>:<receiver>:<queryType>[:args...]
        """
        try:
            content = line[3:]  # 去掉 $CQ
            parts = content.split(":")
            if len(parts) < 3:
                return
            sender = parts[0]
            receiver = parts[1]
            qtype = parts[2]

            if qtype == "CAPS":
                # 能力查询 → 回复 $CR
                asyncio.create_task(self._send_cr_caps(sender))
                logger.debug("CAPS query from %s, responding...", sender)
            elif qtype in ("IsValidATC", "IsValidAtc"):
                # ATC 验证查询 — 作为飞行员客户端，回复空
                pass
            elif qtype == "Com1Freq":
                # 频率查询 — 暂不处理
                pass
            else:
                logger.debug("Unhandled $CQ query: %s from %s", qtype, sender)
        except Exception as e:
            logger.debug("Error handling $CQ: %s", e)

    def _handle_pi_ping(self, line: str):
        """
        处理 $PI Ping，回复 $PO Pong。
        格式: $PI<sender>:<receiver>
        """
        try:
            content = line[3:]  # 去掉 $PI
            parts = content.split(":")
            if len(parts) >= 2:
                sender = parts[0]
                receiver = parts[1]
                po_line = f"$PO{self.callsign}:{sender}"
                asyncio.create_task(self._reply_line(po_line))
                logger.debug("Ping from %s, pong sent", sender)
        except Exception as e:
            logger.debug("Error handling $PI: %s", e)

    def _handle_zc_challenge(self, line: str):
        """
        处理 $ZC 认证挑战。
        部分私有 FSD 服务器实现了 VATSIMAuth challenge-response 协议。
        格式: $ZC<sender>:<receiver>:<challengeKey>
        
        当前策略：发送空 $ZR 响应避免超时。
        若需完整 VATSIMAuth，需要 clientId + privateKey。
        """
        try:
            content = line[3:]  # 去掉 $ZC
            parts = content.split(":")
            if len(parts) >= 3:
                sender = parts[0]
                challenge = parts[2]
                # 发送空响应避免服务器超时断开
                # 完整实现需要 VATSIMAuth 库（OpenVatsimAuth）
                zr_line = f"$ZR{self.callsign}:{sender}:00000000000000000000000000000000"
                asyncio.create_task(self._reply_line(zr_line))
                logger.warning("Auth challenge from %s (len=%d), sent empty response",
                               sender, len(challenge.strip()))
        except Exception as e:
            logger.debug("Error handling $ZC: %s", e)

    async def _reply_line(self, line: str):
        """快速回复一行（不做完整调试记录）"""
        if not self._writer:
            return
        payload = line + "\r\n"
        self._writer.write(payload.encode("utf-8"))
        await self._writer.drain()

    # ── 接收循环 ──────────────────────────────────────────────────

    async def _recv_loop(self):
        """接收循环：读取服务器推送的每一行并分发处理"""
        while self._connected and self._reader:
            try:
                line = await self._read_line(timeout=self._read_timeout)
                if line is None:
                    break  # EOF
                if line:
                    self._dispatch(line)
            except asyncio.TimeoutError:
                # 长时间无数据，继续等待
                continue
            except asyncio.CancelledError:
                break
            except Exception as e:
                logger.error("Recv error: %s", e)
                self.status_changed.emit("error", f"接收错误: {e}")
                break

        # 退出接收循环，断开
        if self._connected:
            self._connected = False
            self._auth_ok = False
            # 更准确的断连原因
            reason = "连接超时：长时间无数据，服务器可能已关闭连接"
            self.status_changed.emit("disconnected", reason)

    # ── 客户端能力标志 ─────────────────────────────────────────
    # Swift 兼容的能力集：ATCINFO（可接收 ATIS）、MODELDESC（飞机信息）、
    # ACCONFIG（飞机配置）。私有 FSD 服务器通过 $CQ:CAPS 查询这些能力，
    # 若不响应会被判定为非合法客户端框架而断开连接。
    CLIENT_CAPABILITIES = {
        "ATCINFO": 1, "MODELDESC": 1, "ACCONFIG": 1,
        "FASTPOS": 0, "VISUPDATE": 0, "INTERIMPOS": 0,
    }

    async def _send_cr_caps(self, requester: str):
        """响应 $CQ:CAPS 能力查询（Swift FSD 协议兼容关键）"""
        caps_parts = [f"{k}={v}" for k, v in self.CLIENT_CAPABILITIES.items()]
        caps_str = ":".join(caps_parts)
        cr_line = f"$CR{self.callsign}:{requester}:CAPS:{caps_str}"
        self.debug_line.emit(f">>> {cr_line}")
        await self._send_line(cr_line)
        logger.debug("Client capabilities sent to %s", requester)

    # ── 接收循环 / 分发器 ──────────────────────────────────────

    def _dispatch(self, line: str):
        """根据前缀分发 FSD 消息"""
        line = line.strip()
        if not line:
            return

        # ── 核心消息类型 ──
        if line.startswith("#TM"):
            self._handle_tm(line)
        elif line.startswith("#SB"):
            self._handle_sb(line)
        elif line.startswith("@") or line.startswith("^"):
            # @ 飞行员位置更新 / ^ 可视位置更新
            self._handle_at_traffic(line)
        elif line.startswith("#AP"):
            self._handle_ap_traffic(line)
        elif line.startswith("#DP"):
            self._handle_dp_traffic(line)
        elif line.startswith("#ER"):
            self._handle_error(line)

        # ── 服务器查询与认证（Swift 兼容关键）──
        elif line.startswith("$CQ"):
            # 客户端能力查询（CAPS/IsValidATC/Com1Freq 等）
            self._handle_cq_query(line)
        elif line.startswith("$PI"):
            # Ping → 回复 Pong
            self._handle_pi_ping(line)
        elif line.startswith("$ZC"):
            # 认证挑战 — 若服务器实现了 VATSIMAuth，需响应
            self._handle_zc_challenge(line)
        elif line.startswith("$DI"):
            # 服务器标识（已处理），后续重复忽略
            pass
        elif line.startswith("$CR"):
            # 其他客户端的 ClientResponse，忽略
            pass
        elif line.startswith("$PO"):
            # Pong 响应，忽略
            pass
        elif line.startswith("$XX"):
            # Rehost 重定向（暂不处理）
            logger.info("Server rehost request: %s", line[:120])

        # ── 被动接收 ──
        elif line.startswith("#PC"):
            self._handle_pc(line)
        elif line.startswith("#AA"):
            # 其他客户端认证数据，忽略
            pass
        elif line.startswith("#DA"):
            # ATC 断开
            pass
        elif line.startswith("$FP") or line.startswith("$HO") or line.startswith("$HA"):
            # 飞行计划 / 移交
            pass
        elif line.startswith("#SL") or line.startswith("#ST"):
            # 可视位置数据流控制（Swift 兼容）
            pass
        elif line.startswith("#DL"):
            # 服务器心跳，忽略即可（#TM/#SB keepalive 已有覆盖）
            pass
        elif line.startswith("#MU"):
            # 静音指令，忽略
            pass
        elif line.startswith("$SF"):
            # 可视位置开关，忽略
            pass

        # ── 未知消息 ──
        else:
            # 只记录日志，不断开连接
            logger.debug("Unhandled FSD message: %s", line[:80])

    async def _send_line(self, line: str):
        """发送一行 FSD 协议数据"""
        if not self._writer:
            return
        payload = line + "\r\n"
        self._writer.write(payload.encode("utf-8"))
        await self._writer.drain()

    async def _read_line(self, timeout: float = 30.0) -> Optional[str]:
        """读取一行（以 \\r\\n 分隔），超时返回 None"""
        if not self._reader:
            return None

        try:
            raw = await asyncio.wait_for(
                self._reader.readline(),
                timeout=timeout
            )
        except asyncio.TimeoutError:
            return None

        if not raw:  # EOF
            return None

        text = raw.decode("utf-8", errors="replace").rstrip("\r\n")
        return text

    async def _close_connection(self):
        """关闭 TCP 连接"""
        self._auth_ok = False
        self._connected = False

        # 停止 keepalive
        if self._keepalive_task and not self._keepalive_task.done():
            self._keepalive_task.cancel()
            try:
                await self._keepalive_task
            except asyncio.CancelledError:
                pass
        self._keepalive_task = None

        try:
            if self._writer:
                self._writer.close()
                await self._writer.wait_closed()
        except Exception:
            pass
        finally:
            self._reader = None
            self._writer = None

    # ── 消息处理器 ────────────────────────────────────────────────

    def _handle_tm(self, line: str):
        """
        #TM 文本消息（ATC or unicom）
        格式: #TM<source>:<dest>:<text>
        """
        # 移除 #TM 前缀后解析
        parts = line[3:].split(":", 2)
        if len(parts) >= 3:
            source, dest, text = parts[0], parts[1], parts[2]
        elif len(parts) == 2:
            source, dest, text = parts[0], parts[1], ""
        else:
            source, dest, text = "UNKNOWN", self.callsign, line[3:]

        logger.info("[%s→%s] %s", source, dest, text)
        self.message_received.emit(source, dest, text)

    def _handle_sb(self, line: str):
        """#SB 服务器广播消息"""
        text = line[3:].strip() if len(line) > 3 else line
        logger.info("[SB] %s", text)
        self.message_received.emit("SERVER", "BROADCAST", text)

    def _handle_pc(self, line: str):
        """#PC 在线人数 / 客户端列表更新"""
        # #PC 格式多样，简单记录
        pass

    def _handle_ap_traffic(self, line: str):
        """其他飞机的 #AP 位置报告，更新 traffic 字典"""
        # 简易解析：提取 callsign 和坐标
        # 完整解析较复杂（含多种变体），这里做基本提取
        try:
            content = line[3:]  # 去掉 #AP
            if ":" in content:
                parts = content.split(":", 1)
                callsign = parts[0]
                # 过滤掉自己的飞机（服务器回传）
                if self._is_own_callsign(callsign):
                    return
                rest = parts[1] if len(parts) > 1 else ""

                # 尝试提取数据
                traffic_info = {
                    "callsign": callsign,
                    "raw": line,
                }

                # 解析后续字段
                fields = rest.split(":")
                if len(fields) >= 1:
                    traffic_info["type"] = fields[0]
                if len(fields) >= 2:
                    try:
                        traffic_info["alt_ft"] = int(fields[1])
                    except ValueError:
                        pass

                self._traffic[callsign] = traffic_info
                self.traffic_updated.emit(list(self._traffic.values()))
        except Exception:
            pass  # 解析失败忽略

    def _handle_at_traffic(self, line: str):
        """
        解析 @ 飞行员位置更新包（FSD V3.000 draft 9 / Swift Private）
        格式: @<flag>:<callsign>:<transponder>:<rating>:<lat>:<lon>:<alt>:<gs>:<PBH>:<flags>
        也可能被 MC 包装: MC:<src>:<dst>:<type>:<id>:<hops>:@<flag>:...
        """
        try:
            # 提取 @ 之后的内容
            if "@" not in line:
                return
            payload = line[line.index("@"):]  # 从 @ 开始截取

            # 去掉 @ 前缀后按 : 分割
            content = payload[1:]
            fields = content.split(":")
            if len(fields) < 10:
                return  # 至少 10 个字段

            flag = fields[0]
            callsign = fields[1]

            # 过滤掉自己的飞机（服务器回传）
            if self._is_own_callsign(callsign):
                return

            xpdr = fields[2]
            # fields[3] = rating
            lat = fields[4]
            lon = fields[5]
            alt = fields[6]
            gs = fields[7]
            pbh_str = fields[8] if len(fields) > 8 else "0"
            # fields[9] = flags

            # 解析坐标为 float（同时处理 FSD packed 和 decimal 格式）
            lat_f = parse_coord(lat, is_lon=False)
            lon_f = parse_coord(lon, is_lon=True)

            # 解包 PBH 获取朝向与地面状态
            try:
                pbh_int = int(pbh_str)
                _, _, hdg, on_gnd = unpack_pbh(pbh_int)
            except (ValueError, OverflowError):
                hdg, on_gnd = 0.0, False

            traffic_info = {
                "callsign": callsign,
                "transponder": xpdr,
                "lat": lat_f,
                "lon": lon_f,
                "alt_ft": alt,
                "gs_kts": gs,
                "heading": hdg,
                "on_ground": on_gnd,
                "flag": flag,
                "raw": line,
            }

            try:
                traffic_info["alt_ft"] = int(alt)
            except ValueError:
                pass
            try:
                traffic_info["gs_kts"] = int(gs)
            except ValueError:
                pass

            self._traffic[callsign] = traffic_info
            self.traffic_updated.emit(list(self._traffic.values()))
        except Exception:
            pass  # 解析失败忽略

    def _handle_dp_traffic(self, line: str):
        """其他飞机断开"""
        # #DP<callsign>
        callsign = line[3:].strip()
        if callsign in self._traffic:
            del self._traffic[callsign]
            self.traffic_updated.emit(list(self._traffic.values()))

    # 坐标解析已迁移至 core/fsd_protocol.py 的 parse_coord / decimal_to_packed_coord

    def _is_own_callsign(self, callsign: str) -> bool:
        """
        判断给定 callsign 是否为自己的呼号。
        兼容服务器回传时可能添加的前缀/后缀或大小写差异。
        """
        if not callsign or not self.callsign:
            return False
        own = self.callsign.strip().upper()
        other = callsign.strip().upper()
        if own == other:
            return True
        # 忽略常见分隔符差异（如 ANA19 与 ANA191 不应视为相同，
        # 但 ANA19 与 " ANA19 " 或 "@ANA19" 应视为相同）
        other_clean = "".join(ch for ch in other if ch.isalnum())
        own_clean = "".join(ch for ch in own if ch.isalnum())
        return own_clean and own_clean == other_clean

    def _handle_error(self, line: str):
        """处理服务器 #ER 错误"""
        logger.error("Server error: %s", line)
        self.status_changed.emit("error", line)
