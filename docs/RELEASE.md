# 发布清单（Release Checklist）— C 版

版本号单一来源：`client-c/VERSION`（语义化版本，纯文本一行）。
发布时同步检查以下位置，并全库 grep 旧版本号兜底：

1. `client-c/VERSION` — 唯一事实来源（构建注入 `AEROFLYLINK_VERSION`、安装器 ISPP 读取）
2. `client-c/setup/aerofly_link_setup.iss` — 无硬编码版本（ISPP 读 VERSION），勿回填
3. `client-c/src/session.c` — `$ID` 的 `client_version "1.0"` 为**协议标识**，
   仅在协议行为变化时升级，不跟随应用版本

## 步骤

1. 确认 `client-c\build.cmd` 全绿（编译 + test_protocol + test_p2 全部断言）。
2. Mock E2E：`python tools/mock_fsd_server.py --port 16809`，
   `AEROFLYLINK_DEBUG=1 client-c\build\aeroflylink-cli.exe --config <scratch配置> --password <pw>`，
   核对 `>>>`/`<<<` 协议跟踪：登录 → 1Hz 上报 → keepalive → `#DP` 退出。
   （tools/ 下两个 Python 脚本是开发工具依赖，非运行时依赖。）
3. GUI 冒烟：`python tools/smoke_gui.py`（Mock FSD 全链路 + 双页截图，退出码 0）。
4. 打包：ISCC 编译 `client-c\setup\aerofly_link_setup.iss`（CI 会自动验证；
   本地需 Inno Setup 6 + 真实 `client-c\setup\AeroflyBridge.dll`）。
5. 安装/卸载走查：安装后启动登录；卸载后确认
   `{userdocs}\Aerofly FS 4\external_dll\AeroflyBridge.dll` **保留**
   （uninsneveruninstall，游戏加载所需）。
6. 静态链接抽查：二进制内不得出现 `msvcrt.dll` / `libgcc`；
   `api-ms-win-crt-*`（Win10+ 内置 UCRT）允许。
7. 合并 PR 后在 GitHub 打 tag：`v<VERSION>`（严格一致）→
   `release.yml` 自动构建并附安装包到 GitHub Release。

## 已知限制（发布说明应包含）

- 密码不持久化，每次启动需重新输入（ADR 0003）。
- 对真实 VATSIM 网络的 JWT 认证流程未实现：`vatsim` 模式发送空 challenge 的 `$ID`，
  在强制 FSD-JWT 的官方服务器上会被拒绝；对 ASC 服务器请使用 legacy 模式
  （revision 9，明文密码 + 服务端 bcrypt），该路径与 FSD 后端 0.5.3 互验通过。
- 无内置地图（Python 版后期也已移除，非 C 版回退）。
- traffic 表中 `#AP` 来源的条目无坐标（仅呼号/机型/高度，与 Python 一致），
  不参与 10nm 附近统计。
- 位置上报需游戏内 AeroflyBridge.dll（外部开源，ADR 0002）或内置模拟模式（`--mock`）。
