"""
TransponderController —— 应答机控制器
======================================
集成于 FSD Client Core 内部，实现规格书 §2.1 所述的「双轨制」应答机控制：

  1. 优先尝试通过 DLL 写入 AFS4（理想路径）
  2. 若 DLL 写入失败或不支持，自动降级到虚拟状态，并提示用户手动操作

VATSIM 应答机规则：仅支持 STBY（关闭）和 ALT（Mode A+C）两种状态。
高度数据来自 DLL 读取的 alt_m，与应答机本身的模式无关。
"""

from __future__ import annotations

import asyncio
import logging
import time
from dataclasses import dataclass, field
from enum import Enum
from typing import Any, Callable, Optional, Protocol

from .dll_bridge import DLLBridge, Telemetry

logger = logging.getLogger("aerofly_link.transponder")


# ════════════════════════════════════════════════════════════════════
#  常量与枚举
# ════════════════════════════════════════════════════════════════════

class XpdrMode(str, Enum):
    """客户端虚拟应答机模式（VATSIM 简化版）。"""

    STBY = "STBY"   # 待机 —— 不发送 #AP 位置报告
    ALT = "ALT"     # 高度报告 —— 正常发送 #AP，含高度信息


# VATSIM 飞行中常用 Squawk Code
SQUAWK_VFR_US = "1200"   # 美国 VFR
SQUAWK_VFR_EU = "7000"   # 欧洲/中国 VFR
SQUAWK_EMERGENCY = "7700"
SQUAWK_RADIO_FAIL = "7600"
SQUAWK_HIJACK = "7500"

# AFS4 返回的应答机模式中，哪些视为"关闭/待机"
_AFS_MODE_STBY_SET = {"OFF", "SBY", "STBY"}
# AFS4 返回的应答机模式中，哪些视为"高度报告"
_AFS_MODE_ALT_SET = {"ON", "ALT", "TA", "TA/RA"}


# ════════════════════════════════════════════════════════════════════
#  数据结构
# ════════════════════════════════════════════════════════════════════

@dataclass
class SyncResult:
    """sync_check 的返回结果。"""

    synced: bool                          # AFS4 实际状态是否与虚拟状态一致
    warnings: list[str] = field(default_factory=list)
    afs_mode: str = "UNKNOWN"
    afs_code: str = "----"


@dataclass
class XpdrResult:
    """应答机操作的返回结果。"""

    status: str                           # "ok" / "error" / "degraded"
    mode: Optional[str] = None
    code: Optional[str] = None
    warning: Optional[str] = None         # 降级时的提示信息
    dll_write: Optional[bool] = None      # DLL 是否成功写入（None=未尝试）


# ════════════════════════════════════════════════════════════════════
#  FSDClient 协议 —— 解耦依赖
# ════════════════════════════════════════════════════════════════════

class FSDClientProtocol(Protocol):
    """FSDClient 的最小接口约定，避免循环导入。"""

    def set_reporting_enabled(self, enabled: bool) -> None: ...
    def set_ident_flag(self, active: bool) -> None: ...


# ════════════════════════════════════════════════════════════════════
#  TransponderController
# ════════════════════════════════════════════════════════════════════

