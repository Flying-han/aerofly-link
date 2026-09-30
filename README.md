# Aerofly Link

> Aerofly Link 0.3.1 — Open-source Aerofly FS 4 community-flying client, implemented in C.

Aerofly Link is an unofficial community project. It is not affiliated with or endorsed by IPACS.

Aerofly FS 4 第三方联机客户端 — 通过外部 AeroflyBridge 获取遥测，并连接社区 FSD 连飞服务器。

> **注意**：Aerofly FS 4 不支持注入外部飞机模型，其他联机玩家的飞机无法在 AFS4 内部显示。

## 功能

- **ASC 默认服务器** — 首次启动使用 `flight.skeet.top:6809`，也可管理社区或自建 FSD 地址
- **实时位置共享** — 将 AFS4 飞机位置（经纬度、高度、航向、速度）以 1Hz 上报
- **应答机控制** — 双轨制应答机（虚拟状态 + DLL 写入尽力而为），支持 STBY/ALT/IDENT
- **ATC 通讯** — 接收和发送文本消息（`@呼号 消息`，缺省发往 UNICOM）
- **附近飞机提示** — 基于大圆距离的 10nm 范围统计
- **飞行计划** — 握手自动发送最小 `$FP`，工作区内可提交完整飞行计划
- **模拟 DLL 模式** — 无需启动 AFS4 即可测试联机功能（GUI 开关 / CLI `--mock`）
- **单窗口图形界面** — 可缩放、深色/浅色主题，简体中文/香港繁体中文/美式英语
- **内置设置页** — 管理服务器、FSD-JWT/legacy 认证、HTTPS 令牌地址、代理和模拟遥测

新配置使用 `auth_mode: "fsd-jwt"`，通过 HTTPS 获取令牌并使用 revision 100；只支持旧认证的社区服务器可选 `legacy`（revision 9）。设置由 GUI 管理，配置文件位于 `%APPDATA%\AeroflyLink\settings.json`。密码只在内存中使用，不写入磁盘。

**VATSIM 规则：**VATSIM 官方要求使用获批客户端。Aerofly Link 当前未列入官方获批清单，因此本程序不会连接 VATSIM；设置页仅显示官方 `AUTOMATIC` 地址供参考。请使用 [VATSIM 获批客户端](https://vatsim.net/docs/policy/approved-software/)连接网络。FSD-JWT 是认证协议选项，不代表客户端获准接入 VATSIM。

v0.3.1 支持 Windows 10 及更新版本。Windows 10 Home/Pro 已于 2025-10-14 结束常规支持；请使用仍受安全更新支持的 Windows 版本或已加入 ESU 的设备（[Microsoft 生命周期说明](https://learn.microsoft.com/en-us/windows/release-health/release-information)）。Linux 和 macOS 暂无支持承诺；真实遥测还需要外部 AeroflyBridge DLL。

## 技术栈

| 组件 | 技术 |
|------|------|
| 客户端 | C11，Win32 窗口 + Nuklear/GDI GUI；静态 CRT |
| 构建 | zig cc（`client-c/build.cmd`） |
| 安装器 | Inno Setup 6 |
| AF4 桥接 DLL | [外部开源 AeroflyBridge.dll](https://github.com/jlgabriel/Aerofly-FS4-Bridge)（见 ADR 0002） |

## 项目结构

```
aerofly-link/
├── client-c/                       # 当前唯一客户端实现
│   ├── build.cmd                   # 一键构建 + 测试（zig cc）
│   ├── VERSION                     # 版本号单一来源
│   ├── aeroflylink.exe.manifest    # comctl v6 + PerMonitorV2 DPI
│   ├── include/link/               # 模块头文件（协议/会话/桥接/编排/GUI）
│   ├── src/                        # 实现
│   ├── tests/                      # C 断言测试 + 无头客户端入口
│   └── setup/                      # Inno Setup 安装包脚本
├── docs/                           # 文档（ADR、重写计划、发布清单）
├── tools/                          # 开发期 Mock FSD 与 GUI/CLI E2E 工具（Python）
└── config/                         # 配置模板
```

## 快速开始

### 从源码构建

```cmd
:: 使用 vfox 激活本机 Zig 工具链（zig cc），然后：
cd client-c
build.cmd
:: 产物: build\aeroflylink.exe（GUI）、build\aeroflylink-cli.exe（无头）
:: 构建末尾自动运行全部单元测试
```

### 运行

1. 启动 `aeroflylink.exe`，填入呼号 / 账号 ID / 密码，默认连接 ASC `flight.skeet.top:6809`。
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
│ + Bridge DLL │ ←───命令JSON─────  │    Link      │ ←───交通数据──── │ (ASC / FSD) │
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
  - `auth_mode: "fsd-jwt"` 通过 HTTPS 获取令牌并使用 revision 100；`legacy` 使用 revision 9。
    这表示认证方言，不表示可以连接 VATSIM 网络。线上互操作结果以 [发布清单](docs/RELEASE.md) 的实测矩阵为准。
- **飞行计划**：握手发送最小 `$FP` 纳入广播列表；工作区可提交完整 17 字段 `$FP`
- **兼容性**：`$CQ:CAPS` 必答、`$PI`→`$PO`、`$ZC`→空 `$ZR`、90s 读超时、
  `#AP`/`@` 位置进 traffic 表、`#DP` 移除

## 配置

客户端配置位于 `%APPDATA%\AeroflyLink\settings.json`（首次连接自动创建），
模板见 [`config/settings.example.json`](config/settings.example.json)。
密码永不落盘（ADR 0003）。

## 文档

- [docs/C_REWRITE_PLAN.md](docs/C_REWRITE_PLAN.md) — C 实现架构与开发计划
- [docs/GITHUB_ACTIONS.md](docs/GITHUB_ACTIONS.md) — 本项目 CI、E2E、artifact 与 Release 使用指南
- [docs/RELEASE.md](docs/RELEASE.md) — 发布清单
- [docs/releases/v0.3.1.md](docs/releases/v0.3.1.md) — v0.3.1 变更与本地验证范围
- [docs/testing/e2e-fsd-client.md](docs/testing/e2e-fsd-client.md) — 本地 FSD Docker E2E 规范与验证边界
- [docs/adr/](docs/adr/) — 架构决策记录（为何用 C、外部 DLL、凭据处理、Win32 UI 等）

## License

见 [LICENSE](LICENSE)。
