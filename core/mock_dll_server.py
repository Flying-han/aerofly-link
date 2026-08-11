"""
Mock DLL Server —— 模拟 AeroflyBridge.dll 的两个端口
======================================================
在没有 AFS4 或游戏不支持 external DLL 时，模拟 AeroflyBridge.dll 的 TCP 端口。

输出格式与 AeroflyBridge.dll v0.3.1 完全兼容：
  localhost:12345  遥测输出（扁平 JSON，模拟飞行数据）
  localhost:12346  命令输入（{"variable":"...","value":...} 格式）

用法::

    # 终端 1：启动 Mock DLL（先启动这个，再开 Aerofly Link）
    python mock_dll_server.py

    # 或者指定起飞机场
    python mock_dll_server.py --airport ZBAA
"""

from __future__ import annotations

import argparse
import asyncio
import json
import math
import time
from typing import Optional

try:
    from .dll_bridge import DEFAULT_COMMAND_PORT, DEFAULT_HOST, DEFAULT_TELEMETRY_PORT
except ImportError:
    from dll_bridge import DEFAULT_COMMAND_PORT, DEFAULT_HOST, DEFAULT_TELEMETRY_PORT  # 直接运行时


# 常用机场坐标（度）
AIRPORT_COORDS: dict[str, tuple[float, float]] = {
    "ZBAA": (40.0801, 116.5846),   # 北京首都
    "ZSPD": (31.1434, 121.8082),   # 上海浦东
    "ZGSZ": (22.6394, 113.8145),   # 深圳宝安
    "ZGGG": (23.3924, 113.3088),   # 广州白云
    "ZUCK": (29.7192, 106.6417),   # 重庆江北
    "ZUUU": (30.5785, 103.9466),   # 成都天府
    "EGLL": (51.4700, -0.4543),    # 伦敦希思罗
    "KJFK": (40.6413, -73.7781),   # 纽约肯尼迪
}


class MockAFS4State:
    """模拟 AFS4 内部的应答机状态。"""

    def __init__(self, center_lat: float = 31.1434, center_lon: float = 121.8082):
        self.xpdr_code: str = "7000"
        self.com1_freq_hz: int = 122800000   # AeroflyBridge 用 Hz
        self.com2_freq_hz: int = 119500000

        # 模拟飞行参数（绕圆圈飞行）
        self._t0 = time.time()
        self.center_lat = center_lat
        self.center_lon = center_lon
        self.radius = 0.02   # 度
        self.alt_m = 3500.0

    def get_telemetry(self) -> dict:
        """
        生成与 AeroflyBridge.dll 兼容的扁平 JSON 遥测数据。

        AeroflyBridge 输出格式（所有单位与真实 DLL 一致）：
          - 经纬度：弧度
          - 航向/俯仰/坡度：弧度
          - 速度：m/s
          - 高度：米
          - 频率：Hz（整数值）
        """
        t = time.time() - self._t0
        angle = t * 0.08   # 缓慢旋转

        lat_deg = self.center_lat + self.radius * math.cos(angle)
        lon_deg = self.center_lon + self.radius * math.sin(angle)
        hdg_deg = (angle * 180 / math.pi + 90) % 360   # 罗盘航向 (North=0, 顺时针)

        # AeroflyBridge 真实 DLL 以「弧度 + 数学约定 (East=0, 逆时针)」输出航向。
        # 罗盘度 H → 数学度 (90 - H) → 弧度。
        hdg_math_rad = math.radians((90 - hdg_deg) % 360)

        return {
            # ── 位置 ──
            "Aircraft.Latitude":  math.radians(lat_deg),
            "Aircraft.Longitude": math.radians(lon_deg),
            "Aircraft.Altitude":  self.alt_m + 200 * math.sin(t * 0.03),
            "Aircraft.HeightAboveGround": 800.0,

            # ── 姿态 ──
            "Aircraft.TrueHeading":      hdg_math_rad,
            "Aircraft.MagneticHeading":  hdg_math_rad,
            "Aircraft.Pitch":            0.05 * math.sin(t * 0.2),
            "Aircraft.Bank":             0.03 * math.cos(t * 0.3),

            # ── 速度（m/s → dll_bridge.py 会转为 kts）──
            "Aircraft.IndicatedAirspeed": 72.0,           # ~140 kts
            "Aircraft.GroundSpeed":       70.0,           # ~136 kts
            "Aircraft.VerticalSpeed":     2.5 * math.sin(t * 0.03),
            "Aircraft.MachNumber":        0.42,

            # ── 无线电（Hz）──
            "Communication.COM1Frequency": self.com1_freq_hz,
            "Communication.COM2Frequency": self.com2_freq_hz,
            "Communication.TransponderCode": int(self.xpdr_code),

            # ── 飞机状态 ──
            "Aircraft.Gear":     0.0,
            "Aircraft.Flaps":    0.0,
            "Aircraft.OnGround": 0.0,
            "Aircraft.OnRunway": 0.0,
            "Aircraft.AngleOfAttack": 0.04,
            "Aircraft.RateOfTurn":     0.0,

            # ── 机身信息 ──
            "Aircraft.Name": "B738",

            # ── 自动驾驶 ──
            "Autopilot.Master":              1.0,
            "Autopilot.SelectedAltitude":    self.alt_m + 200 * math.sin(t * 0.03 + 1.0),
            "Autopilot.SelectedHeading":     hdg_math_rad,
            "Autopilot.SelectedVerticalSpeed": 0.0,

            # ── 引擎 ──
            "Aircraft.EngineRunning1": 1.0,
            "Aircraft.EngineRunning2": 1.0,
        }


