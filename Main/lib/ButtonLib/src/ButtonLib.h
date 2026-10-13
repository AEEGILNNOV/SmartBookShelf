#ifndef BUTTON_LIB_H
#define BUTTON_LIB_H

#include <Arduino.h>

#include "BoardConfig.h"

// =============================================================================
// ButtonLib —— 按键库
//
// 职责：只做“读引脚 + 消抖 + 发事件”，不关心事件拿去干什么。
//
// 消抖用移位历史（和主工程习惯一致）：
//   history <<= 1; history |= (读到有效电平) ? 1 : 0;
//   低 4 位全 1 判“按下”，全 0 判“松开”。
//
// 用法：
//   ButtonLib buttons;
//   buttons.begin();                       // 建队列、配引脚
//   buttons.startTask();                   // 建 buttonTask（或自己在任务里调 poll()）
//   ButtonEvent event;
//   if (xQueueReceive(buttons.queue(), &event, pdMS_TO_TICKS(10)) == pdPASS) { ... }
// =============================================================================

// 按键事件标志（沿用主工程宏命名）
#define BUTTON1_OPENFLAG 0x01   // 按键 1 按下
#define BUTTON2_OPENFLAG 0x02   // 按键 2 按下
#define BUTTON3_OPENFLAG 0x03   // 按键 3 按下
#define BUTTON1_CLOSEFLAG 0x04  // 按键 1 松开
#define BUTTON2_CLOSEFLAG 0x05  // 按键 2 松开
#define BUTTON3_CLOSEFLAG 0x06  // 按键 3 松开

// 按键事件
struct ButtonEvent {
    uint8_t flag;      // 上面的 BUTTONx_xxxFLAG
    uint8_t index;     // 按键编号 1~3
    uint8_t pressed;   // 1 = 按下, 0 = 松开
    uint32_t ms;       // 事件时间戳
};

class ButtonLib {
public:
    // pin1~pin3 = -1 表示不接这个键
    ButtonLib(int pin1 = BUTTON_PIN1, int pin2 = BUTTON_PIN2,
              int pin3 = BUTTON_PIN3);

    // 配引脚 + 建事件队列（长度 BUTTON_QUEUE_SIZE）
    bool begin();

    // 建按键任务（周期 5ms，核心 0）
    bool startTask();

    // 手动轮询一次（不建任务时用这个）
    void poll();

    // 最近一次事件（xQueueReceive 用）
    QueueHandle_t queue() const;

    // 有没有按键被按下（读引脚原始值，不消抖）
    bool anyPressed() const;

private:
    int _pins[3];
    uint8_t _history[3];      // 移位历史
    uint8_t _lastFlag[3];     // 上一次发出去的状态（防止重复发）
    QueueHandle_t _queue;
    bool _activeHigh;

    void emitIfChanged(uint8_t index);
    static void buttonTaskEntry(void *pvParameters);
};

const char *buttonLibVersion();

#endif
