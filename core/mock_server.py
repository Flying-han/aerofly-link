"""
内置 Mock 服务器 —— 模拟 AeroflyBridge.dll
===========================================
当 AF4 游戏无法加载 external DLL 时（如非正版游戏），
Aerofly Link 可自动启动此模拟服务器，提供模拟飞行数据。

输出格式与 AeroflyBridge.dll v0.3.1 完全兼容：
  12345 遥测：扁平 JSON，单位与真实 DLL 一致（弧度/m/s/Hz）
  12346 命令：{"variable":"...","value":...}

使用::

    await mock = MockServer(center_lat=31.1434, center_lon=121.8082)
    await mock.start()
    # ... Aerofly Link 自动连接 ...
    await mock.stop()
"""

from __future__ import annotations

import asyncio
import json
import math
import time
import logging
from typing import Optional

logger = logging.getLogger("aerofly_link.mock_server")

DEFAULT_TELEMETRY_PORT = 12345
DEFAULT_COMMAND_PORT = 12346
DEFAULT_HOST = "127.0.0.1"


class MockServer:
    """
    内建 Mock 服务器，模拟 AeroflyBridge.dll 的行为。

    启动后监听 localhost:12345（遥测）和 localhost:12346（命令），
    数据格式与 AeroflyBridge.dll v0.3.1 一致。
    """

    def __init__(
        self,
        center_lat: float = 31.1434,
        center_lon: float = 121.8082,
        alt_m: float = 3500.0,
        host: str = DEFAULT_HOST,
        telemetry_port: int = DEFAULT_TELEMETRY_PORT,
        command_port: int = DEFAULT_COMMAND_PORT,
    ):
        self._host = host
        self._telemetry_port = telemetry_port
        self._command_port = command_port

        self._center_lat = center_lat
        self._center_lon = center_lon
        self._radius = 0.02    # 绕圈半径（度）
        self._alt_m = alt_m

        self._t0 = time.time()
        self._xpdr_code: str = "7000"
        self._com1_freq_hz: int = 122800000
        self._com2_freq_hz: int = 119500000

        self._telemetry_server = None
        self._command_server = None
        self._running = False

    @property
    def is_running(self) -> bool:
        return self._running

    @property
    def xpdr_code(self) -> str:
        return self._xpdr_code

    async def start(self) -> bool:
        """启动 Mock 服务器。返回 True 表示成功。"""
        if self._running:
            return True

        try:
            self._telemetry_server = await asyncio.start_server(
                self._handle_telemetry,
                self._host,
                self._telemetry_port,
            )
            self._command_server = await asyncio.start_server(
                self._handle_command,
                self._host,
                self._command_port,
            )
        except OSError as e:
            logger.error("Mock 服务器端口绑定失败: %s", e)
            await self.stop()
            return False

        self._running = True
        logger.info(
            "Mock 服务器已启动 %s:%d(遥测) %s:%d(命令)",
            self._host, self._telemetry_port,
            self._host, self._command_port,
        )
        return True

    async def stop(self) -> None:
        """停止 Mock 服务器。"""
        self._running = False

        for server in (self._telemetry_server, self._command_server):
            if server is not None:
                server.close()
                try:
                    await server.wait_closed()
                except Exception:
                    pass

        self._telemetry_server = None
        self._command_server = None
        logger.info("Mock 服务器已停止")

    # ── 遥测生成 ──────────────────────────────────────────────────

    def _generate_telemetry(self) -> dict:
        """生成与 AeroflyBridge.dll 兼容的扁平 JSON 遥测。

        飞机沿心形曲线 (heart curve) 飞行，视觉上更直观。

        心形参数方程:
            x = 16 * sin³(t)
            y = 13*cos(t) - 5*cos(2t) - 2*cos(3t) - cos(4t)

        缩放后映射到地理坐标（中心点 + 半径偏移）。
        """
        t = time.time() - self._t0
        angle = t * 0.06  # 慢一点方便看清爱心形状

        # 心形曲线（归一化到 [-1, 1]）
        sin_t = math.sin(angle)
        cos_t = math.cos(angle)
        hx = math.pow(sin_t, 3)                     # [-1, 1]
        hy = (13*cos_t - 5*math.cos(2*angle)
              - 2*math.cos(3*angle) - math.cos(4*angle)) / 17.0  # 归一化到 ≈[-1, 1]

        # 心形导数（用于航向）
        dx = 3 * sin_t * sin_t * cos_t              # dx/dt / 16
        dy = (-13*sin_t + 10*math.sin(2*angle)
              + 6*math.sin(3*angle) + 4*math.sin(4*angle)) / 16.0

        hdg_deg = (math.degrees(math.atan2(dx, dy)) + 90) % 360

        lat_deg = self._center_lat + self._radius * hy
        lon_deg = self._center_lon + self._radius * hx

        return {
            "Aircraft.Latitude":  math.radians(lat_deg),
            "Aircraft.Longitude": math.radians(lon_deg),
            "Aircraft.Altitude":  self._alt_m + 200 * math.sin(t * 0.03),
            "Aircraft.HeightAboveGround": 800.0,
            "Aircraft.TrueHeading":      math.radians(hdg_deg),
            "Aircraft.MagneticHeading":  math.radians(hdg_deg),
            "Aircraft.Pitch":            0.05 * math.sin(t * 0.2),
            "Aircraft.Bank":             0.03 * math.cos(t * 0.3),
            "Aircraft.IndicatedAirspeed": 72.0,
            "Aircraft.GroundSpeed":       70.0,
            "Aircraft.VerticalSpeed":     2.5 * math.sin(t * 0.03),
            "Aircraft.MachNumber":        0.42,
            "Communication.COM1Frequency": self._com1_freq_hz,
            "Communication.COM2Frequency": self._com2_freq_hz,
            "Communication.TransponderCode": int(self._xpdr_code),
            "Aircraft.Gear":     0.0,
            "Aircraft.Flaps":    0.0,
            "Aircraft.OnGround": 0.0,
            "Aircraft.OnRunway": 0.0,
            "Aircraft.AngleOfAttack": 0.04,
            "Aircraft.RateOfTurn":     0.0,
            "Aircraft.Name": "B738",
            "Autopilot.Master":              1.0,
            "Autopilot.SelectedAltitude":    self._alt_m + 200 * math.sin(t * 0.03 + 1.0),
            "Autopilot.SelectedHeading":     math.radians(hdg_deg),
            "Autopilot.SelectedVerticalSpeed": 0.0,
            "Aircraft.EngineRunning1": 1.0,
            "Aircraft.EngineRunning2": 1.0,
        }

    # ── 连接处理 ──────────────────────────────────────────────────

    async def _handle_telemetry(
        self, reader: asyncio.StreamReader, writer: asyncio.StreamWriter
    ) -> None:
        """遥测端口 12345 连接处理：持续推送 JSON 行。"""
        peer = writer.get_extra_info("peername")
        logger.info("Mock 遥测客户端已连接: %s", peer)
        try:
            while self._running:
                msg = self._generate_telemetry()
                line = (json.dumps(msg, ensure_ascii=False) + "\n").encode("utf-8")
                writer.write(line)
                await writer.drain()
                await asyncio.sleep(0.1)   # 10Hz — 与真实 DLL 帧率一致
        except (ConnectionResetError, BrokenPipeError, ConnectionAbortedError):
            pass
        except asyncio.CancelledError:
            pass
        except Exception as e:
            logger.debug("Mock 遥测异常: %s", e)
        finally:
            logger.info("Mock 遥测客户端断开: %s", peer)
            try:
                writer.close()
            except Exception:
                pass

    async def _handle_command(
        self, reader: asyncio.StreamReader, writer: asyncio.StreamWriter
    ) -> None:
        """命令端口 12346 连接处理：接收 AeroflyBridge JSON 命令。"""
        peer = writer.get_extra_info("peername")
        logger.info("Mock 命令客户端已连接: %s", peer)
        try:
            while self._running:
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

                if variable == "Communication.TransponderCode":
                    self._xpdr_code = str(int(value)).zfill(4)
                    resp = {"status": "ok"}
                elif variable == "Communication.COM1Frequency":
                    self._com1_freq_hz = int(value)
                    resp = {"status": "ok"}
                elif variable == "Communication.COM2Frequency":
                    self._com2_freq_hz = int(value)
                    resp = {"status": "ok"}
                else:
                    resp = {"status": "error", "msg": f"unknown variable: {variable}"}

                writer.write((json.dumps(resp, ensure_ascii=False) + "\n").encode("utf-8"))
                await writer.drain()

        except (ConnectionResetError, BrokenPipeError, ConnectionAbortedError):
            pass
        except asyncio.CancelledError:
            pass
        except Exception as e:
            logger.debug("Mock 命令异常: %s", e)
        finally:
            logger.info("Mock 命令客户端断开: %s", peer)
            try:
                writer.close()
            except Exception:
                pass
