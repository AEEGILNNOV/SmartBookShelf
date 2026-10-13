#ifndef STEPPER_LIB_H
#define STEPPER_LIB_H

#include <Arduino.h>

#include "config.h"

// =============================================================================
// StepperLib —— 步进电机库（28BYJ-48 + ULN2003，宏可切 42 步进 + 驱动器）
//
// 本库只干三件事：初始化引脚、按步数非阻塞地转、软限位保护。
// 不建任务、不加锁：由调用方（stepperTask）唯一持有，别的任务不许直接碰电机。
//
// 【非阻塞怎么用】在任务的 for(;;) 里高频调 service()：
//     Stepper28BYJ m(CFG_STEPPER_M1_PINS);
//     m.begin();
//     m.run(2048);                     // 要求转 2048 步，立即返回
//     for (;;) {
//         m.service();                 // 该发脉冲就发一个，没到点就什么也不做
//         vTaskDelay(pdMS_TO_TICKS(STEPPER_SERVICE_INTERVAL_MS));
//     }
//   不要用 while(busy()) 这种写法，会把整核 CPU 占满。
//
// 【两种驱动模式，同一个类】
//   CFG_STEPPER_DRIVER = 0（默认）28BYJ-48 四相五线：
//     每台电机占 4 个 GPIO，接 ULN2003 的 IN1~IN4，按四相八拍/四拍序列表走。
//     输出轴一圈 = 8 拍 × 64 减速 × ... 详见下面 STEPPER_STEPS_PER_REV 注释。
//   CFG_STEPPER_DRIVER = 1  42 步进 + 步进驱动器：
//     每台电机只用 3 个 GPIO：pins[0]=STEP(pulse) pins[1]=DIR pins[2]=EN，pins[3] 不用。
//     细分与丝杆导程来自 config.h 的 CFG_STEPPER42_xxx。
//
// 【软限位】_posSteps 是“相对上电位置”的步数。上电位置就是零点，没有限位开关，
//   所以第一次调试先用手动小步数（m,1,10）找行程，再把 RANGE 收紧。
// =============================================================================

// ULN2003 四相序列表（8 拍半步序）。A 到 B 递增 = 正转；倒着取 = 反转。
// 数据手册真值：A-AB-B-BC-C-CD-D-DA
extern const uint8_t STEPPER_SEQ8[8];
// 四拍整步序（省电、力矩略小）
extern const uint8_t STEPPER_SEQ4[4];

// 28BYJ-48 输出轴一圈的步数（按四相八拍算）：
//   减速比 1:64，电机内部四相八拍一圈 8×... 实测整圈约 4096 半步。
//   要算“实际走了多少毫米”需要再除以丝杆/齿轮的机械传动比，那部分归机械。
#ifndef STEPPER_STEPS_PER_REV
#define STEPPER_STEPS_PER_REV 4096
#endif

class Stepper28BYJ {
public:
    // pins 固定 4 个：28BYJ 模式下是 IN1~IN4；42 步进模式下只用前 3 个（STEP/DIR/EN）
    // pins 传 nullptr = 这台电机没接线，所有操作都变成空操作（不会碰 GPIO）。
    Stepper28BYJ(const uint8_t *pins, uint8_t pinNum = 4);

    // 这台电机接没接线
    bool isWired() const;

    // 配引脚：全部拉低，默认停机（ULN2003 不发热）
    void begin();

    // 速度：两步之间的延时(us)。会被夹在 CFG_STEPPER_DELAY_MIN_US~MAX_US 之间。
    void setStepDelayUs(uint32_t delayUs);
    uint32_t stepDelayUs() const;

    // 要求走 steps 步（正数正转、负数反转）。立即返回，真正走步靠 service()。
    // 返回 false 表示参数非法或超过 CFG_STEPPER_MAX_STEPS。
    bool run(int32_t steps);

    // 立刻停：清掉剩余步数并释放线圈
    void stop();

    // 在走吗
    bool busy() const;

    // 剩余还没走的步数
    int32_t stepsLeft() const;

    // 相对上电位置的累计步数（软限位用）
    int32_t position() const;

    // 手动机动：把当前位置当零点
    void resetPosition();

    // 软限位行程：允许 _posSteps 落在 [-range, +range]
    void setRangeSteps(int32_t range);
    int32_t rangeSteps() const;

    // 高频调用：到点就发一个脉冲，没到点立刻返回
    void service();

    // 关掉全部线圈（停机后调，避免电机和 ULN2003 发热）
    void release();

private:
    // 28BYJ-48：按序列表走一个拍
    void stepWave(int8_t dir);
    // 42 步进：给 STEP 一个脉冲
    void stepPulse(int8_t dir);

    uint8_t _pins[4];
    uint8_t _pinNum;

    uint32_t _stepDelayUs;   // 两步之间的间隔(us)
    uint32_t _lastStepUs;    // 上一个脉冲的时间戳(micros)

    int32_t _stepsLeft;      // 还剩多少步没走（带符号）
    int32_t _posSteps;       // 相对上电位置的累计步数

    uint8_t _seqIndex;       // 当前在序列表里的位置
    bool _moving;            // 是否在走

    int32_t _rangeSteps;     // 软限位行程，<=0 表示不限位
    bool _enabledHigh;       // 42 步进模式下 EN 的有效电平
    bool _wired;             // 这台电机接没接线（构造时传 nullptr 就是没接）
};

#endif
