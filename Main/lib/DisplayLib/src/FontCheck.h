#ifndef FONT_CHECK_H
#define FONT_CHECK_H

// =============================================================================
// 字库自检（开机跑一次，结果打到串口）
//
// 为什么需要它：LVGL 自带的 lv_font_simsun_16_cjk 只覆盖 1097 个汉字，
// 界面里的“识/别/书/测/距”等 35 个字不在里面，屏上就是空白。
// 换成自己生成的 src/ui_font_16.c 之后，用 lv_font_get_glyph_dsc() 挨个查，
// 一眼就能确认到底还有没有缺字，不用对着屏幕猜。
//
// 本文件放在 DisplayLib 里（只有这个库 include 了 lvgl.h），由 display.begin() 调用。
// =============================================================================

#include <Arduino.h>
#include <lvgl.h>

// ui_font_16 的声明来自 include/lv_conf.h 里的 LV_FONT_CUSTOM_DECLARE

// 开机自检：返回缺字总数（0 = 中文字库完全可用），并把缺的字打进串口
int fontSelfCheck(void);

#endif
