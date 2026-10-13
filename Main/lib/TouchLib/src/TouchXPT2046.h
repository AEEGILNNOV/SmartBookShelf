#ifndef TOUCH_LIB_H
#define TOUCH_LIB_H

#include <Arduino.h>

#include "BoardConfig.h"

// =============================================================================
// TouchLib —— XPT2046 电阻触摸（**独立 5 根线**的位操作驱动）
//
// 来源：LVGLin240320TFTLCD / esp32mouse 参考工程里的 Touch_XPT2046.hpp。
//       那套接线和驱动已经在硬件上跑通，这里只做了两件事：
//         1) 按本工程规范拆成 .h/.cpp（原来是一个 .hpp 全塞在一个类里）；
//         2) 引脚和采样次数等参数改走 config.h（原来是 main.cpp 里的 #define）。
//
// ⚠️ 本驱动**不用**屏的 SPI（屏的 MISO 在本方案里就没接），
//    也**不用** TFT_eSPI 的 TOUCH_CS —— 触摸的 T_CLK/T_DIN/T_DO 是独立三根线，
//    T_CS 单独片选，全部软件位操作。所以 platformio.ini 不用为触摸改任何宏。
//
// 接线（与参考工程一致，见 config.h 第 3.2 节）：
//     T_CS=38   T_IRQ=39（可不接）   T_CLK=40   T_DIN=41   T_DO=42
//
// 线程安全：只允许一个任务调用（本工程只有 displayTask 里的 LVGL 输入设备回调会读）。
// =============================================================================

class TouchXPT2046 {
public:
    TouchXPT2046();

    // 传 -1 表示这根线不接（T_IRQ 允许不接；其余四根必须给）
    void begin(int cs, int irq, int clk, int din, int miso);

    // 读一次触点：返回 true = 落在有效窗口内，x/y 是**原始值**（还要换算成屏幕坐标）
    bool read(uint16_t *x, uint16_t *y);

    // 只读原始值、**不做窗口过滤**：查接线/看芯片到底有没有回数据时用它。
    // 全 4095（0xFFF）= T_DO 悬空被上拉（芯片没回数据）；全 0 = 数据线被拉低或芯片没响应。
    void readRaw(uint16_t *x, uint16_t *y);

    // T_IRQ 电平：-1 = 没接这个脚；0 = LOW（有触摸，XPT2046 是低有效）；1 = HIGH（空闲）。
    // 手指按住屏幕时它会变 0，是判断“触摸模块有没有供电/接线”最快的一招。
    int irqLevel();

    // 是否已经初始化过（begin 给了有效 T_CS）
    bool ready() const { return _cs >= 0; }

    // 打印接线与参数（串口排错用）
    void printConfig() const;

private:
    // 读一个轴的原始值：cmd = 0xD0 读 X，0x90 读 Y（XPT2046 的控制字节）
    uint16_t readAxis(uint8_t cmd);

    int _cs;
    int _irq;
    int _clk;
    int _din;
    int _miso;
};

const char *touchLibVersion();

#endif
