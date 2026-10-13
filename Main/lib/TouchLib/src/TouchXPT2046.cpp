#include "TouchXPT2046.h"

// 引脚 / 原始值窗口 / 采样次数都在 config.h 第 3.2 节
#include "config.h"

// =============================================================================
// TouchLib 实现（XPT2046，位操作，独立 5 根线）
//
// 时序（和参考工程 Touch_XPT2046.hpp 完全一致，不要随手改）：
//   1) 拉低 CS，按位移出 8 位控制字节（0xD0 读 X / 0x90 读 Y），MSB 先出；
//   2) 等 6us（XPT2046 需要一段转换时间），再给一个空时钟；
//   3) 读 12 位结果（MSB 先出）：先给低电平再拉高，在上升沿后采 DO；
//   4) 拉高 CS 结束。
// 每个轴采 CFG_TOUCH_SAMPLES 次取平均，压一下抖动。
// =============================================================================

TouchXPT2046::TouchXPT2046() {
    _cs = -1;
    _irq = -1;
    _clk = -1;
    _din = -1;
    _miso = -1;
}

void TouchXPT2046::begin(int cs, int irq, int clk, int din, int miso) {
    _cs = cs;
    _irq = irq;
    _clk = clk;
    _din = din;
    _miso = miso;

    pinMode(_cs, OUTPUT);
    digitalWrite(_cs, HIGH);           // 片选先放开
    pinMode(_clk, OUTPUT);
    digitalWrite(_clk, LOW);
    pinMode(_din, OUTPUT);
    digitalWrite(_din, LOW);
    pinMode(_miso, INPUT_PULLUP);      // 触摸数据回来，XPT2046 是开漏/需要上拉

    if (_irq >= 0) {
        pinMode(_irq, INPUT_PULLUP);   // 本工程用轮询，这个脚只是备用
    }
}

// 读一个轴的原始值
uint16_t TouchXPT2046::readAxis(uint8_t cmd) {
    digitalWrite(_clk, LOW);
    digitalWrite(_din, LOW);
    digitalWrite(_cs, LOW);

    // 1) 8 位控制字节
    for (int i = 0; i < 8; i++) {
        digitalWrite(_din, (cmd & 0x80) ? HIGH : LOW);
        cmd <<= 1;
        digitalWrite(_clk, LOW);
        digitalWrite(_clk, HIGH);
    }

    // 2) 转换等待 + 一个空时钟
    delayMicroseconds(6);
    digitalWrite(_clk, LOW);
    delayMicroseconds(1);
    digitalWrite(_clk, HIGH);
    digitalWrite(_clk, LOW);

    // 3) 12 位结果
    uint16_t value = 0;
    for (int i = 0; i < 12; i++) {
        digitalWrite(_clk, LOW);
        digitalWrite(_clk, HIGH);
        value = (uint16_t)((value << 1) | (digitalRead(_miso) ? 1 : 0));
    }

    digitalWrite(_cs, HIGH);
    return value;
}

// 只读原始值，不做窗口过滤（排线/查芯片有没有回数据用）
void TouchXPT2046::readRaw(uint16_t *x, uint16_t *y) {
    if (_cs < 0) {
        *x = 0;
        *y = 0;
        return;
    }

    uint32_t xs = 0;
    uint32_t ys = 0;
    const int samples = CFG_TOUCH_SAMPLES;

    for (int i = 0; i < samples; i++) {
        xs += readAxis(0xD0);  // X
        ys += readAxis(0x90);  // Y
    }

    *x = (uint16_t)(xs / (uint32_t)samples);
    *y = (uint16_t)(ys / (uint32_t)samples);
}

// T_IRQ 电平：-1 没接，0 LOW（按下），1 HIGH（空闲）
int TouchXPT2046::irqLevel() {
    if (_irq < 0) {
        return -1;
    }
    return digitalRead(_irq) == LOW ? 0 : 1;
}

// 读一次触点：多采几次取平均，再按有效窗口过滤“其实没人按”的情况
bool TouchXPT2046::read(uint16_t *x, uint16_t *y) {
    if (_cs < 0) {
        return false;
    }

    uint16_t xa = 0;
    uint16_t ya = 0;
    readRaw(&xa, &ya);

    // 原始值落在窗口外当作“没按”（窗口在 config.h 里调）
    if (xa < CFG_TOUCH_RAW_MIN || xa > CFG_TOUCH_RAW_MAX ||
        ya < CFG_TOUCH_RAW_MIN || ya > CFG_TOUCH_RAW_MAX) {
        return false;
    }

    *x = xa;
    *y = ya;
    return true;
}

void TouchXPT2046::printConfig() const {
    Serial.printf("触摸(XPT2046): T_CS=%d T_IRQ=%d T_CLK=%d T_DIN=%d T_DO=%d | "
                  "原始窗口 %d~%d, 每轴采样 %d 次\n",
                  _cs, _irq, _clk, _din, _miso, CFG_TOUCH_RAW_MIN,
                  CFG_TOUCH_RAW_MAX, CFG_TOUCH_SAMPLES);
    Serial.printf("  轴方向: SWAP_XY=%d INVERT_X=%d INVERT_Y=%d "
                  "(不对就改 config.h 第 3.2 节的这三个宏)\n",
                  CFG_TOUCH_SWAP_XY, CFG_TOUCH_INVERT_X, CFG_TOUCH_INVERT_Y);
}

const char *touchLibVersion() {
    return TOUCHLIB_VERSION;
}
