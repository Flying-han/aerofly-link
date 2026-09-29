# client-c — Aerofly Link C 客户端

C11 全量重写的客户端（决策与路线见 [docs/adr/0001-c-rewrite.md](../docs/adr/0001-c-rewrite.md)
与 [docs/C_REWRITE_PLAN.md](../docs/C_REWRITE_PLAN.md)）。
P0 协议层、P1 传输/桥接、P2 无头客户端、P3 Win32 GUI 均已落地。
应用版本号由 `VERSION` 单一管理；当前版本为 `0.3.0`。
新配置默认使用 VATSIM FSD-JWT/revision 100；`jwt_url`、`jwt_proxy`、
`jwt_proxy_bypass`、服务地址和端口都可通过 `settings.json` 自定义。`jwt_proxy` 留空时
使用 Windows 自动代理；也可仅为 JWT 请求设置 WinHTTP proxy list
（如 `http=127.0.0.1:10808;https=127.0.0.1:10808`）。legacy/revision 9 保留为显式兼容模式。
本地 Docker FSD E2E 规范与证据见 [docs/testing/e2e-fsd-client.md](../docs/testing/e2e-fsd-client.md)。

## 布局

```
client-c/
├── include/link/
│   ├── protocol.h    # PBH 位打包/解包、坐标解析、应答机映射、距离
│   ├── message.h     # FSD 报文构造（$AP/$ID/@/#TM/$PO/$FP）与解析
│   ├── frame.h       # CRLF 行组帧（粘包缓冲、超长行丢弃）
│   ├── json.h        # 扁平 JSON 提取器（遥测帧 + settings.json）
│   ├── config.h      # settings.json 读写（密码不落盘，ADR 0003）
│   ├── net.h         # WinSock 薄封装（全非阻塞）
│   ├── session.h     # FSD 会话状态机（问候/认证/keepalive/traffic）
│   ├── bridge.h      # AeroflyBridge.dll 桥接（遥测解析+命令短连接）
│   ├── transponder.h # 应答机双轨制（虚拟状态 + DLL 写入降级）
│   ├── mock.h        # 内嵌模拟 DLL 服务器（心形航线 10Hz）
│   └── app.h         # 应用编排（1Hz 上报/同步检查/附近飞机）
├── src/              # 对应实现 + gui.c（Win32 界面）
├── tests/
│   ├── test_main.c   # 协议层断言
│   ├── test_p2.c     # 会话握手/桥接契约/配置断言
│   └── cli_main.c    # 无头客户端源码
└── build.cmd         # zig cc 显式构建脚本
```

## 构建（Windows，zig cc 由 vfox 管理）

```cmd
cd client-c
build.cmd
```

产物（build/）：
- `aeroflylink.exe` —— GUI 客户端（约 200KB，无运行时依赖）
- `aeroflylink-cli.exe` —— 无头客户端（参数见 `--help`，支持 `--mock`）
- `test_protocol.exe` / `test_p2.exe` —— 测试（构建时自动运行）

## 约定

- 错误处理：返回码（0 成功 / -1 参数或截断 / -2 协议语义错误），无异常。
- 网络输入视为敌意：解析器安全失败，绝不越界。
- 单线程 select 事件循环（无锁、无后台线程）；core 层不含 WinSock。
- 仅 C11（保持 MSVC 可编译）；字符串一律 UTF-8 字节流 + 显式容量。

## 状态

- ✅ P0 协议层 / ✅ P1 传输与桥接 / ✅ P2 无头客户端 / ✅ P3 Win32 GUI
- ✅ legacy 与 FSD-JWT（`vatsim`）认证路径已实现
- ⬜ ASC/VATSIM 线上环境互验（见 docs/RELEASE.md 通过矩阵）
- ✅ 安装器切换到 C 版单文件分发（P4，`client-c/setup/`）
