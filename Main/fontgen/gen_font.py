# -*- coding: utf-8 -*-
"""
重新生成中文字库 src/ui_font_16.c（一次性脚本，平时用不到）

什么时候要跑：
  1. 界面上新加了中文，怕缺字；
  2. 想换字体（SimSun/黑体/楷体）、换字号、换压缩比。

前置条件：
  - Node.js（用 npx 拉 lv_font_conv，官方字库转换工具）
  - Python + fontTools（只在需要从 .ttc 里拆字体时用）

用法（在 Main 目录下）：
    python fontgen/gen_font.py

做了什么：
  1. 扫 src/ 和 lib/ 里的源码，把所有出现的中文字符收集起来（保证界面不留白字）；
  2. 再并上 GB2312 一级汉字 3755 个（保证绝大多数书名都能显示）；
  3. 调 lv_font_conv 把 SimSun 转成 LVGL 的 C 文件，写进 src/ui_font_16.c。

注意：
  - 生成的文件必须放在 src/ 下，PlatformIO 才会编译它；
  - lv_font_conv 生成的 .c 用 #if UI_FONT_16 包住全部数据，
    所以 include/lv_conf.h 里必须 #define UI_FONT_16 1；
  - 生成的是 RLE 压缩字体，lv_conf.h 里必须 #define LV_USE_FONT_COMPRESSED 1，
    否则 LVGL 只打一句 warning 然后什么都不画（整屏空白）。
"""

import os
import re
import glob
import shutil
import subprocess
import sys

# ------------------------------ 参数（要改就改这里） ------------------------------
FONT_SIZE = 16          # 字号(px)。改这个要同步看 config.h 里 DISPLAY_FONT_CJK 的观感
BPP = 2                 # 抗锯齿位深：1/2/4。2 已经够用，体积省一半
FONT_NAME = "ui_font_16"  # 生成的符号名，lv_conf.h 里 LV_FONT_DECLARE 要一致

MAIN = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))  # .../Main
GEN_DIR = os.path.join(MAIN, "fontgen")
OUT_C = os.path.join(MAIN, "src", "ui_font_16.c")
TTF = os.path.join(GEN_DIR, "SimSun-extracted.ttf")
CHARSET_TXT = os.path.join(GEN_DIR, "ui_chars.txt")

# 从 .ttc 里拆出来的字体；系统里 SimSun 是 simsun.ttc（集合字体），
# lv_font_conv 不认 ttc，所以先拆成单 face 的 ttf。
TTC_SOURCE = r"C:\Windows\Fonts\simsun.ttc"


def extract_ttf():
    """SimSun-extracted.ttf 不存在就从 Windows 的 simsun.ttc 里拆一个出来"""
    if os.path.exists(TTF):
        print("[1/4] 已存在字体文件: %s" % TTF)
        return True
    try:
        from fontTools.ttLib import TTCollection
    except ImportError:
        print("缺 fontTools，请先: python -m pip install fonttools")
        return False
    if not os.path.exists(TTC_SOURCE):
        print("找不到 %s，请改脚本里的 TTC_SOURCE，或自己放一个 ttf 到 fontgen/" % TTC_SOURCE)
        return False
    col = TTCollection(TTC_SOURCE, lazy=False)
    names = []
    for f in col.fonts:
        try:
            names.append(f["name"].getDebugName(4))
        except Exception:
            names.append("?")
    print("[1/4] %s 里有: %s" % (os.path.basename(TTC_SOURCE), names))
    col.fonts[0].save(TTF)
    print("      已导出 face[0] -> %s" % os.path.basename(TTF))
    return True


