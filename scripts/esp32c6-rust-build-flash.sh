#!/usr/bin/env bash
# -*- coding: utf-8 -*-
#
# 一键构建(并可选烧录)Rust 工程
#
#   ./scripts/esp32c6-rust-build-flash.sh [/dev/cu.usbmodemXXXX]
#
# 说明:
#   - RISC-V 目标使用上游 nightly + build-std(见 project/rust/rust-toolchain.toml 与 .cargo/config.toml)。
#   - ESP-IDF 与工具链由 embuild 安装到 project/rust/.embuild/espressif/。
#   - 版本通过 project/rust/.cargo/config.toml 的 [env] 固定(ESP_IDF_VERSION=v6.1)。
#   - 首次需安装:
#         rustup toolchain install nightly --component rust-src
#         cargo install ldproxy --locked              # 链接器包装
#         cargo install cargo-espflash --locked       # 仅烧录需要
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
RUST_DIR="${ROOT_DIR}/project/rust"

# 确保 cmake / ninja 在 PATH(Homebrew 可能未 link 到 /usr/local/bin)
if ! command -v cmake >/dev/null 2>&1 || ! command -v ninja >/dev/null 2>&1; then
  if command -v brew >/dev/null 2>&1; then
    PATH="${PATH}:$(brew --prefix)/bin:$(brew --prefix cmake 2>/dev/null)/bin"
    export PATH
  fi
fi

TOOLCHAIN="${ESP_RUST_TOOLCHAIN:-nightly}"
if ! rustup toolchain list | grep -qE "^${TOOLCHAIN}([[:space:]-]|$)"; then
  echo "Error: 未找到 '${TOOLCHAIN}' Rust 工具链。"
  echo "       请执行: rustup toolchain install ${TOOLCHAIN} --component rust-src"
  exit 1
fi

PORT="${1:-}"
if [[ -n "${PORT}" ]] && ! command -v cargo-espflash >/dev/null 2>&1; then
  echo "Error: 未找到 cargo-espflash,无法烧录。"
  echo "       请执行: cargo install cargo-espflash --locked"
  exit 1
fi

echo "[1/2] 构建 Rust (toolchain ${TOOLCHAIN})"
pushd "${RUST_DIR}" >/dev/null
cargo "+${TOOLCHAIN}" build --release --target riscv32imac-esp-espidf
popd >/dev/null

if [[ -z "${PORT}" ]]; then
  echo "[2/2] 未提供串口,跳过烧录。"
  echo "      用法: $0 /dev/cu.usbmodemXXXX"
  exit 0
fi

echo "[2/2] 烧录到 ${PORT}"
pushd "${RUST_DIR}" >/dev/null
cargo "+${TOOLCHAIN}" espflash flash \
  --release \
  --target riscv32imac-esp-espidf \
  --port "${PORT}"
popd >/dev/null
