# Aerofly Link ↔ ASC FSD 端到端测试

本规范验证 Aerofly Link C 客户端与本机 ASC FSD 源码构建之间的 FSD wire、FSD-JWT、
位置/计划/文本、应答机状态、GUI 和依赖故障行为。它不是生产或公网 VATSIM 验收。

## 隔离边界

- FSD 源仓库只读；测试镜像从一个干净 checkout 构建，并写入实际 Git revision 与 UTC build time。
- Docker Compose 使用每次唯一的 project name、专属 MariaDB volume 和专属网络：DB/Redis 只在 internal backend 网络；FSD 另接客户端 bridge 网络，HTTP/TCP 只映射到 `127.0.0.1`。
- 账号、CID、密码、JWT、数据库口令均为临时合成值。配置和响应写在 `client-c/build/`（已忽略）；不要复制 JWT、trace、配置或数据库内容到 PR/Release。
- 不复用、不停止其他容器或卷；清理时只对本次 Compose project 执行 `down -v`。禁止使用全局 Docker prune。
- 本地 Go module 下载使用用户指定的 v2rayN `10808` 代理参数；脚本不修改 v2rayN、WinHTTP、Windows 代理或 vfox 全局设置。
- `e2e_jwt_provider_stub.c` 只链接到单独的测试 GUI/CLI 变体；`build.cmd` 的正式 GUI/CLI 不编译它，也不放宽 TLS 校验。

## 复现环境

Windows PowerShell、vfox 管理的 Zig (`zig cc`)、Docker Desktop Linux engine、Python 3、Pillow。FSD 源码仓库路径通过变量提供，不硬编码到客户端。示例固定使用 FSD `0.6.2`、MariaDB `10.11.19`、Redis `8.10.1`，其他版本应单独记录。

### 构建 FSD 测试镜像

从 FSD 仓库的干净 `main` checkout 读取 revision；不要在 FSD 仓库内生成构建文件：

```powershell
$FsdRepo = 'D:\Code\ASC-WorkSpace\FlightSimulatorDaemonForASC'
$commit = (git -C $FsdRepo rev-parse HEAD).Trim()
$buildTime = [DateTime]::UtcNow.ToString('yyyy-MM-ddTHH:mm:ssZ')
$image = "aerofly-link-e2e-fsd:0.6.2-$($commit.Substring(0,8))"
$proxy = 'http://host.docker.internal:10808'

docker build --tag $image `
  --build-arg VERSION=0.6.2 `
  --build-arg GIT_COMMIT=$commit `
  --build-arg BUILD_TIME=$buildTime `
  --build-arg GOPROXY=https://proxy.golang.org,direct `
  --build-arg HTTP_PROXY=$proxy `
  --build-arg HTTPS_PROXY=$proxy `
  $FsdRepo
```

若 Docker Desktop 守护进程无法从 Docker Hub 解析 Dockerfile 的固定 Go 基础镜像，先检查该摘要是否已在本机缓存。只有核对摘要一致后，才可临时给缓存镜像加唯一标签并通过 Dockerfile `GO_IMAGE` 参数引用；不要改 Docker Desktop 的全局代理设置或覆盖已有标签。

### 生成一次性服务配置并启动隔离栈

复制 FSD 仓库的 `deploy/config.example.json` 到新建的 `client-c/build/e2e_fsd_<UTC>/config.json`，只修改这份临时副本：

| 配置 | 测试值 |
|---|---|
| FSD TCP / HTTP listen | `0.0.0.0:6809` / `0.0.0.0:6810`（仅 Compose 网络可见） |
| `public_host` / `public_base_url` | `fsd-e2e.example.invalid` / `https://e2e.example.invalid` |
| DB host/account | `mariadb:3306` / `fsd`，一次性随机口令 |
| Redis | `redis:6379`、启用、专属 key prefix、一次性口令 |
| JWT secret | 随机合成值，至少 32 字节 |
| SMTP | 禁用 |

配置中的 `public_base_url` 按 FSD 校验要求保留 HTTPS 地址；隔离测试的 HTTP API 仍通过 loopback `http://127.0.0.1:16810` 访问。

下面的 PowerShell 修改只作用于这次复制的配置，口令用随机合成值：

