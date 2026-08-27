# InkDash 完整复刻教程

这份教程从参考购买、到货验收、源码编译写到日常使用。第一次做 ESP32 的读者，可以照章节顺序操作。已有硬件和开发经验的读者，可以从“准备电脑环境”开始。

## 0. 最终会得到什么

完成后，7.5 寸三色墨水屏会依次显示：

1. Codex 额度与近 7 日 Token；
2. 可选的通用服务器状态；
3. 自定义三色壁纸；
4. 健康数据摘要。

电脑在局域网内运行一个小型 Python 服务。它读取本机 Codex 数字记录，向 ESP32 提供 JSON。账号凭据、会话正文和服务器密码不会写进固件。

## 1. 参考购买

### 1.1 第一次复刻建议准备

- 1 套已匹配的 7.5 寸 800×480 黑白红屏幕与 ESP32-C3 控制/驱动板；
- 1 条确认能传数据的 USB-C 线；
- 1 个稳定 5 V USB 电源；
- 1 台 Windows 10/11 电脑；
- 可选：3.7 V 带保护锂电池；
- 可选：原配外壳或实装验证过的外壳。

屏幕和控制板优先成套准备。分别采购裸屏与板卡时，需要自行承担 FPC、波形、GPIO 和高压驱动兼容风险。详细规格见[硬件参考购买与到货核对](hardware-reference.zh-CN.md)。

### 1.2 下单前保存的信息

请保存这些照片或参数：

- 屏幕背面完整料号；
- FPC 排线上的全部字符；
- PCB 正反面、丝印版本；
- ESP32-C3 模组顶标与 Flash 容量；
- 电池尺寸、插头和正负极；
- 内部与外部 USB-C 的位置；
- 屏幕排线原始方向。

后面遇到花屏、BUSY 超时、USB 不识别时，这些信息比“7.5 寸三色屏”更有用。

## 2. 首次上电验收

先不刷固件，完成以下检查：

1. 用原有程序完成一次全刷，确认白、黑、红都能显示。
2. 观察刷新过程。三色全刷约 18 秒，多次闪烁属于正常现象。
3. 连接外部 USB-C，确认设备能稳定供电或充电。
4. 打开外壳后，用数据线连接内部 USB-C。
5. 打开 Windows 设备管理器，确认新增 Espressif USB/JTAG/串口设备。
6. 用手背短暂检查充电 IC、LDO 和屏幕驱动区域，没有异常高温再继续。

内部接口没有出现在设备管理器时，先换一条确认能传文件的数据线，再换电脑 USB 口。很多 USB-C 线只能充电。

## 3. 准备电脑环境

安装：

- Git；
- Python 3.11 或更新版本；
- PlatformIO CLI；
- Visual Studio Code 可选。

PowerShell 执行：

```powershell
git clone https://github.com/yongqixue99-hue/inkdash-esp32.git
Set-Location .\inkdash-esp32

python -m pip install --upgrade pip
python -m pip install platformio
python -m pip install -r requirements-dev.txt

pio --version
python --version
```

先编译一次，让 PlatformIO 下载 ESP32 平台和依赖：

```powershell
pio run
```

编译成功后，应出现：

```text
.pio\build\esp32c3-ink-dashboard\firmware.bin
```

## 4. 配置局域网数据地址

### 4.1 查电脑 IPv4

```powershell
ipconfig
```

找到当前 Wi-Fi 网卡的 IPv4，例如 `192.168.1.100`。忽略虚拟网卡、VPN 和 `127.0.0.1`。

### 4.2 建立私有配置

```powershell
Copy-Item .\include\secrets.example.h .\include\secrets.h
```

编辑 `include/secrets.h`：

```cpp
#define INKDASH_WIFI_SSID ""
#define INKDASH_WIFI_PASSWORD ""
#define INKDASH_API_ORIGIN "http://192.168.1.100:8767"
```

建议先让 SSID 和密码保持空字符串，通过手机配网。`include/secrets.h` 已加入 `.gitignore`，仍要避免截图或手工提交。

