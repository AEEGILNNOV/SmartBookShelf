#include "StepperLib.h"

// =============================================================================
// StepperLib 实现
//
// 两种驱动模式共用“步数 + 非阻塞状态机”这一套接口：
//   28BYJ-48：service() 到点就把下一个拍写进 ULN2003 的 4 个 IN 脚
//   42 步进  ：service() 到点就给 STEP 一个脉冲，DIR 由剩余步数决定符号
// 状态机只有一条时间线（_lastStepUs -> 到点 -> 走一步），所以不会两个模式同时跑。
// =============================================================================

//------------------------------------ 序列表 ------------------------------------//
// ULN2003 真值：A-AB-B-BC-C-CD-D-DA，位含义 bit0=IN1(A) bit1=IN2(B) bit2=IN3(C) bit3=IN4(D)
const uint8_t STEPPER_SEQ8[8] = {
    0x01,  // A
    0x03,  // AB
    0x02,  // B
    0x06,  // BC
    0x04,  // C
    0x0C,  // CD
    0x08,  // D
    0x09   // DA
};

// 四拍整步：直接用八拍表里的单相位置（索引 0,2,4,6）
const uint8_t STEPPER_SEQ4[4] = { 0x01, 0x02, 0x04, 0x08 };

// 八拍模式每次在表里走 1 格；四拍模式每次走 2 格（正好落在单相位置上）
#if CFG_STEPPER_WAVE_MODE
#define STEPPER_SEQ_LEN    8
#define STEPPER_SEQ_STRIDE 1
#else
#define STEPPER_SEQ_LEN    4
#define STEPPER_SEQ_STRIDE 2
#endif

//------------------------------------ 内部工具 ------------------------------------//
// 28BYJ 模式：把“第 index 个拍”的位掩码写到 4 个 IN 脚上
static void stepperWritePins(const uint8_t *pins, uint8_t pinNum, uint8_t value) {
    for (uint8_t i = 0; i < pinNum && i < 4; i++) {
        // bit i 对应 pins[i]；1 = 该相接通
        digitalWrite(pins[i], (value & (uint8_t)(1U << i)) ? HIGH : LOW);
    }
}

// 28BYJ 模式：八个拍里第 index 个的位掩码（四拍模式自动隔一个取）
static uint8_t stepperSeqValue(uint8_t index) {
    return STEPPER_SEQ8[(uint8_t)(index % STEPPER_SEQ_LEN) * STEPPER_SEQ_STRIDE];
}

// =============================================================================
// 构造 / 初始化
// =============================================================================
Stepper28BYJ::Stepper28BYJ(const uint8_t *pins, uint8_t pinNum) {
    // pins == nullptr 表示这台电机没接线（config.h 里对应 xxx_ENABLE=0）：
    // 本对象后面所有操作都会直接返回，不会去碰任何 GPIO。
    _wired = (pins != nullptr);

    if (pinNum > 4) {
        pinNum = 4;
    }
    if (pinNum < 1) {
        pinNum = 1;
    }
    _pinNum = pinNum;

    for (uint8_t i = 0; i < 4; i++) {
        // 0xFF = 这个脚不用（28BYJ 用 4 个，42 步进只上前 3 个）
        _pins[i] = (_wired && i < pinNum) ? pins[i] : 0xFF;
    }

    _stepDelayUs = CFG_STEPPER_STEP_DELAY_US;
    _lastStepUs = 0;
    _stepsLeft = 0;
    _posSteps = 0;
    _seqIndex = 0;
    _moving = false;
    _rangeSteps = 0;
    _enabledHigh = true;
}

void Stepper28BYJ::begin() {
    if (!_wired) {
        return;  // 没接线的电机：不配引脚、不动 GPIO
    }

#if CFG_STEPPER_DRIVER == 0
    // ---- 28BYJ-48 + ULN2003：4 个 IN 脚全部输出并拉低（停机不发热）----
    for (uint8_t i = 0; i < _pinNum && i < 4; i++) {
        pinMode(_pins[i], OUTPUT);
        digitalWrite(_pins[i], LOW);
    }
#else
    // ---- 42 步进 + 驱动器：STEP / DIR / EN ----
    pinMode(_pins[0], OUTPUT);  // STEP
    pinMode(_pins[1], OUTPUT);  // DIR
    digitalWrite(_pins[0], LOW);
    digitalWrite(_pins[1], LOW);

    if (_pinNum >= 3 && _pins[2] != 0xFF) {
        pinMode(_pins[2], OUTPUT);  // EN
#if CFG_STEPPER_EN_ACTIVE_LOW
        _enabledHigh = false;
#else
        _enabledHigh = true;
#endif
        // 使能驱动器（有的模块 EN 低有效，按宏来）
        digitalWrite(_pins[2], _enabledHigh ? HIGH : LOW);
    }
#endif

    _seqIndex = 0;
    _moving = false;
    _stepsLeft = 0;
}

