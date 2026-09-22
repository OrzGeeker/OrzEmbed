#!/usr/bin/env python3
"""生成 project/timer/main/cjk16.h(界面用到的简体汉字 16x16 点阵)。

只嵌入界面实际用到的汉字,体积极小(~1KB),适合小屏。
用法:python3 scripts/gen-cjk16.py
依赖:Pillow、numpy。字体路径按需修改。
"""
import os
import numpy as np
from PIL import ImageFont

FONT = "/System/Library/Fonts/PingFang.ttc"
SIZE, W, H = 16, 16, 16
# 界面里用到的所有汉字(去重后生成)
CHARS = "番茄钟倒计时秒表就绪运行暂停结束调整中专注短休长分开 始切换模式减加按复位".replace(" ", "")
OUT = os.path.join(os.path.dirname(__file__), "..", "project", "timer", "main", "cjk16.h")


def main():
    f = ImageFont.truetype(FONT, SIZE, index=0)
    glyphs, seen = [], set()
    for ch in CHARS:
        if ch in seen:
            continue
        seen.add(ch)
        m = f.getmask(ch, mode="L")
        a = np.array(m).reshape(m.size[1], m.size[0]) if (m.size[0] and m.size[1]) else np.zeros((0, 0))
        rows = []
        for r in range(H):
            v = 0
            for c in range(W):
                if r < a.shape[0] and c < a.shape[1] and a[r, c] > 90:
                    v |= (1 << (W - 1 - c))
            rows.append(v)
        glyphs.append((ord(ch), ch, rows))

    out = [
        "// 自动生成,请勿手改:界面用到的简体汉字 %dx%d 点阵(PingFang %dpx)" % (W, H, SIZE),
        "// 重新生成: python3 scripts/gen-cjk16.py",
        "#pragma once",
        "#include <stdint.h>",
        "#define CJK_W %d" % W,
        "#define CJK_H %d" % H,
        "typedef struct { uint32_t cp; uint16_t rows[%d]; } cjk_glyph_t;" % H,
        "static const cjk_glyph_t cjk16[] = {",
    ]
    for cp, ch, rows in glyphs:
        out.append("    { 0x%04X, { %s } }, // %s" % (cp, ", ".join("0x%04X" % r for r in rows), ch))
    out.append("};")
    out.append("#define CJK16_COUNT ((int)(sizeof(cjk16) / sizeof(cjk16[0])))")

    path = os.path.abspath(OUT)
    with open(path, "w") as fp:
        fp.write("\n".join(out) + "\n")
    print("wrote", path, "chars:", len(glyphs))


if __name__ == "__main__":
    main()