修改配置后重新编译：

```powershell
pio run
```

## 5. 启动 Codex 数据服务

### 5.1 数据来自哪里

服务端有两层数据：

- 本地层：扫描 `~/.codex/sessions` 内的 JSONL，只读取 `token_count` 数字事件，得到周额度、重置时间和本机 Token；
- 账号层：读取当前电脑 `~/.codex/auth.json`，调用 Codex 桌面端使用的账号统计接口，取得每日和累计 Token。

账号层成功时，页面标为 `ACCOUNT`。账号接口变化或暂时失败时，服务自动使用本地层。两层数据不会相加，避免把同一次使用计算两遍。

### 5.2 启动

```powershell
.\scripts\start_dashboard_api.ps1
```

首次启动时，Windows 防火墙可能弹窗。只允许“专用网络”，不要开放到公共网络。

本机浏览器检查：

```text
http://127.0.0.1:8767/health
http://127.0.0.1:8767/dashboard
```

再用手机连接同一 Wi-Fi，访问：

```text
http://电脑局域网IP:8767/dashboard
```

手机能看到 JSON，ESP32 才有机会访问。手机打不开时，依次检查：

- 电脑和设备是否在同一局域网；
- Windows 网络是否设为“专用”；
- 防火墙是否只放行 TCP 8767；
- 路由器是否开启 AP 隔离或访客网络隔离；
- `include/secrets.h` 的 IP 是否写错。

账号统计暂时不用时，可以关闭该层：

```powershell
.\scripts\start_dashboard_api.ps1 -DisableAccountProfile
```

## 6. 完整备份 Flash

这一节适用于分区哈希与仓库参考硬件一致的 4 MB 板。脚本会先验证 USB VID、芯片、Flash 大小和分区表；不匹配时停止，不会写入。

### 6.1 找 COM 口

在设备管理器查看内部 USB-C 对应的 COM 号，例如 `COM5`。

### 6.2 读出 4 MB

```powershell
.\scripts\backup_device_flash.ps1 -Port COM5
```

脚本按 64 KiB 分块读取并验证，结果保存在：

```text
backups\日期时间-COM5\original-flash-4mb.bin
backups\日期时间-COM5\metadata.json
```

请复制到另一块磁盘保存。备份可能含 Wi-Fi 与设备配置，不能上传到 GitHub、网盘公开目录或聊天附件。

### 6.3 无法自动进入下载模式

1. 按住 GPIO9/BOOT 触摸区或板上 BOOT；
2. 轻按 RST；
3. 先松开 RST，再松开 GPIO9/BOOT；
4. 重新查看 COM 口并执行备份。

GPIO3 不是 ROM 下载绑带脚。

## 7. 第一次烧录

先预演：

```powershell
.\scripts\flash_app_only.ps1 `
  -Port COM5 `
  -BackupPath .\backups\日期时间-COM5\original-flash-4mb.bin `
  -WhatIf
```

确认输出中的设备 MAC、当前 OTA 槽和写入地址无误，再执行：

```powershell
.\scripts\flash_app_only.ps1 `
  -Port COM5 `
  -BackupPath .\backups\日期时间-COM5\original-flash-4mb.bin
```

该脚本只写当前应用槽，并校验写入结果。它保留：

- bootloader；
- 分区表；
- NVS；
- 另一 OTA 应用槽；
- 数据分区；
- 完整恢复路径。

如果脚本提示分区哈希不一致，不要修改脚本绕过检查。先为实际 PCB 建立正确的 `platformio.ini`、分区表、GPIO 和恢复方案。

## 8. 第一次配网

固件没有有效 Wi-Fi 记录时，会建立：

```text
SSID: InkDash-Setup-XXXXXX
密码: inkdash75
```

用手机连接该热点，浏览器打开 `http://192.168.4.1`，选择家庭 Wi-Fi 并输入密码。保存后设备会重连，数据页开始刷新。

