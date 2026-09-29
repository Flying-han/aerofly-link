# ADR 0008: 默认使用 VATSIM JWT 并按 FSD wire 契约维持会话

- 状态: 已接受（2026-09-29）
- 关联: ADR 0007、`docs/testing/e2e-fsd-client.md`

## 背景

本机 ASC FSD 源码 E2E 暴露出几项此前的假设与真实服务端行为不一致：客户端把两字段 `#SB<callsign>:SERVER` 当作 SquawkBox 身份声明发送，而 FSD 将 `#SB` 定义为至少含 `sender:target:subtype` 的定向模型消息；legacy keepalive 把 `#TM` 发给 `SERVER`，FSD 返回 `007 No such callsign`；VATSIM revision-100 握手的 JWT 临时缓冲区在构造 `#AP` 前已离开作用域。此外，GUI 页面的 40 槽控件数组登记了 46 个项，越界覆盖连接页控件句柄；启动时第一次 disconnected 同步也误清空了已加载的飞行计划。

## 决策

1. 新安装默认选择 `vatsim`：`$ID` 后以 FSD-JWT 和 revision 100 登录。`jwt_url`、JWT 专用 `jwt_proxy` / `jwt_proxy_bypass`、FSD 主机/端口和服务器列表保持可配置，`legacy` revision 9 仍作为显式兼容选项。空代理使用 Windows 自动代理，不修改系统代理。
2. 不发送无效的登录期 `#SB`；认证后立即提交最小 `$FP` 与初始位置。
3. 两种认证模式统一用缓存位置 `@` 保活；ALT/STBY/IDENT/应答机代码变更同步到缓存，STBY 保活发送 `@S` 与 `0000`。不再将 `#TM` 发给 `SERVER`。
4. JWT 缓冲区覆盖报文构造和发送生命周期；debug trace 对所有 `#AP` 密码字段统一输出 `[REDACTED]`。
5. 工作区控件数组容量覆盖 46 个登记项；初次启动只显示连接页，不清空已加载飞行计划，真正断开后才清空。

## 后果与验证

- VATSIM/FSD-JWT 成为新用户默认，社区私服可将 `jwt_url` 指向自己的 HTTPS 兼容端点，并为令牌请求配置独立代理；服务器地址、端口、模式和服务列表仍由用户控制。仅允许 legacy 的服务器仍可显式选择 legacy。
- 单测必须覆盖默认 VATSIM 配置、自定义 JWT URL/代理/bypass、settings round-trip、密码 trace 脱敏、缓存位置保活和 STBY 缓存状态。
- 本地 FSD 0.6.2 Docker E2E 覆盖 revision-100 登录、双客户端业务路径和 Win32 GUI；由于本地 FSD HTTP JWT fixture 没有受信任 HTTPS 证书，`jwt_acquire()` 的产品 WinHTTP 成功路径仍单独标为未验证。详见 E2E 报告。
