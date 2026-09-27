#ifndef PROJECT_DIAG_COMMON_H
#define PROJECT_DIAG_COMMON_H

#include <Arduino.h>

#include "board_pins.h"

/* ------------------------------------------------------------------ */
/* Display geometry is fixed by the panel; keep the names independent  */
/* from TFT_eSPI so the shared helpers compile in every environment.   */
/* ------------------------------------------------------------------ */
#ifndef SCR_W
#define SCR_W 240
#endif
#ifndef SCR_H
#define SCR_H 320
#endif

inline void diagLogBanner(const char *title) {
    Serial.println();
    Serial.println(F("====================================================="));
    Serial.println(title);
    Serial.println(F("====================================================="));
}

inline void printPsramReport() {
    Serial.printf("chip       : %s  cores=%d  freq=%u MHz\n",
                  ESP.getChipModel(), (int)ESP.getChipCores(),
                  (unsigned)(ESP.getCpuFreqMHz()));
    Serial.printf("flash      : %u bytes (%u MB)\n", (unsigned)ESP.getFlashChipSize(),
                  (unsigned)(ESP.getFlashChipSize() / (1024U * 1024U)));
    Serial.printf("psram found: %s  size=%u bytes (%u MB)\n",
                  psramFound() ? "YES" : "NO",
                  (unsigned)ESP.getPsramSize(),
                  (unsigned)(ESP.getPsramSize() / (1024U * 1024U)));
    Serial.printf("heap       : free=%u  total=%u\n", (unsigned)ESP.getFreeHeap(),
                  (unsigned)ESP.getHeapSize());
}

inline void *diagAlloc(size_t bytes) {
    void *p = psramFound() ? ps_malloc(bytes) : nullptr;
    if (p == nullptr) {
        p = malloc(bytes);
    }
    if (p != nullptr) {
        memset(p, 0, bytes);
    }
    return p;
}

/* ------------------------------------------------------------------ */
/* Keyboard-free control: trigger / echo on the HC-SR04 pins           */
/* ------------------------------------------------------------------ */
inline void triggerPinsInit() {
    pinMode(PIN_US_TRIG, OUTPUT);
    digitalWrite(PIN_US_TRIG, LOW);
    pinMode(PIN_US_ECHO, INPUT);
}

inline uint32_t triggerDistanceMillimeters(uint32_t timeoutUs = 30000UL) {
    digitalWrite(PIN_US_TRIG, LOW);
    delayMicroseconds(3);
    digitalWrite(PIN_US_TRIG, HIGH);
    delayMicroseconds(10);
    digitalWrite(PIN_US_TRIG, LOW);

    uint32_t durationUs = pulseIn(PIN_US_ECHO, HIGH, timeoutUs);
    if (durationUs == 0UL) {
        return 0UL;
    }
    return (durationUs * 343UL) / 2000UL;
}

/* ------------------------------------------------------------------ */
/* Serial command source for the diagnostics                           */
/* ------------------------------------------------------------------ */
inline char pollSerialCommand() {
    if (Serial.available() <= 0) {
        return 0;
    }
    char c = (char)Serial.read();
    if (c >= 'A' && c <= 'Z') {
        c = (char)(c - 'A' + 'a');
    }
    return c;
}

#endif
