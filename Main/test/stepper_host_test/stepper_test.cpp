// =============================================================================
// StepperLib 的 PC 端单元测试（不需要硬件，也不进固件）
//
// 编译运行（在 Main/test/stepper_host_test 目录下，用任意 C++ 编译器）：
//   g++ -std=c++11 -I. -I../../lib/StepperLib/src -I../../include stepper_test.cpp -o stepper_test
//   ./stepper_test
//
// 验证内容：
//   1) 28BYJ 八拍模式的拍序是否是 A-AB-B-BC-C-CD-D-DA
//   2) 四拍模式是否只落在单相位置
//   3) 正转/反转的拍序方向
//   4) 步数与位置计数
//   5) 软限位到边自动停
//   6) 42 步进模式（STEP/DIR/EN）的脉冲数与方向
//   7) 没接线（pins=nullptr）时是否完全不碰 GPIO
// =============================================================================

#include "Arduino.h"

// ---- Arduino 桩实现 ----
int mockPinLevel[MOCK_MAX_PINS] = { 0 };
uint32_t mockMicros = 0;

void pinMode(uint8_t pin, uint8_t mode) {
    (void)pin;
    (void)mode;
}

void digitalWrite(uint8_t pin, uint8_t value) {
    if (pin < MOCK_MAX_PINS) {
        mockPinLevel[pin] = value ? 1 : 0;
    }
}

int digitalRead(uint8_t pin) {
    return (pin < MOCK_MAX_PINS) ? mockPinLevel[pin] : 0;
}

uint32_t micros(void) {
    return mockMicros;
}

void delayMicroseconds(uint32_t us) {
    mockMicros += us;
}

void delay(uint32_t ms) {
    mockMicros += ms * 1000UL;
}

void mockReset(void) {
    for (int i = 0; i < MOCK_MAX_PINS; i++) {
        mockPinLevel[i] = 0;
    }
    mockMicros = 0;
}

#include "StepperLib.h"
#include "../lib/StepperLib/src/StepperLib.cpp"

// ---- 极简断言 ----
static int gFail = 0;
static int gPass = 0;

static void check(bool ok, const char *what) {
    if (ok) {
        gPass++;
        printf("  [PASS] %s\n", what);
    } else {
        gFail++;
        printf("  [FAIL] %s\n", what);
    }
}

static void checkEq(long got, long want, const char *what) {
    if (got == want) {
        gPass++;
        printf("  [PASS] %s (=%ld)\n", what, got);
    } else {
        gFail++;
        printf("  [FAIL] %s: got %ld, want %ld\n", what, got, want);
    }
}

// 把 4 个 IN 脚的电平读成一个 4bit 掩码（bit0=pin[0]=IN1 ... bit3=pin[3]=IN4）
static uint8_t readMask(const uint8_t *pins) {
    uint8_t v = 0;
    for (uint8_t i = 0; i < 4; i++) {
        if (mockPinLevel[pins[i]]) {
            v |= (uint8_t)(1U << i);
        }
    }
    return v;
}

// 让电机走完请求的步数（推进时间，直到不再 busy）
static void runToFinish(Stepper28BYJ &m, int32_t steps) {
    m.run(steps);
    uint32_t guard = 0;
    while (m.busy() && guard++ < 200000UL) {
        mockMicros += m.stepDelayUs();
        m.service();
    }
}

