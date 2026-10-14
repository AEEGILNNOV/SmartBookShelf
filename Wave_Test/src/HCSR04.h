#ifndef HCSR04_H
#define HCSR04_H

#include <Arduino.h>

//------------------------------------ HC-SR04 非阻塞驱动 ------------------------------------//
// 用法（放在某个任务的 for(;;) 里，不阻塞）：
//   1. 每 US_INTERVAL_MS 调一次 startMeasure() 发起测量
//   2. 每次循环调一次 update() 推进状态机
//   3. isReady() 为 true 时读 getDistanceMm()
// 全过程不用 pulseIn，不阻塞任务
class HCSR04 {
public:
    HCSR04(uint8_t trigPin, uint8_t echoPin, uint32_t timeoutUs);

    // 发起一次测量，上次测量还没结束就返回 false
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

    uint32_t _pulseUs;     // 回波高电平时间
    uint32_t _distanceMm;  // 换算后的距离
    uint8_t _errorCode;    // 0 正常，1 没等到上升沿，2 没等到下降沿
    bool _valid;           // 本次读数是否有效
    bool _ready;           // 本次测量是否结束

    uint32_t _trigStartUs; // TRIG 拉高的时刻
    uint32_t _riseStartUs; // ECHO 上升沿的时刻
    uint32_t _waitStartUs; // 进入等待状态时刻，用于判断超时
};

#endif
