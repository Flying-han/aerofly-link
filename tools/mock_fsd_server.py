#!/usr/bin/env python3
"""
Mock FSD 服务器 —— 最小 FSD 协议联调工具
==========================================
记录客户端发来的每一行协议报文（stdout + fsd_mock.log），
用于无头客户端 / GUI 客户端的端到端验证（不连接真实 VATSIM）。

用法::
    python tools/mock_fsd_server.py [--port 6809] [--log fsd_mock.log]

行为：连接后发 $DI 标识 + #TM 欢迎；对 #AP 回 #TM 认证通过；
对 $PI 回 $PO；周期向客户端推送一条 @ 位置包与 $PI ping。
Ctrl+C 退出。
"""
import argparse
import asyncio
import datetime
import threading

clients = set()
lock = threading.Lock()
log_fh = None


def log(msg: str) -> None:
    ts = datetime.datetime.now().strftime("%H:%M:%S.%f")[:-3]
    line = f"[{ts}] {msg}"
    print(line, flush=True)
    if log_fh:
        log_fh.write(line + "\n")
        log_fh.flush()


async def push_traffic(writer) -> None:
    """周期推送 @ 位置包与 $PI（验证客户端的 traffic 表与 pong）。"""
    try:
        while True:
            await asyncio.sleep(4)
            writer.write(b"@N:ANA952:1200:2:31.20000:121.90000:35000:450:1024:0\r\n")
            writer.write(b"$PISRV01:TSTCLIENT\r\n")
            await writer.drain()
    except (ConnectionResetError, BrokenPipeError, asyncio.CancelledError):
        pass


async def handle(reader: asyncio.StreamReader, writer: asyncio.StreamWriter) -> None:
    peer = writer.get_extra_info("peername")
    log(f"── 客户端连接 {peer}")
    with lock:
        clients.add(writer)
    writer.write(b"$DISERVER:CLIENT:ASC FSD V0.5.3:ABCDEFGHIJKLMNOP\r\n")
    writer.write(b"#TMWelcome to Mock FSD (development tool)\r\n")
    await writer.drain()

    pusher = asyncio.create_task(push_traffic(writer))
    try:
        while True:
            raw = await reader.readline()
            if not raw:
                break
            line = raw.decode("utf-8", errors="replace").rstrip("\r\n")
            log(f"<<< {line}")

            if line.startswith("#AP"):
                writer.write(b"#TMAuth OK, welcome pilot\r\n")
                await writer.drain()
            elif line.startswith("$PI"):
                parts = line[3:].split(":")
                if parts:
                    writer.write(f"$POSRV01:{parts[0]}\r\n".encode())
                    await writer.drain()
            elif line.startswith("$CQ"):
                parts = line[3:].split(":")
                if len(parts) >= 3 and parts[2] == "CAPS":
                    writer.write(
                        f"$CR{parts[0]}:{parts[1]}:CAPS:ATCINFO=1\r\n".encode())
                    await writer.drain()
    except (ConnectionResetError, BrokenPipeError):
        pass
    finally:
        pusher.cancel()
        with lock:
            clients.discard(writer)
        log(f"── 客户端断开 {peer}")
        writer.close()


async def main() -> None:
    ap = argparse.ArgumentParser(description="Mock FSD server (dev tool)")
    ap.add_argument("--port", type=int, default=6809)
    ap.add_argument("--log", default="fsd_mock.log")
    args = ap.parse_args()

    global log_fh
    log_fh = open(args.log, "a", encoding="utf-8")

    server = await asyncio.start_server(handle, "127.0.0.1", args.port)
    log(f"Mock FSD 已启动 127.0.0.1:{args.port}（日志: {args.log}）")
    async with server:
        await server.serve_forever()


if __name__ == "__main__":
    try:
        asyncio.run(main())
    except KeyboardInterrupt:
        print("Mock FSD 已停止")
