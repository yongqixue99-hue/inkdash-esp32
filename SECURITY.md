# Security

## 不要提交的内容

- `include/secrets.h`
- `config/server-dashboard.json`
- `artifacts/`
- `backups/`
- Codex `auth.json`
- Codex 会话 JSONL
- Cookie、Bearer Token、账号 ID
- Wi-Fi 密码
- SSH 私钥和 OTA 私钥

完整 Flash 备份可能含设备配置和网络凭据，应放在离线或加密存储中。

## 运行边界

默认 Python 服务监听局域网 TCP 8767，不提供互联网级认证。只在可信专用网络使用，并让防火墙限制来源。需要互联网访问时，请在前面增加 TLS、身份验证、限流和访问日志脱敏。

账号统计地址属于桌面端使用的非公开接口。凭据只用于电脑发出的 HTTPS 请求，任何错误日志和设备响应都不应包含凭据。

## 报告问题

发现可能泄露凭据、绕过 OTA 签名或破坏 Flash 回滚的问题时，请使用 GitHub Security Advisory 私下报告，不要在公开 Issue 中粘贴密钥、备份或完整日志。
