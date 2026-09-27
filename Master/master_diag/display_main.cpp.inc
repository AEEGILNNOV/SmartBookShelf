/*
 * diag_display: bring-up firmware for the ILI9341 240x320 panel.
 * No camera, no SD, no radios - only the display and the three buttons.
 *
 * Serial commands: n = next pattern, b = colour bars, g = grid, c = clear
 * Buttons:         BTN1 = next pattern, BTN2 = colour bars, BTN3 = clear
 */

#include <Arduino.h>
#include <TFT_eSPI.h>
#include <lvgl.h>

#include "board_pins.h"
#include "diag_camera.h"
#include "diag_common.h"
#include "diag_display.h"

enum Pattern {
    PATTERN_LVGL = 0,
    PATTERN_BARS,
    PATTERN_GRADIENT,
    PATTERN_GRID,
    PATTERN_COUNT
};

static volatile uint8_t requestPattern = PATTERN_LVGL;
static uint32_t lvglTicks = 0;

static void displayTask(void *arg) {
    (void)arg;
    uint32_t lastButtonPoll = 0;
    bool buttonLatched = false;

    for (;;) {
        lvglTicks++;

        const uint32_t now = millis();
        if (now - lastButtonPoll >= 25) {
            lastButtonPoll = now;
            const int pressed = diagButtonPressed();
            if (pressed != 0 && !buttonLatched) {
                buttonLatched = true;
                Serial.printf("BTN%d pressed\n", pressed);
                if (pressed == 1) {
                    requestPattern = (uint8_t)((requestPattern + 1) % PATTERN_COUNT);
                } else if (pressed == 2) {
                    requestPattern = PATTERN_BARS;
                } else {
                    requestPattern = PATTERN_GRID;
                }
            } else if (pressed == 0) {
                buttonLatched = false;
            }
        }

        lv_timer_handler();
        delay(5);
    }
}

static void setPattern(uint8_t pattern) {
    TFT_eSPI &tft = diagTft();

    if (pattern == PATTERN_LVGL) {
        Serial.println(F("pattern: LVGL page"));
        diagBuildLvglPage();
        lv_obj_invalidate(lv_scr_act());
        return;
    }

    Serial.printf("pattern: raw TFT_eSPI #%u\n", (unsigned)pattern);
    /* Leaving LVGL mode means wiping the active screen contents. */
    lv_obj_clean(lv_scr_act());
    lv_obj_set_style_bg_color(lv_scr_act(), lv_color_black(), 0);

    switch (pattern) {
        case PATTERN_BARS:
            diagColorBars();
            break;
        case PATTERN_GRADIENT:
            diagGradient();
            break;
        case PATTERN_GRID:
        default:
            diagGridRuler();
            break;
    }
    tft.setTextDatum(TL_DATUM);
}

void setup() {
    Serial.begin(115200);
    delay(300);

    diagLogBanner("SmartBookShelf / diag_display");
    printPsramReport();

    diagButtonsInit();
    Serial.printf("buttons: BTN1=%d BTN2=%d BTN3=%d (active high)\n",
                  BUTTON_PIN1, BUTTON_PIN2, BUTTON_PIN3);

    Serial.println(F("[1] backlight blink"));
    pinMode(PIN_TFT_BL, OUTPUT);
    diagBacklight(false);
    diagFlashBacklight(2);
    Serial.println(F("[2] tft.init()"));
    diagTft().init();

    Serial.println(F("[3] tft.setRotation(TFT_ROTATION)"));
    diagTft().setRotation(TFT_ROTATION);

    Serial.println(F("[4] address-window probe"));
    diagTftProbe();

    Serial.println(F("[5] colour bars"));
    diagColorBars();
    delay(700);

    diagTftLockInit();

    Serial.println(F("[6] LVGL init"));
    if (!diagLvglInit()) {
        Serial.println(F("LVGL init failed, staying on raw patterns"));
        setPattern(PATTERN_GRID);
    } else {
        setPattern(PATTERN_LVGL);
    }

    xTaskCreatePinnedToCore(displayTask, "diagDisplay", 8192, nullptr, 2, nullptr, 0);
    Serial.println(F("ready: n/b/g/c over serial, or the three buttons"));
}

void loop() {
    static uint8_t applied = 0xFF;
    static uint32_t lastReport = 0;
    static uint32_t lastTicks = 0;

    const char cmd = pollSerialCommand();
    switch (cmd) {
        case 'n':
            requestPattern = (uint8_t)((requestPattern + 1) % PATTERN_COUNT);
            break;
        case 'b':
            requestPattern = PATTERN_BARS;
            break;
        case 'g':
            requestPattern = PATTERN_GRID;
            break;
        case 'd':
            requestPattern = PATTERN_LVGL;
            break;
        case 'c':
            if (requestPattern != PATTERN_LVGL) {
                diagTft().fillScreen(TFT_BLACK);
            }
            break;
        default:
            break;
    }

    if (requestPattern != applied) {
        applied = requestPattern;
        setPattern(applied);
    }

    const uint32_t now = millis();
    if (now - lastReport >= 5000) {
        lastReport = now;
        const uint32_t ticks = lvglTicks;
        Serial.printf("alive uptime=%lus lvgl_ticks/s=%lu heap=%u psram_free=%u\n",
                      (unsigned long)(now / 1000UL),
                      (unsigned long)((ticks - lastTicks) / 5UL),
                      (unsigned)ESP.getFreeHeap(),
                      (unsigned)ESP.getFreePsram());
        lastTicks = ticks;
    }
    delay(10);
}
