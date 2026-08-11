"""
DLL 遥测端口原始诊断工具
直接 TCP 连接 127.0.0.1:12345，阻塞读取 15 秒，打印收到的所有字节。
"""
import socket
import time
import os
from pathlib import Path
from datetime import datetime

def _log(msg: str) -> None:
    ts = datetime.now().strftime("%H:%M:%S.%f")[:12]
    line = f"[{ts}] {msg}"
    print(line)
    try:
        log_dir = Path(os.environ.get("APPDATA", Path.home() / "AppData" / "Roaming")) / "AeroflyLink"
        log_dir.mkdir(parents=True, exist_ok=True)
        with open(str(log_dir / "diag.log"), "a", encoding="utf-8") as f:
            f.write(line + "\n")
    except Exception:
        pass

def main():
    _log("dll_raw_test: starting, connecting to 127.0.0.1:12345...")
    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    sock.settimeout(5)

    try:
        sock.connect(("127.0.0.1", 12345))
        _log("dll_raw_test: CONNECTED successfully")
    except Exception as e:
        _log(f"dll_raw_test: CONNECT FAILED: {e}")
        return

    sock.settimeout(2)  # shorter timeout for reads
    total_bytes = 0
    line_count = 0
    start = time.time()

    buffer = b""
    while time.time() - start < 15:
        try:
            chunk = sock.recv(4096)
            if not chunk:
                _log(f"dll_raw_test: EOF (DLL closed connection) after {time.time()-start:.1f}s, total={total_bytes} bytes, {line_count} lines")
                break
            total_bytes += len(chunk)
            buffer += chunk
            # split and log individual lines
            while b"\n" in buffer:
                line, buffer = buffer.split(b"\n", 1)
                line_count += 1
                line_str = line.decode("utf-8", errors="replace").strip()
                if line_count <= 5:
                    _log(f"dll_raw_test: line #{line_count}: {line_str[:200]}")
            if total_bytes > 50000:
                _log(f"dll_raw_test: stopped at 50KB limit, {line_count} lines")
                break
        except socket.timeout:
            elapsed = time.time() - start
            if total_bytes == 0:
                _log(f"dll_raw_test: NO DATA after {elapsed:.1f}s, still waiting...")
            continue
        except Exception as e:
            _log(f"dll_raw_test: ERROR after {time.time()-start:.1f}s: {e}")
            break

    elapsed = time.time() - start
    _log(f"dll_raw_test: DONE after {elapsed:.1f}s, total={total_bytes} bytes, {line_count} lines")

    # try to read remaining in buffer
    if buffer:
        line_count += 1
        _log(f"dll_raw_test: final partial line ({len(buffer)} bytes): {buffer[:200]!r}")

    sock.close()

if __name__ == "__main__":
    main()
