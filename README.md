# Aerofly Link

> Open-source FSD connectivity client for Aerofly FS 4 — 原生 C 单文件实现。

Aerofly Link is an unofficial community project. It is not affiliated with or endorsed by IPACS.

Aerofly FS 4 第三方联机客户端 — 桥接 FSD 协议服务器（VATSIM / 私有服务器），实现位置共享与 ATC 通讯。

> **注意**：Aerofly FS 4 不支持注入外部飞机模型，其他联机玩家的飞机无法在 AFS4 内部显示。

## 功能

- **FSD 协议联机** — 连接 VATSIM 或任何兼容 FSD 协议的服务器
- **实时位置共享** — 将 AFS4 飞机位置（经纬度、高度、航向、速度）以 1Hz 上报
- **应答机控制** — 双轨制应答机（虚拟状态 + DLL 写入尽力而为），支持 STBY/ALT/IDENT
- **ATC 通讯** — 接收和发送文本消息（`@呼号 消息`，缺省发往 UNICOM）
- **附近飞机提示** — 基于大圆距离的 10nm 范围统计
- **飞行计划** — 握手自动发送最小 `$FP`，工作区内可提交完整飞行计划
- **模拟 DLL 模式** — 无需启动 AFS4 即可测试联机功能（GUI 开关 / CLI `--mock`）

## 技术栈

| 组件 | 技术 |
|------|------|
| 客户端 | C11 + Win32（单文件 ~200KB，静态 CRT，无运行时依赖） |
| 构建 | zig cc（`client-c/build.cmd`） |
| 安装器 | Inno Setup 6 |
| AF4 桥接 DLL | [外部开源 AeroflyBridge.dll](https://github.com/jlgabriel/Aerofly-FS4-Bridge)（见 ADR 0002） |

Python 版（PyQt6，127MB 级分发）已在 C 版对齐验收后移除（ADR 0007），
行为基准与历史实现见 git 历史。

## 项目结构

```
aerofly-link/
├── client-c/                       # C 客户端（唯一实现）
│   ├── build.cmd                   # 一键构建 + 测试（zig cc）
│   ├── VERSION                     # 版本号单一来源
│   ├── aeroflylink.exe.manifest    # comctl v6 + PerMonitorV2 DPI
│   ├── include/link/               # 模块头文件（协议/会话/桥接/编排/GUI）
│   ├── src/                        # 实现
│   ├── tests/                      # C 断言测试 + 无头客户端入口
│   └── setup/                      # Inno Setup 安装包脚本
├── docs/                           # 文档（ADR、重写计划、发布清单）
├── tools/                          # 开发工具（Python：Mock FSD / GUI 冒烟）
└── config/                         # 配置模板
```

## 快速开始

### 从源码构建

```cmd
:: 安装 zig（vfox 或 ziglang.org），然后：
cd client-c
build.cmd
:: 产物: build\aeroflylink.exe（GUI）、build\aeroflylink-cli.exe（无头）
:: 构建末尾自动运行全部单元测试
```

### 运行

1. 启动 `aeroflylink.exe`，填入呼号 / CID / 密码 / 服务器，点击「连接服务器」。
2. 真实遥测需要 [AeroflyBridge.dll](https://github.com/jlgabriel/Aerofly-FS4-Bridge) (v0.3.1+)
   放入 `Documents\Aerofly FS 4\external_dll\`（安装器会自动完成）。
   非正版 AFS4 需要 hex-patch 导出名 `Aerofly_FS_4_` → `Aerofly_FS_2_`，
   或使用内置「模拟DLL」开关。
3. 无游戏环境测试：连接前打开「模拟DLL」开关，或 CLI 加 `--mock`。

### 无头客户端（CI / 脚本）

```bash
aeroflylink-cli.exe --config <settings.json> --password PW [--mock]
# stdin: @呼号 消息 | /fp | /ident | /stby | /alt | /squawk NNNN | /quit
```

## 通信架构

```
┌──────────────┐     TCP 12345      ┌──────────────┐     TCP 6809     ┌──────────────┐
│ Aerofly FS4  │ ────遥测JSON────→  │   Aerofly    │ ─────FSD协议───→ │  FSD Server  │
│ + Bridge DLL │ ←───命令JSON─────  │    Link      │ ←───交通数据──── │ (VATSIM等)   │
└──────────────┘     TCP 12346      └──────────────┘                  └──────────────┘
```

| 端口 | 方向 | 协议 | 用途 |
|------|------|------|------|
| 12345 | DLL → Client | TCP/JSON（扁平格式） | 遥测数据（位置、姿态、速度，10Hz） |
| 12346 | Client → DLL | TCP/JSON | 控制命令（应答机） |
| 6809 | Client ↔ Server | TCP/FSD | FSD 协议（位置报告、通讯、ATC） |

## FSD 协议实现要点

- **位置报告**：`@<mode>:<callsign>:<squawk>:<rating>:<lat>:<lon>:<alt>:<gs>:<pbh>:<alt_diff>`
  - 坐标使用十进制 5 位小数（Swift 兼容）
  - PBH 使用 32-bit 位打包（与 Swift `pbh.h` 一致）
  - 应答机 mode 字母：N=ALT, S=STBY, Y=IDENT
- **认证**：`#AP<callsign>:SERVER:<cid>:<password>:<rating>:<revision>:<simtype>:<realname>`
  - legacy（revision 9）与 ASC FSD 后端 0.5.3 互验通过；VATSIM FSD-JWT 未实现
- **飞行计划**：握手发送最小 `$FP` 纳入广播列表；工作区可提交完整 17 字段 `$FP`
- **兼容性**：`$CQ:CAPS` 必答、`$PI`→`$PO`、`$ZC`→空 `$ZR`、90s 读超时、
  `#AP`/`@` 位置进 traffic 表、`#DP` 移除

## 配置

客户端配置位于 `%APPDATA%\AeroflyLink\settings.json`（首次连接自动创建），
模板见 [`config/settings.example.json`](config/settings.example.json)。
密码永不落盘（ADR 0003）。

## 文档

- [docs/C_REWRITE_PLAN.md](docs/C_REWRITE_PLAN.md) — C 实现架构与开发计划
- [docs/RELEASE.md](docs/RELEASE.md) — 发布清单
- [docs/adr/](docs/adr/) — 架构决策记录（为何用 C、外部 DLL、凭据处理、Win32 UI 等）

## License

见 [LICENSE](LICENSE)。
