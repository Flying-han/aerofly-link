# Aerofly Link C 重写——架构与开发计划

- 版本: 1.1（2026-09-18）
- 状态: P0-P3 已落地（协议层/传输桥接/无头客户端/Win32 GUI），P2 线上互验与 P4 切换待办
- 决策依据: [ADR 0001](adr/0001-c-rewrite.md)（为何用 C）、[ADR 0005](adr/0005-performance-budget.md)（性能预算）
- 行为基准: Python 实现（`core/`），对齐完成后以本目录代码为准

## 0. TL;DR

以 C11 从零实现 Aerofly Link 客户端，替换 Python/PyQt 版本，目标是：
安装体积 **127MB → <5MB**、常驻内存 **~150MB → <20MB**、空闲 CPU **≈0**。
路线：协议层 → 传输/桥接层 → 无头客户端（可被现有安装器分发验证）→ UI。
编译器 zig cc，构建用显式命令，C 测试与 Python Mock 契约互验。

## 1. 目标与非目标

**目标**

| # | 目标 | 验收 |
| --- | --- | --- |
| G1 | FSD 协议层与 Python 版行为逐条一致 | 同一组测试用例（`tests/test_fsd_*.py` 的 C 镜像）全绿 |
| G2 | 与 ASC FSD 后端 0.5.3 完成真实互操作（legacy/revision 9） | 登录、位置上报、心跳、$TM 收发、被纳入广播 |
| G3 | 对接外部 AeroflyBridge.dll 扁平 JSON 契约（ADR 0002） | 12345 遥测解析 + 12346 命令往返 |
| G4 | 资源预算达标（ADR 0005） | 空闲 0 网络 0 日志；内存 <20MB；无连接时 0 周期唤醒（退避除外） |
| G5 | 单二进制分发，无运行时依赖（静态链接 CRT） | `AeroflyLink.exe` 独立运行于 Windows 10 x64 |

**非目标**

- 不做防逆向混淆（见 ADR 0004 的立场：控制暴露面，不做表演性加固）。
- 不实现 VATSIM FSD-JWT 认证（与 Python 版一致，见 RELEASE 已知限制）。
- 本计划不锁定 UI 技术选型（P3 时另立 ADR）。

## 2. 动机与量化

见 ADR 0001「事实基础」表。一句话版本：用户主诉的体积/内存问题地板由
Python+Qt 技术栈决定，优化只能削掉地板之上的部分（1.1.0 已做完），
换技术栈是唯一能改变地板的手段。

## 3. 参考实现

- **swift-project（swift）**：FSD 协议事实标准。借鉴其
  `PilotDataUpdate::toTokens/fromTokens`（@ 包字段序与 5 位小数十进制坐标）、
  PBH 位布局（bit1=onGround, 2-11=heading, 12-21=bank, 22-31=pitch，pitch/bank 反转）、
  CAPS/$CQ/$PI 应答行为。**只借鉴行为，不复制代码**（GPL 与本项目 LGPL-3.0 不兼容）。
- **Python 版（本仓库 core/）**：已与 ASC 后端互验过的行为基准，
  C 层每个函数在移植时注明来源行号语义。
- **ASC FSD 后端（Go）**：互操作对象。连接即发 `$DISERVER:...:challenge`，
  legacy 登录走 revision 9 + bcrypt，详见后端 `docs/compatibility/fsd-jwt.md`。

## 4. 目标架构

```
┌─────────────────────────────────────────────────────┐
│                    aerofly-link.exe                  │
│                                                     │
│  ┌──────────┐  ┌───────────┐  ┌──────────────────┐  │
│  │ app      │  │ bridge    │  │ transport        │  │
│  │ 状态机/调度│←─│ DLL 客户端 │  │ TCP+FSD 会话      │←──→ FSD 服务器:6809
│  └────┬─────┘  └─────┬─────┘  └────────┬─────────┘  │
│       │              │                  │            │
│  ┌────┴──────────────┴──────────────────┴─────────┐  │
│  │              core 协议层（纯函数，无 I/O）        │  │
│  │   fsd_protocol / fsd_message / fsd_frame        │  │
│  └────────────────────────────────────────────────┘  │
└─────────────────────────────────────────────────────┘
        │ 12346 命令（短连接）    ↑ 12345 遥测（DLL 推流）
   AeroflyBridge.dll（外部，游戏内）
```

**线程模型**：单线程事件循环（`select` 或 WSAEventSelect），所有 socket 非阻塞。
定时器（位置上报 1Hz / keepalive 30s / 同步检查 5s / 重连退避）统一由循环驱动。
无锁、无后台线程——这是 C 版相对 Python 版（QThread + asyncio 双循环 + 执行器）
最大的结构简化，也是空闲 CPU 归零的保证。

**模块边界与依赖方向**：`core`（协议纯函数）不依赖任何 I/O；
`transport`/`bridge` 依赖 core；`app` 依赖全部；UI（P3）只依赖 app 的状态快照接口。

## 5. core 协议层规范（本 PR 已落地）