// =============================================================================
// 测试 1~4：28BYJ 拍序与方向（需要 CFG_STEPPER_DRIVER=0）
// =============================================================================
#if CFG_STEPPER_DRIVER == 0
static void testWaveSequence(void) {
    printf("\n[测试] 28BYJ 八拍拍序\n");
    const uint8_t pins[4] = { 10, 11, 12, 13 };
    Stepper28BYJ m(pins, 4);
    m.begin();
    m.setStepDelayUs(CFG_STEPPER_DELAY_MIN_US);  // 设成最小，测试跑得快

    // 期望拍序：A-AB-B-BC-C-CD-D-DA（八拍）
    const uint8_t expect8[8] = { 0x01, 0x03, 0x02, 0x06,
                                 0x04, 0x0C, 0x08, 0x09 };

    if (CFG_STEPPER_WAVE_MODE == 0) {
        // 八拍：连走 8 步，每一步记下掩码
        runToFinish(m, 1);  // 先走一步，让 _seqIndex 从 0 前进到 1
        uint8_t got[8];
        got[0] = readMask(pins);
        for (int i = 1; i < 8; i++) {
            runToFinish(m, 1);
            got[i] = readMask(pins);
        }
        bool ok = true;
        for (int i = 0; i < 8; i++) {
            if (got[i] != expect8[i]) {
                ok = false;
                printf("    第 %d 拍 got=0x%02X want=0x%02X\n", i, got[i],
                       expect8[i]);
            }
        }
        check(ok, "八拍拍序 = A-AB-B-BC-C-CD-D-DA");
    } else {
        // 四拍：每一步都必须是单相
        uint8_t got[4];
        for (int i = 0; i < 4; i++) {
            runToFinish(m, 1);
            got[i] = readMask(pins);
        }
        bool ok = true;
        const uint8_t onePhase[4] = { 0x01, 0x02, 0x04, 0x08 };
        for (int i = 0; i < 4; i++) {
            if (got[i] != onePhase[i]) {
                ok = false;
                printf("    第 %d 拍 got=0x%02X want=0x%02X\n", i, got[i],
                       onePhase[i]);
            }
        }
        check(ok, "四拍拍序只落在单相 A-B-C-D");
    }
}

static void testDirection(void) {
    printf("\n[测试] 正转/反转方向\n");
    const uint8_t pins[4] = { 20, 21, 22, 23 };
    Stepper28BYJ m(pins, 4);
    m.begin();
    m.setStepDelayUs(CFG_STEPPER_DELAY_MIN_US);

    runToFinish(m, 1);
    uint8_t fwd = readMask(pins);
    runToFinish(m, 1);
    uint8_t fwd2 = readMask(pins);
    check(fwd != fwd2, "正转两拍不同（确实在换相）");

    // 反转一步应该回到上一拍
    runToFinish(m, -1);
    checkEq(readMask(pins), fwd, "反转一步回到上一拍");
}

static void testPositionAndStop(void) {
    printf("\n[测试] 位置计数 / 停机\n");
    const uint8_t pins[4] = { 30, 31, 32, 33 };
    Stepper28BYJ m(pins, 4);
    m.begin();
    m.setStepDelayUs(CFG_STEPPER_DELAY_MIN_US);

    runToFinish(m, 100);
    checkEq(m.position(), 100, "走 100 步后 position");
    checkEq(m.stepsLeft(), 0, "走完后剩余步数");
    check(!m.busy(), "走完后 busy=false");

    runToFinish(m, -40);
    checkEq(m.position(), 60, "反转 40 步后 position");

    m.run(1000);
    check(m.busy(), "run 之后 busy=true");
    m.stop();
    check(!m.busy(), "stop 之后 busy=false");
    checkEq(m.position(), 60, "stop 不改变已有位置");

    // 停机应该把线圈全拉低（28BYJ 不发热）
    checkEq(readMask(pins), 0, "停机后四个 IN 全部为低");
}

static void testSoftLimit(void) {
    printf("\n[测试] 软限位\n");
    const uint8_t pins[4] = { 40, 41, 42, 43 };
    Stepper28BYJ m(pins, 4);
    m.begin();
    m.setStepDelayUs(CFG_STEPPER_DELAY_MIN_US);
    m.setRangeSteps(50);

    runToFinish(m, 200);  // 要求 200 步，但限位 50
    checkEq(m.position(), 50, "正向撞限位停在 +50");
    check(!m.busy(), "撞限位后自动停");

    runToFinish(m, -200);
    checkEq(m.position(), -50, "反向撞限位停在 -50");

    // 限位内还能正常走
    m.resetPosition();
    runToFinish(m, 20);
    checkEq(m.position(), 20, "resetPosition 后重新计位置");
}

