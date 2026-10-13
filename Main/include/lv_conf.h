#ifndef LV_CONF_H
#define LV_CONF_H

#include <stdint.h>

#define LV_COLOR_DEPTH 16
#define LV_COLOR_16_SWAP 0

#define LV_MEM_CUSTOM 1
#if LV_MEM_CUSTOM
    #define LV_MEM_CUSTOM_INCLUDE <stdlib.h>
    #define LV_MEM_CUSTOM_ALLOC   malloc
    #define LV_MEM_CUSTOM_FREE    free
    #define LV_MEM_CUSTOM_REALLOC realloc
#endif

#define LV_TICK_CUSTOM 1
#if LV_TICK_CUSTOM
    #define LV_TICK_CUSTOM_INCLUDE "Arduino.h"
    #define LV_TICK_CUSTOM_SYS_TIME_EXPR (millis())
#endif

// =============================================================================
// 字体
//
// 【重要】LVGL 自带的 lv_font_simsun_16_cjk **只有 1097 个汉字**
//   （它是用 --symbols 指定的固定子集，不是全字库），
//   本项目界面里的“识/别/书/测/距/态/记/录”等 35 个字都不在里面，
//   所以之前屏上大部分中文是空白。
//
// 现在改用自己生成的全字库：Main/font/ui_font_16.c
//   SimSun 16px / 2bpp / RLE 压缩，含 GB2312 一级汉字 3755 个 + 界面用字，
//   共 4167 个字符、约 1.5MB；重新生成见 Main/fontgen/README.md。
//
// 只保留 montserrat（英文数字）+ 自定义 ui_font_16，把自带 simsun 关掉，
// 省 1MB Flash（LV_FONT_SIMSUN_16_CJK=0 时那个 .c 整个不参与编译）。
// =============================================================================

#define LV_FONT_MONTSERRAT_12 1
#define LV_FONT_MONTSERRAT_14 1
#define LV_FONT_DEFAULT &lv_font_montserrat_14

// 由 include/config.h 的 CFG_FONT_CUSTOM_CJK 决定用哪套中文：
//   1 = 用生成的 ui_font_16（GB2312 一级 3755 字）
//   0 = 退回 LVGL 自带子集字库（只有 1097 字，会缺字，但可用于排错）
#include "config.h"

#if CFG_FONT_CUSTOM_CJK
#define LV_FONT_SIMSUN_16_CJK 0
#else
#define LV_FONT_SIMSUN_16_CJK 1
#endif

// 自己生成的中文字库：src/ui_font_16.c
// 注意 1：lv_font_conv 生成的 .c 用 #if UI_FONT_16 包住全部数据，
//         所以必须把 UI_FONT_16 定义成 1，否则整份字库会被预处理掉。
// 注意 2：生成的字体是 RLE 压缩的（.bitmap_format = 1），
//         LVGL 只有在 LV_USE_FONT_COMPRESSED=1 时才会解压；
//         不开这个开关它会打一句 warning 然后**什么都不画**（整屏空白）。
#if CFG_FONT_CUSTOM_CJK
#define UI_FONT_16 1
#define LV_FONT_CUSTOM_DECLARE LV_FONT_DECLARE(ui_font_16)
#else
#define UI_FONT_16 0
#define LV_FONT_CUSTOM_DECLARE
#endif

// 字库是 RLE 压缩的，必须开解压；解压缓冲按 "一行最多 256 像素" 申请，约 0.5KB
#define LV_USE_FONT_COMPRESSED 1

#define LV_USE_BAR 1
#define LV_USE_BTN 1
#define LV_USE_CHECKBOX 1
#define LV_USE_LABEL 1
#define LV_USE_TABVIEW 1
#define LV_USE_THEME_DEFAULT 1

#define LV_USE_LOG 0

#endif
