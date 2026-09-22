#!/usr/bin/env bash
# 自动板级自检(一条命令):编译 + 烧录 project/hwtest + 抓串口 + 解析 PASS/FAIL
#
# 用法:
#   scripts/esp32c6-hwtest.sh [串口] [--no-flash] [--probe]
# 例:
#   scripts/esp32c6-hwtest.sh                       # 自动自检
#   scripts/esp32c6-hwtest.sh --probe               # 自动自检 + 引导式导通/虚焊探测
#   scripts/esp32c6-hwtest.sh /dev/cu.usbmodem1101  # 指定串口
#   scripts/esp32c6-hwtest.sh --no-flash            # 只读结果(固件已在跑)
#
# 退出码: 全部通过=0, 有 FAIL=1
set -euo pipefail

PORT="/dev/cu.usbmodem14101"
DO_FLASH=1
DO_PROBE=0
for a in "$@"; do
    case "$a" in
        --no-flash) DO_FLASH=0 ;;
        --probe)    DO_PROBE=1 ;;
        /dev/*)     PORT="$a" ;;
        *) echo "未知参数: $a" >&2; exit 2 ;;
    esac
done

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PROJ="$ROOT/project/hwtest"

# cmake 可能没在 PATH(Homebrew keg-only)
if ! command -v cmake >/dev/null 2>&1 && command -v brew >/dev/null 2>&1; then
    BREW_CMAKE="$(brew --prefix cmake 2>/dev/null || true)"
    [ -n "$BREW_CMAKE" ] && export PATH="$BREW_CMAKE/bin:$PATH"
fi

export IDF_PYTHON_ENV_PATH="${IDF_PYTHON_ENV_PATH:-$HOME/.espressif/python_env/idf6.1_py3.13_env}"
# shellcheck disable=SC1091
source "$ROOT/esp-idf/export.sh" >/dev/null 2>&1
IDF_PY="$IDF_PYTHON_ENV_PATH/bin/python"
[ -x "$IDF_PY" ] || IDF_PY="python3"

echo "== 串口: $PORT"
if [ "$DO_FLASH" -eq 1 ]; then
    echo "== 编译 + 烧录 ($PROJ)"
    ( cd "$PROJ" && idf.py build >/dev/null && idf.py -p "$PORT" flash >/dev/null )
fi

echo "== 运行自检并采集串口..."
"$IDF_PY" - "$PORT" "$DO_PROBE" <<'PY'
import re, sys, time
import serial

port, probe = sys.argv[1], sys.argv[2] == "1"
s = serial.Serial(port, 115200, timeout=0.1)
# 让芯片重启运行 app(避开 USB-Serial-JTAG 下载模式)
s.setDTR(False); s.setRTS(True); time.sleep(0.15); s.setRTS(False)
time.sleep(0.3)

def show(raw):
    t = re.sub(r"\x1b\[[0-9;]*m", "", raw.decode("utf-8", "replace"))
    for line in t.splitlines():
        m = re.search(r"HWTEST: (.*)", line)
        msg = m.group(1) if m else (line if " AP " in line else None)
        if msg:
            print(msg)
    return t

# ---- 1. 自动自检 ----
buf = b""
end = time.time() + 90
while time.time() < end:
    chunk = s.read(4096)
    if chunk:
        buf += chunk
        if b"HWTEST_DONE" in buf:
            break
    else:
        time.sleep(0.02)
text = show(buf)

m = re.search(r"PASS=(\d+) FAIL=(\d+) WARN=(\d+)", text)
if not m:
    print("\n!! 未捕获到自检结果(检查串口/是否按了 RST)")
    s.close(); sys.exit(3)
p, f, w = map(int, m.groups())
print(f"\n==== 自动自检: PASS={p}  FAIL={f}  WARN={w} ====")

# ---- 2. 引导式探针(可选) ----
# 需要把每个排针短到 GND;GPIO22 有板上 10K 下拉(常态即为 0)不参与
PROBE = [0,1,2,3,4,5,6,7,8,9,10,11,14,15,18,19,20,21,23]
probe_fail = []
if probe:
    print("\n==== 引导式导通/虚焊探测 ====")
    print("按提示把对应排针短到 GND(保持约 1 秒再松开);丝印脚请自行对照。")
    # 等 PROBE_READY
    end = time.time() + 10
    got = b""
    while time.time() < end and b"PROBE_READY" not in got:
        c = s.read(4096)
        if c:
            got += c
        else:
            time.sleep(0.02)
    for pin in PROBE:
        print(f"\n--> 请把 GPIO{pin} 短到 GND ...", flush=True)
        seen_low = False
        end = time.time() + 12
        cbuf = b""
        while time.time() < end:
            c = s.read(4096)
            if c:
                cbuf += c
            else:
                time.sleep(0.02)
            cbuf_s = cbuf.decode("utf-8", "replace")
            for pin_s, lvl_s in re.findall(r"CHG (\d+) (\d+)", cbuf_s):
                if int(pin_s) == pin and int(lvl_s) == 0:
                    seen_low = True
                    break
            if seen_low:
                break
        if seen_low:
            print(f"    GPIO{pin}: OK (检测到短接) ✅")
        else:
            print(f"    GPIO{pin}: 无反应 ❌ (可能虚焊/开路)")
            probe_fail.append(pin)

    if probe_fail:
        print(f"\n无反应的脚: {probe_fail} → 疑似虚焊/开路")
    else:
        print("\n所有被测排针均导通 ✅")

s.close()
sys.exit(1 if (f or probe_fail) else 0)
PY
