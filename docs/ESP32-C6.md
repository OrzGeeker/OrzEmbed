# ESP32-C6-LCD-1.47

[ESP32-C6-LCD-1.47 商品链接](https://item.jd.com/10117390267248.html)

![ESP32-C6-LCD-1.47](/images/ESP32-C6-LCD-1.47.png)

---

## 规格概览

| 项 | 参数 |
|----|------|
| MCU | **ESP32-C6FH4**(RISC-V RV32IMAC,160MHz,512KB SRAM,4MB 片内 flash) |
| 无线 | Wi-Fi 6(2.4GHz)、Bluetooth LE 5.0、IEEE 802.15.4(Thread/Zigbee) |
| 显示 | 1.47″ **ST7789** 172×320 彩色 IPS(SPI) |
| 存储 | Micro SD 卡座(SPI) |
| 灯 | 板载 **WS2812 RGB 灯**(GPIO8) |
| 按键 | BOOT(GPIO9)、RESET |
| 调试 | 内置 **USB-Serial-JTAG**(烧录 / 串口 / JTAG) |
| 供电 | USB-C 5V;板载 ME6217C33M5G LDO(3.3V,800mA Max) |

## 元件说明

![ESP32-C6-LCD-1.47-DESC](/images/ESP32-C6-LCD-1.47-DESC.png)

1. ESP32-C6FH4
2. ME6217C33M5G 低压降 LDO，电流 800mA (Max)
3. Micro SD 卡座
4. 贴片陶瓷天线
5. BOOT 按键
6. RESET 按键

## GPIO

![ESP32-C6-GPIO](/images/ESP32-C6-GPIO.png)

### 引脚速查(板载外设)

| 功能 | 引脚 |
|------|------|
| LCD MOSI / SD MOSI | GPIO6 |
| LCD SCLK / SD SCLK | GPIO7 |
| SD MISO | GPIO5 |
| SD CS | GPIO4 |
| LCD CS / DC / RST / BL | GPIO14 / GPIO15 / GPIO21 / GPIO22 |
| RGB LED(WS2812) | GPIO8 |
| BOOT 按键 | GPIO9 |
| USB D− / D+ | GPIO12 / GPIO13 |
| 控制台 UART TX / RX | GPIO16 / GPIO17 |

### 排针可自由使用的 GPIO

排针引出:`GP0~GP5`(左)、`GP9/GP12/GP13/GP18/GP19/GP20/GP23`(右)、`TX/RX`。

其中**最安全、不与板载外设冲突**的只有 **8 个**:
`GPIO0 / 1 / 2 / 3 / 18 / 19 / 20 / 23`

需注意:
- `GPIO4 / 5` → 与 **SD 卡**冲突;
- `GPIO9` → **BOOT 按键**,只能当输入,勿做输出;
- `GPIO12 / 13` → **USB**,占用会断烧录/串口;
- `GPIO16 / 17` → **控制台 UART**,占用会断串口输出;
- Strapping 脚:`GPIO4 / 5 / 8 / 9 / 15`;
- `GPIO24~30` 为片内 flash 专用,**GPIO10 / 11 未引出**(SiP flash 型号),**不可用**。

## 板子尺寸

![ESP32-C6-LCD-1.47-Board](/images/ESP32-C6-LCD-1.47-Board.png)

---

## 串口 / 烧录

- 串口为**内置 USB-Serial-JTAG**,设备号为 `/dev/cu.usbmodem*`(每台机器不同,如 `/dev/cu.usbmodem14101`);
- 这是**唯一可用的串口**(无独立 USB-UART 芯片),也用于 `esptool` 烧录与 JTAG 调试。

> ⚠️ 刷写后内置 USB-Serial-JTAG 常停在 **DOWNLOAD 模式**(串口无输出)→ 按 **RST** 或重新插拔 USB 即可运行。
> 主机侧打开串口时不要用 DTR/RTS 触发下载;`idf.py monitor` 无输出时按一下 RST。

---

## 已知硬件问题

本板实测发现 **射频衰减严重**(约 55~60dB):**WiFi 与 BLE 空口几乎不可用**,而 LCD / SD / GPIO / RGB / NVS 等**均正常**。
定位过程、排查步骤与复测方法见 **[板级自检记录](./ESP32-C6-LCD-1.47-BRINGUP.md)**。

##  课程学习

- [社区资源](https://www.espressif.com.cn/zh-hans/ecosystem/community-engagement/courses)
