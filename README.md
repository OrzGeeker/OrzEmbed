# OrzEmbed

RISC-V Embed Development Platform for ESP32-C6-LCD-1.47

## 项目简介

OrzEmbed 是一个专注于 RISC-V 嵌入式开发的项目，主要围绕 ESP32-C6-LCD-1.47 开发板展开。项目旨在提供完整的开发环境和工具链，支持多种编程语言进行嵌入式系统开发。

## 支持的硬件

### [ESP32-C6-LCD-1.47](./docs/ESP32-C6.md)

![ESP32-C6-LCD-1.47](/images/ESP32-C6-LCD-1.47.png)

- ESP32-C6FH4 芯片（RISC-V 架构）
- 1.47 英寸 LCD 显示屏
- Micro SD 卡座
- 贴片陶瓷天线
- BOOT 和 RESET 按键

## 技术栈

- **硬件平台**: ESP32-C6 芯片
- **软件框架**: ESP-IDF (Espressif IoT Development Framework)
- **开发语言**: C/C++, 支持嵌入式 Swift 和 Rust
- **构建工具**: CMake, Ninja
- **依赖管理**: Python 包管理

## 目录结构

```
OrzEmbed/
├── docs/            # 项目文档
│   ├── ESP32-C6.md              # ESP32-C6-LCD-1.47 详细文档
│   ├── ESP32-C6-LCD-1.47-BRINGUP.md  # 板级自检记录(LCD/SD/WiFi)
│   ├── ESP-IDF-ANALYSIS.md      # ESP-IDF 组件分析与学习路径
│   └── ESP-IDF-LEARNING-PATH.md  # ESP-IDF 示例工程学习路径
├── esp-idf/         # ESP-IDF 开发框架（子模块）
├── images/          # 开发板图片和示意图
├── project/         # 多语言项目示例
│   ├── c/           # C 语言项目示例
│   ├── hwtest/      # 板级自动自检固件(一条命令跑完所有外设)
│   ├── swift/       # Swift 语言项目示例(ESP-IDF + idf_swift)
│   └── rust/        # Rust 语言项目示例
├── scripts/         # 脚本工具
│   ├── esp32-setup-macos.sh            # macOS 环境设置脚本
│   ├── esp32c6-build-flash.sh          # C 工程构建/烧录/监视
│   ├── esp32c6-hwtest.sh               # 板级自动自检(编译+烧录+解析 PASS/FAIL)
│   ├── esp32c6-rust-build-flash.sh     # Rust 工程构建/烧录
│   └── esp32c6-swift-build-flash.sh    # Swift 工程构建/烧录
└── README.md        # 项目说明文档
```

## 多语言项目示例

项目包含三种编程语言的 ESP32-C6 开发示例：

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
- GPIO 部分会自动检测**排针间的锡桥/短路**,`--probe` 模式可用引导方式验证**虚焊/开路**;
- 配套脚本一条命令完成编译+烧录+采集+解析:

```bash
./scripts/esp32c6-hwtest.sh            # 自动自检(全部通过退出码 0,有 FAIL 退出码 1)
./scripts/esp32c6-hwtest.sh --probe    # 追加引导式导通/虚焊探测
```

## 使用方法

> 以下命令均在仓库根目录执行。
> C 与 Swift 都基于仓库内 `esp-idf` 子模块；Rust 使用 embuild 安装到 `project/rust/.embuild/` 的 ESP-IDF。

### C 项目
```bash
./scripts/esp32c6-build-flash.sh            # 默认构建烧录 project/c
# 或手动执行：
source ./esp-idf/export.sh
cd project/c && idf.py set-target esp32c6 && idf.py build && idf.py flash monitor
```

### Swift 项目
Embedded Swift 目前**不在稳定版 Swift 中**，需要官方**开发版快照（snapshot）**工具链。

```bash
# 安装快照工具链并使其位于 ~/Library/Developer/Toolchains/swift-latest.xctoolchain
# （首选用 swiftly install main-snapshot；也可手动安装官方 *-osx.pkg）
./scripts/esp32c6-swift-build-flash.sh /dev/tty.usbmodemXXXX
```
工程通过 ESP-IDF 组件 `espressif/idf_swift` 把 Swift 编译进 `idf.py build`。

### Rust 项目
```bash
# 首次需安装（RISC-V 目标使用上游 nightly + build-std）：
rustup toolchain install nightly --component rust-src
cargo install ldproxy --locked
cargo install cargo-espflash --locked          # 仅烧录需要
./scripts/esp32c6-rust-build-flash.sh /dev/tty.usbmodemXXXX
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
./scripts/esp32c6-build-flash.sh [/dev/tty.usbmodemXXXX]

# Rust:构建(并可选烧录)project/rust
./scripts/esp32c6-rust-build-flash.sh [/dev/tty.usbmodemXXXX]

# Swift:构建并烧录 project/swift
./scripts/esp32c6-swift-build-flash.sh [/dev/tty.usbmodemXXXX]

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
