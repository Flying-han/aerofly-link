# client-c — Aerofly Link C 客户端（P0 协议层）

C11 重写的客户端（决策与路线见 [docs/adr/0001-c-rewrite.md](../docs/adr/0001-c-rewrite.md)
与 [docs/C_REWRITE_PLAN.md](../docs/C_REWRITE_PLAN.md)）。
当前为 **P0 阶段：core 协议层骨架**——纯函数、无 I/O、零第三方依赖。

## 布局

```
client-c/
├── include/link/
│   ├── protocol.h   # PBH 位打包/解包、坐标解析、应答机映射、距离
│   ├── message.h    # FSD 报文构造（$AP/$ID/@/#TM/$PI/$FP）与解析（分类/#TM/@ 包）
│   └── frame.h      # CRLF 行组帧（跨 feed 粘包缓冲）
├── src/             # 对应实现
├── tests/test_main.c  # 极简断言测试（与 tests/test_fsd_*.py 数值对齐）
└── build.cmd        # zig cc 显式构建脚本
```

## 构建（Windows，zig cc 由 vfox 管理）

```cmd
cd client-c
build.cmd
```

脚本做四件事：逐文件 `zig cc -std=c11 -Wall -Wextra -O2` 编译 →
`zig ar` 归档 `build\libfsd.a` → 链接测试可执行文件 → 运行测试。
全部通过输出 `ALL TESTS PASSED`，任何失败以非零码退出。

## 约定

- 错误处理：返回码（0 成功 / -1 参数或截断 / -2 协议语义错误），无异常。
- 网络输入视为敌意：解析器安全失败，绝不越界。
- 仅 C11（保持 MSVC 可编译）；core 层不含 WinSock。
- 字符串一律 UTF-8 字节流 + 显式容量。

## 状态

- ✅ P0 协议层（本目录）
- ⬜ P1 transport/bridge —— 见计划第 6 节
