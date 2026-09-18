# -*- coding: utf-8 -*-
"""
MockServer 兼容性测试
=====================
验证内置 Mock 服务器输出与 dll_bridge.VAR_MAP 的解析完全对齐
（AeroflyBridge 扁平 JSON 契约），以及命令端口行为。

注意：每个 scenario 的客户端 writer 必须先于 server.stop() 关闭 ——
Python 3.12+ 的 Server.wait_closed() 会等待所有连接处理器退出。
"""
import asyncio
import json

from core.dll_bridge import Telemetry
from core.mock_dll_server import AIRPORT_COORDS
from core.mock_server import MockServer


def test_mock_telemetry_parses_into_telemetry():
    """遥测帧必须是 VAR_MAP 可识别的扁平 JSON，且坐标/航向转换后数值合理。"""

    async def scenario():
        server = MockServer(center_lat=31.1434, center_lon=121.8082)
        assert await server.start() is True
        reader, writer = await asyncio.open_connection("127.0.0.1", 12345)
        try:
            raw = await asyncio.wait_for(reader.readline(), timeout=2.0)
            return json.loads(raw.decode("utf-8"))
        finally:
            writer.close()
            await server.stop()

    frame = asyncio.run(scenario())

    # 关键变量必须存在（与真实 AeroflyBridge.dll 相同的键名）
    for key in ("Aircraft.Latitude", "Aircraft.Longitude", "Aircraft.Altitude",
                "Aircraft.TrueHeading", "Aircraft.GroundSpeed",
                "Communication.TransponderCode"):
        assert key in frame, f"缺少变量 {key}"

    t = Telemetry.from_aerofly_bridge(frame)
    assert abs(t.lat - 31.1434) < 0.5, "纬度（弧度→度）转换错误"
    assert abs(t.lon - 121.8082) < 0.5, "经度（弧度→度）转换错误"
    assert 0.0 <= t.hdg_true < 360.0, "航向应为 0-360 罗盘度"


def test_mock_command_sets_xpdr():
    """命令端口可写 TransponderCode（应答机双轨制的 DLL 轨道依赖此行为）。"""

    async def scenario():
        server = MockServer()
        assert await server.start() is True
        reader, writer = await asyncio.open_connection("127.0.0.1", 12346)
        try:
            payload = json.dumps(
                {"variable": "Communication.TransponderCode", "value": 7000})
            writer.write((payload + "\n").encode("utf-8"))
            await writer.drain()
            raw = await asyncio.wait_for(reader.readline(), timeout=2.0)
            return json.loads(raw.decode("utf-8"))
        finally:
            writer.close()
            await server.stop()

    resp = asyncio.run(scenario())
    assert resp == {"status": "ok"}


def test_unknown_variable_rejected():
    async def scenario():
        server = MockServer()
        assert await server.start() is True
        reader, writer = await asyncio.open_connection("127.0.0.1", 12346)
        try:
            payload = json.dumps({"variable": "Foo.Bar", "value": 1})
            writer.write((payload + "\n").encode("utf-8"))
            await writer.drain()
            raw = await asyncio.wait_for(reader.readline(), timeout=2.0)
            return json.loads(raw.decode("utf-8"))
        finally:
            writer.close()
            await server.stop()

    resp = asyncio.run(scenario())
    assert resp["status"] == "error"


def test_port_conflict_returns_false():
    """端口被占用时 start() 返回 False 而不是抛异常（主程序依赖此行为降级）。"""

    async def scenario():
        first = MockServer()
        assert await first.start() is True
        try:
            second = MockServer()
            return await second.start()
        finally:
            await first.stop()

    assert asyncio.run(scenario()) is False


def test_airport_coords_sane():
    for icao, (lat, lon) in AIRPORT_COORDS.items():
        assert len(icao) == 4
        assert -90 <= lat <= 90
        assert -180 <= lon <= 180
