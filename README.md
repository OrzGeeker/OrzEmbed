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
│   └── ESP32-C6.md  # ESP32-C6-LCD-1.47 详细文档
├── esp-idf/         # ESP-IDF 开发框架（子模块）
├── images/          # 开发板图片和示意图
├── scripts/         # 脚本工具
│   ├── esp32-setup-macos.sh      # macOS 环境设置脚本
│   └── esp32c6-build-flash.sh     # 构建和烧录脚本
└── README.md        # 项目说明文档
```

## 开发流程

### 1. 环境搭建

```bash
# 在 macOS 上设置开发环境
./scripts/esp32-setup-macos.sh

# 进入 esp-idf 目录并初始化
cd esp-idf
source export.sh
```

### 2. 构建和烧录

```bash
# 构建、烧录并监控
./scripts/esp32c6-build-flash.sh
```

### 3. 常用命令

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

## 社区资源

- [Espressif 社区课程](https://www.espressif.com.cn/zh-hans/ecosystem/community-engagement/courses)
- [esp32.com 论坛](https://esp32.com/)