// =============================================================================
// 速度 / 状态
// =============================================================================
void Stepper28BYJ::setStepDelayUs(uint32_t delayUs) {
    if (delayUs < CFG_STEPPER_DELAY_MIN_US) {
        delayUs = CFG_STEPPER_DELAY_MIN_US;
    }
    if (delayUs > CFG_STEPPER_DELAY_MAX_US) {
        delayUs = CFG_STEPPER_DELAY_MAX_US;
    }
    _stepDelayUs = delayUs;
}

uint32_t Stepper28BYJ::stepDelayUs() const {
    return _stepDelayUs;
}

bool Stepper28BYJ::isWired() const {
    return _wired;
}

bool Stepper28BYJ::busy() const {
    return _moving;
}

int32_t Stepper28BYJ::stepsLeft() const {
    return _stepsLeft;
}

int32_t Stepper28BYJ::position() const {
    return _posSteps;
}

void Stepper28BYJ::resetPosition() {
    _posSteps = 0;
}

void Stepper28BYJ::setRangeSteps(int32_t range) {
    _rangeSteps = (range < 0) ? -range : range;
}

int32_t Stepper28BYJ::rangeSteps() const {
    return _rangeSteps;
}

// =============================================================================
// 走步请求（只登记目标，不发脉冲）
// =============================================================================
bool Stepper28BYJ::run(int32_t steps) {
    if (!_wired) {
        return false;  // 这台电机没接线，转不了
    }
    if (steps == 0) {
        return true;  // 不转也算成功，省得调用方还要判
    }
    if (steps > CFG_STEPPER_MAX_STEPS || steps < -CFG_STEPPER_MAX_STEPS) {
        return false;  // 参数写错（比如多写一个 0），宁可不动也别撞机构
    }

    _stepsLeft = steps;
    _moving = true;
    return true;
}

void Stepper28BYJ::stop() {
    _stepsLeft = 0;
    _moving = false;
    release();
}

void Stepper28BYJ::release() {
    if (!_wired) {
        return;
    }
    if (CFG_STEPPER_DRIVER == 0) {
        // 28BYJ：四个 IN 全拉低，ULN2003 不再导通，电机不发热也不保持力矩
        for (uint8_t i = 0; i < _pinNum && i < 4; i++) {
            digitalWrite(_pins[i], LOW);
        }
    } else {
        // 42 步进：关使能（EN 反有效）
        if (_pinNum >= 3 && _pins[2] != 0xFF) {
            digitalWrite(_pins[2], _enabledHigh ? LOW : HIGH);
        }
    }
}

// =============================================================================
// 28BYJ 模式：按序列表走一个拍
// =============================================================================
void Stepper28BYJ::stepWave(int8_t dir) {
    if (dir >= 0) {
        _seqIndex = (uint8_t)((_seqIndex + 1) % STEPPER_SEQ_LEN);
    } else {
        _seqIndex = (uint8_t)((_seqIndex + STEPPER_SEQ_LEN - 1) % STEPPER_SEQ_LEN);
    }
    stepperWritePins(_pins, _pinNum, stepperSeqValue(_seqIndex));
}

// =============================================================================
// 42 步进模式：给 STEP 一个脉冲（DIR 先建立，再给脉冲）
// =============================================================================
void Stepper28BYJ::stepPulse(int8_t dir) {
    digitalWrite(_pins[1], (dir >= 0) ? HIGH : LOW);  // 先定方向
    delayMicroseconds(2);                             // 方向建立时间

    digitalWrite(_pins[0], HIGH);
    delayMicroseconds(3);                             // 脉冲宽度，驱动器一般 >2.5us 即可
    digitalWrite(_pins[0], LOW);
}

// =============================================================================
// 服务函数：到点就走一步，一步都不多走
//
// 时间线：|--stepDelayUs--|--stepDelayUs--|
//                    ^ 这里 service() 发现到点就发一个脉冲
// =============================================================================
void Stepper28BYJ::service() {
    if (!_wired || !_moving || _stepsLeft == 0) {
        return;
    }

    uint32_t now = micros();
    if ((uint32_t)(now - _lastStepUs) < _stepDelayUs) {
        return;  // 还没到下一个脉冲的时刻
    }
    _lastStepUs = now;

    int8_t dir = (_stepsLeft > 0) ? 1 : -1;

    // ---- 软限位：到边了就停，剩下的步数直接丢掉，不硬顶机构 ----
    if (CFG_STEPPER_SOFT_LIMIT && _rangeSteps > 0) {
        if (dir > 0 && _posSteps >= _rangeSteps) {
            stop();
            return;
        }
        if (dir < 0 && _posSteps <= -_rangeSteps) {
            stop();
            return;
        }
    }

#if CFG_STEPPER_DRIVER == 0
    stepWave(dir);
#else
    stepPulse(dir);
#endif

    _posSteps += dir;
    _stepsLeft -= dir;

    if (_stepsLeft == 0) {
        _moving = false;
        release();  // 停机就断线圈，避免 28BYJ 长时间通电发热
    }
}