async def handle_telemetry_client(
    reader: asyncio.StreamReader,
    writer: asyncio.StreamWriter,
    state: MockAFS4State,
    interval: float = 0.05,
):
    """遥测端口 12345 的连接处理：持续推送 JSON 行。"""
    peer = writer.get_extra_info("peername")
    print(f"[Mock DLL] 遥测客户端已连接: {peer}")
    try:
        while True:
            msg = state.get_telemetry()
            line = (json.dumps(msg, ensure_ascii=False) + "\n").encode("utf-8")
            writer.write(line)
            await writer.drain()
            await asyncio.sleep(interval)
    except (ConnectionResetError, BrokenPipeError):
        pass
    except asyncio.CancelledError:
        pass
    finally:
        print(f"[Mock DLL] 遥测客户端断开: {peer}")
        writer.close()


async def handle_command_client(
    reader: asyncio.StreamReader,
    writer: asyncio.StreamWriter,
    state: MockAFS4State,
):
    """
    命令端口 12346 的连接处理。

    AeroflyBridge 格式：
      接收: {"variable":"Communication.TransponderCode", "value":7000}\n
      返回: {"status":"ok"}\n
    """
    peer = writer.get_extra_info("peername")
    print(f"[Mock DLL] 命令客户端已连接: {peer}")
    try:
        while True:
            line = await reader.readline()
            if not line:
                break
            try:
                msg = json.loads(line.decode("utf-8"))
            except json.JSONDecodeError:
                resp = {"status": "error", "msg": "invalid JSON"}
                writer.write((json.dumps(resp, ensure_ascii=False) + "\n").encode("utf-8"))
                await writer.drain()
                continue

            variable = msg.get("variable", "")
            value = msg.get("value")

            # ── AeroflyBridge 变量名处理 ──
            if variable == "Communication.TransponderCode":
                code = str(int(value)).zfill(4)
                state.xpdr_code = code
                resp = {"status": "ok"}
                print(f"[Mock DLL] TransponderCode → {code}")

            elif variable == "Communication.COM1Frequency":
                state.com1_freq_hz = int(value)
                resp = {"status": "ok"}
                print(f"[Mock DLL] COM1 → {value} Hz")

            elif variable == "Communication.COM2Frequency":
                state.com2_freq_hz = int(value)
                resp = {"status": "ok"}
                print(f"[Mock DLL] COM2 → {value} Hz")

            else:
                resp = {"status": "error", "msg": f"unknown variable: {variable}"}
                print(f"[Mock DLL] 未知变量: {variable}")

            writer.write((json.dumps(resp, ensure_ascii=False) + "\n").encode("utf-8"))
            await writer.drain()

    except (ConnectionResetError, BrokenPipeError):
        pass
    except asyncio.CancelledError:
        pass
    finally:
        print(f"[Mock DLL] 命令客户端断开: {peer}")
        writer.close()


async def main():
    parser = argparse.ArgumentParser(description="Mock AeroflyBridge.dll Server")
    parser.add_argument(
        "--airport", default="ZSPD",
        choices=list(AIRPORT_COORDS.keys()),
        help="模拟起飞机场 ICAO（默认: ZSPD 上海浦东）",
    )
    parser.add_argument("--lat", type=float, help="自定义纬度（度，覆盖 --airport）")
    parser.add_argument("--lon", type=float, help="自定义经度（度，覆盖 --airport）")
    parser.add_argument("--host", default=DEFAULT_HOST)
    parser.add_argument("--telemetry-port", type=int, default=DEFAULT_TELEMETRY_PORT)
    parser.add_argument("--command-port", type=int, default=DEFAULT_COMMAND_PORT)
    args = parser.parse_args()

    # 确定模拟位置
    if args.lat is not None and args.lon is not None:
        center_lat, center_lon = args.lat, args.lon
        airport_name = f"({args.lat}, {args.lon})"
    else:
        center_lat, center_lon = AIRPORT_COORDS[args.airport]
        airport_name = args.airport

    print(f"[Mock DLL] 模拟位置: {airport_name} ({center_lat:.4f}, {center_lon:.4f})")

    state = MockAFS4State(center_lat, center_lon)

    try:
        telemetry_server = await asyncio.start_server(
            lambda r, w: handle_telemetry_client(r, w, state),
            args.host, args.telemetry_port,
        )
        command_server = await asyncio.start_server(
            lambda r, w: handle_command_client(r, w, state),
            args.host, args.command_port,
        )
    except OSError as e:
        print(f"\n[Mock DLL] 端口绑定失败: {e}")
        print(f"[Mock DLL] 请检查 {args.host}:{args.telemetry_port} 和 {args.host}:{args.command_port} 是否被占用")
        print("[Mock DLL] 按 Ctrl+C 退出...")
        try:
            await asyncio.Future()
        except asyncio.CancelledError:
            pass
        return

    print(f"[Mock DLL] ========================================")
    print(f"[Mock DLL]  遥测端口  {args.host}:{args.telemetry_port}")
    print(f"[Mock DLL]  命令端口  {args.host}:{args.command_port}")
    print(f"[Mock DLL]  模拟位置  {airport_name}")
    print(f"[Mock DLL]  输出格式  AeroflyBridge 扁平 JSON")
    print(f"[Mock DLL] ========================================")
    print("[Mock DLL] 按 Ctrl+C 退出")

    async with telemetry_server, command_server:
        await asyncio.Future()   # 永久运行


if __name__ == "__main__":
    try:
        asyncio.run(main())
    except KeyboardInterrupt:
        print("\n[Mock DLL] 已停止")
