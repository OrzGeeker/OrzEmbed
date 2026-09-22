# OrzEmbed

RISC-V Embed Development Platform for ESP32-C6-LCD-1.47

## 项目简介

OrzEmbed 是一个专注于 RISC-V 嵌入式开发的项目，主要围绕 ESP32-C6-LCD-1.47 开发板展开。项目旨在提供完整的开发环境和工具链，支持多种编程语言进行嵌入式系统开发。

## 支持的硬件

### [ESP32-C6-LCD-1.47](./docs/ESP32-C6.md)

![ESP32-C6-LCD-1.47](/images/ESP32-C6-LCD-1.47.png)

- **ESP32-C6FH4** 芯片(RISC-V RV32IMAC,160MHz,512KB SRAM,4MB flash)
- **1.47 英寸 LCD**(ST7789,172×320,SPI)
- **Micro SD 卡座**(SPI,与 LCD 共总线)
- **板载 WS2812 RGB 灯**(GPIO8)
- 贴片陶瓷天线(2.4GHz)
- BOOT / RESET 按键
- 内置 **USB-Serial-JTAG**(烧录 + 串口 + JTAG 调试,即 `/dev/cu.usbmodem*`)

> ⚠️ **本板已知硬件问题:射频衰减严重** —— 板载陶瓷天线/匹配网络异常,导致 **WiFi 与 BLE 空口几乎不可用**(衰减约 55~60dB);其余功能(LCD / SD / GPIO / RGB / NVS 等)均正常。
> 定位与排查方法详见 [板级自检记录](./docs/ESP32-C6-LCD-1.47-BRINGUP.md)。

## 技术栈

- **硬件平台**: ESP32-C6 芯片
- **软件框架**: ESP-IDF (Espressif IoT Development Framework)
- **开发语言**: C/C++, 支持嵌入式 Swift 和 Rust
- **构建工具**: CMake, Ninja
- **依赖管理**: Python 包管理

## 目录结构

```
OrzEmbed/
├── components/      # 共享 ESP-IDF 组件(各工程复用)
│   ├── orz_board/   #   板级引脚/尺寸定义(单一来源)
│   ├── orz_lcd/     #   ST7789 驱动 + 绘制 + 字库(6x9/16x16)
│   ├── orz_rgb/     #   WS2812 RGB 灯驱动
│   └── orz_input/   #   多按键扫描(短按/长按)
├── docs/            # 项目文档
│   ├── ESP32-C6.md              # ESP32-C6-LCD-1.47 详细文档
│   ├── ESP32-C6-LCD-1.47-BRINGUP.md  # 板级自检记录(LCD/SD/WiFi)
│   ├── ESP-IDF-ANALYSIS.md      # ESP-IDF 组件分析与学习路径
│   └── ESP-IDF-LEARNING-PATH.md  # ESP-IDF 示例工程学习路径
├── esp-idf/         # ESP-IDF 开发框架（子模块）
├── images/          # 开发板图片和示意图
├── project/         # 各应用/语言示例(均为可独立编译的工程)
│   ├── c/           # C 语言项目示例
│   ├── hwtest/      # 板级自动自检固件(一条命令跑完所有外设)
│   ├── timer/       # 独立计时器应用(番茄钟/倒计时/秒表,4 键 4 灯)
│   ├── swift/       # Swift 语言项目示例(ESP-IDF + idf_swift)
│   └── rust/        # Rust 语言项目示例
├── scripts/         # 脚本工具
│   ├── esp32-setup-macos.sh            # macOS 环境设置脚本
│   ├── esp32c6-build-flash.sh          # 通用 C 工程构建/烧录/监视(--project 指定)
│   ├── esp32c6-hwtest.sh               # 板级自动自检(编译+烧录+解析 PASS/FAIL)
│   ├── esp32c6-rust-build-flash.sh     # Rust 工程构建/烧录
│   ├── esp32c6-swift-build-flash.sh    # Swift 工程构建/烧录
│   ├── gen-font6x9.py                  # 生成 ASCII 字库 -> components/orz_lcd/fonts
│   └── gen-cjk16.py                    # 生成汉字字库 -> components/orz_lcd/fonts
└── README.md        # 项目说明文档
```

> **工程结构约定**:各应用是独立可编译的 ESP-IDF 工程(`project/*`),通过
> `EXTRA_COMPONENT_DIRS` 引用仓库根的共享组件(`components/`);引脚、LCD 驱动、
> 字库、按键逻辑只维护一份,新增应用直接复用。字库为生成物,由 `scripts/gen-*.py` 输出。

