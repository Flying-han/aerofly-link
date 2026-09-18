# 发布清单（Release Checklist）

版本号单一来源：`core/__init__.py` 的 `__version__`（语义化版本）。
发布时同步修改以下位置，并全库 grep 旧版本号兜底：

1. `core/__init__.py` — `__version__`（事实来源）
2. `installer/installer.py` — `APP_VERSION`（安装器独立打包，无法 import core）
3. `tools/aerofly_link_setup.iss` — `MyAppVersion`、`OutputBaseFilename`
4. `core/fsd_protocol.py` — `CLIENT_VERSION`（**协议标识**，仅在协议行为变化时升级，
   不跟随应用版本）

## 步骤

1. 确认 `python -m pytest tests core -q` 全绿；`python tests/smoke_ui.py` 冒烟通过。
2. `python -m compileall main.py main_window.py core ui installer`。
3. PyInstaller 打包主程序（`aerofly_link_folder.spec`），确认 `dist/AeroflyLink`
   体积与依赖符合预期（不应出现 Qt6WebEngine*）。
4. 制作 `installer/data/aerofly_link.zip`（dist 内容）与 `installer/data/dll.zip`
   （外部 AeroflyBridge.dll，见 ADR 0002）。
5. 打包安装器（`installer/installer.spec`），本机走一遍安装→启动→卸载。
6. 合并 PR 后在 GitHub 打 tag：`v<版本号>`（tag 与 `__version__` 严格一致）。

## 已知限制（发布说明应包含）

- 密码不持久化，每次启动需重新输入（ADR 0003）。
- 对真实 VATSIM 网络的 JWT 认证流程未实现：`vatsim` 模式发送空 challenge 的 `$ID`，
  在强制 FSD-JWT 的官方服务器上会被拒绝；对 ASC 私有服务器请使用 legacy 模式
  （revision 9，明文密码 + 服务端 bcrypt），该路径与 FSD 后端 0.5.3 互验通过。
