#ifndef PROJECT_DIAG_CAMERA_H
#define PROJECT_DIAG_CAMERA_H

#include <Arduino.h>

#include "board_pins.h"
#include "camera_config.h"
#include "diag_common.h"
#include "esp_camera.h"
#include "esp_err.h"

#ifndef CAM_PREVIEW_FRAME_SIZE
#define CAM_PREVIEW_FRAME_SIZE FRAMESIZE_VGA
#endif
#ifndef CAM_PREVIEW_QUALITY
#define CAM_PREVIEW_QUALITY 20
#endif

#ifndef CAM_PIN_D0
#define CAM_PIN_D0 PIN_CAM_D0
#endif
#ifndef CAM_PIN_D1
#define CAM_PIN_D1 PIN_CAM_D1
#endif
#ifndef CAM_PIN_D2
#define CAM_PIN_D2 PIN_CAM_D2
#endif
#ifndef CAM_PIN_D3
#define CAM_PIN_D3 PIN_CAM_D3
#endif
#ifndef CAM_PIN_D4
#define CAM_PIN_D4 PIN_CAM_D4
#endif
#ifndef CAM_PIN_D5
#define CAM_PIN_D5 PIN_CAM_D5
#endif
#ifndef CAM_PIN_D6
#define CAM_PIN_D6 PIN_CAM_D6
#endif
#ifndef CAM_PIN_D7
#define CAM_PIN_D7 PIN_CAM_D7
#endif

struct CamStatus {
    bool inited;
    bool lastOk;
    esp_err_t lastErr;
    uint32_t frames;
    uint32_t failures;
    uint32_t lastBytes;
    uint32_t lastWidth;
    uint32_t lastHeight;
    uint32_t lastFetchUs;
    uint32_t sinceLastFrameMs;
    bool lastWasJpeg;
};

inline CamStatus &camStatus() {
    static CamStatus status = {};
    return status;
}

inline void camStatusNoteFrame(const camera_fb_t *fb, uint32_t fetchUs) {
    CamStatus &s = camStatus();
    if (fb == nullptr) {
        s.lastOk = false;
        s.lastErr = ESP_FAIL;
        s.failures++;
        return;
    }
    s.lastOk = true;
    s.lastErr = ESP_OK;
    s.frames++;
    s.lastBytes = (uint32_t)fb->len;
    s.lastWidth = fb->width;
    s.lastHeight = fb->height;
    s.lastFetchUs = fetchUs;
    s.lastWasJpeg = (fb->format == PIXFORMAT_JPEG);
    s.sinceLastFrameMs = millis();
}

inline const char *cameraErrName(esp_err_t err) {    switch (err) {
        case ESP_OK:
            return "ESP_OK";
        case ESP_ERR_NO_MEM:
            return "ESP_ERR_NO_MEM";
        case ESP_ERR_INVALID_ARG:
            return "ESP_ERR_INVALID_ARG";
        case ESP_ERR_INVALID_STATE:
            return "ESP_ERR_INVALID_STATE";
        case ESP_ERR_NOT_SUPPORTED:
            return "ESP_ERR_NOT_SUPPORTED";
        case ESP_ERR_TIMEOUT:
            return "ESP_ERR_TIMEOUT";
        case ESP_FAIL:
            return "ESP_FAIL (sensor probe / SCCB read failed)";
        default:
            return "unknown";
    }
}

/* Raw pin numbers are non-const globals in esp32-camera, so they are
 * assigned at run time instead of in a static initialiser. */
inline void cameraCaptureConfig(camera_config_t &cfg) {
    memset(&cfg, 0, sizeof(cfg));

    cfg.pin_pwdn = PIN_CAM_PWDN;
    cfg.pin_reset = PIN_CAM_RESET;
    cfg.pin_xclk = PIN_CAM_XCLK;
    cfg.pin_sccb_sda = PIN_CAM_SIOD;
    cfg.pin_sccb_scl = PIN_CAM_SIOC;
    cfg.pin_d7 = CAM_PIN_D7;
    cfg.pin_d6 = CAM_PIN_D6;
    cfg.pin_d5 = CAM_PIN_D5;
    cfg.pin_d4 = CAM_PIN_D4;
    cfg.pin_d3 = CAM_PIN_D3;
    cfg.pin_d2 = CAM_PIN_D2;
    cfg.pin_d1 = CAM_PIN_D1;
    cfg.pin_d0 = CAM_PIN_D0;
    cfg.pin_vsync = PIN_CAM_VSYNC;
    cfg.pin_href = PIN_CAM_HREF;
    cfg.pin_pclk = PIN_CAM_PCLK;

    cfg.xclk_freq_hz = CAM_XCLK_FREQUENCY;
    cfg.ledc_timer = LEDC_TIMER_0;
    cfg.ledc_channel = LEDC_CHANNEL_0;
    cfg.pixel_format = PIXFORMAT_JPEG;
    cfg.frame_size = CAM_CAPTURE_FRAME_SIZE;
    cfg.jpeg_quality = CAM_CAPTURE_QUALITY;
    cfg.fb_count = CAM_FB_COUNT;
    cfg.fb_location = CAMERA_FB_IN_PSRAM;
    cfg.grab_mode = CAMERA_GRAB_LATEST;
}