def collect_chars():
    """收集源码里出现的全部中文字符 + GB2312 一级汉字"""
    files = []
    for pattern in ("src/*.c", "src/*.cpp", "src/*.h",
                    "lib/*/src/*.c", "lib/*/src/*.cpp", "lib/*/src/*.h",
                    "include/*.h"):
        files += glob.glob(os.path.join(MAIN, pattern))
    # vendor 的二哈驱动里没有界面文字，跳过（它的注释会带一堆无关汉字）
    files = [f for f in files if "DFRobot_HuskylensV2" not in f]
    files = [f for f in files if os.path.abspath(f) != os.path.abspath(OUT_C)]

    ui_chars = set()
    for f in files:
        text = open(f, "rb").read().decode("utf-8", errors="replace")
        # 去掉注释，避免把注释里的字也算进来（注释里的字不上屏，白占体积）
        text = re.sub(r"//[^\n]*", "", text)
        text = re.sub(r"/\*.*?\*/", "", text, flags=re.S)
        for ch in text:
            cp = ord(ch)
            if (0x4E00 <= cp <= 0x9FFF or 0x3000 <= cp <= 0x303F
                    or 0xFF00 <= cp <= 0xFFEF or ch in "°±×÷→←↑↓…—"):
                ui_chars.add(ch)
    ui_chars |= set(chr(c) for c in range(0x20, 0x7F))  # ASCII 可见字符

    # GB2312 一级汉字（区位 16-55，共 3755 字），覆盖绝大多数常见书名
    gb1 = set()
    for hi in range(0xB0, 0xD8):
        for lo in range(0xA1, 0xFF):
            try:
                gb1.add(bytes([hi, lo]).decode("gb2312"))
            except Exception:
                pass

    all_chars = "".join(sorted(ui_chars | gb1))
    os.makedirs(GEN_DIR, exist_ok=True)
    with open(CHARSET_TXT, "w", encoding="utf-8") as fh:
        fh.write("".join(sorted(ui_chars)))
    print("[2/4] 界面用字 %d 个 + GB2312 一级 %d 个 -> 合计 %d 个"
          % (len(ui_chars), len(gb1), len(all_chars)))
    print("      界面用字清单写到 %s" % CHARSET_TXT)
    return all_chars


def generate(chars):
    """调 lv_font_conv 生成 lvgl 格式的 C 文件"""
    # 双引号/反斜杠/反引号会破坏命令行传参，且几乎不会出现在中文界面里
    safe = "".join(c for c in chars if c not in '"\'\\`$;|&<>')
    npx = shutil.which("npx") or shutil.which("npx.cmd")
    if not npx:
        print("找不到 npx，请先装 Node.js")
        return False
    cmd = [npx, "--yes", "lv_font_conv@1.5.3",
           "--size", str(FONT_SIZE), "--bpp", str(BPP),
           "--format", "lvgl", "--font", TTF,
           "--lv-font-name", FONT_NAME,
           "-o", OUT_C,
           "--symbols", safe,
           "--force-fast-kern-format"]
    print("[3/4] 正在转换（%d 个字符，可能要 30 秒左右）..." % len(safe))
    p = subprocess.run(cmd, capture_output=True, text=True)
    if p.returncode != 0 or not os.path.exists(OUT_C):
        err = (p.stderr or p.stdout or "").strip().splitlines()
        print("      失败: %s" % (err[-1] if err else "未知错误"))
        return False
    print("      完成: %s (%.1f KB)" % (OUT_C, os.path.getsize(OUT_C) / 1024.0))
    return True


def strip_local_paths():
    """把头注释里的本机绝对路径换成文件名，便于入库和别人复现"""
    raw = open(OUT_C, "rb").read().decode("utf-8", errors="replace")
    raw = raw.replace(TTF, os.path.basename(TTF))
    raw = raw.replace(OUT_C, os.path.basename(OUT_C))
    open(OUT_C, "wb").write(raw.encode("utf-8"))
    print("[4/4] 已清理头注释里的本机路径")


def main():
    print("=== 生成中文字库 %s (%dpx, %dbpp) ===" % (FONT_NAME, FONT_SIZE, BPP))
    if not extract_ttf():
        return 1
    chars = collect_chars()
    if not generate(chars):
        return 1
    strip_local_paths()
    print("\n完成。记得确认:")
    print("  - include/lv_conf.h 里 #define UI_FONT_16 1")
    print("  - include/lv_conf.h 里 #define LV_USE_FONT_COMPRESSED 1")
    print("  - include/lv_conf.h 里 LV_FONT_CUSTOM_DECLARE LV_FONT_DECLARE(%s)" % FONT_NAME)
    print("  - include/config.h 里 DISPLAY_FONT_CJK == &%s" % FONT_NAME)
    return 0


if __name__ == "__main__":
    sys.exit(main())
