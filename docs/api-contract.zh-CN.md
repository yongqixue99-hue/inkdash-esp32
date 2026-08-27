# 局域网数据接口约定

ESP32 默认从 `INKDASH_API_ORIGIN` 读取数据。示例：

```cpp
#define INKDASH_API_ORIGIN "http://192.168.1.100:8767"
```

接口只建议放在可信局域网。公开互联网部署时，应增加 HTTPS、访问控制和网络隔离。

## 1. 健康检查

```http
GET /health
```

示例：

```json
{
  "status": "ok",
  "account_profile": true,
  "wallpaper": false,
  "health_image": false
}
```

## 2. Codex 页面

```http
GET /dashboard
Accept: application/json
```

完整示例：

```json
{
  "remaining_percent": 72,
  "used_percent": 28,
  "reset_date": "2026-08-28",
  "reset_at": 1787861400,
  "usage_scope": "ACCOUNT",
  "daily_token_dates": [
    "2026-08-21",
    "2026-08-22",
    "2026-08-23",
    "2026-08-24",
    "2026-08-25",
    "2026-08-26",
    "2026-08-27"
  ],
  "daily_tokens": [12000000, 18000000, 0, 22000000, 15000000, 9000000, 6000000],
  "daily_usage_centi_yi": [12, 18, 0, 22, 15, 9, 6],
  "today_tokens": 6000000,
  "week_tokens": 82000000,
  "cumulative_tokens": 358000000,
  "peak_tokens": 22000000,
  "generated_at": 1787832000,
  "snapshot_age_seconds": 2
}
```

校验规则：

- `remaining_percent`、`used_percent` 为 0–100，合计 100；
- `reset_date` 与每日日期使用 `YYYY-MM-DD`；
- `reset_at`、`generated_at` 为正 Unix 秒；
- `usage_scope` 为 `ACCOUNT`、`WIN` 或 `WIN+MAC`；
- 三个日数组都必须恰好 7 项，日期严格递增；
- `today_tokens` 等于最后一个日桶；
- `week_tokens` 等于七项之和；
- `daily_usage_centi_yi` 每一项约等于 Token 除以 1,000,000，容差 500,000 Token；
- `ACCOUNT` 范围下，累计值不得小于七日合计；
- `snapshot_age_seconds` 超过 86,400 时显示 `STALE`。

`daily_usage_centi_yi` 的 1 表示 `0.01 亿`，也就是 1,000,000 Token。

## 3. 通用服务器页面

```http
GET /server-dashboard
```

未配置：

```json
{
  "name": "SERVER",
  "configured": false,
  "traffic_period": "MONTH",
  "traffic_used_centi_gb": 0,
  "traffic_limit_centi_gb": 0,
  "plan_limit_centi_gb": 0,
  "expiry_date": "",
  "expiry_days_remaining": 0,
  "traffic_reset_date": "",
  "cpu_percent": 0,
  "memory_percent": 0,
  "disk_percent": 0,
  "rx_centi_gb": 0,
  "tx_centi_gb": 0,
  "uptime_days": 0,
  "generated_at": 1787832000,
  "snapshot_age_seconds": 0
}
```

已配置字段见 `config/server-dashboard.example.json`。规则：

- `name` 最多 15 个 UTF-8 字节；
- `traffic_period` 固定为 `MONTH`；
- 流量使用百分之一 GB，`2560` 表示 `25.60 GB`；
- `traffic_limit_centi_gb` 与 `plan_limit_centi_gb` 都大于 0 且相等；
- CPU、内存、磁盘为 0–100；
- 到期和重置日期必须存在；
- 超过 24 小时没有更新时显示 `STALE`。

## 4. 壁纸与健康页

```http
GET /wallpaper.bin
GET /health.bin
Accept: application/vnd.inkdash.wallpaper
```

二进制布局：

| 偏移 | 字节 | 内容 |
|---:|---:|---|
| 0 | 8 | ASCII `INKWALL1` |
| 8 | 2 | 宽度 800，小端 |
| 10 | 2 | 高度 480，小端 |
| 12 | 4 | 单色平面长度 48,000 |
| 16 | 4 | 黑色平面 CRC32 |
| 20 | 4 | 红色平面 CRC32 |
| 24 | 4 | 格式版本 1 |
| 28 | 48,000 | 黑色 1-bit 平面 |
| 48,028 | 48,000 | 红色 1-bit 平面 |

总长固定为 96,028 字节。任何长度、尺寸、版本或 CRC 错误都会被拒绝，旧缓存保持不变。

## 5. HTTP 行为

- 数字接口响应必须是 HTTP 200 和非空 JSON；
- 固件默认响应上限为 8 KiB；
- 数字请求连接和读取超时为 6 秒；
- 图片请求超时为 15 秒；
- 设备 User-Agent 为 `InkDash-ESP32C3/1`；
- 数据服务不记录认证头，也不接收 Codex 凭据。