inline void cameraPreviewConfig(camera_config_t &cfg) {
    cameraCaptureConfig(cfg);
    cfg.pixel_format = PIXFORMAT_YUV422;
    cfg.frame_size = CAM_PREVIEW_FRAME_SIZE;
    cfg.jpeg_quality = CAM_PREVIEW_QUALITY;
}

inline void cameraPrintPinMap() {
    Serial.println(F("--- OV2640 DVP pin map ------------------------------------"));
    Serial.printf("  XCLK=%-3d PCLK=%-3d VSYNC=%-3d HREF=%-3d\n", PIN_CAM_XCLK,
                  PIN_CAM_PCLK, PIN_CAM_VSYNC, PIN_CAM_HREF);
    Serial.printf("  SIOD=%-3d SIOC=%-3d  (SCCB)\n", PIN_CAM_SIOD, PIN_CAM_SIOC);
    Serial.printf("  D0=%-3d D1=%-3d D2=%-3d D3=%-3d\n", CAM_PIN_D0, CAM_PIN_D1,
                  CAM_PIN_D2, CAM_PIN_D3);
    Serial.printf("  D4=%-3d D5=%-3d D6=%-3d D7=%-3d\n", CAM_PIN_D4, CAM_PIN_D5,
                  CAM_PIN_D6, CAM_PIN_D7);
    Serial.printf("  RESET=%d PWDN=%d (unused when -1)\n", PIN_CAM_RESET,
                  PIN_CAM_PWDN);
    Serial.println(F("-----------------------------------------------------------"));
}

inline bool cameraStart(camera_config_t &cfg) {
    if (cfg.fb_location == CAMERA_FB_IN_PSRAM && !psramFound()) {
        Serial.println(F("camera: no PSRAM, falling back to DRAM framebuffers"));
        cfg.fb_location = CAMERA_FB_IN_DRAM;
        cfg.fb_count = 1;
    }

    const esp_err_t err = esp_camera_init(&cfg);
    Serial.printf("esp_camera_init -> %d (%s)\n", (int)err, cameraErrName(err));
    if (err != ESP_OK) {
        return false;
    }

    sensor_t *sensor = esp_camera_sensor_get();
    if (sensor == nullptr) {
        Serial.println(F("camera: esp_camera_sensor_get() returned NULL"));
        return false;
    }
    Serial.printf("sensor pid=0x%02X ver=0x%02X midh=0x%02X midl=0x%02X\n",
                  sensor->id.PID, sensor->id.VER, sensor->id.MIDH,
                  sensor->id.MIDL);
    Serial.printf("sensor set: brightness=%d contrast=%d saturation=%d\n",
                  sensor->status.brightness, sensor->status.contrast,
                  sensor->status.saturation);
    return true;
}

inline void cameraPrintAdjustments(const camera_config_t &cfg) {
    sensor_t *sensor = esp_camera_sensor_get();
    if (sensor == nullptr) {
        return;
    }
    if (cfg.pixel_format == PIXFORMAT_YUV422) {
        sensor->set_vflip(sensor, 1);
        sensor->set_hmirror(sensor, 0);
        sensor->set_brightness(sensor, 0);
    } else {
        sensor->set_vflip(sensor, 0);
        sensor->set_hmirror(sensor, 0);
    }
}

/* ------------------------------------------------------------------ */
/* YUV422 (UYVY) -> RGB565, with optional integer down/up scaling      */
/* ------------------------------------------------------------------ */
inline uint16_t diagRgbTo565(uint8_t r, uint8_t g, uint8_t b) {
    return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

#endif
