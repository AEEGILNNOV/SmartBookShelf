#ifndef PROJECT_DIAG_DISPLAY_H
#define PROJECT_DIAG_DISPLAY_H

#include <Arduino.h>
#include <TFT_eSPI.h>
#include <esp_heap_caps.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <lvgl.h>

#include "board_pins.h"
#include "diag_common.h"

/* ------------------------------------------------------------------ */
/* Shared TFT instance for the diagnostic firmwares                    */
/* ------------------------------------------------------------------ */
inline TFT_eSPI &diagTft() {
    static TFT_eSPI tft;
    return tft;
}

inline void diagBacklight(bool on) {
    if (PIN_TFT_BL < 0) {
        return;
    }
    pinMode(PIN_TFT_BL, OUTPUT);
    digitalWrite(PIN_TFT_BL, on ? TFT_BACKLIGHT_ON : !TFT_BACKLIGHT_ON);
}

/* The CJK font is declared by LVGL only when LV_FONT_SIMSUN_16_CJK is on. */
inline const lv_font_t *diagUiFont() {
#ifdef LV_FONT_SIMSUN_16_CJK
    return &lv_font_simsun_16_cjk;
#else
    return &lv_font_montserrat_14;
#endif
}

/* ------------------------------------------------------------------ */
/* LVGL <-> TFT_eSPI glue                                              */
/* ------------------------------------------------------------------ */
/* The TFT may also be driven from a second task (camera preview), so the
 * flush callback takes the same bus lock. */
inline SemaphoreHandle_t &diagTftMutex() {
    static SemaphoreHandle_t mutex = nullptr;
    return mutex;
}

inline void diagTftLockInit() {
    if (diagTftMutex() == nullptr) {
        diagTftMutex() = xSemaphoreCreateMutex();
    }
}

inline bool diagTftLockTake(uint32_t timeoutMs = 200) {
    if (diagTftMutex() == nullptr) {
        return false;
    }
    return xSemaphoreTake(diagTftMutex(), pdMS_TO_TICKS(timeoutMs)) == pdTRUE;
}

inline void diagTftLockGive() {
    if (diagTftMutex() != nullptr) {
        xSemaphoreGive(diagTftMutex());
    }
}

inline void diagLvglFlush(lv_disp_drv_t *drv, const lv_area_t *area,
                          lv_color_t *colorP) {
    TFT_eSPI &tft = diagTft();
    const uint32_t w = (uint32_t)(area->x2 - area->x1 + 1);
    const uint32_t h = (uint32_t)(area->y2 - area->y1 + 1);

    const bool locked = diagTftMutex() != nullptr &&
                        xSemaphoreTake(diagTftMutex(), pdMS_TO_TICKS(200)) == pdTRUE;

    tft.startWrite();
    tft.setAddrWindow(area->x1, area->y1, w, h);
    tft.pushColors((uint16_t *)&colorP->full, w * h, true);
    tft.endWrite();

    if (locked) {
        xSemaphoreGive(diagTftMutex());
    }

    lv_disp_flush_ready(drv);
}

/* Draw buffers live in PSRAM when the module has it. */
inline bool diagLvglInit(uint32_t linesPerBuf = 40) {
    lv_init();

    size_t pixels = (size_t)SCR_W * linesPerBuf;
    lv_color_t *buf1 = (lv_color_t *)diagAlloc(pixels * sizeof(lv_color_t));
    lv_color_t *buf2 = (lv_color_t *)diagAlloc(pixels * sizeof(lv_color_t));
    if (buf1 == nullptr || buf2 == nullptr) {
        Serial.println(F("LVGL: draw buffer allocation FAILED"));
        return false;
    }

    static lv_disp_draw_buf_t drawBuf;
    static lv_disp_drv_t dispDrv;
    lv_disp_draw_buf_init(&drawBuf, buf1, buf2, (uint32_t)pixels);

    lv_disp_drv_init(&dispDrv);
    dispDrv.hor_res = SCR_W;
    dispDrv.ver_res = SCR_H;
    dispDrv.flush_cb = diagLvglFlush;
    dispDrv.draw_buf = &drawBuf;
    dispDrv.full_refresh = 0;
    dispDrv.user_data = &diagTft();
    lv_disp_drv_register(&dispDrv);

    Serial.printf("LVGL: buffers %u KB x2 in %s\n",
                  (unsigned)(pixels * sizeof(lv_color_t) / 1024),
                  psramFound() ? "PSRAM" : "internal RAM");
    return true;
}

inline void diagButtonsInit() {
    pinMode(BUTTON_PIN1, INPUT);
    pinMode(BUTTON_PIN2, INPUT);
    pinMode(BUTTON_PIN3, INPUT);
}

