# ADR 0002: 生产使用外部 AeroflyBridge.dll，仓库内 dll/ 仅作参考脚手架

- 状态: 已接受（2026-09-18，追溯 7616dfa 之后的实际状态）

## 背景

仓库 `dll/` 目录有一套 C++ "AFS4 Bridge DLL" 脚手架（dll_main / telemetry_server /
command_server / afs4_api）。审查确认：

1. `afs4_api.hpp` 的真实 AFS4 API 实现是 TODO，默认初始化使用 **MockAFS4Api**；
2. 其遥测输出为 `{"type":...,"timestamp":...,"data":{lat,lon,...}}` 包装格式、
   命令格式为 `{"type":"control","command":...,"params":{...}}`；
3. 而 Python 客户端（`core/dll_bridge.py`）对接的是**外部开源**
   [AeroflyBridge.dll](https://github.com/jlgabriel/Aerofly-FS4-Bridge) v0.3.1 的
   扁平格式：遥测 `{"Aircraft.Latitude": <弧度>, ...}`、命令
   `{"variable":...,"value":...}`。

两者协议不兼容：仓库内脚手架若被编译加载，客户端无法解析其任何数据。
它早于外部 DLL 选型，是被遗留下来未被标注的历史产物（README 的构建说明也是残留）。

## 决策

1. 生产链路固定使用外部开源 AeroflyBridge.dll（安装器 `installer/data/dll.zip` 分发）。
2. 仓库内 `dll/` 降级为**参考脚手架**：仅用于理解端口分工（12345 遥测 / 12346 命令）
   与未来自研 DLL 的起点；在其源码头与 `dll/README.md` 中明确标注协议不兼容与
   "勿用于生产"。
3. CI 不再编译该脚手架（编译通过不等于可用，反而暗示其有效性）。
4. C 重写（ADR 0001）的桥接层按外部 DLL 的**扁平 JSON 契约**实现；
   自研 DLL 若立项，另立 ADR 并同步迁移两端。

## 后果

- 新贡献者不会再把 `dll/` 误认为在用组件（此前 README 指导编译它，属文档事故）。
- `tools/aerofly_link_setup.iss` 的 MyAppURL 曾指向外部 DLL 仓库，已改为本项目地址。
