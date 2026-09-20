# ADR 0007: 删除 Python 版本，C 版成为唯一实现

- 状态: 已接受（2026-09-20）

## 背景

C 重写（ADR 0001）已完成 P0-P3：协议层与 Python 版逐条对齐（含读超时、
`#AP` traffic、`$FP` 归一化、配置互读等全部已知差距），Win32 GUI 功能对齐，
构建/测试/安装器/CI 已切换到 C 轨（P4）。剩余验收为与 ASC 生产服务器的
线上互验（P2 收尾）。

Python 版（PyQt6）是行为基准与过渡期 oracle：互验排障时可与 C 客户端并跑
对比协议跟踪。其分发体积 127MB、常驻内存 ~150MB，与 C 版（~250KB 单文件）
并存无运维价值。

## 决策

1. **Python 版直接删除，不归档 legacy/**：项目起初是个人小工具，Python 实现
   无独立保留价值；git 历史即归档。
2. **保留 `tools/mock_fsd_server.py` 与 `tools/smoke_gui.py`**：C 版 E2E 与
   GUI 冒烟的开发工具（开发机需 Python + Pillow，运行时零依赖不变）。
3. **pytest 体系随 Python 版终结**：C 断言（test_main / test_p2，250+ 项）
   已覆盖原 pytest 数值对齐用例与契约测试。
4. **仓库内 `dll/` C++ 脚手架一并移除**：ADR 0002 已判定与生产无关且协议
   不兼容，CI 早已不构建。
5. **行为基准转移**：Python 移除后，行为事实来源为 C 测试 + git 历史；
   后续协议变更须先改 C 测试再改实现。

## 后果

- 仓库体积与 CI 时长显著下降（单 job，无 Python/PyInstaller）。
- 互验排障失去并跑 oracle——互验须在删除前完成（这正是时序约束）。
- 新贡献者入口唯一：`client-c/build.cmd`，无双实现困惑。
- 若未来需要参考 Python 协议细节：checkout `refactor/quality-v1.1` 分支。
