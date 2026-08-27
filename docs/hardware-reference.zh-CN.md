# 硬件参考购买与到货核对

本页给出参考购买规格和核对方法。它不绑定店铺，也不保证任意同名商品能直接兼容。电子纸最容易踩坑的地方，是屏幕尺寸相同、接口却不同；主控都叫 ESP32-C3、板上 GPIO 和 Flash 分区也可能完全不同。

## 1. 首选参考购买组合

适合第一次复刻的组合如下：

1. 已焊接并能点亮的 ESP32-C3 电子纸控制/驱动板，4 MB Flash；
2. 与该板成套测试过的 7.5 寸 800×480 黑白红三色屏；
3. 能传输数据的 USB-C 线；
4. 稳定的 5 V USB 电源；
5. 原配外壳或已经实装验证的外壳；
6. 可选的单节 3.7 V 带保护锂电池，插头和极性必须与板端匹配。

这一组合省去了 FPC 方向、电子纸高压驱动、缺货元件、焊接和首板调试。下单页面如果只写“7.5 寸墨水屏”，信息不够；至少应确认下一节的参数。

## 2. 屏幕参考规格

当前固件基线：

| 项目 | 参考要求 |
|---|---|
| 尺寸 | 7.5 寸 |
| 分辨率 | 800×480 |
| 颜色 | 黑 / 白 / 红三色 |
| 控制器 | UC8179 类 |
| 接口 | SPI，24P FPC，0.5 mm 间距 |
| 当前驱动类 | `GxEPD2_750c_Z08` |
| 全刷时间 | 实测约 18 秒，多次闪烁属于正常刷新过程 |

