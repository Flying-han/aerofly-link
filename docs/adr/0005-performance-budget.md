# ADR 0005: 性能预算——空闲即静默，资源有上限

- 状态: 已接受（2026-09-18）
- 关联: ADR 0001（C 重写动机）、ADR 0004（日志限频）

## 背景

用户反馈"程序无论是否在运行都占用 CPU/RAM"。1.1.0 逐项审计后的成因与对策：

| 成因 | 位置（修复前） | 对策 |
| --- | --- | --- |
| UI 线程每秒一次阻塞 socket 探测（500ms 超时） | `main_window._probe_dll_port` | 删除；DLL 状态读 `dll_bridge` 的既有连接状态 |
| 游戏未运行时每 2s 一次遥测重连尝试 + 每次一条 WARNING | `dll_bridge._connect_telemetry` | 指数退避封顶 30s；日志前 3 次与每 20 次各一条 |
| 每 2s 一次命令端口探测（无限循环） | `dll_bridge._probe_command_port` | 删除；命令为短连接，按需建连 |
| 遥测轮询 10Hz 且数据不变也发 Qt 信号 | `main_window` 遥测定时器 | 4Hz + 值不变不发射 |
| 每个流入 @ 包全量发射 traffic 列表信号 | `fsd_client` | 1Hz 节流 |
| 通讯日志 QTextEdit 无限累积 HTML | `ui/log_panel.py` | QPlainTextEdit + maximumBlockCount(500) |
| 逐帧诊断写盘 + fsd_packets.log | `diag_logger` / `fsd_client` | 默认关闭（ADR 0004）/ 删除 |
| 死依赖 PyQt6-WebEngine（Chromium 级体积） | `requirements.txt` | 移除 |

## 决策（预算基线，后续回归以此验收）

1. **空闲静默**：游戏未运行、未连接 FSD 时，进程除退避定时器外不做周期性
   网络尝试，不产生日志行。
2. **连接后节流**：遥测进 UI ≤4Hz；traffic 信号 ≤1Hz；位置上报 1Hz（协议需要）。
3. **内存上限**：用户可见文本缓冲（通讯日志）封顶 500 块；遥测快照仅保留最新一帧。
4. **C 重写继承同一预算**，并以更小地板交付（见 ADR 0001 事实基础表）。

## 后果

- 新功能引入任何常驻定时器/重试循环时，必须在本 ADR 补充预算条目或说明豁免。
- "附近飞机"提示依赖距离计算，仅在自身遥测有效时启用（避免 0,0 死角）。
