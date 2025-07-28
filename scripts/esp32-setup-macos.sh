#!/usr/bin/env bash
# -*- coding: utf-8 -*-

export HOMEBREW_NO_AUTO_UPDATE
export HOMEBREW_NO_ENV_HINTS
# https://docs.espressif.com/projects/esp-idf/zh_CN/latest/esp32/get-started/linux-macos-setup.html
brew install cmake ninja dfu-util ccache

# Install esp-idf and configure virtual env
repo_root=$(git rev-parse --show-toplevel)
cd ${repo_root}/esp-idf
export IDF_GITHUB_ASSETS="dl.espressif.cn/github_assets"
./install.sh