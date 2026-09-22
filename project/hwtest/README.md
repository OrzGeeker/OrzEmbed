# 板级自动自检 (`project/hwtest`)

一条命令跑完板载外设自检,输出 `PASS/FAIL` 汇总,**并把每项结果画在 LCD 上**。

## 覆盖项

芯片 / Flash / NVS / GPIO(桥接+卡死)/ LCD(ST7789)/ microSD / RGB(WS2812)/ WiFi。

## 运行

```bash
./scripts/esp32c6-hwtest.sh            # 自动自检(有 FAIL 时退出码非 0)
./scripts/esp32c6-hwtest.sh --probe    # 追加引导式“导通/虚焊”探测
```

脚本会编译 + 烧录本工程、采集串口并解析结果。

## 排针焊接验收

- `gpio_bridge`:自动检测排针间**锡桥/短路**(逐个拉高 + 基线差分 + 双向确认,抑制浮空脚误报);
- `--probe`:逐个提示把排针短接到 GND,自动判定**虚焊/开路**。

> 若板上焊有外部 LED(如 `project/timer` 的 GP18/19/20/23),`gpio_pull` 会显示这些脚被拉低(WARN),属正常。

## 依赖

`orz_board` / `orz_lcd` / `orz_rgb`(仓库根 `components/`),外加 `nvs_flash / esp_wifi / esp_netif / sdmmc / fatfs` 等 IDF 组件。
