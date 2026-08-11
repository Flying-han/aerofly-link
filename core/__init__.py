"""
Aerofly Link FSD Client Core
=============================
FSD 协议客户端核心包，包含应答机控制器与 DLL 桥接。
"""

from .dll_bridge import DLLBridge, Telemetry
from .transponder_controller import (
    TransponderController,
    XpdrMode,
    XpdrResult,
    SyncResult,
)

__all__ = [
    "DLLBridge",
    "Telemetry",
    "TransponderController",
    "XpdrMode",
    "XpdrResult",
    "SyncResult",
]
