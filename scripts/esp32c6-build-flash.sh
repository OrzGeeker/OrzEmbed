#!/usr/bin/env bash
# -*- coding: utf-8 -*-
#
# 构建、烧录并监视 C 工程(ESP-IDF)
#
#   ./scripts/esp32c6-build-flash.sh [/dev/tty.usbmodemXXXX] [--project <dir>]
#
# 说明:
#   - 若当前 shell 未加载 ESP-IDF 环境(IDF_PATH 未设置),会自动 source esp-idf/export.sh。
#   - 默认工程目录为 project/c,可用 --project 指定。
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PROJECT_DIR="${ROOT_DIR}/project/c"
PORT=""

# 确保 cmake / ninja 在 PATH(Homebrew 可能未 link 到 /usr/local/bin)
if ! command -v cmake >/dev/null 2>&1 || ! command -v ninja >/dev/null 2>&1; then
  if command -v brew >/dev/null 2>&1; then
    PATH="${PATH}:$(brew --prefix)/bin:$(brew --prefix cmake 2>/dev/null)/bin"
    export PATH
  fi
fi
if ! command -v cmake >/dev/null 2>&1; then
  echo "Error: 未找到 cmake。请先运行 ./scripts/esp32-setup-macos.sh"
  exit 1
fi

while [[ $# -gt 0 ]]; do
  case "$1" in
    -p|--project)
      PROJECT_DIR="$2"
      shift 2
      ;;
    *)
      PORT="$1"
      shift
      ;;
  esac
done

if [[ ! -f "${PROJECT_DIR}/CMakeLists.txt" ]]; then
  echo "Error: ${PROJECT_DIR} 不是有效的 ESP-IDF 工程(缺少 CMakeLists.txt)"
  exit 1
fi

# ESP-IDF v6.1 要求 Python >= 3.10;优先选择与当前 esp-idf 版本匹配的 venv
# （如 esp-idf v6.1 -> idf6.1_py3.x_env），否则回退到任一 >= 3.10 的 venv。
select_idf_python_env() {
  local base="$HOME/.espressif/python_env" d preferred="" fallback="" ver=""
  [[ -d "$base" ]] || return 1
  if [[ -f "${ROOT_DIR}/esp-idf/tools/cmake/version.cmake" ]]; then
    local maj min
    maj="$(sed -n 's/^set(IDF_VERSION_MAJOR \([0-9]*\))/\1/p' "${ROOT_DIR}/esp-idf/tools/cmake/version.cmake")"
    min="$(sed -n 's/^set(IDF_VERSION_MINOR \([0-9]*\))/\1/p' "${ROOT_DIR}/esp-idf/tools/cmake/version.cmake")"
    [[ -n "$maj" && -n "$min" ]] && ver="idf${maj}.${min}_"
  fi
  for d in "$base"/idf*_env; do
    [[ -x "$d/bin/python" ]] || continue
    "$d/bin/python" -c 'import sys; exit(0 if sys.version_info[:2] >= (3, 10) else 1)' 2>/dev/null || continue
    if [[ -n "$ver" && "$(basename "$d")" == ${ver}* ]]; then
      preferred="$d"
      break
    fi
    [[ -z "$fallback" ]] && fallback="$d"
  done
  if [[ -n "$preferred" ]]; then
    echo "$preferred"
  elif [[ -n "$fallback" ]]; then
    echo "$fallback"
  else
    return 1
  fi
}

if [[ -z "${IDF_PATH:-}" ]]; then
  if [[ -f "${ROOT_DIR}/esp-idf/export.sh" ]]; then
    if [[ -z "${IDF_PYTHON_ENV_PATH:-}" ]]; then
      if _env="$(select_idf_python_env)"; then
        export IDF_PYTHON_ENV_PATH="${_env}"
      fi
    fi
    echo "==> 加载 ESP-IDF 环境"
    # shellcheck disable=SC1091
    source "${ROOT_DIR}/esp-idf/export.sh" >/dev/null
  else
    echo "Error: 未找到 ${ROOT_DIR}/esp-idf/export.sh,请先初始化 esp-idf 子模块。"
    exit 1
  fi
fi

cd "${PROJECT_DIR}"
# set-target 会执行 fullclean 并重建 sdkconfig,只在目标未设置时才执行
if [[ ! -f sdkconfig ]] || ! grep -q '^CONFIG_IDF_TARGET="esp32c6"' sdkconfig; then
  idf.py set-target esp32c6
fi
idf.py build

FLASH_ARGS=(flash)
MONITOR_ARGS=(monitor)
if [[ -n "${PORT}" ]]; then
  FLASH_ARGS=(-p "${PORT}" flash)
  MONITOR_ARGS=(-p "${PORT}" monitor)
fi

idf.py "${FLASH_ARGS[@]}"

if [[ -t 0 && -t 1 ]]; then
  echo "==> 监视(Ctrl+] 退出)"
  idf.py "${MONITOR_ARGS[@]}"
else
  echo "==> 非交互终端,已跳过 monitor。查看串口输出可执行: idf.py ${PORT:+-p ${PORT} }monitor"
fi
