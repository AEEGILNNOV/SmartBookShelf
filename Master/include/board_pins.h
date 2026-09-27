#ifndef PROJECT_BOARD_PINS_H
#define PROJECT_BOARD_PINS_H

/*
 * ESP32-S3 N16R8 pin map for the "速定位书格" (SmartBookShelf) master board.
 *
 * Usable GPIOs on this module are only 0-21 and 38-48 (33 pins).
 * Never connect anything to:
 *   GPIO 26-32  in-package SPI flash
 *   GPIO 33-37  in-package octal PSRAM (N16R8 = 8 MB OPI PSRAM)
 *   GPIO 22-25  do not exist
 *   GPIO 19/20  native USB D-/D+ (keep free for flashing/monitoring)
 *   GPIO 43/44  UART0 console
 *
 * Allocation (no pin is used twice):
 *   8-12,47    ILI9341
 *   13         microSD chip select (shares SCLK/MOSI with the panel)
 *   4,5        HC-SR04 trig/echo
 *   6,7        UART link to the ESP32-C3 stepper controller
 *   1,2,3,14,15,16,17,18,21,38,39,48   OV2640 (SCCB + DVP + control)
 *   40,41,42   buttons
 *
 * Every pin can be overridden from platformio.ini, e.g.
 *   build_flags = -DPIN_SD_CS=13 -DBUTTON_PIN2=46
 */

/* ------------------------------------------------------------------ */
/* Display: ILI9341 240x320 SPI, portrait (rotation 2)                */
/* ------------------------------------------------------------------ */
#ifndef PIN_TFT_SCLK
#define PIN_TFT_SCLK 12
#endif
#ifndef PIN_TFT_MOSI
#define PIN_TFT_MOSI 11
#endif
#ifndef PIN_TFT_MISO
#define PIN_TFT_MISO -1
#endif
#ifndef PIN_TFT_CS
#define PIN_TFT_CS 10
#endif
#ifndef PIN_TFT_DC
#define PIN_TFT_DC 9
#endif
#ifndef PIN_TFT_RST
#define PIN_TFT_RST 8
#endif
#ifndef PIN_TFT_BL
#define PIN_TFT_BL 47
#endif

/* ------------------------------------------------------------------ */
/* microSD: SPI, shares SCLK/MOSI with the display, own chip select   */
/* ------------------------------------------------------------------ */
#ifndef PIN_SD_CS
#define PIN_SD_CS 13
#endif
#ifndef SD_SPI_FREQUENCY
#define SD_SPI_FREQUENCY 20000000
#endif

/* ------------------------------------------------------------------ */
/* Buttons: active high, external pull-down (BUTTON_TRUE_VALUE = 1)   */
/* ------------------------------------------------------------------ */
#ifndef BUTTON_PIN1
#define BUTTON_PIN1 42
#endif
#ifndef BUTTON_PIN2
#define BUTTON_PIN2 41
#endif
#ifndef BUTTON_PIN3
#define BUTTON_PIN3 40
#endif
#ifndef BUTTON_TRUE_VALUE
#define BUTTON_TRUE_VALUE 1
#endif

/* ------------------------------------------------------------------ */
/* HC-SR04 ultrasonic rangefinder: 5V module, Echo needs a divider    */
/* ------------------------------------------------------------------ */
#ifndef PIN_US_TRIG
#define PIN_US_TRIG 4
#endif
#ifndef PIN_US_ECHO
#define PIN_US_ECHO 5
#endif

/* ------------------------------------------------------------------ */
/* UART link to the ESP32-C3 Super Mini (stepper controller)          */
/* ------------------------------------------------------------------ */
#ifndef PIN_LINK_TX
#define PIN_LINK_TX 6
#endif
#ifndef PIN_LINK_RX
#define PIN_LINK_RX 7
#endif

/* ------------------------------------------------------------------ */
/* OV2640: ESP32-S3 8-bit parallel DVP                                */
/* Usual breakouts have no PWDN/RESET pin, so both stay -1.           */
/* ------------------------------------------------------------------ */
#ifndef PIN_CAM_XCLK
#define PIN_CAM_XCLK 15
#endif
#ifndef PIN_CAM_PCLK
#define PIN_CAM_PCLK 16
#endif
#ifndef PIN_CAM_VSYNC
#define PIN_CAM_VSYNC 17
#endif
#ifndef PIN_CAM_HREF
#define PIN_CAM_HREF 18
#endif
#ifndef PIN_CAM_SIOD
#define PIN_CAM_SIOD 1
#endif
#ifndef PIN_CAM_SIOC
#define PIN_CAM_SIOC 48
#endif
#ifndef PIN_CAM_D0
#define PIN_CAM_D0 3
#endif
#ifndef PIN_CAM_D1
#define PIN_CAM_D1 21
#endif
#ifndef PIN_CAM_D2
#define PIN_CAM_D2 39
#endif
#ifndef PIN_CAM_D3
#define PIN_CAM_D3 14
#endif
#ifndef PIN_CAM_D4
#define PIN_CAM_D4 38
#endif
#ifndef PIN_CAM_D5
#define PIN_CAM_D5 40
#endif
#ifndef PIN_CAM_D6
#define PIN_CAM_D6 41
#endif
#ifndef PIN_CAM_D7
#define PIN_CAM_D7 42
#endif
#ifndef PIN_CAM_RESET
#define PIN_CAM_RESET -1
#endif
#ifndef PIN_CAM_PWDN
#define PIN_CAM_PWDN -1
#endif

/*
 * NOTE: the camera data lines D5/D6/D7 use GPIO 40/41/42, which are the
 * three button pins. Bring-up order therefore is:
 *   1. panel + camera only            -> keep buttons disconnected
 *   2. move the buttons (for example -DBUTTON_PIN1=45 -DBUTTON_PIN2=46
 *      -DBUTTON_PIN3=0) or move D5/D6/D7 to free pins of your board.
 */

#endif