[Good Display GDEY075Z08 官方规格](https://www.good-display.com/product/394.html)与这些参数高度接近：800×480、黑白红、UC8179、24P/0.5 mm。它是参考候选，尚不能凭参数直接断定为同一块屏。购买前仍要比较：

- 屏幕背面完整料号；
- FPC 上的全部字符；
- 触点朝向和 FPC 插入方向；
- BUSY 电平和显示波形；
- 全白、全黑、全红三张测试图。

如果采用裸屏加转接板，可以参考 [DESPI-C02 官方页](https://www.good-display.com/product/516.html)。这种模块化连接需要重新核对 GPIO、电源和外壳，不能直接使用本仓库的 Rev A 固件配置。

## 3. 控制板参考规格

本仓库验证的板级条件：

| 功能 | 参考型号或要求 |
|---|---|
| MCU 模组 | `ESP32-C3-MINI-1-N4`，4 MB，参考料号 `C2838502` |
| USB | ESP32-C3 原生 USB D+/D− 已接到内部 USB-C |
| 显示 GPIO | CS7、DC2、RST8、BUSY10、CLK4、MOSI6 |
| 电池检测 | ADC GPIO0，使能 GPIO20 |
| 触摸按键 | GPIO9、GPIO3，低电平有效 |
| 机械按键 | 现有前置按键接 EN 与 GND，按下会重启 |
| 分区 | 4 MB，双 OTA 应用槽，每槽 `0x150000` |

主控同为 ESP32-C3 仍不代表二进制兼容。商品页或实物必须能证明 Flash 容量、GPIO、USB 数据线和屏幕驱动电路一致。

## 4. 已能锁定的板上器件

这些型号用于识别与维修，也可作为自研 PCB 的参考。单套数量不等于商城最低购买数量。

| 功能 | 精确型号 | 参考料号 | 单套数量 |
|---|---|---:|---:|
| MCU 模组 | ESP32-C3-MINI-1-N4 | [C2838502](https://www.lcsc.com/product-detail/C2838502.html) | 1 |
| 锂电充电 | TP4057-42-SOT26-R | [C12044](https://www.lcsc.com/product-detail/C12044.html) | 1 |
| 温湿度 | SHT40-AD1B-R2 | [C2909890](https://www.lcsc.com/product-detail/C2909890.html) | 1 |
| 3.3 V LDO | ME6210A33M3G | [C236680](https://www.lcsc.com/product-detail/Voltage-Regulators-Linear-Low-Drop-Out-LDO-Regulators_MICRONE-Nanjing-Micro-One-Elec-ME6210A33M3G_C236680.html) | 1 |
| RTC | RX8025T-UB | [C17353](https://www.lcsc.com/product-detail/Real-Time-Clocks_Seiko-Epson-RX8025T-UB_C17353.html) | 1 |
| 24P FPC 座 | AFC24-S24FIC-00 | [C262292](https://www.lcsc.com/product-detail/FFC-FPC-Connectors_JUSHUO-AFC24-S24FIC-00_C262292.html) | 1 |
| USB-C 母座 | MC-118LD-H65 | [C6332304](https://www.lcsc.com/product-image/C6332304.html) | 1 |
| 电源开关 | MSS12C02LS-BB2.0 | C3008585 | 1 |
| P 沟道 MOS | AO3401A | C15127 | 2 |
| N 沟道 MOS | AO3400A | C20917 | 3 |
| 小信号肖特基 | MBR0530 | C77336 | 3 |
| 100 µF/10 V 钽电容 | CA45-B010K107T | C122644 | 1 |

SHT40、RTC 和 MicroSD 并非当前 InkDash 页面运行的必需项。已有板上可以保留，自研简化板应由硬件工程师重新评估。

## 5. 电池参考购买要求

能确认的电气范围只有：单节 3.7 V 锂电/锂聚合物，充满 4.2 V。容量、长宽厚、保护板、线端插头和极性都要按手中机壳与 PCB 测量。

参考购买时向商品参数逐项核对：

- 单节 3.7 V，带保护板；
- 满充 4.2 V；
- 长、宽、厚均能放入外壳，保留排线和散热空间；
- 插头与板端连接器配套；
- 红黑线极性与 PCB 标识一致；
- 充电电流不超过电芯允许值。

极性不确定时不要插电池。第一次开发使用 USB 供电，确认充电回路和温升后再装电池。

## 6. 两个 USB-C 接口怎么分

当前结构中两个接口作用不同：

- 内部 USB-C：连到 ESP32-C3 原生 USB，电脑会识别 Espressif USB 设备；用于备份、烧录和串口维护。
- 外部 USB-C：方便合壳后供电/充电；当前实机中无法用于首次 ROM 烧录。

判断方法很简单：使用确认支持数据的线连接电脑，在 Windows 设备管理器查看是否出现新的 Espressif USB/JTAG/串口设备。只出现充电电流、设备管理器没有变化，说明该接口没有形成可用数据链路。

GPIO9 同时是 ESP32-C3 启动绑带脚。自动下载失败时，按住 GPIO9/BOOT，轻按 RST，再松开 GPIO9，设备会进入 ROM 下载模式。

## 7. 为什么一键 BOM 会显示 180 元左右

商城的“购买数量”通常受 MOQ、整盘或整管数量影响，它和一块板实际焊多少颗是两件事。现有截图中：

- FPC 座单板只用 1 个，最低购买量却可能是 37 个；
- 大量 0603 阻容单板只用几颗，页面按 20、50 或 100 个购买；
- 相同 MOS 被拆成两行时，容易重复承担两次 MOQ；
- 报价仍可能不含 PCB、贴片、屏幕、电池、外壳、运费和返工。

现有 BOM 还存在封装冲突、20 pF/15 pF 数值冲突、缺货触摸 IC、缺货肖特基、LED 型号缺失和电子纸升压电感参数缺失。因此不要把该购物车直接发布成“一键下单”。

## 8. PCB 自制边界

完整 PCBA 下单包至少需要：

- 与同一 PCB 修订号绑定的 Gerber 和 Drill；
- 板层、板厚、铜厚、表面处理与天线净空要求；
- 含 `Designator / Value / Footprint / MPN / C-number / DNP` 的 BOM；
- CPL/Pick-and-Place 坐标、面和旋转角；
- 正反面装配图、Pin 1 与极性图；
- DRC、DFM 和至少两块首板的 USB、充电、三色刷新、温升验证。

仓库目前没有把 PCB 自制列为入门主线。可参考 [OSHWHub 7.5 寸墨水屏项目](https://oshwhub.com/sakading/7-5-cun-mo-shui-ping)理解常见功能块，实际兼容性仍需按原理图、Gerber、GPIO 和面板背标重新核验。

## 9. 公开硬件替代路线

以下路线资料更完整，但不能直接刷本仓库 Rev A 二进制：

- [LaskaKit ESPink](https://github.com/LaskaKit/ESPink)：生产资料较完整，也列出 7.5 寸三色屏支持；主控和 GPIO 与本项目不同，需要移植。
- [Seeed XIAO 7.5 ePaper Panel](https://www.seeedstudio.com/XIAO-7-5-ePaper-Panel-p-6416.html)：屏幕、电池、USB 和外壳配套清楚，现成版本为黑白屏，无法复现红色效果。
- [lmarzen/esp32-weather-epd](https://github.com/lmarzen/esp32-weather-epd)：适合参考成品教程结构和模块化接线，硬件使用经典 ESP32 与独立驱动板。

## 10. 到货验收清单

通电或拆机前拍照保存：

- 屏幕背标与 FPC 全部字样；
- PCB 正反面与丝印版本；
- ESP32 模组完整顶标；
- 电池标签、尺寸、插头和极性；
- 内外两个 USB-C 接口位置；
- 屏幕排线原始插入方向。

上电后依次检查：

1. 原有画面能完成白、黑、红刷新；
2. 内部 USB-C 能被电脑识别；
3. Flash 为 4 MB；
4. 完整备份能读出并通过校验；
5. USB 供电时板上没有异常发热；
6. 电池版充电时电芯和充电 IC 温升正常。

任一项不成立，先停止烧录，按屏幕和 PCB 的真实型号重新适配。
