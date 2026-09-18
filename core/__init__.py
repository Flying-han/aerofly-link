"""
Aerofly Link FSD Client Core
=============================
FSD 协议客户端核心包，包含应答机控制器与 DLL 桥接。

版本号单一来源：main.py 与文档从此处读取。
发布时同步修改：installer/installer.py 的 APP_VERSION、
tools/aerofly_link_setup.iss 的 MyAppVersion（见 docs/RELEASE.md）。
"""

# 应用版本（语义化版本）
__version__ = "1.1.0"

from .dll_bridge import DLLBridge, Telemetry
from .transponder_controller import (
    TransponderController,
    XpdrMode,
    XpdrResult,
    SyncResult,
)

__all__ = [
    "__version__",
    "DLLBridge",
    "Telemetry",
    "TransponderController",
    "XpdrMode",
    "XpdrResult",
    "SyncResult",
]