也可以保留内部 USB-C，通过串口配置：

```powershell
python .\scripts\provision_over_usb.py --port COM5 --ssid "你的Wi-Fi名称"
```

脚本会在终端安全提示输入密码，不回显密码。Windows 已保存该 Wi-Fi 时，也可以用：

```powershell
python .\scripts\provision_over_usb.py --port COM5 --profile "你的Wi-Fi名称"
```

固件把 Wi-Fi 写入独立 A/B 扇区。短暂断网时每 30 秒重试，不会立即覆盖屏幕。连续一小时无法关联原 Wi-Fi 时，恢复热点才会重新出现。

## 9. 验证第一张数据页

正常串口流程大致如下：

```text
ESP32-C3 Ink Dashboard starting
Wi-Fi connected
Codex dashboard payload validated
Displaying live page: codex-quota
Display refresh complete
```

保存 3 分钟串口日志：

```powershell
python .\scripts\capture_serial_log.py `
  --port COM5 `
  --seconds 180 `
  --log .\logs\first-boot.log
```

屏幕状态含义：

- `LIVE`：刚取得并通过校验的数据；
- `STALE`：显示上次有效快照，数据超过时效或本次请求失败；
- `OFFLINE`：设备从未取得有效快照；
- `WIFI SETUP`：等待配网。

固件不会用演示数字填充失败页面。

## 10. 按键与换页

### 10.1 触摸输入

GPIO9 和 GPIO3 都会被当作直接换页键。短按后固件保存页码、获取目标页数据，再执行一次全刷。

### 10.2 前置椭圆按键

当前机械按键接在 `EN` 与 GND 之间。ESP32 软件无法像普通 GPIO 那样持续读取它。按下时芯片重启。

本项目的处理方法：

1. 页码用校验和与序号写入两个 Flash 扇区；
2. 启动时判断复位原因；
3. 识别为外部/上电式复位时，页码前进一页；
4. 看门狗、崩溃和软件重启不换页；
5. 新页面先取数据，再刷新屏幕。

页序为：

```text
Codex → 服务器 → 壁纸 → 健康 → Codex
```

按键后要等待重启、联网和约 18 秒全刷完成。完全断电再上电也可能前进一页，这是 EN 方案的硬件限制。

USB 维护时可直接请求换页：

```powershell
python .\scripts\provision_over_usb.py --port COM5 --next-page
```

## 11. 自定义壁纸

准备任意图片，生成 800×480 三色包：

```powershell
python .\tools\build_network_wallpaper.py `
  .\my-picture.png `
  --output-dir .\artifacts\content
```

输出：

```text
artifacts\content\wallpaper-preview.png
artifacts\content\wallpaper.bin
artifacts\content\wallpaper.json
```

数据服务会自动在 `/wallpaper.bin` 提供该文件。固件检查：

- 魔数 `INKWALL1`；
- 800×480；
- 两个 48,000 字节色彩平面；
- 固定总长 96,028 字节；
- 黑色与红色平面的 CRC32。

下载成功后，设备把壁纸写入 A/B 缓存。电脑关机或网络断开，旧壁纸仍能显示。

## 12. 健康数据页

复制示例后填写真实七日数据：

```powershell
Copy-Item .\docs\health-dashboard-data.example.json .\health-data.json
```

生成三色页面：

```powershell
python .\tools\render_health_dashboard.py `
  .\health-data.json `
  --output-dir .\artifacts\content
```

生成器不会用样例值补空字段。没有数据的项目会保持空缺或标为不可用。输出的 `health.bin` 由数据服务通过 `/health.bin` 提供，并使用独立 A/B 缓存。

## 13. 通用服务器页

不需要服务器页时可以保持未配置，屏幕会显示 `SETUP`。

需要时：

```powershell
Copy-Item `
  .\config\server-dashboard.example.json `
  .\config\server-dashboard.json
