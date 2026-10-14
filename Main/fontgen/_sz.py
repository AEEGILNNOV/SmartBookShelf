# -*- coding: utf-8 -*-
# 量一下"不压缩"版本的体积，决定是否可行
import subprocess, os, time
OUT = r"D:\Project\SmartBookShelf\Main\fontgen"
FONT = os.path.join(OUT, "SimSun-extracted.ttf")
chars = None
# 重建与当前字库一致的字符集：界面用字 + GB2312 一级
UI = open(os.path.join(OUT, "ui_chars.txt"), encoding="utf-8").read()
SAFE = "".join(ch for ch in UI if ch not in '"\'\\`$;|&<>')
gb1 = set()
for hi in range(0xB0, 0xD8):
    for lo in range(0xA1, 0xFF):
        try: gb1.add(bytes([hi, lo]).decode("gb2312"))
        except Exception: pass
full = SAFE + "".join(sorted(gb1))
print("字符集: %d 个" % len(full))

for tag, extra in [
    ("nocomp_bpp2", ["--no-compress","--no-prefilter"]),
    ("nocomp_bpp1", ["--no-compress","--no-prefilter"]),
]:
    bpp = "2" if "bpp2" in tag else "1"
    out_c = os.path.join(OUT, "_size_%s.c" % tag)
    cmd = ["npx.cmd","--yes","lv_font_conv@1.5.3","--size","16","--bpp",bpp,
           "--format","lvgl","--font",FONT,"--lv-font-name","ui_font_16",
           "-o",out_c,"--symbols",full,"--force-fast-kern-format"] + extra
    t0 = time.time()
    p = subprocess.run(cmd, capture_output=True, text=True)
    if p.returncode != 0 or not os.path.exists(out_c):
        err = (p.stderr or p.stdout or "").strip().splitlines()
        print("%s 失败: %s" % (tag, err[-1] if err else "?")); continue
    kb = os.path.getsize(out_c)/1024.0
    bf = "?"
    import re
    m = re.search(r"\.bitmap_format\s*=\s*(\d+)", open(out_c, encoding="utf-8", errors="replace").read())
    if m: bf = m.group(1)
    print("%s -> %8.1f KB  bitmap_format=%s  (%.0f 秒)" % (tag, kb, bf, time.time()-t0))
    os.remove(out_c)