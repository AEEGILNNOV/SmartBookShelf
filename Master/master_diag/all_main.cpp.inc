/*
 * diag: display + camera + buttons in one firmware.
 *
 * Pages: 1 = display test (raw TFT_eSPI patterns + LVGL page)
 *        2 = camera test (JPEG stats, frame difference readings)
 *        3 = preview   (live scaled YUV422 -> TFT, camera stats sidebar)
 *
 * Serial commands: 1 / 2 / 3 select a page, n = next pattern,
 *                  c = one JPEG capture, r = camera driver cycle,
 *                  t = run one change-detection step
 */

#include <Arduino.h>
#include <TFT_eSPI.h>
#include <esp_heap_caps.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#include <lvgl.h>

#include "board_pins.h"
#include "camera_config.h"
#include "diag_camera.h"
#include "diag_common.h"
#include "diag_display.h"
#include "esp_camera.h"

/* ------------------------------------------------------------------ */
/* State shared between the LVGL task and the camera task              */
/* ------------------------------------------------------------------ */
enum DiagPage {
    PAGE_DISPLAY = 1,
    PAGE_CAMERA = 2,
    PAGE_PREVIEW = 3
};

static volatile uint8_t gPage = PAGE_DISPLAY;
static volatile uint8_t gRequestedPage = PAGE_DISPLAY;
static volatile bool gCameraReady = false;
static volatile bool gPreviewRunning = false;
static volatile uint8_t gLvglPattern = 0;

/* Camera work always runs on the camera task: it owns a 16 KB stack,
 * which the photo/encode paths need. The serial command and the button
 * handler only raise this flag. */
static volatile bool gSnapshotRequest = false;

static uint8_t *gPreviewBuf = nullptr;
static uint16_t gPreviewW = 0;
static uint16_t gPreviewH = 0;

/* ------------------------------------------------------------------ */
/* Change detection state                                             */
/* ------------------------------------------------------------------ */
struct DiffState {
    bool hasBaseline;
    uint8_t *gray;
    uint8_t prevAvg;
    uint32_t comparisons;
    uint32_t lastChangedPixels;
    uint32_t lastTotalPixels;
    uint32_t lastChangedPct;
    uint32_t lastLuma;
};

static DiffState gDiff = {};

static void diffStateReset() {
    gDiff.hasBaseline = false;
    gDiff.prevAvg = 0;
}

/* Sample a sparse luma grid out of a JPEG frame (own JPEG decoder). */
static bool diffSampleJpeg(const camera_fb_t *fb, uint8_t *out, int w, int h,
                           uint32_t *avgOut) {
    const int strideX = (int)fb->width / w;
    const int strideY = (int)fb->height / h;
    if (strideX < 1 || strideY < 1) {
        return false;
    }

    /* Fast approximate luma: decode the JPEG once at low quality into a
     * tiny RGB buffer using the camera's own decoder. */
    size_t bufLen = (size_t)w * h;
    uint8_t *rgb = (uint8_t *)diagAlloc(bufLen);
    if (rgb == nullptr) {
        return false;
    }

    bool ok = fmt2rgb888(fb->buf, fb->len, fb->format, rgb);
    if (!ok) {
        free(rgb);
        return false;
    }

    uint32_t sum = 0;
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            const int sx = x * strideX;
            const int sy = y * strideY;
            const uint8_t *p = rgb + ((size_t)sy * fb->width + sx) * 3;
            const uint8_t luma = (uint8_t)((p[0] * 77 + p[1] * 150 + p[2] * 29) >> 8);
            out[y * w + x] = luma;
            sum += luma;
        }
    }
    free(rgb);

    *avgOut = (w * h) > 0 ? sum / (uint32_t)(w * h) : 0;
    return true;
}

/* Cheap luma grid straight out of a YUV422 frame (UYVY: U Y0 V Y1). */
static bool diffSampleYuv422(const camera_fb_t *fb, uint8_t *out, int w, int h,
                             uint32_t *avgOut) {
    const int strideX = (int)fb->width / w;
    const int strideY = (int)fb->height / h;
    if (strideX < 1 || strideY < 1) {
        return false;
    }

    uint32_t sum = 0;
    for (int y = 0; y < h; y++) {
        const uint8_t *row = fb->buf + (size_t)(y * strideY) * fb->width * 2;
        for (int x = 0; x < w; x++) {
            const int sx = (x * strideX) & ~1;
            const uint8_t luma = row[(size_t)sx * 2 + 1];
            out[y * w + x] = luma;
            sum += luma;
        }
    }
    *avgOut = sum / (uint32_t)(w * h);
    return true;
}