class TransponderController:
    """
    应答机控制器（双轨制）。

    「双轨制」核心思想：
      - **虚拟轨道**：客户端内部维护的应答机状态（mode / squawk / ident），
        无论 DLL 是否可用，虚拟状态始终即时更新。
      - **DLL 轨道**：尝试通过 DLL Bridge 将状态写入 AFS4 游戏内。
        若 DLL 返回 unsupported 或抛出异常，标记为不可写，
        后续操作跳过 DLL 直接使用虚拟状态，并向 UI 提示用户手动操作。

    两轨并行：虚拟状态保证 VATSIM 上报逻辑正确，
    DLL 写入保证游戏内面板与客户端一致（尽力而为）。
    """

    # ── 初始化 ────────────────────────────────────────────────────

    def __init__(
        self,
        dll_bridge: DLLBridge,
        fsd_client: Optional[FSDClientProtocol] = None,
    ):
        """
        :param dll_bridge: DLL Bridge 客户端实例（已连接或待连接）
        :param fsd_client: FSD 客户端实例，用于控制 VATSIM 上报行为
        """
        self.dll = dll_bridge
        self.fsd_client = fsd_client

        # ── 虚拟状态（客户端内部维护，始终有效）──
        self.virtual_mode: XpdrMode = XpdrMode.ALT
        self.squawk: str = "1200"
        self.ident_active: bool = False
        self.ident_until: float = 0.0

        # ── DLL 写入能力探测 ──
        # None = 尚未测试  True = 支持  False = 不支持/不可达
        self.dll_can_write_mode: Optional[bool] = None
        self.dll_can_write_code: Optional[bool] = None

        # ── 同步检测 ──
        self.sync_check_interval: float = 5.0   # 每 5 秒检测一次
        self._sync_task: Optional[asyncio.Task] = None
        self._last_sync: Optional[SyncResult] = None

        # ── UI 回调 ──
        # 当应答机状态变化时通知 UI
        self.on_state_change: Optional[Callable[[dict], None]] = None
        # 当同步检测产生警告时通知 UI
        self.on_sync_warning: Optional[Callable[[SyncResult], None]] = None

    # ── 属性 ──────────────────────────────────────────────────────

    @property
    def mode(self) -> XpdrMode:
        """当前虚拟应答机模式。"""
        return self.virtual_mode

    @property
    def is_reporting(self) -> bool:
        """是否正在向 VATSIM 上报位置（ALT 模式 = True）。"""
        return self.virtual_mode == XpdrMode.ALT

    @property
    def dll_writable(self) -> bool:
        """DLL 是否可以写入应答机模式（None 表示尚未测试）。"""
        return self.dll_can_write_mode

    # ═══════════════════════════════════════════════════════════════
    #  核心操作
    # ═══════════════════════════════════════════════════════════════

    async def set_mode(self, mode: str | XpdrMode) -> XpdrResult:
        """
        切换应答机模式（STBY ↔ ALT）—— 双轨制核心入口。

        流程：
          1. 验证模式合法性
          2. 立即更新虚拟状态（保证 VATSIM 上报逻辑即时生效）
          3. 若 DLL 写入能力未被标记为 False，尝试通过 DLL 写入 AFS4
             - 成功 → dll_can_write_mode = True
             - 返回 unsupported → dll_can_write_mode = False，附带降级提示
             - 异常 → dll_can_write_mode = False，附带错误信息
          4. 同步 VATSIM 上报行为
          5. 通知 UI

        :param mode: "STBY" 或 "ALT"（也接受 XpdrMode 枚举）
        :return: XpdrResult，status 为 "ok"（DLL 成功）或 "degraded"（降级到虚拟）
        """
        # ── 1. 验证 ──
        if isinstance(mode, str):
            mode_upper = mode.upper()
            if mode_upper not in ("STBY", "ALT"):
                return XpdrResult(
                    status="error",
                    warning=f"无效的应答机模式: {mode}（仅支持 STBY / ALT）",
                )
            target_mode = XpdrMode(mode_upper)
        else:
            target_mode = mode

        # ── 2. 立即更新虚拟状态（不依赖 DLL）──
        self.virtual_mode = target_mode
        logger.info("应答机模式切换 → %s（虚拟状态已更新）", target_mode.value)

        result = XpdrResult(status="ok", mode=target_mode.value)

        # ── 3. 尝试 DLL 写入（双轨制：DLL 轨道）──
        if self.dll_can_write_mode is not False:
            try:
                dll_resp = await self.dll.send_command(
                    "set_xpdr_mode", {"mode": target_mode.value}
                )
                status = dll_resp.get("status", "")

                if status == "ok":
                    self.dll_can_write_mode = True
                    result.dll_write = True
                    logger.info("DLL 写入应答机模式 %s 成功", target_mode.value)

                elif status == "unsupported":
                    # AFS4 不支持外部控制应答机模式 —— 降级到虚拟模式
                    self.dll_can_write_mode = False
                    result.status = "degraded"
                    result.dll_write = False
                    result.warning = (
                        "AFS4 不支持外部控制应答机模式，"
                        "请手动将 AFS4 面板调至 "
                        f"{'ALT' if target_mode == XpdrMode.ALT else 'SBY'}"
                    )
                    logger.warning("DLL 不支持写入应答机模式，已降级到虚拟状态")

                else:
                    self.dll_can_write_mode = False
                    result.status = "degraded"
                    result.dll_write = False
                    result.warning = f"DLL 返回未知状态: {status}，请手动调整 AFS4 面板"

            except ConnectionError as e:
                self.dll_can_write_mode = False
                result.status = "degraded"
                result.dll_write = False
                result.warning = f"DLL 连接不可用: {e}，请手动调整 AFS4 面板"
                logger.warning("DLL 命令端口连接失败: %s", e)

            except asyncio.TimeoutError:
                self.dll_can_write_mode = False
                result.status = "degraded"
                result.dll_write = False
                result.warning = "DLL 响应超时，请手动调整 AFS4 面板"
                logger.warning("DLL 写入应答机模式超时")

            except Exception as e:
                self.dll_can_write_mode = False
                result.status = "degraded"
                result.dll_write = False
                result.warning = f"DLL 控制失败: {e}，请手动调整 AFS4 面板"
                logger.error("DLL 写入应答机模式异常: %s", e, exc_info=True)

        else:
            # 已知 DLL 不支持，直接使用虚拟状态
            result.status = "degraded"
            result.dll_write = False
            result.warning = (
                "AFS4 不支持外部控制应答机模式，"
                "请手动将 AFS4 面板调至 "
                f"{'ALT' if target_mode == XpdrMode.ALT else 'SBY'}"
            )

        # ── 4. 同步 VATSIM 上报行为 ──
        self._update_vatsim_reporting(target_mode)

        # ── 5. 通知 UI ──
        self._notify_state_change()

        return result

    async def set_squawk(self, code: str) -> XpdrResult:
        """
        设置应答机 Squawk Code（4 位八进制）。

        Squawk Code 写入通常比模式写入更受 AFS4 支持，
        但仍采用双轨制：虚拟状态先更新，DLL 写入为尽力而为。

        :param code: 4 位八进制字符串，每位仅 0-7，如 "7000", "1234"
        :return: XpdrResult
        """
        # ── 验证 ──
        if not self._validate_squawk(code):
            return XpdrResult(
                status="error",
                warning=(
                    f"无效的 Squawk Code: {code}"
                    "（须为 4 位八进制，每位 0-7）"
                ),
            )

        # ── 更新虚拟状态 ──
        self.squawk = code
        logger.info("Squawk Code 设置 → %s", code)

        result = XpdrResult(status="ok", code=code)

        # ── 尝试 DLL 写入 ──
        if self.dll_can_write_code is not False:
            try:
                dll_resp = await self.dll.send_command(
                    "set_xpdr_code", {"code": code}
                )
                if dll_resp.get("status") == "ok":
                    self.dll_can_write_code = True
                    result.dll_write = True
                elif dll_resp.get("status") == "unsupported":
                    self.dll_can_write_code = False
                    result.status = "degraded"
                    result.dll_write = False
                    result.warning = (
                        f"AFS4 不支持外部设置 Squawk Code，"
                        f"请手动在 AFS4 面板输入 {code}"
                    )
                else:
                    result.dll_write = False
            except Exception as e:
                self.dll_can_write_code = False
                result.status = "degraded"
                result.dll_write = False
                result.warning = (
                    f"DLL 设置 Squawk Code 失败: {e}，"
                    f"请手动在 AFS4 面板输入 {code}"
                )
                logger.warning("DLL 写入 Squawk Code 失败: %s", e)

        # 通知 UI
        self._notify_state_change()
        return result

    async def trigger_ident(self, duration: float = 5.0) -> XpdrResult:
        """
        触发 IDENT 信号，持续指定时长后自动关闭。

        IDENT 使 ATC 雷达上的飞机标签闪烁/高亮。
        无论 DLL 是否支持，虚拟 IDENT 状态都会生效并控制 VATSIM 上报。

        :param duration: IDENT 持续秒数（默认 5 秒）
        :return: XpdrResult
        """
        self.ident_active = True
        self.ident_until = time.time() + duration
        logger.info("IDENT 触发，持续 %.1f 秒", duration)

        result = XpdrResult(status="ok")

        # 尝试通过 DLL 触发 AFS4 IDENT（尽力而为）
        try:
            dll_resp = await self.dll.send_command(
                "ident", {"duration": int(duration)}
            )
            if dll_resp.get("status") == "ok":
                result.dll_write = True
            elif dll_resp.get("status") == "unsupported":
                result.dll_write = False
                result.warning = "AFS4 不支持外部触发 IDENT，请在面板上手动操作"
            else:
                result.dll_write = False
        except Exception as e:
            result.dll_write = False
            result.warning = f"DLL IDENT 触发失败: {e}"
            logger.warning("DLL IDENT 触发失败: %s", e)

        # 通知 FSD 客户端在 #AP 中添加 IDENT 标志
        if self.fsd_client:
            self.fsd_client.set_ident_flag(True)

        # 定时自动清除
        asyncio.create_task(self._auto_clear_ident(duration))

        self._notify_state_change()
        return result

    # ═══════════════════════════════════════════════════════════════
    #  同步检测
    # ═══════════════════════════════════════════════════════════════

    async def sync_check(self) -> SyncResult:
        """
        检测 AFS4 内实际应答机状态与客户端虚拟状态是否一致。

        每 5 秒自动执行一次（由 start_sync_check 启动）。
        也可手动调用。

        检测内容：
          1. 模式不一致：AFS4 显示 SBY 但客户端设为 ALT（或反之）
          2. 代码不一致：AFS4 显示 7000 但客户端设为 1234

        :return: SyncResult
        """
        try:
            afs_data = await self.dll.get_telemetry(timeout=3.0)
            if afs_data is None:
                result = SyncResult(
                    synced=False,
                    warnings=["无法获取 AFS4 遥测数据，同步检测跳过"],
                )
                self._last_sync = result
                return result

            afs_mode_raw = afs_data.xpdr_mode or "UNKNOWN"
            afs_code = afs_data.xpdr_code or "----"

            warnings: list[str] = []

            # 模式不一致检测
            afs_mode_simplified = self._simplify_afs_mode(afs_mode_raw)
            if afs_mode_simplified != self.virtual_mode.value:
                warnings.append(
                    f"⚠️ AFS4 应答机模式为 {afs_mode_raw}，"
                    f"客户端设置为 {self.virtual_mode.value}，"
                    f"请在 AFS4 内手动调整"
                )

            # 代码不一致检测
            if afs_code != self.squawk and afs_code != "----":
                warnings.append(
                    f"⚠️ AFS4 应答机代码为 {afs_code}，"
                    f"客户端设置为 {self.squawk}"
                )

            result = SyncResult(
                synced=len(warnings) == 0,
                warnings=warnings,
                afs_mode=afs_mode_raw,
                afs_code=afs_code,
            )
            self._last_sync = result

            if warnings and self.on_sync_warning:
                self.on_sync_warning(result)

            return result

        except Exception as e:
            result = SyncResult(
                synced=False,
                warnings=[f"同步检测失败: {e}"],
            )
            self._last_sync = result
            return result

    def start_sync_check(self) -> None:
        """启动周期性同步检测后台任务。"""
        if self._sync_task and not self._sync_task.done():
            return
        self._sync_task = asyncio.create_task(self._sync_check_loop())
        logger.info("同步检测已启动，间隔 %.1fs", self.sync_check_interval)

    async def stop_sync_check(self) -> None:
        """停止周期性同步检测。"""
        if self._sync_task and not self._sync_task.done():
            self._sync_task.cancel()
            try:
                await self._sync_task
            except asyncio.CancelledError:
                pass
        self._sync_task = None

    async def _sync_check_loop(self) -> None:
        """周期性同步检测循环。"""
        while True:
            try:
                await asyncio.sleep(self.sync_check_interval)
                await self.sync_check()
            except asyncio.CancelledError:
                break
            except Exception as e:
                logger.error("同步检测循环异常: %s", e)

    # ═══════════════════════════════════════════════════════════════
    #  VATSIM 上报控制
    # ═══════════════════════════════════════════════════════════════

    def _update_vatsim_reporting(self, mode: XpdrMode) -> None:
        """
        根据应答机模式控制 VATSIM 位置上报行为。

        - STBY：停止发送 #AP 位置报告（ATC 雷达看不到你）
        - ALT：正常发送 #AP（含高度信息，ATC 雷达正常显示）
        """
        if self.fsd_client:
            self.fsd_client.set_reporting_enabled(mode == XpdrMode.ALT)
            logger.debug(
                "VATSIM 上报: %s",
                "Reporting" if mode == XpdrMode.ALT else "Standby",
            )

    def get_current_xpdr_for_ap(self) -> str:
        """
        获取当前用于 #AP 消息的应答机代码。

        STBY 模式返回 "0000"（FSDClient 可据此跳过发送 #AP），
        ALT 模式返回实际 Squawk Code。

        :return: 4 位八进制字符串
        """
        if self.virtual_mode == XpdrMode.STBY:
            return "0000"
        return self.squawk

    def is_ident_active(self) -> bool:
        """
        IDENT 是否正在发送（含自动过期检测）。

        :return: True 如果 IDENT 仍在有效期内
        """
        if self.ident_active and time.time() > self.ident_until:
            self.ident_active = False
            if self.fsd_client:
                self.fsd_client.set_ident_flag(False)
            self._notify_state_change()
        return self.ident_active

    # ═══════════════════════════════════════════════════════════════
    #  内部辅助方法
    # ═══════════════════════════════════════════════════════════════

    async def _auto_clear_ident(self, duration: float) -> None:
        """IDENT 到期后自动清除。"""
        try:
            await asyncio.sleep(duration)
        except asyncio.CancelledError:
            return
        self.ident_active = False
        if self.fsd_client:
            self.fsd_client.set_ident_flag(False)
        logger.info("IDENT 已自动清除")
        self._notify_state_change()

    @staticmethod
    def _validate_squawk(code: str) -> bool:
        """
        验证 Squawk Code 格式：4 位八进制（每位仅 0-7）。

        :param code: 待验证的代码字符串
        :return: True 如果合法
        """
        if not isinstance(code, str) or len(code) != 4:
            return False
        return all(c in "01234567" for c in code)

    @staticmethod
    def _simplify_afs_mode(afs_mode: str) -> str:
        """
        将 AFS4 返回的应答机模式简化为 VATSIM 两种状态。

        AFS4 可能返回 OFF/SBY/ON/ALT/TA/TA-RA 等，
        简化为 "STBY"（关闭/待机）或 "ALT"（报告）。

        :param afs_mode: AFS4 原始模式字符串
        :return: "STBY" 或 "ALT"
        """
        mode_upper = (afs_mode or "").upper()
        if mode_upper in _AFS_MODE_STBY_SET:
            return "STBY"
        # ON/ALT/TA/TA-RA 等均视为高度报告
        return "ALT"

    def _notify_state_change(self) -> None:
        """通知 UI 应答机状态已变化。"""
        if self.on_state_change:
            self.on_state_change(self.get_state_snapshot())

    def get_state_snapshot(self) -> dict:
        """
        获取当前应答机状态的完整快照（供 UI 显示）。

        :return: 包含所有状态字段的字典
        """
        return {
            "mode": self.virtual_mode.value,
            "squawk": self.squawk,
            "ident_active": self.is_ident_active(),
            "is_reporting": self.is_reporting,
            "dll_can_write_mode": self.dll_can_write_mode,
            "dll_can_write_code": self.dll_can_write_code,
            "last_sync_synced": self._last_sync.synced if self._last_sync else None,
            "last_sync_warnings": (
                self._last_sync.warnings if self._last_sync else []
            ),
            "afs_mode": self._last_sync.afs_mode if self._last_sync else "UNKNOWN",
            "afs_code": self._last_sync.afs_code if self._last_sync else "----",
        }
