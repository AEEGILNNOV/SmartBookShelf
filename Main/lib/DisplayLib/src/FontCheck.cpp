#include "FontCheck.h"

#include "config.h"

// =============================================================================
// 字库自检实现
//
// 核心就一件事：把界面里用到的词逐字丢给 lv_font_get_glyph_dsc()，
// 查不到字形的就是缺字。这样不用接线、不用对着屏幕猜，
// 开机看串口就知道中文字库到底可不可用。
// =============================================================================

// 界面里最容易缺、也是换成自带字库时确实缺的那批字
static const char *FONT_PROBE_WORDS[] = {
    "速定位书格", "识别", "图书", "测距", "状态", "记录", "错误", "距离",
    "超声波", "步进电机", "夹层", "升降", "推出", "界面", "系统信息", "运行时间",
    "已记录书数", "画面里没有目标", "知识库", "芯片", "内存",
};
#define FONT_PROBE_WORD_COUNT \
    ((int)(sizeof(FONT_PROBE_WORDS) / sizeof(FONT_PROBE_WORDS[0])))

// 查一个字有没有字形：lv_font_get_glyph_dsc 返回 false 就是缺字
static bool fontHasGlyph(const lv_font_t *font, uint32_t letter) {
    lv_font_glyph_dsc_t dsc;
    return lv_font_get_glyph_dsc(font, &dsc, letter, 0);
}

// 把一个 UTF-8 字符串拆成码点逐字检查，返回缺字个数，并把缺的字打进 Serial
static int probeWord(const lv_font_t *font, const char *utf8) {
    int missing = 0;
    const uint8_t *p = (const uint8_t *)utf8;
    char missBuf[64];
    int missOff = 0;
    missBuf[0] = '\0';

    while (*p != 0) {
        uint32_t cp = 0;
        int extra = 0;

        if (*p < 0x80) {
            cp = *p;
            extra = 0;
        } else if ((*p & 0xE0) == 0xC0) {
            cp = *p & 0x1F;
            extra = 1;
        } else if ((*p & 0xF0) == 0xE0) {
            cp = *p & 0x0F;
            extra = 2;
        } else if ((*p & 0xF8) == 0xF0) {
            cp = *p & 0x07;
            extra = 3;
        } else {
            p++;  // 非法字节，跳过
            continue;
        }

        const uint8_t *start = p;  // 记住这个字的起始字节，缺字时原样打印
        p++;
        for (int i = 0; i < extra && *p != 0; i++) {
            cp = (cp << 6) | (*p & 0x3F);
            p++;
        }

        if (cp < 0x80) {
            continue;  // ASCII 不查
        }

        if (!fontHasGlyph(font, cp)) {
            missing++;
            if (missOff + extra + 1 < (int)sizeof(missBuf)) {
                for (int i = 0; i <= extra; i++) {
                    missBuf[missOff++] = (char)start[i];
                }
                missBuf[missOff] = '\0';
            }
        }
    }

    if (missing > 0) {
        Serial.printf("    缺 %2d 字: %s\n", missing, missBuf);
    }
    return missing;
}

// 开机自检：返回缺字总数（0 = 可用）
int fontSelfCheck(void) {
    // 用哪个字库由 config.h 的 DISPLAY_FONT_CUSTOM_CJK 决定，
    // 关掉自定义字库时这里不能引用 ui_font_16（它根本没参与编译）
    const lv_font_t *font = DISPLAY_FONT_CJK;

    // bpp / 压缩方式在“字体格式描述符”里，不在 lv_font_t 上
    // （lv_font_t.dsc 指向 lv_font_fmt_txt_dsc_t）
    const lv_font_fmt_txt_dsc_t *fdsc = (const lv_font_fmt_txt_dsc_t *)font->dsc;

    Serial.printf("--- 字库自检 (%s) ---\n",
                  CFG_FONT_CUSTOM_CJK ? "src/ui_font_16.c" : "LVGL 自带 simsun");
    Serial.printf("  行高=%d, 压缩=%s\n",
                  (int)font->line_height,
                  (fdsc != nullptr &&
                   fdsc->bitmap_format == LV_FONT_FMT_TXT_COMPRESSED)
                      ? "RLE(需 LV_USE_FONT_COMPRESSED=1)"
                      : "无");

    // 压缩字库没开解压时，LVGL 只打一句 warning 然后什么都不画（整屏空白）
    if (fdsc != nullptr &&
        fdsc->bitmap_format == LV_FONT_FMT_TXT_COMPRESSED &&
        LV_USE_FONT_COMPRESSED == 0) {
        Serial.println("  [严重] 字库是压缩的, 但 LV_USE_FONT_COMPRESSED=0, "
                       "整屏都会是空白!");
    }

    int totalMissing = 0;
    for (int i = 0; i < FONT_PROBE_WORD_COUNT; i++) {
        int m = probeWord(font, FONT_PROBE_WORDS[i]);
        if (m > 0) {
            Serial.printf("  [%s]\n", FONT_PROBE_WORDS[i]);
        }
        totalMissing += m;
    }

    if (totalMissing == 0) {
        Serial.printf("  自检通过: %d 组词全部有字形, 中文字库可用\n",
                      FONT_PROBE_WORD_COUNT);
    } else {
        Serial.printf("  自检失败: %d 组词里有 %d 个字没有字形\n",
                      FONT_PROBE_WORD_COUNT, totalMissing);
    }
    return totalMissing;
}