static void diffRun(const camera_fb_t *fb) {
    const int w = CAM_DIFF_SAMPLE_W;
    const int h = CAM_DIFF_SAMPLE_H;
    const size_t count = (size_t)w * h;

    if (gDiff.gray == nullptr) {
        gDiff.gray = (uint8_t *)diagAlloc(count);
        if (gDiff.gray == nullptr) {
            return;
        }
    }

    uint8_t *sample = (uint8_t *)diagAlloc(count);
    if (sample == nullptr) {
        return;
    }

    uint32_t avg = 0;
    bool ok = false;
    if (fb->format == PIXFORMAT_YUV422) {
        ok = diffSampleYuv422(fb, sample, w, h, &avg);
    } else {
        ok = diffSampleJpeg(fb, sample, w, h, &avg);
    }

    if (ok) {
        uint32_t changed = 0;
        if (gDiff.hasBaseline) {
            for (size_t i = 0; i < count; i++) {
                const int d = (int)sample[i] - (int)gDiff.gray[i];
                if (d > CAM_DIFF_PIXEL_THRESHOLD || d < -CAM_DIFF_PIXEL_THRESHOLD) {
                    changed++;
                }
            }
        }

        memcpy(gDiff.gray, sample, count);
        gDiff.hasBaseline = true;
        gDiff.comparisons++;
        gDiff.lastChangedPixels = changed;
        gDiff.lastTotalPixels = (uint32_t)count;
        gDiff.lastChangedPct = (uint32_t)(changed * 100U / count);
        gDiff.lastLuma = avg;

        Serial.printf("DIFF changed=%lu/%lu (%lu%%) luma=%lu trigger=%s\n",
                      (unsigned long)changed, (unsigned long)count,
                      (unsigned long)gDiff.lastChangedPct, (unsigned long)avg,
                      (gDiff.comparisons > 1 &&
                       gDiff.lastChangedPct >= CAM_DIFF_CHANGED_RATIO_PCT)
                          ? "YES"
                          : "no");
    }
    free(sample);
}

/* ------------------------------------------------------------------ */
/* Camera driver handling                                             */
/* ------------------------------------------------------------------ */
static bool camStartVideo() {
    camera_config_t cfg;
    cameraPreviewConfig(cfg);
    cfg.fb_count = 2;
    cfg.grab_mode = CAMERA_GRAB_LATEST;
    if (!cameraStart(cfg)) {
        return false;
    }
    cameraPrintAdjustments(cfg);
    gCameraReady = true;
    return true;
}

static bool camStartStill() {
    camera_config_t cfg;
    cameraCaptureConfig(cfg);
    if (!cameraStart(cfg)) {
        return false;
    }
    cameraPrintAdjustments(cfg);
    gCameraReady = true;
    return true;
}

static void camStop() {
    if (gCameraReady) {
        esp_camera_deinit();
        gCameraReady = false;
    }
    gPreviewRunning = false;
    diffStateReset();
}

