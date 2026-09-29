# 发布清单（Release Checklist）— C 版

版本号单一来源：`client-c/VERSION`（语义化版本，纯文本一行）。
发布时同步检查以下位置，并全库 grep 旧版本号兜底：

1. `client-c/VERSION` — 唯一事实来源（构建注入 `AEROFLYLINK_VERSION`、安装器 ISPP 读取）
2. `client-c/setup/aerofly_link_setup.iss` — 无硬编码版本（ISPP 读 VERSION），勿回填
3. `client-c/src/session.c` — `$ID` 的 `client_version "1.0"` 为**协议标识**，
   仅在协议行为变化时升级，不跟随应用版本

## 步骤

1. 确认 `client-c\build.cmd` 全绿（zig cc 编译 + `test_protocol` + `test_p2`）。
2. 本地 FSD E2E：按 [`docs/testing/e2e-fsd-client.md`](testing/e2e-fsd-client.md) 从干净的 ASC FSD 源 checkout 构建隔离镜像，验证 FSD-JWT/revision 100；严格记录 `jwt_acquire()` TLS 路径是否由可信 HTTPS fixture 实际覆盖。
3. Mock CLI E2E：`python tools/smoke_cli.py`。脚本用隔离配置和合成密码，验证登录、飞行计划、
   位置、聊天、应答机、凭据日志脱敏及 `#DP` 退出；Mock FSD 日志不记录密码。
   `tools/` 下的 Python 程序都是开发工具，不是客户端运行时依赖。
4. GUI E2E：先运行 `client-c\build-e2e-gui.cmd`，再运行 `python tools/smoke_gui.py`
   （隔离 APPDATA、Mock 或本地 ASC FSD 全链路、登录/断开、窗口截图、三语言和两种主题、
   WM_CLOSE 退出码 0）。E2E 可执行文件仅位于忽略目录 `client-c\build\`，不进入安装包。
5. CI 会用占位 DLL 编译 Inno Setup 脚本来验证语法和路径；产物会删除，不会上传或分发。
   正式安装包仅由 `release.yml` 下载真实 `AeroflyBridge.dll` 后生成。本地打包需 Inno Setup 6
   和真实 `client-c\setup\AeroflyBridge.dll`。
6. 安装/卸载走查：安装后启动登录；卸载后确认
   `{userdocs}\Aerofly FS 4\external_dll\AeroflyBridge.dll` **保留**
   （uninsneveruninstall，游戏加载所需）。
7. 静态链接抽查：二进制内不得出现 `msvcrt.dll` / `libgcc`；
   `api-ms-win-crt-*`（Win10+ 内置 UCRT）允许。
8. 合并 PR 且 Actions 全绿后，在该合并 commit 上打 tag：`v<VERSION>`（严格一致）。
   `release.yml` 会校验 tag、下载并核对 `client-c/setup/AeroflyBridge.sha256`，再自动公开
   安装包到 GitHub Release。推送 tag 即触发公开发布。

Actions 触发方式、artifact 内容、v2rayN 下的 Git 命令和手动复跑步骤见
[`docs/GITHUB_ACTIONS.md`](GITHUB_ACTIONS.md)。

## 已知限制（发布说明应包含）

- 密码不持久化，每次启动需重新输入（ADR 0003）。
- `auth_mode=fsd-jwt` 使用 HTTPS 取得令牌并以 revision 100 登录；`legacy` 使用 revision 9。
  这表示 FSD 认证方言，不代表获准连接 VATSIM。VATSIM 官方要求使用获批客户端；Aerofly Link
  当前不在公开获批清单中，GUI 只显示其 AUTOMATIC 地址作参考并阻止 `*.vatsim.net` 直连。
  见 [ADR 0009](adr/0009-server-defaults-and-vatsim-policy.md)。
- v0.3.1 支持目标为 Windows 10 及更新版本；Linux/macOS 未承诺。真实遥测仍需外部 AeroflyBridge DLL。
- 本地 FSD 源码 Docker E2E、公网 ASC/VATSIM 以及产品 WinHTTP 成功获取 JWT 的可信 HTTPS 路径必须分别记录，不互相替代；本地 test-only JWT stub 不验证产品 HTTPS 成功路径。
- 无内置地图。
- traffic 表中 `#AP` 来源的条目无坐标（仅呼号/机型/高度），
  不参与 10nm 附近统计。
- 位置上报需游戏内 AeroflyBridge.dll（外部开源，ADR 0002）或内置模拟模式（`--mock`）。
