# -*- coding: utf-8 -*-
"""
FSD 协议常量与工具函数
======================
从 fsd_client.py 中拆出的纯协议层代码，无 Qt 依赖，
方便单元测试和复用。

内容：
  - 协议版本号 / 客户端标识常量
  - PBH (Pitch/Bank/Heading) 位打包/解包
  - 坐标格式转换（十进制 <-> FSD packed）
  - 应答机模式映射
"""
from __future__ import annotations

import math

# ── 协议版本号 ─────────────────────────────────────────────────
PROTOCOL_VERSION_LEGACY = 9  # FSD V3.000 draft 9 / Swift private
PROTOCOL_VERSION_VATSIM = 100  # VATSIM 现代协议

# ── 客户端标识 ─────────────────────────────────────────────────
CLIENT_NAME = "Aerofly Link"
CLIENT_VERSION = "1.0"
SIMULATOR_TYPE = "Aerofly FS 4"
SIM_TYPE_CODE = "0"  # FSD simtype 标识（0=未知）
DEFAULT_RATING = 2  # 默认飞行员等级（S1=Student Pilot）
CLIENT_ID_HEX = "0000"  # 未授权客户端，跳过 challenge

# ── PBH 位打包常量（与 Swift 一致）────────────────────────────
PBH_PITCH_MULT = 256.0 / 90.0
PBH_BANK_MULT = 512.0 / 180.0
PBH_HDG_MULT = 1024.0 / 360.0


def pack_pbh(pitch_deg: float, bank_deg: float, heading_deg: float, on_ground: bool) -> int:
    """
    将 Pitch/Bank/Heading 打包为 FSD 32 位无符号整数（与 Swift packPBH 一致）。

    Swift PBH union bit-field 布局（C++ bit-field 从 LSB 开始分配）:
      bit 0:   unused
      bit 1:   onGround
      bit 2-11: heading (10-bit unsigned)
      bit 12-21: bank (10-bit signed, inverted)
      bit 22-31: pitch (10-bit signed, inverted)

    FSD 协议中 pitch 和 bank 被反转（取负），与 vPilot 一致。
    """
    p = int(math.floor(pitch_deg * -PBH_PITCH_MULT))
    b = int(math.floor(bank_deg * -PBH_BANK_MULT))
    h = int(heading_deg * PBH_HDG_MULT)

    p = p & 0x3FF
    b = b & 0x3FF
    h = h & 0x3FF
    og = 1 if on_ground else 0

    return ((p & 0x3FF) << 22) | ((b & 0x3FF) << 12) | ((h & 0x3FF) << 2) | (og << 1)


def unpack_pbh(pbh: int):
    """从 FSD 32 位 PBH 整数解包为 (pitch_deg, bank_deg, heading_deg, on_ground)。"""
    p_raw = (pbh >> 22) & 0x3FF
    b_raw = (pbh >> 12) & 0x3FF
    h_raw = (pbh >> 2) & 0x3FF
    on_ground = ((pbh >> 1) & 1) == 1

    def _s10(v):
        if v & 0x200:
            return v - 0x400
        return v

    pitch_deg = math.floor(_s10(p_raw) / -PBH_PITCH_MULT)
    bank_deg = math.floor(_s10(b_raw) / -PBH_BANK_MULT)
    heading_deg = h_raw / PBH_HDG_MULT

    return pitch_deg, bank_deg, heading_deg, on_ground


def xpdr_mode_to_fsd_letter(mode: str) -> str:
    """
    将应答机模式映射为 FSD 协议字母。

    FSD serializer (serializer.cpp) 映射:
      StateStandby -> "S"
      ModeC/ModeA/ModeS/ModeMil* -> "N"   (所有活跃模式)
      StateIdent -> "Y"
    """
    mode_upper = (mode or "").upper()
    if mode_upper == "STBY":
        return "S"
    if mode_upper == "IDENT":
        return "Y"
    return "N"  # ALT / ModeC / 默认正常模式


def parse_coord(raw: str, is_lon: bool = False) -> float:
    """
    解析 FSD 坐标，兼容两种格式：
    1. FSD packed 格式: DDMM.mmm (lat) 或 DDDMM.mmm (lon)，值 > 180
    2. 十进制格式: DD.ddd，绝对值在 [-180, 180]
    """
    val = float(raw)
    if abs(val) <= 180:
        return val
    deg = int(val / 100)
    minutes = val - deg * 100
    return deg + minutes / 60.0


def decimal_to_packed_coord(decimal_degrees: float, is_lon: bool = False) -> str:
    """
    将十进制坐标转换为 FSD packed 格式：
    - 纬度: DDMM.mmm（南纬加负号前缀）
    - 经度: DDDMM.mmm（始终 0-360 范围，无负号）
    """
    if is_lon:
        if decimal_degrees < 0:
            decimal_degrees += 360.0
    sign = -1 if decimal_degrees < 0 else 1
    abs_val = abs(decimal_degrees)
    deg = int(abs_val)
    minutes = (abs_val - deg) * 60.0
    if is_lon:
        packed = f"{deg:03d}{minutes:06.3f}"
    else:
        packed = f"{deg:02d}{minutes:06.3f}"
    if sign < 0:
        packed = f"-{packed}"
    return packed
