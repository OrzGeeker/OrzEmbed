# 独立计时器 (`project/timer`)

插电即运行的桌面计时器:**番茄钟 / 倒计时 / 秒表**,不依赖网络或主机。

## 硬件

| 用途 | 引脚 |
|------|------|
| 4 个开关 K1~K4(对 GND) | GP0 / GP1 / GP2 / GP3 |
| 4 个绿色 LED | GP18 / GP19 / GP20 / GP23 |
| 板载 RGB(WS2812) | GP8 |
| 板载 LCD(ST7789 172×320) | 见 `components/orz_board` |

## 操作(4 键)

| 键 | 短按 | 长按(0.8s) |
|----|------|-------------|
| K1 | 开始 / 暂停 | 复位 |
| K2 | 切换模式(番茄钟→倒计时→秒表) | — |
| K3 | −1 分钟(倒计时 / 专注时长) | — |
| K4 | +1 分钟 | — |

## LED 提示

| 状态 | 4 个 LED |
|------|-----------|
| 就绪 | 慢速跑马灯 |
| 运行 | 剩余时间进度条(4→0) |
| 暂停 | 进度条慢闪 |
| 结束 | 四灯快闪 |

RGB 灯:就绪暗蓝 / 运行模式色 / 暂停黄 / 结束红闪。设置存 NVS,断电不丢。

## 构建烧录

```bash
./scripts/esp32c6-build-flash.sh /dev/cu.usbmodemXXXX --project project/timer
```

## 依赖

`orz_board` / `orz_lcd` / `orz_rgb` / `orz_input`(仓库根 `components/`,通过 `EXTRA_COMPONENT_DIRS` 引入)。
