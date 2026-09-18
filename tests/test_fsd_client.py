# -*- coding: utf-8 -*-
"""
FSDClient 无网络逻辑测试
========================
通过 monkeypatch _send_line 抓取报文，验证协议行为：
  - send_text_message 的 #TM 构造（假发送按钮的修复回归）
  - legacy keepalive 不再抛 NameError（回归）
  - traffic 信号节流
"""
import asyncio

from core.fsd_client import FSDClient


def _make_client(**overrides) -> FSDClient:
    config = {
        "callsign": "TST123", "cid": "1", "password": "x",
        "server": "127.0.0.1", "port": 6809,
        "eco": "private", "type": "legacy",
    }
    config.update(overrides)
    fsd = FSDClient(config)
    fsd._connected = True
    fsd._auth_ok = True
    fsd._reporting_enabled = True
    return fsd


def test_send_text_message_builds_tm_line():
    captured = []

    async def fake_send(line):
        captured.append(line)

    fsd = _make_client()
    fsd._send_line = fake_send
    ok = asyncio.run(fsd.send_text_message("ZGGG_TWR", "请求放行"))
    assert ok is True
    assert captured == ["#TMTST123:ZGGG_TWR:请求放行"]


def test_send_text_message_sanitizes_separator():
    """FSD 分隔符 ':' 必须被清理，否则报文字段错位。"""
    captured = []

    async def fake_send(line):
        captured.append(line)

    fsd = _make_client()
    fsd._send_line = fake_send
    asyncio.run(fsd.send_text_message("UNICOM", "hello: world"))
    assert captured == ["#TMTST123:UNICOM:hello  world"]


def test_send_text_message_rejects_empty():
    fsd = _make_client()

    async def fake_send(line):  # pragma: no cover - 不应被调用
        raise AssertionError("不应发送空消息")

    fsd._send_line = fake_send
    assert asyncio.run(fsd.send_text_message("", "hi")) is False
    assert asyncio.run(fsd.send_text_message("UNICOM", "  ")) is False


def test_send_text_message_fails_when_disconnected():
    fsd = _make_client()
    fsd._connected = False
    assert asyncio.run(fsd.send_text_message("UNICOM", "hi")) is False


def test_legacy_keepalive_no_name_error():
    """回归：legacy 心跳此前在日志语句引用未定义变量（k_alt 等），
    每 30 秒产生一条 spurious warning。"""
    fsd = _make_client(type="legacy", eco="legacy")

    async def scenario():
        sent = []

        async def fake_send(line):
            sent.append(line)

        fsd._send_line = fake_send
        fsd._keepalive_interval = 0.05
        task = asyncio.create_task(fsd._keepalive_loop())
        await asyncio.sleep(0.2)
        task.cancel()
        return sent

    sent = asyncio.run(scenario())
    assert sent, "legacy keepalive 应发送 #TM 心跳"
    assert sent[0] == "#TMTST123:SERVER:@"


def test_traffic_emit_throttled():
    """traffic_updated 每秒最多发射一次（多机高频 @ 包下避免信号风暴）。"""
    fsd = _make_client(type="vatsim", eco="vatsim")
    emissions = []
    fsd.traffic_updated.connect(lambda lst: emissions.append(lst))

    line = "@N:OTHER1:1200:2:31.1434:121.8082:11480:136:1024:0"
    for _ in range(50):
        fsd._handle_at_traffic(line)

    assert len(emissions) <= 2, f"50 个 @ 包触发了 {len(emissions)} 次信号"


def test_own_callsign_filtered():
    fsd = _make_client()
    fsd._handle_at_traffic("@N:TST123:1200:2:31.1434:121.8082:11480:136:1024:0")
    assert fsd._traffic == {}, "服务器回传的自身位置不应进入 traffic 列表"