/* ------------------------------------------------------------------ */
/* YUV422 -> RGB565 video push                                        */
/* ------------------------------------------------------------------ */
static void pushVideoFrame(const camera_fb_t *fb) {
    if (fb->format != PIXFORMAT_YUV422 || fb->width < 2) {
        return;
    }

    const uint32_t freeHeap = ESP.getFreeHeap();
    if (freeHeap < 60000) {
        return;
    }

    int scaleNum = SCR_W;
    int scaleDen = fb->width;
    if (SCR_H * fb->width < fb->height * SCR_W) {
        scaleNum = SCR_H;
        scaleDen = fb->height;
    }
    if (scaleDen <= 0 || scaleNum <= 0) {
        return;
    }

    int dstW = ((int)fb->width * scaleNum) / scaleDen;
    int dstH = ((int)fb->height * scaleNum) / scaleDen;
    if (scaleNum == SCR_W) {
        dstW = SCR_W;
    } else {
        dstH = SCR_H;
    }
    if (dstW < 2) {
        dstW = 2;
    }
    if (dstH < 2) {
        dstH = 2;
    }
    if (dstW > SCR_W) {
        dstW = SCR_W;
    }
    if (dstH > SCR_H) {
        dstH = SCR_H;
    }

    if (gPreviewBuf == nullptr || gPreviewW != (uint16_t)dstW ||
        gPreviewH != (uint16_t)dstH) {
        if (gPreviewBuf != nullptr) {
            free(gPreviewBuf);
            gPreviewBuf = nullptr;
        }
        gPreviewBuf = (uint8_t *)diagAlloc((size_t)dstW * dstH * 2);
        if (gPreviewBuf == nullptr) {
            Serial.println(F("preview buffer allocation failed"));
            return;
        }
        gPreviewW = (uint16_t)dstW;
        gPreviewH = (uint16_t)dstH;
    }

    uint8_t *dst = gPreviewBuf;
    for (int y = 0; y < dstH; y++) {
        const uint8_t *row =
            fb->buf + ((size_t)y * fb->height / dstH) * fb->width * 2;
        for (int x = 0; x < dstW; x++) {
            const int sx = (x * (int)fb->width / dstW) & ~1;
            const int c = row[sx * 2];
            const int d = row[sx * 2 + 1] - 128;
            const int e = row[sx * 2 + 2] - 128;
            const int yy = row[sx * 2 + 1];
            int r = yy + ((359 * e) >> 8);
            int g = yy - ((88 * d + 183 * e) >> 8);
            int b = yy + ((454 * d) >> 8);
            r = r < 0 ? 0 : (r > 255 ? 255 : r);
            g = g < 0 ? 0 : (g > 255 ? 255 : g);
            b = b < 0 ? 0 : (b > 255 ? 255 : b);
            const uint16_t r5 = (uint16_t)((r & 0xF8) << 8);
            const uint16_t g6 = (uint16_t)((g & 0xFC) << 3);
            const uint16_t b5 = (uint16_t)(b >> 3);
            const uint16_t c565 = (uint16_t)(r5 | g6 | b5);
            dst[0] = (uint8_t)(c565 >> 8);
            dst[1] = (uint8_t)(c565 & 0xFF);
            dst += 2;
        }
    }

    TFT_eSPI &tft = diagTft();
    const int x0 = (SCR_W - dstW) / 2;
    const int y0 = (SCR_H - dstH) / 2;
    tft.startWrite();
    tft.setAddrWindow(x0, y0, (uint32_t)dstW, (uint32_t)dstH);
    tft.pushColors((uint16_t *)(void *)gPreviewBuf, (uint32_t)dstW * dstH, false);
    tft.endWrite();
}

static void cameraTask(void *arg) {
    (void)arg;
    uint32_t lastDiff = 0;
    uint32_t lastLeakCheck = 0;
    bool didInitialCapture = false;

    for (;;) {
        if (!gCameraReady) {
            delay(200);
            continue;
        }

        camera_fb_t *fb = esp_camera_fb_get();
        if (fb == nullptr) {
            delay(100);
            continue;
        }

        const uint8_t page = gPage;

        if (!didInitialCapture) {
            didInitialCapture = true;
            diffRun(fb);
        }

        if (gSnapshotRequest) {
            gSnapshotRequest = false;
            Serial.println(F("snapshot requested"));
            diffRun(fb);
        }

        if (page == PAGE_PREVIEW) {
            if (diagTftLockTake()) {
                pushVideoFrame(fb);
                diagTftLockGive();
            }
        }

        const uint32_t now = millis();
        if (page != PAGE_DISPLAY && now - lastDiff >= 1000) {
            lastDiff = now;
            diffRun(fb);
        }

        esp_camera_fb_return(fb);
        delay(page == PAGE_PREVIEW ? 40 : 250);

        if (now - lastLeakCheck >= 15000) {
            lastLeakCheck = now;
            Serial.printf("cam task: heap=%u psram_free=%u page=%u ready=%d\n",
                          (unsigned)ESP.getFreeHeap(),
                          (unsigned)ESP.getFreePsram(), (unsigned)page,
                          (int)gCameraReady);
        }
    }
}

/* ------------------------------------------------------------------ */
/* LVGL pages                                                         */
/* ------------------------------------------------------------------ */
static lv_obj_t *gInfoLabel = nullptr;

static void buildCameraPage() {
    lv_obj_t *scr = lv_scr_act();
    lv_obj_clean(scr);
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x101820), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

    lv_obj_t *title = lv_label_create(scr);
    lv_label_set_text(title, "摄像头自检 / CAMERA");
    lv_obj_set_style_text_color(title, lv_color_white(), 0);
    lv_obj_set_style_text_font(title, diagUiFont(), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 6);

    gInfoLabel = lv_label_create(scr);
    lv_obj_set_style_text_color(gInfoLabel, lv_color_hex(0x80E0FF), 0);
    lv_obj_set_style_text_font(gInfoLabel, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_line_space(gInfoLabel, 3, 0);
    lv_label_set_text(gInfoLabel, "starting...");
    lv_obj_align(gInfoLabel, LV_ALIGN_TOP_LEFT, 8, 32);
}