```

按[接口约定](api-contract.zh-CN.md)更新真实数字。`generated_at` 使用 Unix 秒时间戳。静态文件超过 24 小时会被设备标为 `STALE`，适合由你自己的采集脚本定时改写。

## 14. 日常刷新策略

- Codex 页可每 30 分钟拉取一次；只有可见数字或状态改变才全刷。
- 当前页面每 4 小时做维护刷新，用于日期和电量。
- 数字接口默认 6 秒超时，主地址尝试两次。
- 页面切换时强制获取目标页；失败则显示最后一次有效缓存。
- 电池电压最多每 3 小时测一次，减少分压电路耗电。

电子纸断电后仍保留像素。断网期间看到旧画面是预期行为，状态标记负责告诉你数据是否过期。

## 15. 可选签名 OTA

第一次稳定运行前，不要急着配置 OTA。OTA 完整流程见[签名 OTA 与回滚](ota.zh-CN.md)。简化步骤：

1. 生成自己的 P-256 私钥和固件公钥头文件；
2. 重新有线刷入新的信任根；
3. 配置 Manifest 地址和固件 URL 前缀；
4. 用高于旧版本的数字版本号构建；
5. 先上传不可变固件，再发布签名 Manifest；
6. 用 USB 供电或电量高于 60% 时升级。

私钥只保存在离线或加密存储中，不能提交仓库。

## 16. 外壳与 3D 打印

有合适原壳时无需打印。需要自制外壳时，至少测量：

- 屏幕外轮廓和有效显示区；
- PCB 长宽、厚度和最高元件；
- FPC 折弯半径；
- 电池长宽厚；
- 内外 USB-C 开孔；
- 电源开关、触摸区和 EN 按键行程；
- 螺丝柱、卡扣与屏幕玻璃受力位置。

先打印低填充率试装件，确认屏幕窗口、按键和接口，再打印最终件。未经实装的 STL 不应标成通用外壳。

## 17. 常见故障

### 电脑没有新增设备

- 换数据线和 USB 口；
- 改用内部 USB-C；
- 按住 GPIO9/BOOT 后点按 RST；
- 设备管理器按硬件 ID确认 `VID_303A`。

### 备份脚本提示分区不匹配

这块板不属于当前参考分区。停止操作，保存已读备份，按实际硬件适配。不要替换哈希强行写入。

### 屏幕一直白、花屏或红色不对

- 核对屏幕背标和 FPC；
- 核对 24P 方向；
- 核对驱动类；
- 核对 BUSY、RST、DC、CS、CLK、MOSI；
- 先运行厂商对应型号的三色测试程序。

### `dashboard` 返回 503

- 打开 Codex 桌面端并产生一次会话；
- 检查 `~/.codex/sessions` 是否存在 JSONL；
- 账号层失败时用 `-DisableAccountProfile` 验证本地层；
- 查看终端日志，日志不会输出凭据。

### 手机能访问接口，ESP32 不行

- 检查固件中的电脑 IP；
- 确认 ESP32 与电脑在同一网段；
- 关闭 AP 隔离；
- 防火墙仅放行专用网络 TCP 8767；
- 串口查看 HTTP 状态码和连接错误。

### 按键按一次像重启

前置机械按键接 EN，重启就是它的工作方式。等待约 18 秒完成新页全刷。GPIO9/GPIO3 触摸输入才是固件可直接轮询的按键。

### 电量显示不准

这块板没有燃料计。百分比由电压粗略换算，充电和负载会造成波动。使用 USB 供电时，固件显示充电符号，不伪装成 100%。

## 18. 完成验收

关闭外壳前完成：

- 4 MB 备份在两处保存；
- `/health` 与 `/dashboard` 可从同一局域网访问；
- Codex 页面能从真实数据刷新；
- 前置按键和触摸键能循环页面；
- 断开电脑服务后能显示缓存；
- 壁纸损坏或下载失败不会覆盖上一张；
- USB 供电、锂电和充电温升均正常；
- 私有配置、备份和 OTA 私钥均未进入 Git。

这些项目通过后，才适合合壳并转入日常 OTA 更新。
