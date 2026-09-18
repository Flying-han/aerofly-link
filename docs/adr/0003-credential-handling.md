# ADR 0003: 凭据处理——密码仅驻内存，不落盘

- 状态: 已接受（2026-09-18）

## 背景

`main_window._save_settings()` 曾把连接页表单（含 VATSIM/服务器密码明文）整体写入
`config/settings.json`（打包环境为 `%APPDATA%/AeroflyLink/settings.json`）。
该文件不受 DPAPI/任何加密保护，任何同权限进程可读。VATSIM 密码可支配账号的
在线身份，泄露面不可接受；且 `settings.json` 在 .gitignore 中只是防误提交，
不能防本机读取。

## 决策

1. **密码只在本次运行的内存中使用，绝不持久化**：`_save_settings()` 在写盘前
   移除 `password` 键；`_load_settings()` 不读取密码（历史上也未曾回填）。
2. 其余连接参数（呼号/CID/服务器/等级等）继续明文保存——它们不构成冒用凭据。
3. 不引入"加密存储"（DPAPI 等）：对明文密码做可逆加密只是增加破译步骤，
   不是安全边界；未来若做，必须整件托管给操作系统凭据库，另立 ADR。
4. C 重写客户端（ADR 0001）遵循同一策略：密码仅内存驻留，配置文件不含密码。

## 后果

- 用户每次启动需重新输入密码（与多数 VATSIM 客户端的保守做法一致）。
- `config/settings.example.json` 保留 `password` 空键仅为结构示意，运行时不会写入值；
  文档需继续说明该字段不回填。