```powershell
$dbPassword = [guid]::NewGuid().ToString('N')
$redisPassword = [guid]::NewGuid().ToString('N')
$jwtSecret = [guid]::NewGuid().ToString('N') + [guid]::NewGuid().ToString('N')
$config = Get-Content -Raw -Encoding UTF8 "$FsdRepo\deploy\config.example.json" | ConvertFrom-Json
$config.server.fsd_server.host = '0.0.0.0'
$config.server.fsd_server.public_host = 'fsd-e2e.example.invalid'
$config.server.http_server.host = '0.0.0.0'
$config.server.http_server.port = 6810
$config.server.http_server.public_base_url = 'https://e2e.example.invalid'
$config.server.http_server.email.enabled = $false
$config.server.http_server.jwt.secret = $jwtSecret
$config.database.host = 'mariadb'
$config.database.username = 'fsd'
$config.database.password = $dbPassword
$config.database.migration_username = 'fsd'
$config.database.migration_password = $dbPassword
$config.redis.enabled = $true
$config.redis.address = 'redis:6379'
$config.redis.password_env = 'FSD_REDIS_PASSWORD'
$config.redis.key_prefix = 'ale2e_' + [guid]::NewGuid().ToString('N') + ':'
$json = $config | ConvertTo-Json -Depth 100
[IO.File]::WriteAllText((Join-Path $runDir 'config.json'), $json, [Text.UTF8Encoding]::new($false))
```

```powershell
$runDir = 'D:\Code\ASC-WorkSpace\aerofly-link\client-c\build\e2e_fsd_<UTC>'
$project = 'aeroflylink-e2e-<UTC>'
$env:E2E_FSD_IMAGE = $image
$env:E2E_FSD_CONFIG = (Join-Path $runDir 'config.json').Replace('\','/')
$env:E2E_DB_PASSWORD = '<一次性随机值>'
$env:E2E_REDIS_PASSWORD = '<另一个一次性随机值>'
$env:E2E_FSD_TCP_PORT = '16809'
$env:E2E_FSD_HTTP_PORT = '16810'
$compose = 'tools/e2e-fsd-compose.yaml'

docker compose -p $project -f $compose config --quiet
docker compose -p $project -f $compose up -d
docker compose -p $project -f $compose ps -a
```

确认 FSD、MariaDB、Redis 为 healthy，`migrate` 退出码为 0；检查 `/healthz`、`/readyz` 和 `/api/server/configuration` 的版本为 `0.6.2`。用 FSD 自带 `admin create` 在这个空数据库中创建管理员测试账号，保存它自动分配的合成 CID；不要手工插入明文密码。

### 构建 test-only VATSIM 客户端

先运行正常的 `client-c\build.cmd`（会编译并执行 C 单测），再将本地 FSD JWT 写入进程环境变量 `AEROFLYLINK_E2E_JWT`。`e2e_jwt_provider_stub.c` 只替代测试链接的 `jwt_acquire()`，而 `session.c`、JWT wire 报文及服务端校验仍是真实实现：

```powershell
$env:ZIG_GLOBAL_CACHE_DIR = Join-Path (Get-Location) 'client-c/build/zig-global-cache'
$env:ZIG_LOCAL_CACHE_DIR = Join-Path (Get-Location) 'client-c/build/zig-local-cache'
Push-Location client-c
cmd.exe /d /c build.cmd
if ($LASTEXITCODE -ne 0) { throw 'client-c build failed' }
zig cc -std=c11 -Iinclude -c tests/e2e_jwt_provider_stub.c -o build/e2e_jwt_provider_stub.obj
$libs = @('-lws2_32','-ldwmapi','-luxtheme','-lgdi32','-lcomctl32','-lcomdlg32','-lwinhttp')
zig cc -static build/e2e_jwt_provider_stub.obj build/cli_main.obj build/libfsd.a $libs -o build/aeroflylink-e2e-cli.exe
Pop-Location
```

GUI E2E 用 `client-c\build-e2e-gui.cmd` 构建独立的 `build\aeroflylink-e2e.exe`；它使用同一 GUI、Nuklear 对象和 FSD core，只在测试宏下从进程环境读取合成密码并链接 JWT provider stub。运行 `tools/smoke_gui.py` 时使用隔离 APPDATA，驱动连接命令并轮询 FSD `/api/clients`。该可执行文件留在 gitignored `client-c/build/`，正式 `build.cmd` 与安装包不包含测试注入路径。

## 覆盖矩阵

