#ifndef ULTRA_LIB_H
#define ULTRA_LIB_H

#include <Arduino.h>

#include "config.h"

// =============================================================================
// UltraLib —— HC-SR04 超声波测距库（非阻塞状态机）
//
// 从 Wave_Test 的 HCSR04 驱动移植过来，接口保持不变，只做两件事：
//   1) 所有参数走 config.h（超时/量程/滤波窗口），不再散落在源码里；
//   2) 修掉 startMeasure() 里 _ready 没清掉的问题（见下面注释）。
//
// 【为什么不阻塞】全程不用 pulseIn()：
//   TRIG 拉高 -> 用 micros() 记时刻 -> 状态机等 ECHO 上升沿/下降沿 -> 算距离。
//   在任务的 for(;;) 里高频调 update()（1ms 以内），任何一个等待阶段都有超时，
//   模块没接、线断了都只会返回错误码，不会把任务卡死。
//
// 【接线与电平】黑/红是 GND/5V，TRIG 接 GPIO（3.3V 可以驱动），
//   **ECHO 输出 5V，必须分压（1k + 2k）后再接 GPIO**，否则可能伤 S3。
// =============================================================================

// 距离换算常量：声速按 343m/s，距离(mm) = 回波高电平(us) × 343 / 2000
#define ULTRA_SOUND_MM_PER_US_NUM 343UL
#define ULTRA_SOUND_MM_PER_US_DEN 2000UL

// 测距快照：waveTask 写（waveMutex 保护），显示/串口读
struct WaveState {
    uint32_t distanceMm;      // 本次距离(mm)，无效时是 0
    uint32_t pulseUs;         // 回波高电平时间(us)
    uint32_t filteredMm;      // 滤波后的距离(mm)，没开滤波就等于 distanceMm
    uint32_t minMm;           // 统计：最小有效距离
    uint32_t maxMm;           // 统计：最大有效距离
    uint32_t totalCount;      // 统计：总测量次数
    uint32_t invalidCount;    // 统计：无效次数（含超时和超量程）
    uint32_t timeoutRiseCount;// 统计：ECHO 一直没拉高
    uint32_t timeoutFallCount;// 统计：ECHO 一直没拉低
    uint32_t lastMs;          // 最近一次有效测量的 millis()

    uint8_t errorCode;        // 0 正常，1 没等到上升沿，2 没等到下降沿
    bool valid;               // 本次读数是否有效

    // ---- 测书厚用的时间窗快照（B 键记起点，A 键算厚度）----
    bool asnapValid;          // 起点快照是否已记录
    uint32_t asnapMm;         // 起点距离(mm)
    uint32_t asnapMs;         // 起点时刻(millis)
    int32_t thicknessMm;      // 厚度 = 起点距离 - 当前距离（A 键算出来填这里）
    bool thicknessValid;      // 厚度是否算过且有效
};

class HCSR04 {
public:
    // trigPin / echoPin 由调用方从 config.h 传进来（本库不自己写死引脚）
    HCSR04(uint8_t trigPin, uint8_t echoPin, uint32_t timeoutUs);

    // 配引脚：TRIG 输出低，ECHO 输入。只做一次，可以在任务里重复调。
    void begin();

    // 发起一次测量。上次还没结束就返回 false（不会打断上一次）。
    bool startMeasure();

    // 推进状态机，需要高频调用（1ms 以内更好）
    void update();

    // 本次测量是否已结束（有结果或超时）
    bool isReady() const;

    // 本次是否测到了有效距离
    bool isValid() const;

    // 距离，单位 mm，无效时返回 0
    uint32_t getDistanceMm() const;

    // 回波高电平时间，单位 us
    uint32_t getPulseUs() const;

    // 错误码：0 正常，1 回波一直没拉高，2 回波一直没拉低
    uint8_t getErrorCode() const;

    // 是否有测量正在进行
    bool isBusy() const;

    // 超时时间(us)，改这个会影响量程
    uint32_t getTimeoutUs() const;

private:
    // 状态机阶段
    enum : uint8_t {
        STATE_IDLE = 0,
        STATE_TRIG_PULSE,
        STATE_WAIT_RISE,
        STATE_WAIT_FALL
    };

    uint8_t _trigPin;
    uint8_t _echoPin;
    uint32_t _timeoutUs;

    uint8_t _state;

    uint32_t _pulseUs;      // 回波高电平时间
    uint32_t _distanceMm;   // 换算后的距离
    uint8_t _errorCode;     // 0 正常，1 没等到上升沿，2 没等到下降沿
    bool _valid;            // 本次读数是否有效
    bool _ready;            // 本次测量是否结束

    uint32_t _trigStartUs;  // TRIG 拉高的时刻
    uint32_t _riseStartUs;  // ECHO 上升沿的时刻
    uint32_t _waitStartUs;  // 进入等待状态的时刻，用于判断超时
};

const char *ultraLibVersion();

#endif
