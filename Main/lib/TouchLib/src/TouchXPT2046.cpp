#include "TouchXPT2046.h"

// 引脚 / 原始值窗口 / 采样次数都在 config.h 第 3.2 节
#include "config.h"

// =============================================================================
// TouchLib 实现（XPT2046，位操作，独立 5 根线）
//
// 时序（和参考工程 Touch_XPT2046.hpp 完全一致，不要随手改）：
//   1) 拉低 CS，按位移出 8 位控制字节（0xD0 读 X / 0x90 读 Y），MSB 先出，
//   2) 等 6us（XPT2046 需要一段转换时间），再给一个空时钟，
//   3) 读 12 位结果（MSB 先出）：先给低电平再拉高，在上升沿后采 DO，
//   4) 拉高 CS 结束。
// 每个轴采 CFG_TOUCH_SAMPLES 次取平均，压一下抖动。
// =============================================================================

TouchXPT2046::TouchXPT2046() {
    _cs = -1;
    _irq = -1;
    _clk = -1;
    _din = -1;
    _miso = -1;
    _lastZ = 0;
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

// 只读原始值，不做任何过滤（排线/查芯片有没有回数据用）
void TouchXPT2046::readRaw(uint16_t *x, uint16_t *y, uint16_t *z) {
    if (_cs < 0) {
        *x = 0;
        *y = 0;
        *z = 0;
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
    *z = readPressure();       // 压力值也一起给（read() 用它判断“按得够不够重”）
}

// 压力值 Z：XPT2046 用 Z1(0xB0)/Z2(0xC0) 两个通道差分算出来，和 TFT_eSPI 的算法一致。
// 值越大 = 按得越重，读到 4095（0xFFF）表示“其实没按”，TFT_eSPI 也把它当 0。
uint16_t TouchXPT2046::readPressure() {
    if (_cs < 0) {
        _lastZ = 0;
        return 0;
    }

    uint32_t sum = 0;
    const int samples = CFG_TOUCH_SAMPLES;

    for (int i = 0; i < samples; i++) {
        uint16_t z1 = readAxis(0xB0);
        uint16_t z2 = readAxis(0xC0);
        int32_t z = 4095 + (int32_t)(z1 >> 3) - (int32_t)(z2 >> 3);
        if (z == 4095) {
            z = 0;  // 和 TFT_eSPI 一样：4095 = 没按
        }
        if (z < 0) {
            z = 0;
        }
        if (z > 4095) {
            z = 4095;
        }
        sum += (uint32_t)z;
    }

    _lastZ = (uint16_t)(sum / (uint32_t)samples);
    return _lastZ;
}

// T_IRQ 电平：-1 没接，0 LOW（按下），1 HIGH（空闲）
int TouchXPT2046::irqLevel() {
    if (_irq < 0) {
        return -1;
    }
    return digitalRead(_irq) == LOW ? 0 : 1;
}

// 读一次触点：
//   1) 先看坐标在不在窗口内（不在 = 没人按，这样空闲时不用白读压力，省时间）
//   2) 再看压力够不够（CFG_TOUCH_PRESSURE_MIN = 灵敏度/触发门限）
bool TouchXPT2046::read(uint16_t *x, uint16_t *y) {
    if (_cs < 0) {
        return false;
    }

    uint16_t xa = 0;
    uint16_t ya = 0;
    uint32_t xs = 0;
    uint32_t ys = 0;
    const int samples = CFG_TOUCH_SAMPLES;

    for (int i = 0; i < samples; i++) {
        xs += readAxis(0xD0);  // X
        ys += readAxis(0x90);  // Y
    }
    xa = (uint16_t)(xs / (uint32_t)samples);
    ya = (uint16_t)(ys / (uint32_t)samples);

    // 坐标窗口：落在外面当作“没按”（窗口在 config.h 里调）
    if (xa < CFG_TOUCH_RAW_MIN || xa > CFG_TOUCH_RAW_MAX ||
        ya < CFG_TOUCH_RAW_MIN || ya > CFG_TOUCH_RAW_MAX) {
        return false;
    }

    // 压力门限：这就是“灵敏度”。调大 = 要按得更重才认，调 0 = 不判压力。
    uint16_t z = readPressure();
    if (z < CFG_TOUCH_PRESSURE_MIN) {
        return false;
    }

    *x = xa;
    *y = ya;
    return true;
}

void TouchXPT2046::printConfig() const {
    Serial.printf("触摸(XPT2046): T_CS=%d T_IRQ=%d T_CLK=%d T_DIN=%d T_DO=%d | "
                  "坐标窗口 %d~%d, 压力门限 %d, 每轴采样 %d 次\n",
                  _cs, _irq, _clk, _din, _miso, CFG_TOUCH_RAW_MIN,
                  CFG_TOUCH_RAW_MAX, CFG_TOUCH_PRESSURE_MIN, CFG_TOUCH_SAMPLES);
    Serial.printf("  灵敏度=压力门限 %d（调大=按更重才触发），轴方向 SWAP_XY=%d INVERT_X=%d INVERT_Y=%d\n",
                  CFG_TOUCH_PRESSURE_MIN, CFG_TOUCH_SWAP_XY, CFG_TOUCH_INVERT_X,
                  CFG_TOUCH_INVERT_Y);
}

const char *touchLibVersion() {
    return TOUCHLIB_VERSION;
}
