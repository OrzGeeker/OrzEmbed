#!/usr/bin/env python3
"""生成 project/hwtest/main/font6x9.h 点阵字库。

把系统等宽字体(Menlo)渲染成 6x9 位图,ASCII 32..126。字模无关平台,
仅用于在 ST7789 上画文本。用法:
    python3 scripts/gen-font6x9.py
依赖:Pillow、numpy。
"""
import os
import numpy as np
from PIL import ImageFont

FONT_PATH = "/System/Library/Fonts/Menlo.ttc"
SIZE, W, H, BASE = 10, 6, 9, 7   # 字号 / 单元宽 / 单元高 / 基线行
FIRST, LAST = 32, 126
OUT = os.path.join(os.path.dirname(__file__), "..", "project", "hwtest", "main", "font6x9.h")


def build():
    f = ImageFont.truetype(FONT_PATH, SIZE, index=0)
    asc, _ = f.getmetrics()
    chars = []
    for c in range(FIRST, LAST + 1):
        ch = chr(c)
        try:
            m = f.getmask(ch, mode="L")
            a = np.array(m).reshape(m.size[1], m.size[0]) if (m.size[0] and m.size[1]) else np.zeros((0, 0))
        except Exception:
            a = np.zeros((0, 0))
        bb = f.getbbox(ch) if ch != " " else (0, 0, 0, 0)
        cell = np.zeros((H, W), dtype=np.uint8)
        oy = BASE - asc
        if a.size:
            for r in range(a.shape[0]):
                cy = oy + bb[1] + r
                if 0 <= cy < H:
                    for x in range(min(W, a.shape[1])):
                        if a[r, x] > 90:
                            cell[cy, x] = 1
        bits = [sum(((cell[r, x] & 1) << (W - 1 - x)) for x in range(W)) for r in range(H)]
        chars.append(bits)
    return chars


def main():
    chars = build()
    out = [
        "// 自动生成,请勿手改:Menlo %dpx -> %dx%d 点阵,ASCII %d..%d" % (SIZE, W, H, FIRST, LAST),
        "// 每字符 %d 字节,每字节低 %d 位有效(bit%d = 最左像素)" % (H, W, W - 1),
        "// 重新生成: python3 scripts/gen-font6x9.py",
        "#pragma once",
        "#include <stdint.h>",
        "#define FONT_W %d" % W,
        "#define FONT_H %d" % H,
        "static const uint8_t font6x9[%d][%d] = {" % (LAST - FIRST + 1, H),
    ]
    for i, bits in enumerate(chars):
        ch = chr(FIRST + i)
        disp = {chr(92): "\\\\", "'": "\\'"}.get(ch, ch)
        out.append("    { %s }, // '%s'" % (", ".join("0x%02X" % b for b in bits), disp if ch != " " else " "))
    out.append("};")
    with open(os.path.abspath(OUT), "w") as fp:
        fp.write("\n".join(out) + "\n")
    print("wrote", os.path.abspath(OUT), "chars:", len(chars))


if __name__ == "__main__":
    main()
