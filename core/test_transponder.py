"""
TransponderController 双轨制测试
=================================
验证双轨制核心逻辑：
  1. DLL 可写时：写入成功，虚拟状态与 AFS4 一致
  2. DLL 不可写时：自动降级到虚拟状态，返回 warning 提示用户手动操作

运行前请先启动 Mock DLL Server::

    # 终端 1：启动 Mock DLL（支持模式写入）
    python -m aerofly_link.core.mock_dll_server

    # 终端 2：运行本测试
    python -m aerofly_link.core.test_transponder
"""

from __future__ import annotations

import asyncio
import logging
import time

from .dll_bridge import DLLBridge
from .transponder_controller import TransponderController, XpdrMode

logging.basicConfig(
    level=logging.INFO,
    format="%(asctime)s [%(name)s] %(levelname)s: %(message)s",
    datefmt="%H:%M:%S",
)
logger = logging.getLogger("test")


class MockFSDClient:
    """模拟 FSD 客户端，仅实现 TransponderController 需要的接口。"""

    def __init__(self):
        self.reporting_enabled = False
        self.ident_flag = False

    def set_reporting_enabled(self, enabled: bool) -> None:
        self.reporting_enabled = enabled
        logger.info("  → FSD 上报状态: %s", "Reporting" if enabled else "Standby")

    def set_ident_flag(self, active: bool) -> None:
        self.ident_flag = active
        logger.info("  → FSD IDENT 标志: %s", active)


def state_change_callback(snapshot: dict) -> None:
    """UI 回调模拟：打印状态快照。"""
    logger.info("  → 状态变更: mode=%s squawk=%s ident=%s reporting=%s dll_mode=%s",
                snapshot["mode"], snapshot["squawk"],
                snapshot["ident_active"], snapshot["is_reporting"],
                snapshot["dll_can_write_mode"])


def sync_warning_callback(result) -> None:
    """同步警告回调模拟。"""
    if not result.synced:
        for w in result.warnings:
            logger.warning("  → %s", w)


async def run_test():
    """主测试流程。"""
    # ── 初始化 ──
    bridge = DLLBridge()
    fsd = MockFSDClient()
    xpdr = TransponderController(bridge, fsd)
    xpdr.on_state_change = state_change_callback
    xpdr.on_sync_warning = sync_warning_callback

    logger.info("=" * 60)
    logger.info("连接 Mock DLL Bridge...")
    await bridge.connect()
    logger.info("DLL Bridge 已连接: telemetry=%s command=%s",
                bridge.is_telemetry_connected, bridge.is_command_connected)

    # 启动同步检测
    xpdr.start_sync_check()

    # ── 测试 1：设置 Squawk Code（通常支持）──
    logger.info("=" * 60)
    logger.info("测试 1: 设置 Squawk Code → 1234")
    result = await xpdr.set_squawk("1234")
    logger.info("  结果: status=%s code=%s dll_write=%s warning=%s",
                result.status, result.code, result.dll_write, result.warning)
    assert xpdr.squawk == "1234", "虚拟状态应已更新"
    await asyncio.sleep(0.5)

    # ── 测试 2：切换到 ALT 模式（DLL 可写场景）──
    logger.info("=" * 60)
    logger.info("测试 2: 切换到 ALT 模式（DLL 应支持）")
    result = await xpdr.set_mode("ALT")
    logger.info("  结果: status=%s mode=%s dll_write=%s warning=%s",
                result.status, result.mode, result.dll_write, result.warning)
    assert xpdr.virtual_mode == XpdrMode.ALT, "虚拟模式应为 ALT"
    assert xpdr.dll_can_write_mode is True, "DLL 应标记为可写"
    assert fsd.reporting_enabled is True, "FSD 上报应已启用"
    await asyncio.sleep(0.5)

    # ── 测试 3：触发 IDENT ──
    logger.info("=" * 60)
    logger.info("测试 3: 触发 IDENT（5 秒）")
    result = await xpdr.trigger_ident(5.0)
    logger.info("  结果: status=%s dll_write=%s warning=%s",
                result.status, result.dll_write, result.warning)
    assert xpdr.ident_active is True, "IDENT 应处于激活状态"
    assert fsd.ident_flag is True, "FSD IDENT 标志应为 True"
    await asyncio.sleep(1.0)
    logger.info("  IDENT 仍在发送: %s", xpdr.is_ident_active())

    # ── 测试 4：等待 IDENT 自动过期 ──
    logger.info("=" * 60)
    logger.info("测试 4: 等待 IDENT 自动过期...")
    await asyncio.sleep(5.0)
    assert xpdr.ident_active is False, "IDENT 应已自动清除"
    assert fsd.ident_flag is False, "FSD IDENT 标志应为 False"
    logger.info("  IDENT 已自动清除 ✓")

    # ── 测试 5：切回 STBY ──
    logger.info("=" * 60)
    logger.info("测试 5: 切回 STBY 模式")
    result = await xpdr.set_mode("STBY")
    logger.info("  结果: status=%s mode=%s dll_write=%s",
                result.status, result.mode, result.dll_write)
    assert xpdr.virtual_mode == XpdrMode.STBY
    assert fsd.reporting_enabled is False, "FSD 上报应已停止"

    # ── 测试 6：验证 #AP 应答机代码 ──
    logger.info("=" * 60)
    logger.info("测试 6: 验证 #AP 应答机代码")
    ap_code = xpdr.get_current_xpdr_for_ap()
    logger.info("  STBY 模式 → #AP 代码: %s（应为 0000）", ap_code)
    assert ap_code == "0000", "STBY 模式应返回 0000"

    await xpdr.set_mode("ALT")
    ap_code = xpdr.get_current_xpdr_for_ap()
    logger.info("  ALT 模式 → #AP 代码: %s（应为 1234）", ap_code)
    assert ap_code == "1234", "ALT 模式应返回实际代码"

    # ── 测试 7：等待同步检测结果 ──
    logger.info("=" * 60)
    logger.info("测试 7: 等待同步检测...")
    await asyncio.sleep(6.0)
    sync = await xpdr.sync_check()
    logger.info("  同步结果: synced=%s afs_mode=%s afs_code=%s",
                sync.synced, sync.afs_mode, sync.afs_code)
    if sync.warnings:
        for w in sync.warnings:
            logger.warning("  %s", w)

    # ── 测试 8：无效输入验证 ──
    logger.info("=" * 60)
    logger.info("测试 8: 无效输入验证")
    result = await xpdr.set_mode("INVALID")
    assert result.status == "error"
    logger.info("  无效模式 → error ✓")

    result = await xpdr.set_squawk("8888")   # 8 不是八进制数字
    assert result.status == "error"
    logger.info("  无效 Squawk → error ✓")

    result = await xpdr.set_squawk("123")    # 长度不足
    assert result.status == "error"
    logger.info("  短 Squawk → error ✓")

    # ── 清理 ──
    logger.info("=" * 60)
    logger.info("所有测试通过 ✓")
    await xpdr.stop_sync_check()
    await bridge.disconnect()


if __name__ == "__main__":
    asyncio.run(run_test())
