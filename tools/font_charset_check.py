# -*- coding: utf-8 -*-
"""
中文字库覆盖率自检：界面里用到的字，字库到底有没有

用途：改界面文字之后跑一遍，确认没有新的缺字（避免上屏才发现空白）。
不依赖 LVGL 运行，直接把字库 .c 里的 cmap 解出来跟源码对比。

用法（在仓库根目录）：
    python tools/font_charset_check.py

退出码：0 = 无缺字；1 = 有缺字
"""

import os
import re
import glob
import sys

MAIN = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "Main")
# 只查我们自己的字库
FONT_C = os.path.join(MAIN, "src", "ui_font_16.c")


def covered_codepoints(path):
    """从 lv_font_conv 生成的 .c 里解出 cmap 覆盖的所有码点"""
    raw = open(path, "rb").read().decode("utf-8", errors="replace")
    blocks = re.findall(
        r"\{\s*\.range_start\s*=\s*(\d+)\s*,\s*\.range_length\s*=\s*(\d+)\s*,\s*"
        r"\.glyph_id_start\s*=\s*(\d+)\s*,\s*"
        r"\.unicode_list\s*=\s*(\w+)\s*,\s*\.glyph_id_ofs_list\s*=\s*(\w+)\s*,\s*"
        r"\.list_length\s*=\s*(\d+)\s*,\s*\.type\s*=\s*(\w+)", raw)

    def vals(name):
        if name == "NULL":
            return []
        m = re.search(re.escape(name) + r"\s*\[\]\s*=\s*\{(.*?)\};", raw, re.S)
        return [int(h, 16) for h in re.findall(r"0x([0-9a-fA-F]+)", m.group(1))] if m else []

    cov = set()
    for rs, rl, gs, ul, go, ll, ty in blocks:
        rs, rl = int(rs), int(rl)
        if "FORMAT0" in ty and ty.endswith("TINY"):
            cov |= set(range(rs, rs + rl))
        elif "FORMAT0" in ty and ty.endswith("FULL"):
            for i, o in enumerate(vals(go)):
                if o != 0:
                    cov.add(rs + i)
        else:
            for v in vals(ul):
                cov.add(rs + v)
    return cov


def source_chars():
    """源码里会显示到屏幕/串口的中文（去注释）"""
    files = []
    for pattern in ("src/*.c", "src/*.cpp", "src/*.h",
                    "lib/*/src/*.c", "lib/*/src/*.cpp", "lib/*/src/*.h",
                    "include/*.h"):
        files += glob.glob(os.path.join(MAIN, pattern))
    files = [f for f in files if "DFRobot_HuskylensV2" not in f]
    files = [f for f in files if os.path.abspath(f) != os.path.abspath(FONT_C)]

    need = {}
    for f in files:
        text = open(f, "rb").read().decode("utf-8", errors="replace")
        text = re.sub(r"//[^\n]*", "", text)
        text = re.sub(r"/\*.*?\*/", "", text, flags=re.S)
        for ch in text:
            cp = ord(ch)
            if (0x4E00 <= cp <= 0x9FFF or 0x3000 <= cp <= 0x303F
                    or 0xFF00 <= cp <= 0xFFEF):
                need.setdefault(ch, set()).add(os.path.basename(f))
    return need


def main():
    if not os.path.exists(FONT_C):
        print("找不到字库文件: %s" % FONT_C)
        return 1
    cov = covered_codepoints(FONT_C)
    hanzi = [c for c in cov if 0x4E00 <= c <= 0x9FFF]
    print("字库: %s" % os.path.relpath(FONT_C, os.path.dirname(MAIN)))
    print("  覆盖码点 %d 个, 其中汉字 %d 个" % (len(cov), len(hanzi)))

    need = source_chars()
    missing = sorted(ch for ch in need if ord(ch) not in cov)
    print("  源码需要字符 %d 个, 缺 %d 个" % (len(need), len(missing)))
    if missing:
        print("\n缺字清单（按出现文件）:")
        for ch in missing:
            print("  %s  <- %s" % (ch, ",".join(sorted(need[ch]))))
        print("\n结论: 有缺字。要么改界面用词，要么跑 Main/fontgen/gen_font.py 重新生成字库。")
        return 1
    print("\n结论: 界面用字全部有字形，没有缺字。")
    return 0


if __name__ == "__main__":
    sys.exit(main())
