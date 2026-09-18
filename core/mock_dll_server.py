"""
Mock DLL Server CLI —— 模拟 AeroflyBridge.dll 的两个端口
=========================================================
在终端独立运行模拟服务器（与主程序内置的「模拟DLL」按钮共用同一实现，
见 core/mock_server.py）。适用于先起 Mock、后启动 Aerofly Link 的开发流程。

输出格式与 AeroflyBridge.dll v0.3.1 兼容：
  localhost:12345  遥测输出（扁平 JSON，模拟飞行数据）
  localhost:12346  命令输入（{"variable":"...","value":...} 格式）

用法::

    python core/mock_dll_server.py
    python core/mock_dll_server.py --airport ZBAA
    python core/mock_dll_server.py --lat 31.14 --lon 121.80
"""

from __future__ import annotations

import argparse
import asyncio
import logging

try:
    from .mock_server import MockServer
    from .dll_bridge import DEFAULT_COMMAND_PORT, DEFAULT_HOST, DEFAULT_TELEMETRY_PORT
except ImportError:  # 直接运行: python core/mock_dll_server.py
    from mock_server import MockServer
    from dll_bridge import DEFAULT_COMMAND_PORT, DEFAULT_HOST, DEFAULT_TELEMETRY_PORT

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


async def main():
    parser = argparse.ArgumentParser(description="Mock AeroflyBridge.dll Server")
    parser.add_argument(
        "--airport", default="ZSPD",
        choices=sorted(AIRPORT_COORDS),
        help="模拟起飞机场 ICAO（默认: ZSPD 上海浦东）",
    )
    parser.add_argument("--lat", type=float, help="自定义纬度（度，覆盖 --airport）")
    parser.add_argument("--lon", type=float, help="自定义经度（度，覆盖 --airport）")
    parser.add_argument("--alt", type=float, default=3500.0, help="模拟高度（米）")
    parser.add_argument("--host", default=DEFAULT_HOST)
    parser.add_argument("--telemetry-port", type=int, default=DEFAULT_TELEMETRY_PORT)
    parser.add_argument("--command-port", type=int, default=DEFAULT_COMMAND_PORT)
    args = parser.parse_args()

    logging.basicConfig(
        level=logging.INFO,
        format="[Mock DLL] %(message)s",
        datefmt="%H:%M:%S",
    )
    # mock_server 的 logger 输出到控制台
    logging.getLogger("aerofly_link.mock_server").setLevel(logging.INFO)
    logging.getLogger("aerofly_link.mock_server").addHandler(logging.StreamHandler())
    logging.getLogger("aerofly_link.mock_server").propagate = False

    if args.lat is not None and args.lon is not None:
        center_lat, center_lon = args.lat, args.lon
        airport_name = f"({args.lat}, {args.lon})"
    else:
        center_lat, center_lon = AIRPORT_COORDS[args.airport]
        airport_name = args.airport

    server = MockServer(
        center_lat=center_lat,
        center_lon=center_lon,
        alt_m=args.alt,
        host=args.host,
        telemetry_port=args.telemetry_port,
        command_port=args.command_port,
    )
    ok = await server.start()
    if not ok:
        print(f"[Mock DLL] 端口绑定失败: 请检查 {args.host}:{args.telemetry_port} "
              f"和 {args.host}:{args.command_port} 是否被占用")
        return

    print("[Mock DLL] ========================================")
    print(f"[Mock DLL]  遥测端口  {args.host}:{args.telemetry_port}")
    print(f"[Mock DLL]  命令端口  {args.host}:{args.command_port}")
    print(f"[Mock DLL]  模拟位置  {airport_name} ({center_lat:.4f}, {center_lon:.4f})")
    print(f"[Mock DLL]  输出格式  AeroflyBridge 扁平 JSON")
    print("[Mock DLL] ========================================")
    print("[Mock DLL] 按 Ctrl+C 退出")

    try:
        await asyncio.Future()   # 永久运行
    finally:
        await server.stop()


if __name__ == "__main__":
    try:
        asyncio.run(main())
    except KeyboardInterrupt:
        print("\n[Mock DLL] 已停止")
