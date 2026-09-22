#!/usr/bin/env bash
# -*- coding: utf-8 -*-
#
# macOS 开发环境一键搭建
#
#   ./scripts/esp32-setup-macos.sh
#
# 功能:
#   1. 通过 Homebrew 安装构建工具(cmake/ninja/dfu-util/ccache)
#   2. 同步 esp-idf 子模块(含嵌套子模块)
#   3. 使用 Python >= 3.10 安装 esp32c6 工具链
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
IDF_DIR="${ROOT_DIR}/esp-idf"

export HOMEBREW_NO_AUTO_UPDATE="${HOMEBREW_NO_AUTO_UPDATE:-1}"
export HOMEBREW_NO_ENV_HINTS="${HOMEBREW_NO_ENV_HINTS:-1}"

# ---- 1. 构建工具 ----
for pkg in cmake ninja dfu-util ccache; do
  if brew list --versions "${pkg}" >/dev/null 2>&1; then
    echo "==> 已安装: ${pkg}"
  else
    echo "==> brew install ${pkg}"
    brew install "${pkg}"
  fi
done
if ! command -v cmake >/dev/null 2>&1; then
  PATH="$(brew --prefix)/bin:${PATH}"
  export PATH
fi

# ---- 2. 选择 Python >= 3.10 ----
# ESP-IDF v5.2 的依赖检查在 Python 3.9 下会因 importlib.metadata 无法识别
# 带点号的包名(如 ruamel.yaml.clib)而误报缺失,因此要求 >= 3.10。
find_python() {
  local p
  for p in python3.13 python3.12 python3.11 python3.10 python3; do
    command -v "${p}" >/dev/null 2>&1 || continue
    if "${p}" -c 'import sys; exit(0 if sys.version_info[:2] >= (3, 10) else 1)' 2>/dev/null; then
      command -v "${p}"
      return 0
    fi
  done
  return 1
}

PYTHON="$(find_python || true)"
if [[ -z "${PYTHON}" ]]; then
  echo "Error: 未找到 Python >= 3.10。请先安装,例如:"
  echo "       brew install python@3.11"
  exit 1
fi
echo "==> 使用 Python: ${PYTHON} ($("${PYTHON}" --version 2>&1))"

# ---- 3. 同步子模块 ----
# 仅在 esp-idf 缺失时初始化子模块(避免把手动固定的版本回退到 gitlink 记录值);
# 嵌套子模块总是同步。
if [[ ! -f "${IDF_DIR}/tools/idf.py" ]]; then
  echo "==> 初始化 esp-idf 子模块"
  git -C "${ROOT_DIR}" submodule update --init esp-idf
fi
echo "==> 同步 esp-idf 嵌套子模块"
git -C "${IDF_DIR}" submodule update --init --recursive

# ---- 4. 安装工具链 ----
# install.sh 通过 detect_python.sh 选择 PATH 中的 python3,这里用临时目录强制指向 Python >= 3.10
PY_WRAP_DIR="$(mktemp -d)"
ln -sf "${PYTHON}" "${PY_WRAP_DIR}/python3"
ln -sf "${PYTHON}" "${PY_WRAP_DIR}/python"
trap 'rm -rf "${PY_WRAP_DIR}"' EXIT

export IDF_GITHUB_ASSETS="${IDF_GITHUB_ASSETS:-dl.espressif.com/github_assets}"
echo "==> 安装 esp32c6 工具链(IDF_GITHUB_ASSETS=${IDF_GITHUB_ASSETS})"
pushd "${IDF_DIR}" >/dev/null
PATH="${PY_WRAP_DIR}:${PATH}" ./install.sh esp32c6
popd >/dev/null

echo ""
echo "==> 完成。构建工程:"
echo "      C     : ./scripts/esp32c6-build-flash.sh"
echo "      Rust  : ./scripts/esp32c6-rust-build-flash.sh   (需 nightly + rust-src, ldproxy)"
echo "      Swift : ./scripts/esp32c6-swift-build-flash.sh  (需 Embedded Swift 开发版快照工具链)"