inline int diagButtonPressed() {
    if (digitalRead(BUTTON_PIN1) == BUTTON_TRUE_VALUE) {
        return 1;
    }
    if (digitalRead(BUTTON_PIN2) == BUTTON_TRUE_VALUE) {
        return 2;
    }
    if (digitalRead(BUTTON_PIN3) == BUTTON_TRUE_VALUE) {
        return 3;
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* Raw TFT_eSPI test patterns (no LVGL involved)                       */
/* ------------------------------------------------------------------ */
inline void diagColorBars() {
    TFT_eSPI &tft = diagTft();
    const uint16_t colors[] = {TFT_RED,  TFT_GREEN,   TFT_BLUE, TFT_YELLOW,
                               TFT_CYAN, TFT_MAGENTA, TFT_WHITE, TFT_BLACK};
    const int barW = SCR_W / 8;

    for (int i = 0; i < 8; i++) {
        tft.fillRect(i * barW, 0, barW, SCR_H, colors[i]);
    }
    tft.setTextDatum(MC_DATUM);
    tft.setTextSize(2);
    tft.setTextColor(TFT_BLACK, TFT_WHITE);
    tft.drawString("240x320", SCR_W / 2, SCR_H / 2);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.setTextDatum(TL_DATUM);
}

inline void diagGradient() {
    TFT_eSPI &tft = diagTft();

    for (int y = 0; y < SCR_H; y++) {
        uint8_t g = (uint8_t)((y * 255) / (SCR_H - 1));
        tft.drawFastHLine(0, y, SCR_W, tft.color565(g, g, g));
    }
    for (int x = 0; x < SCR_W; x++) {
        uint8_t r = (uint8_t)((x * 255) / (SCR_W - 1));
        tft.drawFastVLine(x, 0, SCR_H / 3, tft.color565(r, 30, 255 - r));
    }
}

inline void diagGridRuler() {
    TFT_eSPI &tft = diagTft();

    tft.fillScreen(TFT_BLACK);
    for (int x = 0; x < SCR_W; x += 20) {
        uint16_t c = (x % 100 == 0) ? TFT_RED : TFT_DARKGREY;
        tft.drawFastVLine(x, 0, SCR_H, c);
    }
    for (int y = 0; y < SCR_H; y += 20) {
        uint16_t c = (y % 100 == 0) ? TFT_RED : TFT_DARKGREY;
        tft.drawFastHLine(0, y, SCR_W, c);
    }
    tft.drawRect(0, 0, SCR_W, SCR_H, TFT_GREEN);
    tft.drawRect(SCR_W - 21, SCR_H - 21, 20, 20, TFT_WHITE);
    tft.setTextDatum(TL_DATUM);
    tft.setTextSize(1);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.drawString("(0,0)", 4, 4);
    tft.setTextColor(TFT_YELLOW, TFT_BLACK);
    tft.drawString("TL must be (0,0)", 4, SCR_H - 12);
}

/* ------------------------------------------------------------------ */
/* LVGL test page                                                      */
/* ------------------------------------------------------------------ */
inline void diagBuildLvglPage() {
    lv_obj_t *scr = lv_scr_act();
    lv_obj_set_style_bg_color(scr, lv_color_hex(0xF5F5F5), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

    lv_obj_t *title = lv_label_create(scr);
    lv_label_set_text(title, "显示屏自检 / DISPLAY TEST");
    lv_obj_set_style_text_color(title, lv_color_black(), 0);
    lv_obj_set_style_text_font(title, diagUiFont(), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 8);

    lv_obj_t *bar = lv_bar_create(scr);
    lv_obj_set_size(bar, 200, 12);
    lv_obj_align(bar, LV_ALIGN_TOP_MID, 0, 40);
    lv_bar_set_range(bar, 0, 100);
    lv_bar_set_value(bar, 66, LV_ANIM_OFF);

    lv_obj_t *lines = lv_label_create(scr);
    lv_obj_set_style_text_font(lines, diagUiFont(), 0);
    lv_obj_set_style_text_color(lines, lv_color_black(), 0);
    lv_obj_set_style_text_line_space(lines, 4, 0);
    lv_label_set_text(lines,
                      "LVGL 8.4 works\n"
                      "240 x 320 portrait\n"
                      "按键1 切换图案\n"
                      "按键2 RGB 色块\n"
                      "按键3 清屏");
    lv_obj_align(lines, LV_ALIGN_CENTER, 0, 10);

    lv_obj_t *rect = lv_obj_create(scr);
    lv_obj_set_size(rect, 60, 40);
    lv_obj_align(rect, LV_ALIGN_BOTTOM_RIGHT, -8, -8);
    lv_obj_set_style_bg_color(rect, lv_color_hex(0x0088FF), 0);
    lv_obj_set_style_border_width(rect, 0, 0);
}

/* ------------------------------------------------------------------ */
/* TFT_eSPI level helpers (need the shared TFT instance)               */
/* ------------------------------------------------------------------ */
inline void diagFlashBacklight(uint8_t times, uint16_t onMs = 120,
                               uint16_t offMs = 120) {
    for (uint8_t i = 0; i < times; i++) {
        diagBacklight(true);
        delay(onMs);
        diagBacklight(false);
        delay(offMs);
    }
    diagBacklight(true);
}

inline void diagTftProbe() {
    TFT_eSPI &tft = diagTft();
    Serial.printf("TFT_eSPI: width=%d height=%d rotation=%u\n", tft.width(),
                  tft.height(), (unsigned)TFT_ROTATION);

    const uint32_t before = millis();
    tft.fillScreen(TFT_RED);
    const uint32_t fillMs = millis() - before;
    tft.fillScreen(TFT_BLACK);

    const uint8_t id = tft.readcommand8(ILI9341_RDMADCTL);
    Serial.printf("readcommand8(RDMADCTL)=%u (0 when MISO is not wired)\n",
                  (unsigned)id);
    Serial.printf("fillScreen(240x320) took %lu ms\n", (unsigned long)fillMs);
}

#endif
