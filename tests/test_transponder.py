# -*- coding: utf-8 -*-
"""
TransponderController 逻辑测试（不依赖 DLL / FSD 网络）
=======================================================
核心回归：
  1. DLL 不暴露模式时，sync_check 不得产生模式假警告
  2. 双轨制：DLL unsupported 时降级且虚拟状态即时生效
  3. STBY → #AP 代码 0000
"""
import asyncio

from core.dll_bridge import Telemetry
from core.transponder_controller import TransponderController, XpdrMode


class FakeBridge:
    """模拟 DLLBridge：可注入遥测快照与命令返回值。"""

    def __init__(self, telemetry: Telemetry | None = None, cmd_status: str = "unsupported"):
        self._telemetry = telemetry
        self._cmd_status = cmd_status
        self.commands = []

    async def get_telemetry(self, timeout: float = 3.0):
        return self._telemetry

    async def send_command(self, command: str, params=None, timeout: float = 3.0):
        self.commands.append((command, params))
        return {"status": self._cmd_status}


class FakeFSD:
    def __init__(self):
        self.reporting = None
        self.ident = None

    def set_reporting_enabled(self, enabled: bool) -> None:
        self.reporting = enabled

    def set_ident_flag(self, active: bool) -> None:
        self.ident = active


def test_sync_check_no_false_mode_warning():
    """回归：Telemetry.xpdr_mode 恒为占位 "SBY"（DLL 不暴露模式），
    此前与虚拟 ALT 模式比较导致每 5 秒弹出一次假警告。"""
    bridge = FakeBridge(telemetry=Telemetry(xpdr_code="1200"))
    xpdr = TransponderController(bridge)
    xpdr.virtual_mode = XpdrMode.ALT

    result = asyncio.run(xpdr.sync_check())
    assert result.synced is True, f"不应有假警告: {result.warnings}"
    assert result.warnings == []


def test_sync_check_detects_code_mismatch():
    bridge = FakeBridge(telemetry=Telemetry(xpdr_code="7000"))
    xpdr = TransponderController(bridge)
    xpdr.squawk = "1200"

    result = asyncio.run(xpdr.sync_check())
    assert result.synced is False
    assert any("7000" in w for w in result.warnings)


def test_degraded_mode_update_keeps_virtual_state():
    """DLL 返回 unsupported → 降级，但虚拟模式立即生效并同步 FSD 上报。"""
    bridge = FakeBridge(cmd_status="unsupported")
    fsd = FakeFSD()
    xpdr = TransponderController(bridge, fsd)

    result = asyncio.run(xpdr.set_mode("STBY"))
    assert result.status == "degraded"
    assert xpdr.virtual_mode == XpdrMode.STBY
    assert fsd.reporting is False, "STBY 必须停止 #AP 上报"
    assert xpdr.dll_can_write_mode is False


def test_stby_returns_zero_code_for_ap():
    bridge = FakeBridge()
    xpdr = TransponderController(bridge)
    asyncio.run(xpdr.set_mode("STBY"))
    assert xpdr.get_current_xpdr_for_ap() == "0000"
    asyncio.run(xpdr.set_mode("ALT"))
    assert xpdr.get_current_xpdr_for_ap() == xpdr.squawk


def test_invalid_squawk_rejected():
    bridge = FakeBridge()
    xpdr = TransponderController(bridge)
    for bad in ("8888", "123", "12A4", ""):
        result = asyncio.run(xpdr.set_squawk(bad))
        assert result.status == "error", f"{bad!r} 应被拒绝"


def test_ident_auto_clears():
    bridge = FakeBridge()
    fsd = FakeFSD()
    xpdr = TransponderController(bridge, fsd)

    asyncio.run(xpdr.trigger_ident(duration=0.1))
    assert xpdr.ident_active is True
    assert fsd.ident is True

    async def wait_and_check():
        await asyncio.sleep(0.25)
        return xpdr.is_ident_active()

    assert asyncio.run(wait_and_check()) is False
    assert fsd.ident is False
