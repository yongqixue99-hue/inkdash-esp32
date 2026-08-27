# 签名 OTA 与回滚

OTA 属于可选进阶功能。默认配置中 Manifest 地址和固件 URL 前缀为空，设备不会主动检查升级。

## 1. 信任模型

- 发布电脑保存 ECDSA P-256 私钥；
- ESP32 固件只嵌入公钥；
- Manifest 绑定硬件 ID、频道、版本号、显示版本、固件大小、SHA-256 和 URL；
- 固件只接受版本号更高、签名正确、URL 前缀一致的镜像；
- 下载写入非活动 OTA 槽；
- 新镜像经过一次完整启动和屏幕刷新后才确认健康。

## 2. 生成自己的密钥

```powershell
.\scripts\New-InkDashOtaSigningKey.ps1
```

它会生成：

```text
artifacts\private\inkdash-ota-p256-private.pk8
artifacts\ota\inkdash-ota-p256-public.pem
include\generated\ota_public_key.h
```

前两项在 `artifacts/` 下，不会被 Git 跟踪。`ota_public_key.h` 需要随固件提交；私钥需要离线加密备份。

运行脚本后，要通过内部 USB-C 再完成一次有线首刷，让设备信任新公钥。

## 3. 配置发布地址

在 `include/secrets.h` 中配置：

```cpp
#define INKDASH_FIRMWARE_MANIFEST_ENDPOINT \
  "https://updates.example.com/inkdash/stable/manifest.json"
#define INKDASH_FIRMWARE_URL_PREFIX \
  "https://updates.example.com/inkdash/stable/"
```

两个值必须同时配置。URL 前缀要精确包含频道目录，并以 `/` 结尾。

## 4. 构建签名发布包

数字版本号必须单调增加，已经发布的数字不能重用：

```powershell
.\scripts\Build-InkDashOtaRelease.ps1 `
  -VersionCode 2026082701 `
  -Version 1.0.0 `
  -Channel stable `
  -BaseUrl https://updates.example.com/inkdash
```

输出位于：

```text
artifacts\ota\repository\stable\2026082701\firmware.bin
artifacts\ota\repository\stable\2026082701\release.json
artifacts\ota\repository\stable\manifest.json
```

发布顺序：

1. 上传版本目录中的 `firmware.bin`；
2. 从实际 URL 下载一次，核对长度和 SHA-256；
3. 最后原子替换 `stable/manifest.json`。

Manifest 先上线会让设备看见一个尚未完整上传的版本。

## 5. 设备安装条件

- 自动检查在启动约 60 秒后进行，成功后每天一次；
- 检查失败后约 6 小时重试；
- 自动安装需要 USB 外接电源，或估算电量不低于 60%；
- USB 指令可以立即请求检查：

```powershell
python .\scripts\provision_over_usb.py --port COM5 --ota-check
```

## 6. 回滚流程

写入新槽前，应用健康日志保存目标槽、旧槽、版本和镜像哈希。新固件启动后依次检查：

- 数字快照存储可用；
- Wi-Fi 存储可用；
- 壁纸和健康页缓存可用；
- 当前页面完成一次全刷。

全部通过后清除待确认状态。确认前再次复位，下一次启动会选择旧槽。

应用甚至无法运行到健康管理器时，仍需要内部 USB-C 和完整备份恢复。OTA 不能代替有线救砖路径。

## 7. 私钥事故

- 私钥丢失：无法再发布被现有设备接受的新版本，需要有线刷入新公钥；
- 私钥泄露：立即停止发布，生成新密钥并有线轮换；
- 误发错误固件但已确认健康：用更高版本号发布修复版，设备拒绝降级；
- Manifest 或固件损坏：签名、长度和 SHA-256 校验会拒绝安装。
