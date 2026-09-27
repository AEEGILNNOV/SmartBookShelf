#ifndef PROJECT_TFT_USER_SETUP_H
#define PROJECT_TFT_USER_SETUP_H

#include "board_pins.h"

#define USER_SETUP_LOADED
#define USER_SETUP_INFO "ILI9341 240x320 SPI (no touch)"

#define ILI9341_DRIVER

#ifndef TFT_WIDTH
#define TFT_WIDTH 240
#endif
#ifndef TFT_HEIGHT
#define TFT_HEIGHT 320
#endif

#ifndef TFT_MOSI
#define TFT_MOSI PIN_TFT_MOSI
#endif
#ifndef TFT_SCLK
#define TFT_SCLK PIN_TFT_SCLK
#endif
#ifndef TFT_MISO
#define TFT_MISO PIN_TFT_MISO
#endif
#ifndef TFT_CS
#define TFT_CS PIN_TFT_CS
#endif
#ifndef TFT_DC
#define TFT_DC PIN_TFT_DC
#endif
#ifndef TFT_RST
#define TFT_RST PIN_TFT_RST
#endif
#ifndef TFT_BL
#define TFT_BL PIN_TFT_BL
#endif
#ifndef TFT_BACKLIGHT_ON
#define TFT_BACKLIGHT_ON 1
#endif

/* 27 MHz is the value already proven on this board + module combination. */
#ifndef SPI_FREQUENCY
#define SPI_FREQUENCY 27000000
#endif
#ifndef SPI_READ_FREQUENCY
#define SPI_READ_FREQUENCY 16000000
#endif

/* TFT_eSPI 2.5.43 mistakes FSPI (SPI2) for register 0 on ESP32-S3,
 * so the real SPI index has to be forced. */
#define USE_FSPI_PORT

#endif