static void testInvalidSteps(void) {
    printf("\n[测试] 非法步数\n");
    const uint8_t pins[4] = { 50, 51, 52, 53 };
    Stepper28BYJ m(pins, 4);
    m.begin();

    check(!m.run(CFG_STEPPER_MAX_STEPS + 1), "超过最大步数被拒绝");
    check(!m.run(-(CFG_STEPPER_MAX_STEPS + 1)), "负向超限也被拒绝");
    check(m.run(10), "正常步数被接受");
}

static void testUnwired(void) {
    printf("\n[测试] 没接线的电机\n");
    mockReset();
    Stepper28BYJ m(nullptr);  // 不接线
    m.begin();                // 不应该碰任何 GPIO
    bool allZero = true;
    for (int i = 0; i < MOCK_MAX_PINS; i++) {
        if (mockPinLevel[i] != 0) {
            allZero = false;
        }
    }
    check(allZero, "begin() 不碰任何引脚");
    check(!m.isWired(), "isWired()=false");
    check(!m.run(100), "没接线时 run() 返回 false");
    m.service();  // 不应崩
    check(true, "service() 安全返回");
}
#endif  // CFG_STEPPER_DRIVER == 0

// =============================================================================
// 测试 6：42 步进模式（STEP/DIR/EN）
// =============================================================================
#if CFG_STEPPER_DRIVER == 1
static void testPulseMode(void) {
    printf("\n[测试] 42 步进 STEP/DIR/EN\n");
    const uint8_t pins[4] = { 10, 11, 12, 0 };
    Stepper28BYJ m(pins, 3);
    m.begin();
    m.setStepDelayUs(CFG_STEPPER_DELAY_MIN_US);

    // DIR 脚应该已经按宏定好方向（使能脚也要有效）
    printf("  DIR=pin%d level=%d, EN=pin%d level=%d\n", pins[1],
           mockPinLevel[pins[1]], pins[2], mockPinLevel[pins[2]]);

    runToFinish(m, 10);
    checkEq(m.position(), 10, "走 10 步后 position");
    checkEq(mockPinLevel[pins[0]], 0, "STEP 脉冲结束后回到低电平");
    checkEq(mockPinLevel[pins[1]], 1, "正转时 DIR 为高");

    runToFinish(m, -5);
    checkEq(mockPinLevel[pins[1]], 0, "反转时 DIR 为低");
    checkEq(m.position(), 5, "反转后 position");
}
#endif

// =============================================================================
int main(void) {
    printf("========================================\n");
    printf(" StepperLib PC 端逻辑测试\n");
    printf(" 驱动模式 CFG_STEPPER_DRIVER=%d (%s)\n", CFG_STEPPER_DRIVER,
           (CFG_STEPPER_DRIVER == 0) ? "28BYJ+ULN2003" : "42步进+驱动器");
    printf(" 拍序模式 CFG_STEPPER_WAVE_MODE=%d (%s)\n", CFG_STEPPER_WAVE_MODE,
           (CFG_STEPPER_WAVE_MODE == 0) ? "八拍" : "四拍");
    printf(" 最大步数 CFG_STEPPER_MAX_STEPS=%d\n", CFG_STEPPER_MAX_STEPS);
    printf("========================================\n");

#if CFG_STEPPER_DRIVER == 0
    testWaveSequence();
    testDirection();
    testPositionAndStop();
    testSoftLimit();
    testInvalidSteps();
    testUnwired();
#else
    testPulseMode();
#endif

    printf("\n========================================\n");
    printf(" 通过 %d 项, 失败 %d 项\n", gPass, gFail);
    printf("========================================\n");
    return (gFail == 0) ? 0 : 1;
}