| 组件 | 头文件 | 内容 | Python 对应 |
| --- | --- | --- | --- |
| PBH 位打包 | `include/link/protocol.h` | `fsd_pack_pbh / fsd_unpack_pbh`（floor 语义与 Swift 一致） | `fsd_protocol.pack_pbh/unpack_pbh` |
| 坐标 | 同上 | `fsd_parse_coord`（packed/十进制双格式）、`fsd_distance_nm` | `parse_coord / distance_nm` |
| 应答机 | 同上 | `fsd_xpdr_letter`（S/N/Y）、`fsd_squawk_valid` | `xpdr_mode_to_fsd_letter` |
| 报文构造 | `include/link/message.h` | auth($AP)/ident($ID)/position(@)/tm(#TM)/ping($PI)/最小飞行计划($FP) | `fsd_client._build_* / send_*` |
| 报文解析 | 同上 | `fsd_classify`（前缀分类）、`fsd_parse_tm`、`fsd_parse_position`（@ 包→结构体） | `_dispatch / _handle_tm / _handle_at_traffic` |
| 行组帧 | `include/link/frame.h` | CRLF/LF 分帧、1024 上限、跨 feed 粘包缓冲 | `StreamReader.readline` 语义 |

**约定**（新贡献者必读）：

- 错误处理一律返回码：`0` 成功；`-1` 参数/截断；`-2` 协议语义错误。无异常、无 setjmp。
- 解析器对越界/缺字段一律安全失败（返回负值或 UNKNOWN），绝不越界读——
  输入来自网络，视为敌意。
- `char` 缓冲一律定长 + 显式容量参数；字符串一律 UTF-8 透传（协议即字节流）。
- 不用 GNU 扩展（保持 MSVC 可编译）；仅依赖 C11 + WinSock2（core 不含 WinSock）。

## 6. 阶段计划

| 阶段 | 内容 | 验收标准 | 状态 |
| --- | --- | --- | --- |
| **P0 协议层骨架** | core 协议层 + 128 项 C 断言 + build.cmd | 测试全绿；与 pytest 用例数值一致 | ✅ 完成 |
| **P1 传输与桥接** | session 状态机（问候/认证/keepalive/traffic）+ bridge（遥测映射/命令短连接/指数退避） | 回环 FSD 握手全流程测试；桥接↔Mock 契约测试 | ✅ 完成 |
| **P2 无头客户端** | app 编排器 + `aeroflylink-cli.exe`（配置/控制台命令/优雅退出）+ tools/mock_fsd_server.py | **真实 E2E：CLI 对 Mock FSD 完成登录-$FP-1Hz 上报-pong 全链路**；线上 ASC 互验待做 | ✅ 完成（线上互验待办） |
| **P3 UI** | 选型 Win32（纯 C、无依赖，见 ADR 0006）→ `aeroflylink.exe`（连接页/工作区/状态栏，功能对齐 Python 版） | 构建+冒烟截图+WM_CLOSE 优雅退出；暗色主题等视觉打磨列入 backlog | ✅ 完成 |
| **P4 切换发布** | 安装器分发 C 版单文件；Python 版标记维护模式；仓库结构收敛 | RELEASE 流程更新；文档一致 | 待开发 |

## 7. 构建与工具链

- 编译器：zig cc（vfox 管理：`vfox add zig` / `vfox use zig`）。
  目标 `x86_64-windows-gnu`，静态链接 CRT（`-static`），无外部依赖。
- 构建：`client-c/build.cmd`（显式命令，逐文件编译 → `zig ar` 出 `libfsd.a` → 测试）。
  不引入 CMake/Meson，直到 P2 出现多目标需求再评估（YAGNI）。
- CI（P1 起）：GitHub Actions 安装 zig（`mlugg/setup-zig`）后执行同一 build.cmd。

## 8. 测试策略

1. **C 单元测试**：`client-c/tests/test_main.c`，极简断言宏，零依赖
   （不引 CHECK/Criterion，保持"clone 即可构建"）。
2. **数值对齐**：`tests/test_fsd_protocol.py` 的每个用例在 C 侧有镜像
   （PBH 90°→1024、packed 坐标往返、距离容差），保证 G1。
3. **契约互验（P1）**：C 无头客户端 ↔ Python `core/mock_server.py`；
   遥测解析结果与 `Telemetry.from_aerofly_bridge` 输出逐字段一致。
4. **互操作（P2）**：接 ASC FSD 0.5.3（sweatbox/私有实例）完成登录-上报-心跳-
   断开全流程，服务端日志无协议错误。
5. **健壮性**：行组帧喂入模糊字节（截断、超长、无换行 EOF），断言不崩溃。

## 9. 风险与对策

| 风险 | 对策 |
| --- | --- |
| C 实现与 Python 行为漂移 | 移植期"每函数注明来源语义"；P2 前以 Python 为准，并跑对比 |
| 单线程循环被慢阻塞（DNS 等） | 一律 `getaddrinfo` 异步化或限定 IP/主机名解析在连接状态机内做超时 |
| WinSock 细节（非阻塞 connect、WSAGetLastError） | transport 层集中封装，core 零 WinSock；P1 先写传输层测试 |
| zig cc ↔ MSVC ABI 差异影响未来 DLL 自研 | 全程 C 接口 + 独立进程通信（TCP），无跨语言链接面 |
| 双实现文档不同步 | 本文件为唯一事实来源；Python README 标注过渡状态 |

## 10. 与现有仓库的关系

- `client-c/` 新增目录，不触碰 Python 运行链路；过渡期 CI 双轨（pytest + C 测试）。
- 仓库内 `dll/` 脚手架与 C 重写无关（见 ADR 0002）；自研 DLL 若立项另行决策。
- 完成切换（P4）后：Python 版归档为 `legacy/` 或剥离独立仓库，届时另立 ADR。