static void buildPreviewPage() {
    lv_obj_t *scr = lv_scr_act();
    lv_obj_clean(scr);
    lv_obj_set_style_bg_color(scr, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
}

static void refreshCameraPage() {
    if (gInfoLabel == nullptr) {
        return;
    }
    char text[512];
    snprintf(text, sizeof(text),
             "camera init : %s\n"
             "frames ok    : %u\n"
             "frames lost  : %u\n"
             "heap         : %u B\n"
             "psram free   : %u KB\n"
             "last change  : %u%% of %ux%u\n"
             "last luma    : %u\n"
             "diff samples : %u\n"
             "threshold    : %u%% / pixel %u\n"
             "--- buttons / serial ---\n"
             "BTN1 next page    (1/2/3)\n"
             "BTN2 snapshot     (c)\n"
             "BTN3 camera cycle (r)",
             gCameraReady ? "OK" : "FAILED", (unsigned)camStatus().frames,
             (unsigned)camStatus().failures, (unsigned)ESP.getFreeHeap(),
             (unsigned)(ESP.getFreePsram() / 1024),
             (unsigned)gDiff.lastChangedPct, CAM_DIFF_SAMPLE_W,
             CAM_DIFF_SAMPLE_H, (unsigned)gDiff.lastLuma,
             (unsigned)gDiff.comparisons, CAM_DIFF_CHANGED_RATIO_PCT,
             CAM_DIFF_PIXEL_THRESHOLD);
    lv_label_set_text(gInfoLabel, text);
}

/* ------------------------------------------------------------------ */
/* Display patterns for page 1                                        */
/* ------------------------------------------------------------------ */
static void applyDisplayPattern(uint8_t pattern) {
    lv_obj_t *scr = lv_scr_act();

    if (pattern == 0) {
        lv_obj_clean(scr);
        diagBuildLvglPage();
        lv_obj_invalidate(scr);
        return;
    }

    lv_obj_clean(scr);
    lv_obj_set_style_bg_color(scr, lv_color_black(), 0);
    switch (pattern) {
        case 1:
            diagColorBars();
            break;
        case 2:
            diagGradient();
            break;
        default:
            diagGridRuler();
            break;
    }
    diagTft().setTextDatum(TL_DATUM);
}

static void applyPage(uint8_t page) {
    gPage = page;
    Serial.printf("page -> %u\n", (unsigned)page);

    if (page == PAGE_DISPLAY) {
        camStop();
        applyDisplayPattern(gLvglPattern);
    } else if (page == PAGE_CAMERA) {
        camStop();
        buildCameraPage();
        if (camStartStill()) {
            refreshCameraPage();
        } else {
            gInfoLabel = lv_obj_get_child(lv_scr_act(), 0);
            refreshCameraPage();
        }
    } else {
        camStop();
        buildPreviewPage();
        if (camStartVideo()) {
            gPreviewRunning = true;
        }
    }
}

/* ------------------------------------------------------------------ */
/* Button handling                                                    */
/* ------------------------------------------------------------------ */
static void pollButtons() {
    static bool latched = false;
    static uint32_t lastPoll = 0;

    const uint32_t now = millis();
    if (now - lastPoll < 25) {
        return;
    }
    lastPoll = now;

    const int pressed = diagButtonPressed();
    if (pressed == 0) {
        latched = false;
        return;
    }
    if (latched) {
        return;
    }
    latched = true;

    Serial.printf("BTN%d\n", pressed);

    if (pressed == 1) {
        uint8_t next = (uint8_t)(gPage + 1);
        if (next > PAGE_PREVIEW) {
            next = PAGE_DISPLAY;
        }
        gRequestedPage = next;
    } else if (pressed == 2) {
        if (gPage == PAGE_DISPLAY) {
            gLvglPattern = (uint8_t)((gLvglPattern + 1) % 4);
            applyDisplayPattern(gLvglPattern);
        } else {
            gSnapshotRequest = true;
        }
    } else {
        if (gPage != PAGE_DISPLAY) {
            camStop();
            delay(150);
            if (gPage == PAGE_CAMERA) {
                camStartStill();
            } else {
                camStartVideo();
            }
        } else {
            diagTft().fillScreen(TFT_BLACK);
        }
    }
}

/* ------------------------------------------------------------------ */
/* LVGL task                                                          */
/* ------------------------------------------------------------------ */
static void lvglTask(void *arg) {
    (void)arg;
    uint32_t lastCameraRefresh = 0;

    for (;;) {
        pollButtons();

        if (gRequestedPage != gPage) {
            applyPage(gRequestedPage);
        }

        if (gPage == PAGE_CAMERA) {
            const uint32_t now = millis();
            if (now - lastCameraRefresh >= 500) {
                lastCameraRefresh = now;
                refreshCameraPage();
            }
        }

        lv_timer_handler();
        delay(5);
    }
}

void setup() {
    Serial.begin(115200);
    delay(300);

    diagLogBanner("SmartBookShelf / diag (display + camera)");
    printPsramReport();

    diagButtonsInit();
    Serial.printf("buttons: BTN1=%d BTN2=%d BTN3=%d (active high)\n",
                  BUTTON_PIN1, BUTTON_PIN2, BUTTON_PIN3);

    Serial.println(F("[1] backlight blink"));
    pinMode(PIN_TFT_BL, OUTPUT);
    diagBacklight(false);
    diagFlashBacklight(2);

    Serial.println(F("[2] tft.init() + rotation"));
    diagTft().init();
    diagTft().setRotation(TFT_ROTATION);
    diagTftProbe();

    Serial.println(F("[3] colour bars"));
    diagColorBars();
    delay(600);

    Serial.println(F("[4] LVGL init"));
    if (!diagLvglInit(40)) {
        Serial.println(F("LVGL init failed"));
    }

    diagTftLockInit();

    Serial.println(F("[5] grey detection grid"));
    if (gDiff.gray == nullptr) {
        gDiff.gray = (uint8_t *)diagAlloc((size_t)CAM_DIFF_SAMPLE_W * CAM_DIFF_SAMPLE_H);
        Serial.printf("diff grid %dx%d %s\n", CAM_DIFF_SAMPLE_W, CAM_DIFF_SAMPLE_H,
                      gDiff.gray != nullptr ? "ok" : "FAILED");
    }

    Serial.println(F("[6] camera init"));
    cameraPrintPinMap();
    if (camStartStill()) {
        delay(400);
    } else {
        Serial.println(F("camera init failed - pages 2/3 will stay empty"));
    }

    Serial.println(F("[7] start tasks"));
    xTaskCreatePinnedToCore(cameraTask, "diagCamera", 16384, nullptr, 2, nullptr, 0);
    xTaskCreatePinnedToCore(lvglTask, "diagLvgl", 8192, nullptr, 2, nullptr, 0);

    applyPage(PAGE_DISPLAY);
    Serial.println(F("ready: 1/2/3 pages, n/c/r/t serial commands"));
}

void loop() {
    static uint32_t lastReport = 0;
    const uint32_t now = millis();

    switch (pollSerialCommand()) {
        case '1':
            gRequestedPage = PAGE_DISPLAY;
            break;
        case '2':
            gRequestedPage = PAGE_CAMERA;
            break;
        case '3':
            gRequestedPage = PAGE_PREVIEW;
            break;
        case 'n':
            if (gPage == PAGE_DISPLAY) {
                gLvglPattern = (uint8_t)((gLvglPattern + 1) % 4);
                applyDisplayPattern(gLvglPattern);
            }
            break;
        case 'c':
            gSnapshotRequest = true;
            break;
        case 'r':
            camStop();
            delay(200);
            if (gPage == PAGE_PREVIEW) {
                camStartVideo();
            } else {
                camStartStill();
            }
            break;
        case 't': {
            if (!gCameraReady) {
                Serial.println(F("camera not ready"));
                break;
            }
            camera_fb_t *fb = esp_camera_fb_get();
            if (fb != nullptr) {
                diffRun(fb);
                esp_camera_fb_return(fb);
            }
            break;
        }
        default:
            break;
    }

    if (now - lastReport >= 5000) {
        lastReport = now;
        Serial.printf("alive page=%u heap=%u psram_free=%u frames=%lu refusals=%lu "
                      "diff=%lu%%\n",
                      (unsigned)gPage, (unsigned)ESP.getFreeHeap(),
                      (unsigned)ESP.getFreePsram(),
                      (unsigned long)camStatus().frames,
                      (unsigned long)camStatus().failures,
                      (unsigned long)gDiff.lastChangedPct);
    }
    delay(10);
}
