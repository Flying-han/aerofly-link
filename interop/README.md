# P2 线上互验操作单（对 ASC 生产服务器）

> 真实互验配置位于 `.gitignore` 屏蔽的 `asc_live.json`；模板和本操作单纳入版本控制。不要把密码、JWT 或完整认证 trace 加入 issue/PR。

## 准备

1. 复制模板并填入真实信息（**不要填密码**）：

   ```cmd
   copy interop\asc_live.template.json interop\asc_live.json
   notepad interop\asc_live.json
   ```

   需要改的字段：`callsign`（测试呼号）、`cid`、`server`（ASC 服务器地址）、
   `port`（一般 6809）、`jwt_url`（兼容的 FSD-JWT HTTPS 端点）。

   `jwt_proxy` 可单独为令牌请求设置 WinHTTP HTTP proxy list，例：
   `http=127.0.0.1:10808;https=127.0.0.1:10808`；留空时使用 Windows 自动代理。
   此字段要求 HTTP 代理协议；若本机代理客户端同时提供 SOCKS 和 HTTP 监听端口，
   应填 HTTP 监听端口。`jwt_proxy_bypass` 配置命名代理下直连的主机列表。

   **`type` 必须与你要用的连接模式一致**：
   - `"type": "vatsim"`（VATSIM/swift 模式，**推荐**）——走 FSD-JWT 流程：
     `$ID` → `POST {jwt_url}` 换短时效令牌 → `#AP` rev100。`jwt_url` 默认
     `https://api.skeet.top/api/fsd-jwt`，社区或私有部署可在配置里覆盖。
   - `"type": "legacy"`（revision 9 明文密码 + 服务端 bcrypt）——仅用于明确允许
     legacy 登录的兼容服务器。
   此 E2E 的目标服务器使用 VATSIM JWT/revision 100；模式值大小写不敏感。

## 运行

```cmd
cd /d D:\Code\ASC-WorkSpace\aerofly-link
set AEROFLYLINK_DEBUG=1
client-c\build\aeroflylink-cli.exe --config interop\asc_live.json --password 你的密码
```

启动即自动连接。`TRACE >>>/<<<` 行是协议原始收发，作为判定证据。

## 通过矩阵（跑的时候逐项核对）

| # | 项 | 怎么看 |
|---|----|--------|
| 1 | 握手 ≤15s 认证完成 | `<<< $DI` → `>>> $ID` →（vatsim 模式多一步 HTTPS）→ `>>> #AP` → `[FSD] 已连接并认证` |
| 2 | 无 `#ER` 拒绝 | TRACE 中无 `<<< #ER` |
| 3 | 1Hz 位置上报、服务端可见在线 | 每 1s 一条 `>>> @N:呼号:...`；服务器/雷达出现该呼号 |
| 4 | 飞行计划被接受 | 握手即有 `>>> $FP...NOFP`；输入 `/fp` 后无 `#ER` 回显 |
| 5 | 空闲 >10min 保持在线 | 放置 10 分钟；两种认证模式都以缓存位置 `@` 保活，不发往 `SERVER` 的 `#TM` |
| 6 | CAPS 应答 | 若服务器发 `<<< $CQ...:CAPS`，随后必有 `>>> $CR...CAPS:...` |
| 7 | PING | `<<< $PI` → `>>> $PO`；`<<< $ZC` → `>>> $ZR` |
| 8 | 收服务器文本 | `<<< #TM...` 在日志显示 |
| 9 | 优雅退出 | 输入 `/quit` → 末尾一条 `>>> #DP呼号` → `已断开连接` |
| 10 | 读超时（可选） | 连接后防火墙断其出站 90s 内应打印 `连接超时：长时间无数据`（单测已覆盖，可跳） |
| 11 | 文本送达 | 输入 `@ZGGG_TWR 测试` → `>>> #TM呼号:ZGGG_TWR:测试`；服务端/对端能收到 |

## 退出与收尾

- 客户端：输入 `/quit` 回车。
- 把完整控制台输出（含 TRACE）复制回来归档进 PR 描述；
  服务端有日志的话附「呼号在线/离线、无协议错误」的摘录。

## 常见问题

- `JWT 获取失败：JWT 换取被拒绝（HTTP 401）`：CID/密码错误，或账号未开通。
- `$ER...006 Invalid CID/password`：legacy 模式凭据错误。
- `认证超时`：服务器地址/端口或模式不对，先确认服务端允许所选模式。
- `连接超时`（立刻出现）：地址/端口不通，用 `telnet <host> <port>` 先验。
- 想换呼号重测：`--callsign TST456` 参数可临时覆盖。