## 示例工程

仓库包含 **5 个可独立编译/烧录的工程**:

| 工程 | 语言/框架 | 说明 | 依赖共享组件 |
|------|-----------|------|--------------|
| [`project/c`](./project/c/) | C / ESP-IDF | 基础示例(日志 / Hello World) | — |
| [`project/hwtest`](./project/hwtest/) | C / ESP-IDF | 板级自动自检(屏上 PASS/FAIL 面板 + 引导探针) | orz_lcd / orz_rgb |
| [`project/timer`](./project/timer/) | C / ESP-IDF | 独立计时器(番茄钟 / 倒计时 / 秒表,4 键 4 灯,中文界面) | orz_lcd / orz_rgb / orz_input |
| [`project/rust`](./project/rust/) | Rust / esp-idf-svc | GPIO 翻转 + 日志 | — |
| [`project/swift`](./project/swift/) | Embedded Swift / idf_swift | GPIO 翻转示例 | — |

下面按逐个工程说明:

### 1. C 语言项目 (`project/c/`)
- **项目配置**：`CMakeLists.txt` - ESP-IDF 项目配置文件
- **主代码**：`main/main.c` - 实现了基本的日志输出和 Hello World 示例
- **功能**：演示了 ESP32-C6 的基本日志功能与工程结构

### 2. Swift 语言项目 (`project/swift/`)
- **工程结构**：标准 ESP-IDF CMake 工程 + `main/idf_component.yml`（依赖 `espressif/idf_swift`）
- **主代码**：`main/Main.swift`、`main/Led.swift` - Embedded Swift GPIO 翻转示例
- **功能**：通过 `idf_swift` 组件在 `idf.py build` 流程中编译 Embedded Swift

### 3. Rust 语言项目 (`project/rust/`)
- **项目配置**：`Cargo.toml`、`rust-toolchain.toml`（nightly）、`.cargo/config.toml`
- **主代码**：`src/main.rs` - GPIO 翻转与日志输出
- **功能**：使用 esp-idf-svc / esp-idf-hal（RISC-V，上游 nightly + `build-std`）开发

### 4. 板级自检固件 (`project/hwtest/`)
- 上电自动跑完 **芯片 / Flash / NVS / SD / LCD / RGB / GPIO / WiFi** 并打印 `PASS/FAIL` 汇总;
- **同时在 ST7789 屏上逐项显示各功能状态**(绿=PASS / 红=FAIL / 黄=WARN / 灰=详情),一眼即可看到整块板是否正常;
- GPIO 部分会自动检测**排针间的锡桥/短路**(双向确认,抑制浮空脚误报),`--probe` 模式可用引导方式验证**虚焊/开路**;
- 配套脚本一条命令完成编译+烧录+采集+解析:

```bash
./scripts/esp32c6-hwtest.sh            # 自动自检(全部通过退出码 0,有 FAIL 退出码 1)
./scripts/esp32c6-hwtest.sh --probe    # 追加引导式导通/虚焊探测
```

### 5. 独立计时器应用 (`project/timer/`)
- **插电即运行、不依赖网络/主机**的桌面计时器:番茄钟 / 倒计时 / 秒表;
- **4 个外接开关**(GP0~GP3)操作,**4 个绿色 LED**(GP18/19/20/23)提示,**板载 RGB** 指示状态;
- 界面为**简体中文**(内置 36 汉字的 16×16 点阵字库);
- 构建烧录(复用通用脚本):

```bash
./scripts/esp32c6-build-flash.sh /dev/cu.usbmodemXXXX --project project/timer
```

| 键 | 引脚 | 短按 | 长按 |
|----|------|------|------|
| K1 | GP0 | 开始 / 暂停 | 复位 |
| K2 | GP1 | 切换模式(番茄钟→倒计时→秒表) | — |
| K3 | GP2 | −1 分钟(倒计时 / 专注时长) | — |
| K4 | GP3 | +1 分钟 | — |

| 状态 | 4 个 LED |
|------|-----------|
| 就绪 | 慢速跑马灯 |
| 运行 | 剩余时间进度条(4→0) |
| 暂停 | 进度条慢闪 |
| 结束 | 四灯快闪 |

> 时长设置存 flash(NVS)断电不丢;详见 [`project/timer`](./project/timer/)。

## 使用方法

> 以下命令均在仓库根目录执行。
> C 与 Swift 都基于仓库内 `esp-idf` 子模块；Rust 使用 embuild 安装到 `project/rust/.embuild/` 的 ESP-IDF。

