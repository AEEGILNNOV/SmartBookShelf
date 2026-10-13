#include "UltraLib.h"

// =============================================================================
// UltraLib 实现 —— HC-SR04 非阻塞测距状态机
//
// IDLE --startMeasure()--> TRIG_PULSE --10us 到--> WAIT_RISE
//      --ECHO 拉高--> WAIT_FALL --ECHO 拉低--> IDLE（出结果）
// 任何一个等待阶段超时都会回到 IDLE 并置错误码，绝不会死等。
//
// 32 位 micros() 回绕：所有时间比较都写成 (now - start) >= xxx 的无符号减法，
// 这样即使 micros() 从 0xFFFFFFFF 回绕到 0 也不会算错。
// =============================================================================

HCSR04::HCSR04(uint8_t trigPin, uint8_t echoPin, uint32_t timeoutUs) {
    _trigPin = trigPin;
    _echoPin = echoPin;
    if (timeoutUs == 0) {
        timeoutUs = CFG_WAVE_TIMEOUT_US;
    }
    _timeoutUs = timeoutUs;

    _state = STATE_IDLE;
    _pulseUs = 0;
    _distanceMm = 0;
    _errorCode = 0;
    _valid = false;
    _ready = false;
    _trigStartUs = 0;
    _riseStartUs = 0;
    _waitStartUs = 0;
}

// 配引脚：TRIG 输出并拉低，ECHO 输入
void HCSR04::begin() {
    pinMode(_trigPin, OUTPUT);
    digitalWrite(_trigPin, LOW);
    // ECHO 是 5V 电平，必须分压后再接进来；这里只当普通输入读
    pinMode(_echoPin, INPUT);
    _state = STATE_IDLE;
    _ready = false;
    _valid = false;
}

// 发起一次测量
bool HCSR04::startMeasure() {
    if (_state != STATE_IDLE) {
        return false;  // 上一次还没结束
    }

    _ready = false;
    _valid = false;
    _errorCode = 0;
    _pulseUs = 0;
    _distanceMm = 0;

    // 先拉低一小会儿，保证 TRIG 有一个干净的上升沿
    digitalWrite(_trigPin, LOW);
    _trigStartUs = micros();
    _state = STATE_TRIG_PULSE;

    return true;
}

// 推进状态机
void HCSR04::update() {
    uint32_t now = micros();
    uint32_t elapsed = 0;

    switch (_state) {
        case STATE_IDLE:
            break;

        // TRIG 拉高 10us
        case STATE_TRIG_PULSE:
            digitalWrite(_trigPin, HIGH);
            elapsed = now - _trigStartUs;
            if (elapsed >= 10UL) {
                digitalWrite(_trigPin, LOW);
                _waitStartUs = now;
                _state = STATE_WAIT_RISE;
            }
            break;

        // 等 ECHO 上升沿
        case STATE_WAIT_RISE:
            if (digitalRead(_echoPin) == HIGH) {
                _riseStartUs = now;
                _state = STATE_WAIT_FALL;
            } else if ((now - _waitStartUs) > _timeoutUs) {
                // 超时：模块没回应，一般是没接好、没给 5V，或者 TRIG 没接对
                _errorCode = 1;
                _pulseUs = 0;
                _distanceMm = 0;
                _valid = false;
                _ready = true;
                _state = STATE_IDLE;
            }
            break;

        // 等 ECHO 下降沿，顺便量高电平时间
        case STATE_WAIT_FALL:
            if (digitalRead(_echoPin) == LOW) {
                _pulseUs = now - _riseStartUs;
                // 距离 mm = 高电平时间 us × 343m/s ÷ 2 = us × 343 / 2000
                // 用 uint64_t 中间量防溢出
                _distanceMm = (uint32_t)(((uint64_t)_pulseUs *
                                          ULTRA_SOUND_MM_PER_US_NUM) /
                                         ULTRA_SOUND_MM_PER_US_DEN);
                _valid = true;
                _errorCode = 0;
                _ready = true;
                _state = STATE_IDLE;
            } else if ((now - _waitStartUs) > _timeoutUs) {
                // 超时：回波一直不拉低，一般是分压/接线问题，或者超出量程
                _errorCode = 2;
                _pulseUs = 0;
                _distanceMm = 0;
                _valid = false;
                _ready = true;
                _state = STATE_IDLE;
            }
            break;

        default:
            _state = STATE_IDLE;
            break;
    }
}

bool HCSR04::isReady() const {
    return _ready;
}

bool HCSR04::isValid() const {
    return _valid;
}

uint32_t HCSR04::getDistanceMm() const {
    return _distanceMm;
}

uint32_t HCSR04::getPulseUs() const {
    return _pulseUs;
}

uint8_t HCSR04::getErrorCode() const {
    return _errorCode;
}

bool HCSR04::isBusy() const {
    return (_state != STATE_IDLE);
}

uint32_t HCSR04::getTimeoutUs() const {
    return _timeoutUs;
}

const char *ultraLibVersion() {
    return "1.0.0";
}
