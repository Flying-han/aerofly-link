# Aerofly Link C 重写——架构与开发计划

- 版本: 1.3（2026-09-29）
- 状态: P0-P4 完成；v0.3.1 GUI 以 Nuklear/Win32-GDI 重做（见 ADR 0010）；Windows 10+ 为目标，Linux/macOS 不作 v0.3.1 支持承诺。认证 WinHTTP HTTPS 成功路径及公网互验仍未验证
- 决策依据: [ADR 0001](adr/0001-c-rewrite.md)（为何用 C）、[ADR 0005](adr/0005-performance-budget.md)（性能预算）
- 行为基准: 历史 Python 实现（Git 历史）；当前协议事实来源为 C 实现与 C 测试

## 0. TL;DR

以 C11 从零实现 Aerofly Link 客户端，替换 Python/PyQt 版本，目标是：
安装体积 **127MB → <5MB**、常驻内存 **~150MB → <20MB**、空闲 CPU **≈0**。
路线：协议层 → 传输/桥接层 → 无头客户端（可被现有安装器分发验证）→ UI。
编译器 zig cc，构建用显式命令，C 测试与开发期 Python Mock 服务互验。

## 1. 目标与非目标

**目标**

| # | 目标 | 验收 |
| --- | --- | --- |
| G1 | FSD 协议层与历史 Python 版行为逐条一致 | 历史协议用例在 C 测试中保留等价覆盖 |
| G2 | 与 ASC FSD 0.6.2 完成 VATSIM 新协议互操作，并保留 revision 9 兼容 | 本地 FSD Docker E2E 验证 revision 100 登录、位置/飞行计划/文本/应答机/断开；revision 9 在兼容测试验证 |
| G3 | 对接外部 AeroflyBridge.dll 扁平 JSON 契约（ADR 0002） | 12345 遥测解析 + 12346 命令往返 |
| G4 | 资源预算达标（ADR 0005） | 空闲 0 网络 0 日志；内存 <20MB；无连接时 0 周期唤醒（退避除外） |
| G5 | GUI/CLI 静态 CRT 发布，无客户端运行时依赖 | GUI 与 CLI 独立运行于 Windows 10 x64；安装包包含 GUI 和外部桥接 DLL |

**非目标**

- 不做防逆向混淆（见 ADR 0004 的立场：控制暴露面，不做表演性加固）。
- 不提供 Aerofly 模拟器内置地图或其他飞机模型注入。
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
- **历史 Python 版（Git 历史）**：移植期行为参考；旧实现已从工作树移除，
  协议改动以 C 测试和当前实现为准。
- **ASC FSD 后端（Go）**：互操作对象。新配置使用 FSD-JWT/revision 100；legacy revision 9 保留为用户明确选择的兼容协议，详见后端
  `docs/compatibility/fsd-jwt.md`。

说明：此处的 JWT/revision-100 描述 FSD 认证方言，不表示 Aerofly Link 获准连接 VATSIM 网络；v0.3.1 的 ASC 目标地址和 VATSIM 边界见 ADR 0009。

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
| **P0 协议层骨架** | core 协议层 + C 断言 + build.cmd | C 测试通过；历史 Python 用例有等价覆盖 | ✅ 完成 |
| **P1 传输与桥接** | session 状态机（问候/认证/keepalive/traffic）+ bridge（遥测映射/命令短连接/指数退避） | 回环 FSD 握手全流程测试；桥接↔Mock 契约测试 | ✅ 完成 |
| **P2 无头客户端** | app 编排器 + `aeroflylink-cli.exe`；本地 FSD Docker E2E，见 `docs/testing/e2e-fsd-client.md` | VATSIM JWT/revision 100 登录、双客户端/计划/文本/位置/keepalive/错误和断开；公网互验独立记录 | ✅ 本地 E2E 完成；公网互验待做 |
| **P3 UI** | 选型 Win32（纯 C、无依赖，见 ADR 0006）→ `aeroflylink.exe`（连接页/工作区/状态栏，功能对齐历史 Python 版） | 构建+冒烟截图+WM_CLOSE 优雅退出；暗色主题等视觉打磨列入 backlog | ✅ 完成 |
| **P4 切换发布** | 安装器分发 C 版；移除 Python 客户端与仓库 C++ 脚手架（ADR 0007）；仓库结构收敛 | 发布流程、CI、文档和源码均只有 C 客户端；开发期 Mock/GUI 工具可保留 Python | ✅ 完成 |

## 7. 构建与工具链

- 编译器：zig cc（使用开发者已安装并由 vfox 激活的 Zig 工具链；构建脚本不安装或更新开发工具）。
  目标 `x86_64-windows-gnu`，静态链接 CRT（`-static`），无外部依赖。
- 构建：`client-c/build.cmd`（显式命令，逐文件编译 → `zig ar` 出 `libfsd.a` → 测试）。
  不引入 CMake/Meson，直到 P2 出现多目标需求再评估（YAGNI）。
- CI（P1 起）：GitHub Actions 安装 zig（`mlugg/setup-zig`）后执行同一 build.cmd。

## 8. 测试策略

1. **C 单元测试**：`client-c/tests/test_main.c`，极简断言宏，零依赖
   （不引 CHECK/Criterion，保持"clone 即可构建"）。
2. **数值对齐**：历史 `tests/test_fsd_protocol.py` 的核心用例在 C 侧有镜像
   （PBH 90°→1024、packed 坐标往返、距离容差），保证 G1。
3. **契约互验（P1）**：C 无头客户端 ↔ `tools/mock_fsd_server.py`；
   AeroflyBridge 扁平 JSON 契约由 C 测试覆盖。
4. **互操作（P2）**：从本地 ASC FSD 0.6.2 源码构建隔离 Docker 服务，使用
   VATSIM revision 100 完成登录、位置、计划、文本、应答机与断开流程；另验
   revision 9 兼容路径。公网 VATSIM、生产 ASC 和受信 HTTPS JWT 端点作为独立门禁记录。
5. **健壮性**：行组帧喂入模糊字节（截断、超长、无换行 EOF），断言不崩溃。

## 9. 风险与对策

| 风险 | 对策 |
| --- | --- |
| C 实现与 Python 行为漂移 | 移植期"每函数注明来源语义"；P2 前以 Python 为准，并跑对比 |
| 单线程循环被慢阻塞（DNS 等） | 一律 `getaddrinfo` 异步化或限定 IP/主机名解析在连接状态机内做超时 |
| WinSock 细节（非阻塞 connect、WSAGetLastError） | transport 层集中封装，core 零 WinSock；P1 先写传输层测试 |
| zig cc ↔ MSVC ABI 差异影响未来 DLL 自研 | 全程 C 接口 + 独立进程通信（TCP），无跨语言链接面 |
| 历史 Python 行为基准不可直接运行 | 保留 Git 历史作为参考；新协议行为先更新 C 测试 |

## 10. 与现有仓库的关系

- `client-c/` 是唯一客户端实现；旧 Python 客户端与仓库内 C++ DLL 脚手架已按
  ADR 0007 从工作树移除，历史版本保留在 Git 历史中。
- `tools/mock_fsd_server.py` 与 `tools/smoke_gui.py` 是仅供开发期使用的 Python 工具，
  不属于客户端运行时或产品实现。
- 切换（P4）决策：Python 版直接删除，不归档（ADR 0007）。