### C 项目
```bash
./scripts/esp32c6-build-flash.sh            # 默认构建烧录 project/c
# 或手动执行:
source ./esp-idf/export.sh
cd project/c && idf.py build && idf.py -p /dev/cu.usbmodemXXXX flash monitor
```

### Swift 项目
Embedded Swift 目前**不在稳定版 Swift 中**，需要官方**开发版快照（snapshot）**工具链。

```bash
# 安装快照工具链并使其位于 ~/Library/Developer/Toolchains/swift-latest.xctoolchain
# （首选用 swiftly install main-snapshot；也可手动安装官方 *-osx.pkg）
./scripts/esp32c6-swift-build-flash.sh /dev/cu.usbmodemXXXX
```
工程通过 ESP-IDF 组件 `espressif/idf_swift` 把 Swift 编译进 `idf.py build`。

### Rust 项目
```bash
# 首次需安装（RISC-V 目标使用上游 nightly + build-std）：
rustup toolchain install nightly --component rust-src
cargo install ldproxy --locked
cargo install cargo-espflash --locked          # 仅烧录需要
./scripts/esp32c6-rust-build-flash.sh /dev/cu.usbmodemXXXX
```

## 开发流程

### 1. 环境搭建

```bash
# 在 macOS 上设置开发环境(安装工具 + 同步子模块 + 安装 esp32c6 工具链)
./scripts/esp32-setup-macos.sh
```

> **前置要求**：Python **>= 3.10**（建议 3.13）、CMake（>= 3.22）、Ninja。
> ESP-IDF v6.1 在 Python 3.9 下会因 `importlib.metadata` 无法识别带点号的包名而误报依赖缺失。
> 手动加载环境可执行 `source ./esp-idf/export.sh`。

### 2. 构建和烧录

```bash
# C:构建、烧录并监控(默认 project/c)
./scripts/esp32c6-build-flash.sh [/dev/cu.usbmodemXXXX]

# Rust:构建(并可选烧录)project/rust
./scripts/esp32c6-rust-build-flash.sh [/dev/cu.usbmodemXXXX]

# Swift:构建并烧录 project/swift
./scripts/esp32c6-swift-build-flash.sh [/dev/cu.usbmodemXXXX]

# 板级自动自检(编译+烧录 project/hwtest 并解析结果)
./scripts/esp32c6-hwtest.sh [/dev/cu.usbmodemXXXX] [--probe]
```

### 3. 版本约定

| 组件 | 版本 | 说明 |
|------|------|------|
| ESP-IDF（子模块） | **v6.1** | C 与 Swift 工程构建 |
| Python | **3.13** | ESP-IDF v6.1 要求 >= 3.10 |
| Rust（host） | **1.98.1** | stable |
| Rust（target） | **nightly** + `build-std` | 目标 `riscv32imac-esp-espidf` |
| esp-idf-svc / hal / sys | 0.53 / 0.47 / 0.38 | 支持 ESP-IDF v6.1 |
| Swift | **6.5-dev**（main-snapshot） | Embedded Swift（稳定版不支持） |
| espressif/idf_swift | ^1.0.0 | 提供 ESP-IDF 的 Swift 集成 |

> 说明：C 与 Swift 工程使用仓库内 `esp-idf` 子模块；Rust 使用 embuild 安装到
> `project/rust/.embuild/` 的 ESP-IDF。三条线统一为 ESP-IDF v6.1。

### 4. 常用命令

- `idf.py set-target esp32c6`: 设置目标芯片
- `idf.py build`: 编译项目
- `idf.py flash`: 烧录到开发板
- `idf.py monitor`: 查看开发板输出（按 Ctrl+] 退出）

## 相关文档

- [Embeded Swift Documentation](https://docs.swift.org/embedded/documentation/embedded/)
- [Embeded Rust Documentation](https://doc.rust-lang.org/stable/embedded-book/)
- [ESP-IDF Documentation](https://docs.espressif.com/projects/esp-idf/)
- [ESP-IDF 组件分析与学习路径](./docs/ESP-IDF-ANALYSIS.md)
- [ESP-IDF 示例工程学习路径](./docs/ESP-IDF-LEARNING-PATH.md)
- [板级自检记录(LCD/SD/WiFi)](./docs/ESP32-C6-LCD-1.47-BRINGUP.md)

## 社区资源

- [Espressif 社区课程](https://www.espressif.com.cn/zh-hans/ecosystem/community-engagement/courses)
- [esp32.com 论坛](https://esp32.com/)
