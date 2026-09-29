# 本项目的 GitHub Actions 使用指南

## CI 会做什么

`.github/workflows/ci.yml` 在针对 `main` 的 PR、推送到 `main`，以及手动运行时启动 Windows CI：

1. 在 `windows-2022` runner 上固定安装 Zig `0.15.1` 和 Python `3.13`。
2. 编译正式 GUI/CLI，并运行 170 条协议断言和 145 条会话、桥接、配置断言。
3. 编译隔离的 E2E GUI，运行 CLI 与 GUI Mock FSD 流程。GUI 测试包含三种语言、两种主题、紧凑宽度、标题栏本地化和正常断开。
4. 上传 GUI/CLI 二进制和 GUI 截图。Artifact 保留 7 天；E2E 凭据是合成值，Mock 日志会脱敏，不上传测试配置目录。
5. 用 Inno Setup `6.7.1` 与占位 DLL 编译脚本，只检查脚本、版本读取和文件路径。该占位安装包会立即删除，**不会上传，也不可分发**。

Action 使用完整 commit SHA 固定版本。Dependabot 每周检查 Action 更新并创建 PR；审查更新时同时确认上游 release 和 commit 来源。

## 查看和运行 CI

### PR 自动检查

1. 把功能分支推到 GitHub，并对 `main` 创建 PR。
2. 打开仓库的 **Actions → CI**，查看本次运行。
3. 进入失败的 job 和 step 查看完整日志；提交修复后，PR 会自动启动新一轮运行。
4. 全部 required checks 通过后再走仓库的 PR 审查与合并流程。

成功的 CI run 页面底部有 **Artifacts**：

- `aeroflylink-windows-x64`：正式 GUI、CLI 和 manifest，可用于临时试用，不等同于安装包或正式 Release。
- `gui-e2e-evidence`：本地 Mock GUI 截图与已脱敏的 FSD 日志。

### 手动运行

有仓库写权限的成员可打开 **Actions → CI → Run workflow**，选择分支后运行。手动运行只会测试所选分支，不会创建安装包、Release 或 tag。日常 PR 不需要手动触发。

## 本地开发和推送

Actions 在 GitHub 提供的 Windows 虚拟机中运行，不能访问开发机本地文件或本机 v2rayN。开发机访问 GitHub 时可为单条 Git 命令使用 v2rayN HTTP 代理；本项目约定地址为 `127.0.0.1:10808`：

```powershell
# 检查代理端口
Test-NetConnection 127.0.0.1 -Port 10808

# 更新远端信息；代理只对这一条命令生效
git -c http.proxy=http://127.0.0.1:10808 fetch origin

# 推送功能分支；不会改写全局 Git 配置
git -c http.proxy=http://127.0.0.1:10808 push -u origin codex/your-change
```

不要把代理写进仓库配置或提交凭据。当前 Windows GUI E2E 依赖真实 Win32 窗口与 `PrintWindow`；Linux 上的 `act` 无法代替 GitHub Windows runner 验收该路径。本地复现命令为：

```powershell
cd client-c
build.cmd
build-e2e-gui.cmd
cd ..
python -m pip install -r tools/requirements-e2e.txt
python tools/smoke_cli.py
python tools/smoke_gui.py
```

## Release 与安全边界

`.github/workflows/release.yml` 只对 `v*` tag 启动。它会先检查 tag 必须与 `client-c/VERSION` 完全一致，再构建、下载上游 Bridge DLL、校验 SHA-256、编译真实安装包、生成安装包校验和，并将安装包附到 GitHub Release。发布 job 的 `GITHUB_TOKEN` 只有 `contents: write`；CI job 仅有 `contents: read`。

正式流程是：PR 合并 → 确认所合并 commit 的 Actions 全绿 → 确认 `client-c/VERSION`、上游 Bridge tag 与固定 SHA-256 → 在该合并 commit 上创建 `v<VERSION>` tag。推送 tag 会**自动公开 Release**，因此未准备好分发安装包时不要推 tag。

上游 Bridge 版本和摘要分别记录在 `release.yml` 与 `client-c/setup/AeroflyBridge.sha256`。更新它们时，从 [Aerofly-FS4-Bridge 官方 Release](https://github.com/jlgabriel/Aerofly-FS4-Bridge/releases)选定版本，核对 DLL 后计算：

```powershell
Get-FileHash .\AeroflyBridge.dll -Algorithm SHA256
```

通过代码审查更新版本 URL 与摘要；不要取消发布工作流中的哈希校验。CI 占位 DLL 只用于测试 Inno Setup 语法。

没有额外的仓库 Secrets：Release 使用 GitHub 自动提供的 `GITHUB_TOKEN`。任何上游下载失败或摘要不匹配都会让 Release job 失败，不应通过关闭校验来绕过。
