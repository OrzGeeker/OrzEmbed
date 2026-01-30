#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SWIFT_DIR="${ROOT_DIR}/project/swift"
RUST_DIR="${ROOT_DIR}/project/rust"

PORT="${1:-}"
if [[ -z "${PORT}" ]]; then
  PORT_CANDIDATE="$(ls /dev/tty.usbmodem* 2>/dev/null | head -n 1 || true)"
  if [[ -z "${PORT_CANDIDATE}" ]]; then
    echo "Error: No serial port provided and no /dev/tty.usbmodem* found"
    echo "Usage: $0 /dev/tty.usbmodemXXXX"
    exit 1
  fi
  PORT="${PORT_CANDIDATE}"
fi

unset IDF_PATH
export ESP_IDF_VERSION="v5.2.3"
export ESP_IDF_TOOLS_INSTALL_DIR="custom:${RUST_DIR}/.embuild/espressif"
export IDF_PYTHON_ENV_PATH="${RUST_DIR}/.embuild/espressif/python_env/idf5.2_py3.9_env"
export PATH="${RUST_DIR}/.embuild/espressif/tools/riscv32-esp-elf/esp-13.2.0_20230928/riscv32-esp-elf/bin:${PATH}"

echo "[1/4] Build Swift (embedded)"
pushd "${SWIFT_DIR}" >/dev/null
swift build --experimental-swift-embedded --destination ./destination.json --target MyApp -c release
SWIFT_BIN_PATH="$(swift build --experimental-swift-embedded --destination ./destination.json --target MyApp -c release --show-bin-path)"
SWIFT_ELF="${SWIFT_BIN_PATH}/MyApp"
SWIFT_BIN="${SWIFT_BIN_PATH}/MyApp.bin"
riscv32-esp-elf-objcopy -O binary "${SWIFT_ELF}" "${SWIFT_BIN}"
popd >/dev/null

echo "[2/4] Build ESP-IDF bootloader/partition table"
pushd "${RUST_DIR}" >/dev/null
cargo +esp build --release --target riscv32imac-esp-espidf
BOOTLOADER_BIN="${RUST_DIR}/target/riscv32imac-esp-espidf/release/bootloader.bin"
PARTITION_BIN="${RUST_DIR}/target/riscv32imac-esp-espidf/release/partition-table.bin"
popd >/dev/null

echo "[3/4] Flash to ${PORT}"
"${IDF_PYTHON_ENV_PATH}/bin/python" -m esptool --chip esp32c6 --port "${PORT}" --baud 460800 \
  write_flash -z 0x0 "${BOOTLOADER_BIN}" 0x8000 "${PARTITION_BIN}" 0x10000 "${SWIFT_BIN}"

echo "[4/4] Done"
