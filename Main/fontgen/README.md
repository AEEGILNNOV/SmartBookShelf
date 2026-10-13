# fontgen —— 中文字库生成

`src/ui_font_16.c` 是**生成的**，不要手改；要改字库就重跑本目录的脚本。

## 为什么要自己生成

LVGL 8.4 自带的 `lv_font_simsun_16_cjk` **不是全字库**：它由 `--symbols` 指定了一个
固定子集，只覆盖 **1097 个汉字**。本项目界面用到的 326 个字里有 **135 个不在里面**，
其中 35 个是屏幕可见的（识 / 别 / 书 / 测 / 距 / 态 / 记 / 录 …），
所以之前屏上大部分中文是空白。

## 现在的字库

| 项 | 值 |
| --- | --- |
| 源字体 | SimSun（宋体），从 `C:\Windows\Fonts\simsun.ttc` 拆出的 face[0] |
| 字号 | 16 px（与原来一致，版式不变） |
| 位深 | 2 bpp（抗锯齿够用，体积约为 4bpp 的一半） |
| 压缩 | RLE（`lv_font_conv` 默认），靠 `LV_USE_FONT_COMPRESSED 1` 解压 |
| 字符集 | 界面用字 + **GB2312 一级汉字 3755 个**，共约 4167 字符 |
| 结果 | 覆盖 3822 个码点，**界面用字零缺字**；Flash 占用很小（见下） |

> 顺带的好处：GB2312 一级汉字覆盖了绝大多数常见书名，
> 识别出书名后能直接显示，不会因为缺字变空白。

## 重新生成

```powershell
cd D:\Project\SmartBookShelf\Main
python fontgen\gen_font.py
```

需要 Node.js（脚本用 `npx lv_font_conv@1.5.3`）和 Python + `fontTools`
（只在从 `simsun.ttc` 拆字体时用到）。脚本会自动：

1. 扫 `src/`、`lib/*/src/`、`include/` 源码，收集所有中文字符（去注释）；
2. 并上 GB2312 一级汉字 3755 个；
3. 调 `lv_font_conv` 生成 `src/ui_font_16.c`；
4. 清掉头注释里的本机路径。

## 生成后必须对上的 4 个配置

改动字库或换字体名后，确认这几处一致（否则会链接失败或整屏空白）：

| 位置 | 必须是 |
| --- | --- |
| `include/lv_conf.h` | `#define UI_FONT_16 1` —— 生成文件用 `#if UI_FONT_16` 包住全部数据，不定义就整份被预处理掉 |
| `include/lv_conf.h` | `#define LV_USE_FONT_COMPRESSED 1` —— 不开它，LVGL 只打一句 warning 然后**什么都不画** |
| `include/lv_conf.h` | `#define LV_FONT_CUSTOM_DECLARE LV_FONT_DECLARE(ui_font_16)` |
| `include/config.h` | `#define DISPLAY_FONT_CJK (&ui_font_16)` |

另外 `platformio.ini` 里必须有 `-DLV_LVGL_H_INCLUDE_SIMPLE`：
生成的 `.c` 只在定义了这个宏时才 `#include "lvgl.h"`，否则会去找 `"lvgl/lvgl.h"` 报找不到文件。

## 检查有没有缺字

```powershell
python tools\font_charset_check.py
```

它把字库的 cmap 解出来跟源码里的中文对比，有缺字会列出来并返回非 0。
开机时 `display.begin()` 还会跑一次 `fontSelfCheck()`，把结果和缺字打到串口，
不用对着屏幕猜。

## 文件说明

| 文件 | 入库 | 说明 |
| --- | --- | --- |
| `gen_font.py` | 是 | 生成脚本 |
| `ui_chars.txt` | 是 | 界面用字清单（生成过程的中间产物，便于核对） |
| `SimSun-extracted.ttf` | 否 | 17MB，从系统字体拆出来的，脚本会按需重建 |
