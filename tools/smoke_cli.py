# -*- coding: utf-8 -*-
"""Run the optional headless C client against the local Mock FSD server."""
import json
import os
import subprocess
import sys
import tempfile
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CLI = os.environ.get(
    "AEROFLYLINK_CLI_EXE",
    os.path.join(ROOT, r"client-c\build\aeroflylink-cli.exe"))
PORT = int(os.environ.get("AEROFLYLINK_CLI_SMOKE_PORT", "16819"))
CALLSIGN = "E2E2101"
CID = "1111111"
PASSWORD = "Local-E2E-Only-Not-A-Real-Password"
PROFILE_DIR = os.path.join(ROOT, r"client-c\build\e2e-smoke")
os.makedirs(PROFILE_DIR, exist_ok=True)
RUN_DIR = tempfile.mkdtemp(
    prefix="cli_", dir=PROFILE_DIR)
CONFIG_PATH = os.path.join(RUN_DIR, "settings.json")
SERVER_LOG = os.path.join(RUN_DIR, "fsd_mock.log")

settings = {
    "callsign": CALLSIGN,
    "cid": CID,
    "realname": "Aerofly Link CLI E2E",
    "server": "127.0.0.1",
    "port": PORT,
    "auth_mode": "legacy",
    "language": "en-US",
    "theme": "dark",
    "mock_lat": "31.1434",
    "mock_lon": "121.8082",
    "mock_alt": "3500",
    "aircraft": "B738",
    "flight_rules": "IFR",
    "wake_category": "Medium",
    "tas": "450",
    "dep_airport": "ZSPD",
    "dest_airport": "ZBAA",
    "cruise_alt": "F350",
    "route": "CDY G212",
    "servers": [f"127.0.0.1:{PORT}"],
}
with open(CONFIG_PATH, "w", encoding="utf-8") as config_file:
    json.dump(settings, config_file, ensure_ascii=False)

no_window = getattr(subprocess, "CREATE_NO_WINDOW", 0)
mock = None
client = None
try:
    mock = subprocess.Popen(
        [sys.executable, "-u", os.path.join(ROOT, "tools", "mock_fsd_server.py"),
         "--port", str(PORT), "--log", SERVER_LOG],
        stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL, creationflags=no_window)
    time.sleep(0.8)
    if mock.poll() is not None:
        raise RuntimeError("Mock FSD did not start")

    client = subprocess.Popen(
        [CLI, "--config", CONFIG_PATH, "--password", PASSWORD],
        stdin=subprocess.PIPE, stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL, text=True, encoding="utf-8",
        creationflags=no_window)

    # Wait for the initial authenticated handshake before sending operations.
    deadline = time.time() + 10
    while time.time() < deadline:
        if client.poll() is not None:
            raise RuntimeError(f"CLI exited before the FSD handshake: {client.returncode}")
        if os.path.exists(SERVER_LOG):
            with open(SERVER_LOG, "r", encoding="utf-8") as log_file:
                if f"<<< #AP{CALLSIGN}" in log_file.read():
                    break
        time.sleep(0.1)
    else:
        raise RuntimeError("Mock FSD did not receive the CLI authentication packet")
    time.sleep(0.4)  # let the client consume the server's auth response

    for command in ("/ident", "/stby", "/squawk 7000", "/alt",
                    "@ZGGG_TWR E2E smoke", "/quit"):
        client.stdin.write(command + "\n")
        client.stdin.flush()
        time.sleep(0.15)
    client.stdin.close()
    client.wait(timeout=10)
    if client.returncode != 0:
        raise RuntimeError(f"CLI exited with code {client.returncode}")

    with open(SERVER_LOG, "r", encoding="utf-8") as log_file:
        trace = log_file.read()
    required = (
        f"<<< #AP{CALLSIGN}:SERVER:{CID}:[REDACTED]",
        f"<<< $FP{CALLSIGN}",
        "<<< @",
        f"<<< #TM{CALLSIGN}:",
        f"<<< #DP{CALLSIGN}",
    )
    missing = [packet for packet in required if packet not in trace]
    if missing:
        raise RuntimeError(f"Mock FSD did not observe expected packets: {missing}")
    if PASSWORD in trace:
        raise RuntimeError("The Mock FSD trace contains the synthetic password")
    print("[cli-smoke] PASS: login, flight plan, position, chat, transponder controls, redaction, and disconnect")
finally:
    if client is not None and client.poll() is None:
        client.kill()
        client.wait(timeout=5)
    if mock is not None and mock.poll() is None:
        mock.terminate()
        try:
            mock.wait(timeout=5)
        except subprocess.TimeoutExpired:
            mock.kill()