| 场景 | 驱动 | 通过条件 |
|---|---|---|
| C 构建/协议回归 | `client-c\build.cmd` / vfox `zig cc` | `test_protocol` 和 `test_p2` 全绿；库归档只含 12 个客户端对象 |
| 认证令牌 | 本地 `POST /api/fsd-jwt` | 合成有效凭据返回 HS512、`aud=fsd-login`、CID 一致、120 秒 token；错密码返回 HTTP 401 |
| 新协议登录 | 两个 C CLI，test-only JWT provider 注入本地 FSD 签发 token | 每个连接均发送 `$ID` 与 revision 100 的 `#AP`，服务端 `/api/clients` 注册两个 pilot |
| 负向认证 | revision 100 + 结构合法、签名无效的合成 JWT | 返回 `$ER...006 Invalid CID/password`，不进入在线状态，不回退到 legacy 密码 |
| 飞行/位置 | 两客户端都连接内置 C Mock DLL | `@` 连续上报、peer traffic 转发；完整 `$FP` 写入临时 MariaDB，核对呼号/机场/航路 |
| 文本/应答机 | CLI `/ident`、`/stby`、`/alt`、`/squawk`、`@呼号 消息` | 双向 `#TM` 到达；位置 mode/代码反映 IDENT、STBY/0000、恢复 ALT/4321 |
| keepalive/退出 | 保持会话超过两个 30 秒窗口 | STBY 时发缓存位置 `@S...:0000`；无 `$ER`；`/quit` 发 `#DP`，在线快照清空 |
| GUI | `build-e2e-gui.cmd` + 隔离 APPDATA | Nuklear GUI 连接本地 FSD，`/api/clients` 出现测试呼号；连接页/工作区/设置、多语言/浅色截图生成，WM_CLOSE 退出 |
| Redis 故障 | 仅停止该 Compose project 的 `redis` service | `/api/fsd-jwt` 限流返回 `503 RATE_LIMIT_BACKEND_UNAVAILABLE`；Redis 恢复后 FSD ready `200` |
| 社区自定义 | C 单测 + 配置读写 | `jwt_url`、FSD 服务地址、认证模式、`jwt_proxy` 与 bypass 列表可配置并经过 app/session 和 settings round-trip；空 proxy 使用 Windows 自动代理 |

GUI 外部 FSD 模式检查隔离设置中的呼号/CID和所选服务端；测试变体从合成环境值准备掩码密码字段。正式构建没有测试环境密码注入，也不会把密码写入设置。

## TLS 验收边界

本地 FSD 的 JWT HTTP endpoint 在 Compose 内提供 HTTP，而产品 `jwt_acquire()` 严格要求 HTTPS 并验证系统信任链。VATSIM E2E 使用测试链接替代 `jwt_acquire()`，把**本地 FSD 签发的有效短时 token**注入 C 会话，因此验证了真实 C `$ID` / revision-100 `#AP` wire、服务端签名校验、权限与会话行为；它没有验证产品 WinHTTP 成功获取 token 的 TLS 路径。

本轮不向 Windows 证书信任库添加临时 CA、不放宽证书验证，也不向公网认证端点发送合成账号。若要关闭这一项，需提供受系统信任的隔离 HTTPS FSD fixture，或另行授权临时安装并在测试后移除一次性本地测试 CA。公网 VATSIM、生产 ASC、staging/EuroScope 验收仍是单独门禁。

WinHTTP 使用 `WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY` 处理空 `jwt_proxy`；显式配置时仅 JWT HTTP 会话使用配置内的命名代理和 bypass 列表，不修改 Windows 全局代理。vfox Zig 构建及配置 round-trip 单测验证配置传递；由于隔离服务端没有受信任 HTTPS 证书，本轮未进行产品 WinHTTP TLS 成功请求验收。

## v0.3.0 本地证据（2026-09-29）

