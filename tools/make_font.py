# -*- coding: utf-8 -*-
"""
从 Noto Sans SC 生成游戏用的子集字体。

为什么需要这个脚本：
    raylib 的 LoadFontEx 只接受 TrueType 轮廓（glyf）。CJK 字体动辄 17MB，
    直接入库会让仓库变得很重；而游戏实际只会显示 CollectGlyphs() 收集到的
    那几百个字。所以这里把可变字体实例化到固定字重，再裁到实际用到的字符。

字符集来源：
    扫描所有 *.c 里的非 ASCII 字符（界面文字都写在这些文件中），
    加上完整可打印 ASCII 与常用标点。

用法：
    python tools/make_font.py
    python tools/make_font.py --src "C:/Windows/Fonts/NotoSansSC-VF.ttf" --out assets/font.ttf

改了界面文字之后如果出现方块，重跑一次即可。
"""

import argparse
import glob
import os
import sys

from fontTools import subset
from fontTools.ttLib import TTFont
from fontTools.varLib.instancer import instantiateVariableFont

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# 界面上可能用到、但源码里不一定出现的符号，一并带上
EXTRA = "·—…「」『』《》（）【】、。，！？：；％°×÷→←↑↓←↑→↓□■●○◆◇★☆"

DEFAULT_SRC = r"C:/Windows/Fonts/NotoSansSC-VF.ttf"
DEFAULT_OUT = os.path.join(ROOT, "assets", "font.ttf")


def collect_chars():
    chars = set(chr(c) for c in range(32, 127))
    chars.update(EXTRA)
    for path in glob.glob(os.path.join(ROOT, "*.c")):
        with open(path, "r", encoding="utf-8", errors="ignore") as f:
            for ch in f.read():
                if ord(ch) > 127:
                    chars.add(ch)
    return chars


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--src", default=DEFAULT_SRC)
    ap.add_argument("--out", default=DEFAULT_OUT)
    ap.add_argument("--weight", type=int, default=400, help="wght 轴实例化到的字重")
    args = ap.parse_args()

    if not os.path.exists(args.src):
        sys.exit("找不到源字体: %s" % args.src)

    chars = collect_chars()
    text = "".join(sorted(chars))

    font = TTFont(args.src)
    if "fvar" in font:
        # raylib / stb_truetype 不认识 fvar，先钉死到单一字重，否则会按默认实例渲染
        axes = {a.axisTag: a.defaultValue for a in font["fvar"].axes}
        if "wght" in axes:
            axes["wght"] = args.weight
        instantiateVariableFont(font, axes, inplace=True)

    opts = subset.Options()
    opts.layout_features = ["*"]
    opts.name_IDs = ["*"]
    opts.notdef_outline = True
    opts.recalc_bounds = True
    opts.drop_tables += ["DSIG"]

    sub = subset.Subsetter(options=opts)
    sub.populate(text=text)
    sub.subset(font)

    os.makedirs(os.path.dirname(args.out), exist_ok=True)
    font.save(args.out)

    size = os.path.getsize(args.out)
    print("字符集 %d 个 -> %s (%.1f KB)" % (len(chars), args.out, size / 1024.0))


if __name__ == "__main__":
    main()
