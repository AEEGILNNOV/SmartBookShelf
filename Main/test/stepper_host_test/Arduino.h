#ifndef ARDUINO_H
#define ARDUINO_H

// =============================================================================
// 本地测试用的 Arduino 桩（只在 Main/test/stepper_host_test 里用，不进固件）
// 目的：让 StepperLib.cpp 不做任何修改就能在 PC 上编译运行，
//       用来验证“拍序 / 方向 / 步数 / 软限位”这些纯逻辑，不需要接线。
// =============================================================================

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define HIGH 1
#define LOW  0
#define OUTPUT 1
#define INPUT  0

// 记录每个引脚最后一次被写成的电平，测试靠它还原拍序
#define MOCK_MAX_PINS 64
extern int mockPinLevel[MOCK_MAX_PINS];

void pinMode(uint8_t pin, uint8_t mode);
void digitalWrite(uint8_t pin, uint8_t value);
int digitalRead(uint8_t pin);

// 时间：mockMicros 由测试推进，delayMicroseconds 会推进时间
extern uint32_t mockMicros;
uint32_t micros(void);
void delayMicroseconds(uint32_t us);
void delay(uint32_t ms);

// 测试用的小工具
void mockReset(void);

#endif