- 客户端分支基线：`refactor/quality-v1.1`；`client-c/VERSION=0.3.0`。
- vfox Zig `0.15.1`：`170/170` 协议断言、`106/106` 会话/桥接/配置断言通过。
- 本地 FSD checkout：`FlightSimulatorDaemonForASC`，tag `Version-0.6.2`，commit `8142a8570c1f3ca612734363cdefe637fc0d2c97`；本次 OCI image revision label 与该 commit 一致。Docker Desktop `4.92.0` / Engine `29.8.0`。
- MariaDB `10.11.19` migration、Redis `8.10.1`、FSD health/ready/version 均通过；有效/无效 JWT、Redis fail-closed/恢复通过。
- 两个 VATSIM revision-100 C CLI：在线快照、`50/84` 位置报告、STBY keepalive、IDENT、squawk、双向 `#TM`、飞行计划落库、无服务端拒绝、优雅断开及离线清理通过。
- Win32 GUI VATSIM 登录/在线断言、连接页和工作区截图、正常 WM_CLOSE 通过；初始同步阶段清空 flight-plan 表单的 bug 已修复，截图确认配置字段保留。
- 发布 GUI + 本地 Mock GUI 冒烟通过，Mock 观测到 `$ID`、`#AP`、`$FP`、`@`、`#DP`；GUI fixture 同时确认配置呼号/CID和 VATSIM 类型被正确恢复。
- 兼容路径另测：legacy keepalive 改为 `@`；STBY `@S/0000` 跨 30 秒保活通过。
- 仓库内 Compose fixture 使用 internal DB/Redis backend 和 FSD client bridge；启动 migration、loopback `readyz` 验证通过。
- GUI 构建首次暴露静态库残留测试/界面对象问题；`build.cmd` 现先重建 `libfsd.a`，归档成员检查通过。
- 尚未通过：产品 WinHTTP 对隔离 HTTPS JWT endpoint 的完整成功路径、公网 VATSIM/生产互操作。

## v0.3.1 本地验证（2026-09-29）

- 客户端版本：`client-c/VERSION=0.3.1`。vfox Zig `0.15.1`：170/170 协议断言、145/145 会话/桥接/配置断言通过；Nuklear/GDI GUI 构建无编译警告。正式 GUI 与隔离 E2E GUI 均检查为 Windows GUI 子系统（PE subsystem 2）。
- `python tools/smoke_cli.py`：本地 Mock FSD 登录、计划、位置、聊天、应答机和 `#DP` 退出通过。Mock 日志将 `#AP` 密码字段替换为 `[REDACTED]`；密码未写入配置。
- `client-c\build-e2e-gui.cmd` + `python tools/smoke_gui.py`：本地 Mock FSD 与隔离 `%APPDATA%` E2E 通过，确认登录在线、连接/飞行/设置页、紧凑宽度、简中/繁中/美式英语、深色/浅色、三种本地化标题栏，以及关闭时发送 `#DP`；12 张 GUI 截图保存在忽略的 `client-c/build/e2e-smoke/`。
- 本地 FSD GUI E2E：干净 `FlightSimulatorDaemonForASC` `Version-0.6.2` checkout，commit `8142a8570c1f3ca612734363cdefe637fc0d2c97`。只读源码构建镜像，OCI revision 标签与 commit 一致；镜像 digest `sha256:b3eafffaf5d4c5250cd9424d2b97e75c09c2c2d6deafaf7086e7bd623ff45384`。基础镜像 `golang:1.27.1-bookworm@sha256:69a7b9788769bec032d238959b61854e9ae87f57be9029ec04e9885fabf99195`。
- Docker Desktop/Engine `29.8.1` Linux；MariaDB `10.11.19`、Redis `8.10.1`。migration、`/healthz`、`/readyz`、服务版本、合成管理员与本地 `/api/fsd-jwt` 均通过。GUI 以 revision 100 登录，在线快照出现测试呼号，关闭 GUI 后在线条目清除。
- Docker 测试使用唯一 Compose project、一次性账号/JWT/数据库卷和仅绑定 `127.0.0.1` 的端口；结束后删除该 project 的容器/卷/网络及本轮镜像和临时配置。没有运行全局 prune，FSD 源仓库保持只读。
- 本地 FSD JWT 由 test-only 链接 stub 注入，因此验证真实 FSD 会话 wire 和服务端校验，但**不验证**产品 WinHTTP 对受信 HTTPS endpoint 的成功获取路径。没有连接公网 ASC、VATSIM、staging 或生产环境。

## 收尾

仅清理本次 project：

```powershell
docker compose -p $project -f $compose down -v --remove-orphans
```

只移除本次新建、确认标签和 digest 的 E2E image/base alias；不运行 `docker system prune`。测试配置、JWT、日志和截图位于唯一的 ignored `client-c/build/e2e_fsd_<UTC>/` 下，确认不再需要后只删除该准确目录。
