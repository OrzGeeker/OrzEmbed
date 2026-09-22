#!/usr/bin/env bash
# -*- coding: utf-8 -*-
#
# 构建并烧录 Embedded Swift 工程(基于 ESP-IDF + espressif/idf_swift)
#
#   ./scripts/esp32c6-swift-build-flash.sh [/dev/tty.usbmodemXXXX]
#
# 说明:
#   - Embedded Swift 目前不在稳定版 Swift 中,需安装官方“开发版快照”工具链。
#   - 本脚本仅负责把 Swift 工具链加入 PATH,随后复用 esp32c6-build-flash.sh
#     的 idf.py 构建/烧录流程(project/swift 是一个标准 ESP-IDF 工程)。
#   - 可用 SWIFT_TOOLCHAIN 指定工具链路径(默认 ~/Library/Developer/Toolchains/swift-latest.xctoolchain)。
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PROJECT_DIR="${ROOT_DIR}/project/swift"
PORT="${1:-}"

SWIFT_TOOLCHAIN="${SWIFT_TOOLCHAIN:-$HOME/Library/Developer/Toolchains/swift-latest.xctoolchain}"
if [[ ! -x "${SWIFT_TOOLCHAIN}/usr/bin/swift" ]]; then
  echo "Error: 未找到 Swift 工具链: ${SWIFT_TOOLCHAIN}/usr/bin/swift"
  echo "       Embedded Swift 需要开发版快照工具链,请参考 README「Swift 项目」一节。"
  exit 1
fi

# 将 Swift 工具链置于 PATH 前部,idf_swift 组件会据此定位 swiftc
PATH="${SWIFT_TOOLCHAIN}/usr/bin:${PATH}"
export PATH

# 复用通用 ESP-IDF 构建/烧录脚本
if [[ -n "${PORT}" ]]; then
  exec "${ROOT_DIR}/scripts/esp32c6-build-flash.sh" --project "${PROJECT_DIR}" "${PORT}"
else
  exec "${ROOT_DIR}/scripts/esp32c6-build-flash.sh" --project "${PROJECT_DIR}"
fi
