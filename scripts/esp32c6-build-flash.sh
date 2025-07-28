#!/usr/bin/env bash
# -*- coding: utf-8 -*-

idf.py set-target esp32c6 && idf.py build && idf.py flash monitor # use ctrl+] 退出监视