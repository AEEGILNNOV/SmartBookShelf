/*
 * diag_camera: bring-up firmware for the OV2640 on the ESP32-S3 DVP port.
 * No display, no LVGL - only the camera, printed to the serial monitor.
 *
 * Serial commands: c = one JPEG capture, y = one YUV422 capture,
 *                  s = 10 JPEG captures, r = restart driver around latest frame
 */

#include <Arduino.h>

#include "diag_common.h"
#include "diag_camera.h"
#include "esp_camera.h"

static camera_config_t camCfg;

static void printFrameInfo(const camera_fb_t *fb) {
    Serial.printf("  fb: %ux%u format=%d len=%u bytes", fb->width, fb->height,
                  (int)fb->format, (unsigned)fb->len);
    size_t limit = fb->len < 24 ? fb->len : 24;
    Serial.print(" data=");
    for (size_t i = 0; i < limit; i++) {
        Serial.printf("%02X", fb->buf[i]);
    }
    Serial.println();
}

static void captureJpegOnce() {
    const uint32_t t0 = micros();
    camera_fb_t *fb = esp_camera_fb_get();
    const uint32_t fetchUs = micros() - t0;

    if (fb == nullptr) {
        Serial.println(F("esp_camera_fb_get() -> NULL (capture failed)"));
        return;
    }
    Serial.printf("JPEG capture ok, %lu us\n", (unsigned long)fetchUs);
    printFrameInfo(fb);

    /* Prove that a JPEG frame can be re-encoded on the fly: this is the path
     * the thumbnail writer will use later on. */
    uint8_t *out = nullptr;
    size_t outLen = 0;
    const uint32_t t1 = millis();
    if (frame2jpg(fb, 80, &out, &outLen) && out != nullptr) {
        Serial.printf("  frame2jpg re-encode: %u bytes in %lu ms\n",
                      (unsigned)outLen, (unsigned long)(millis() - t1));
        free(out);
    } else {
        Serial.println(F("  frame2jpg re-encode failed"));
    }

    esp_camera_fb_return(fb);
}

static void captureYuvOnce() {
    camera_config_t cfg;
    cameraPreviewConfig(cfg);
    cfg.fb_count = 1;
    cfg.grab_mode = CAMERA_GRAB_WHEN_EMPTY;

    if (!cameraStart(cfg)) {
        Serial.println(F("YUV422 pipeline could not start"));
        return;
    }
    cameraPrintAdjustments(cfg);

    camera_fb_t *fb = esp_camera_fb_get();
    if (fb == nullptr) {
        Serial.println(F("YUV422 capture failed"));
    } else {
        printFrameInfo(fb);
        esp_camera_fb_return(fb);
    }

    esp_camera_deinit();
}

void setup() {
    Serial.begin(115200);
    delay(300);

    diagLogBanner("SmartBookShelf / diag_camera (OV2640)");
    printPsramReport();
    cameraPrintPinMap();

    cameraCaptureConfig(camCfg);
    Serial.printf("requesting: %ux%u JPEG q=%d fb_count=%d fb_location=%d\n",
                  (unsigned)resolution[camCfg.frame_size].width,
                  (unsigned)resolution[camCfg.frame_size].height,
                  camCfg.jpeg_quality, camCfg.fb_count,
                  (int)camCfg.fb_location);

    if (!cameraStart(camCfg)) {
        Serial.println(F("CAMERA INIT FAILED."));
        Serial.println(F("checklist: 3.3V supply, GND, SIOD/SIOC pull-ups,"));
        Serial.println(F("           and the DVP map printed above."));
        return;
    }
    cameraPrintAdjustments(camCfg);

    delay(500);
    Serial.println(F("initial JPEG capture:"));
    captureJpegOnce();
    Serial.println(F("ready: c=1 JPEG, y=1 YUV422, s=10 JPEG, r=driver cycle"));
}

void loop() {
    static uint32_t frames = 0;
    static uint32_t failures = 0;
    static uint32_t lastReport = 0;

    switch (pollSerialCommand()) {
        case 'c':
            captureJpegOnce();
            break;
        case 'y':
            captureYuvOnce();
            break;
        case 's':
            for (int i = 0; i < 10; i++) {
                const uint32_t t0 = millis();
                camera_fb_t *fb = esp_camera_fb_get();
                if (fb == nullptr) {
                    failures++;
                    Serial.printf("shot %d: FAILED\n", i);
                } else {
                    Serial.printf("shot %d: %ux%u %u bytes in %lu ms\n", i,
                                  fb->width, fb->height, (unsigned)fb->len,
                                  (unsigned long)(millis() - t0));
                    frames++;
                    esp_camera_fb_return(fb);
                }
            }
            break;
        case 'r': {
            Serial.println(F("deinit / init camera"));
            esp_camera_deinit();
            delay(200);
            cameraCaptureConfig(camCfg);
            cameraStart(camCfg);
            cameraPrintAdjustments(camCfg);
            break;
        }
        default:
            break;
    }

    const uint32_t now = millis();
    if (now - lastReport >= 5000) {
        lastReport = now;
        Serial.printf("alive uptime=%lus heap=%u psram_free=%u ok=%lu fail=%lu\n",
                      (unsigned long)(now / 1000UL), (unsigned)ESP.getFreeHeap(),
                      (unsigned)ESP.getFreePsram(), (unsigned long)frames,
                      (unsigned long)failures);
    }
    delay(20);
}
