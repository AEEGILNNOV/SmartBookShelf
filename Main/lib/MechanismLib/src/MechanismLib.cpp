#include "MechanismLib.h"

// =============================================================================
// MechanismLib 实现
//
// 状态机只有 5 个状态，一条主线：
//
//   IDLE --请求--> MOVING --走完--> DONE --调用方 takeResult()--> IDLE
//                      |
//                      +--超时----> FAILED  --takeResult()--> IDLE
//                      +--急停----> ESTOPPED --takeResult()--> IDLE
//
// 为什么要把 DONE/FAILED 也做成状态、而不是直接回 IDLE：
//   因为"动完了"和"动失败了"对上层是两件完全不同的事：
//     · 成功了 -> 可以更新书单、可以落盘；
//     · 失败了 -> **必须保留原书单记录**，并提示用户检查机构后人工恢复。
//   如果状态机一回到 IDLE 就把结果冲掉，上层就只能靠猜了。
// =============================================================================

MechanismLib::MechanismLib() {
    _lift = nullptr;
    _push = nullptr;
    _inited = false;

    _state = MECH_IDLE;
    _activeAxis = MECH_AXIS_NONE;
    _lastError = MECH_ERR_NONE;

    // 上电时位置**一律无效**：没有限位开关，软件不知道平台在哪。
    // 必须由人工归零建立零点，或者从 Flash 里恢复一个"上次正常保存过"的位置。
    _positionValid = false;
    _needHoming = true;

    _targetSteps = 0;
    _actionStartPos = 0;
    _actionStartMs = 0;
    _pushLastSteps = 0;
    _moveCount = 0;
    _lastErrorText[0] = 0;
}

void MechanismLib::begin(Stepper28BYJ *lift, Stepper28BYJ *push) {
    _lift = lift;
    _push = push;
    _inited = true;
    _state = MECH_IDLE;
    _activeAxis = MECH_AXIS_NONE;
    _lastError = MECH_ERR_NONE;
    _lastErrorText[0] = 0;

    Serial.printf("机械: 已注册 升降=%s 推书=%s (上电位置无效, 需要人工归零)\n",
                  (_lift != nullptr && _lift->isWired()) ? "已接线" : "未接线",
                  (_push != nullptr && _push->isWired()) ? "已接线" : "未接线");
}

// -----------------------------------------------------------------------------
// 工具：按软限位把请求步数夹到允许范围
//
// 为什么是夹而不是直接拒绝：
//   点动时用户可能就是想走到边上停住，夹到边上比整个拒绝更符合直觉；
//   但如果夹完变成 0，说明**已经**在边上了，上层就该知道这一步走不了。
//   返回 0 -> 调用方判为 MECH_ERR_PARAM（不允许默默什么都不做）。
//
// ⚠️ 这里只依赖 Stepper28BYJ::position()（上电/归零后的累计步数）。
//    开环系统里它和真实位置必然有误差，所以软限位要留余量（见 config.h）。
// -----------------------------------------------------------------------------
int32_t MechanismLib::clampToRange(Stepper28BYJ *motor, int32_t steps) const {
    if (motor == nullptr || steps == 0) {
        return 0;
    }
    int32_t range = motor->rangeSteps();
    if (!CFG_STEPPER_SOFT_LIMIT || range <= 0) {
        return steps;  // 没开软限位就不夹
    }

    int32_t pos = motor->position();

    if (steps > 0) {
        int32_t room = range - pos;   // 还能往正方向走多少步
        if (room <= 0) {
            return 0;
        }
        return (steps > room) ? room : steps;
    }

    int32_t room = range + pos;       // 还能往负方向走多少步（pos 是负的时候 room 变小）
    if (room <= 0) {
        return 0;
    }
    return (steps < -room) ? -room : steps;
}

// -----------------------------------------------------------------------------
// 请求：点动
// -----------------------------------------------------------------------------
bool MechanismLib::jog(uint8_t axis, int32_t steps, bool allowInvert, bool rawMode) {
    if (!_inited) {
        _lastError = MECH_ERR_NOT_INITED;
        snprintf(_lastErrorText, sizeof(_lastErrorText), "机械没初始化");
        return false;
    }

    // ① 互斥：已经有轴在动就不接受新动作。
    //    重复点按钮、连着发命令都靠这一条拦下来，不能让两个目标互相覆盖。
    if (_state == MECH_MOVING) {
        _lastError = MECH_ERR_BUSY;
        snprintf(_lastErrorText, sizeof(_lastErrorText), "另一个动作正在进行");
        Serial.println("机械: 拒绝点动 —— 已有动作在进行(互斥)");
        return false;
    }
    if (steps == 0) {
        _lastError = MECH_ERR_PARAM;
        snprintf(_lastErrorText, sizeof(_lastErrorText), "点动步数是 0");
        return false;
    }

    Stepper28BYJ *motor = nullptr;
    const char *axisName = "?";
    if (axis == MECH_AXIS_LIFT) {
        motor = _lift;
        axisName = "升降";
    } else if (axis == MECH_AXIS_PUSH) {
        motor = _push;
        axisName = "推书";
    } else {
        _lastError = MECH_ERR_PARAM;
        snprintf(_lastErrorText, sizeof(_lastErrorText), "轴号非法");
        return false;
    }

    if (motor == nullptr || !motor->isWired()) {
        _lastError = MECH_ERR_NOT_WIRED;
        snprintf(_lastErrorText, sizeof(_lastErrorText), "%s 没接线/没启用",
                 axisName);
        Serial.printf("机械: 拒绝点动 —— %s 没接线(检查 config.h 的 ENABLE 宏)\n",
                      axisName);
        return false;
    }

    // ② 方向约定：把"机械意义上的正方向"翻译成"电机该怎么转"。
    //    CFG_xxx_DIR_SIGN = 1 表示 run() 传正数就是机械正方向，-1 表示反过来。
    //    这样标定时只要改一个宏，不用去动逻辑。
    int32_t dirSign = (axis == MECH_AXIS_LIFT) ? CFG_LIFT_DIR_SIGN
                                               : CFG_PUSH_DIR_SIGN;
    int32_t signedSteps = (dirSign >= 0) ? steps : -steps;

    // ③ 推书轴默认只允许"推出"方向，回退必须显式允许（allowInvert）。
    //    ⚠️ 只有实际机械结构允许推书板退回来才该允许，否则会撞机构。
    if (axis == MECH_AXIS_PUSH && !allowInvert && (steps < 0)) {
        _lastError = MECH_ERR_PARAM;
        snprintf(_lastErrorText, sizeof(_lastErrorText), "推书回退未启用");
        Serial.println("机械: 拒绝推书回退（CFG_PUSH_AUTO_RETRACT=0 或机械不支持）");
        return false;
    }

    // ④ 单次步数上限 / ⑤ 软限位：**这两道是业务保护，标定时要用 rawMode 跳过**。
    //
    // 为什么标定要跳过：标定本身就要"把机构往边上顶、量出真实行程"，
    // 软限位会把它提前拦住，量不到真值。所以调试页默认走 rawMode。
    int32_t clamped = signedSteps;
    if (rawMode) {
        // ---- 调试模式：只做"参数合法性"校验，不做业务限位 ----
        int32_t mag = (signedSteps >= 0) ? signedSteps : -signedSteps;
        if (mag > CFG_STEPPER_MAX_STEPS) {
            // 硬上限还是要守：超过 StepperLib 单次能力的话 run() 会直接拒绝，
            // 与其让它失败，不如夹到这个硬上限并明确告诉用户。
            clamped = (signedSteps >= 0) ? CFG_STEPPER_MAX_STEPS : -CFG_STEPPER_MAX_STEPS;
            Serial.printf("机械: [调试] %s 点动 %ld 步超过硬上限 %d, 已夹到 %ld 步\n",
                          axisName, (long)mag, CFG_STEPPER_MAX_STEPS, (long)clamped);
        }
    } else {
        // ---- 正常模式：单次上限 + 软限位双保险 ----
        int32_t limit = (axis == MECH_AXIS_LIFT) ? CFG_LIFT_MAX_MOVE_STEPS
                                                 : CFG_PUSH_MAX_MOVE_STEPS;
        int32_t mag = (signedSteps >= 0) ? signedSteps : -signedSteps;
        if (mag > limit) {
            _lastError = MECH_ERR_PARAM;
            snprintf(_lastErrorText, sizeof(_lastErrorText), "%s 单次步数超上限 %ld",
                     axisName, (long)limit);
            Serial.printf("机械: 拒绝点动 —— %s 要 %ld 步, 超过单次上限 %ld\n",
                          axisName, (long)mag, (long)limit);
            return false;
        }

        clamped = clampToRange(motor, signedSteps);
        if (clamped == 0) {
            _lastError = MECH_ERR_PARAM;
            snprintf(_lastErrorText, sizeof(_lastErrorText), "%s 已到软限位", axisName);
            Serial.printf("机械: 拒绝点动 —— %s 已经到软限位(±%ld 步)\n", axisName,
                          (long)motor->rangeSteps());
            return false;
        }
        if (clamped != signedSteps) {
            // 夹短了要告诉用户：否则用户以为走了 500 步，实际只走了 80 步
            Serial.printf("机械: %s 点动 %ld 步被软限位夹到 %ld 步\n", axisName,
                          (long)signedSteps, (long)clamped);
        }
    }

    // ⑥ 真正登记走步。这里只登记，脉冲在 service() 里发。
    if (!motor->run(clamped)) {
        _lastError = MECH_ERR_PARAM;
        snprintf(_lastErrorText, sizeof(_lastErrorText), "%s run() 拒绝", axisName);
        return false;
    }

    _state = MECH_MOVING;
    _activeAxis = axis;
    _lastError = MECH_ERR_NONE;
    _lastErrorText[0] = 0;
    _targetSteps = clamped;
    _actionStartPos = motor->position();   // 记下起点，走完才能判"够不够"
    _actionStartMs = millis();
    if (axis == MECH_AXIS_PUSH) {
        _pushLastSteps = clamped;
    }
    _moveCount++;

    Serial.printf("机械: %s 点动 %ld 步 (方向符号=%ld, 当前位置=%ld, 模式=%s)\n",
                  axisName, (long)clamped, (long)dirSign, (long)motor->position(),
                  rawMode ? "调试-不限位" : "正常-带软限位");
    return true;
}

// -----------------------------------------------------------------------------
// 请求：升降走到绝对位置（取书/存书用）
//
// ⚠️ 这是"依赖绝对位置"的动作。位置无效时**必须拒绝**：
//    掉电恢复后没人工归零之前，绝对位置是不知道的，盲走会撞机构。
// -----------------------------------------------------------------------------
bool MechanismLib::moveLiftTo(int32_t targetPos) {
    if (!_inited) {
        _lastError = MECH_ERR_NOT_INITED;
        snprintf(_lastErrorText, sizeof(_lastErrorText), "机械没初始化");
        return false;
    }
    if (_state == MECH_MOVING) {
        _lastError = MECH_ERR_BUSY;
        snprintf(_lastErrorText, sizeof(_lastErrorText), "另一个动作正在进行");
        return false;
    }
    if (!_positionValid) {
        _lastError = MECH_ERR_POS_INVALID;
        snprintf(_lastErrorText, sizeof(_lastErrorText), "位置无效，需要人工归零");
        Serial.println("机械: 拒绝升降定位 —— 位置无效, 请先在调试页人工归零");
        return false;
    }
    if (_lift == nullptr || !_lift->isWired()) {
        _lastError = MECH_ERR_NOT_WIRED;
        snprintf(_lastErrorText, sizeof(_lastErrorText), "升降没接线/没启用");
        return false;
    }

    // 相对步数 = 目标绝对位置 - 当前位置
    int32_t delta = targetPos - _lift->position();
    if (delta == 0) {
        // 已经在目标位置了：直接判成功，省一次无意义的走步（也避免"0 步"被判非法）
        _activeAxis = MECH_AXIS_LIFT;
        _targetSteps = 0;
        _actionStartPos = _lift->position();
        _state = MECH_DONE;
        _lastError = MECH_ERR_NONE;
        _lastErrorText[0] = 0;
        Serial.println("机械: 升降已经在目标位置, 不需要移动");
        return true;
    }

    int32_t dirSign = CFG_LIFT_DIR_SIGN;
    int32_t signedDelta = (dirSign >= 0) ? delta : -delta;

    int32_t mag = (signedDelta >= 0) ? signedDelta : -signedDelta;
    if (mag > CFG_LIFT_MAX_MOVE_STEPS) {
        _lastError = MECH_ERR_PARAM;
        snprintf(_lastErrorText, sizeof(_lastErrorText), "升降位移 %ld 步超上限",
                 (long)mag);
        Serial.printf("机械: 拒绝升降定位 —— 需要走 %ld 步, 超过上限 %d\n",
                      (long)mag, CFG_LIFT_MAX_MOVE_STEPS);
        return false;
    }

    int32_t clamped = clampToRange(_lift, signedDelta);
    if (clamped == 0) {
        _lastError = MECH_ERR_PARAM;
        snprintf(_lastErrorText, sizeof(_lastErrorText), "升降已到软限位");
        Serial.println("机械: 拒绝升降定位 —— 已经在软限位边上");
        return false;
    }

    if (!_lift->run(clamped)) {
        _lastError = MECH_ERR_PARAM;
        snprintf(_lastErrorText, sizeof(_lastErrorText), "升降 run() 拒绝");
        return false;
    }

    _state = MECH_MOVING;
    _activeAxis = MECH_AXIS_LIFT;
    _lastError = MECH_ERR_NONE;
    _lastErrorText[0] = 0;
    _targetSteps = clamped;
    _actionStartPos = _lift->position();   // 记下起点
    _actionStartMs = millis();
    _moveCount++;

    Serial.printf("机械: 升降定位 -> %ld 步 (现在 %ld, 走 %ld)\n",
                  (long)targetPos, (long)_lift->position(), (long)clamped);
    return true;
}

// -----------------------------------------------------------------------------
// 请求：推书推出 / 回退
// -----------------------------------------------------------------------------
bool MechanismLib::movePushOut() {
    // 推出就是"朝机械正方向走 CFG_PUSH_OUT_STEPS 步"
    // 复用 jog 的全部保护（互斥 / 上限 / 软限位 / 方向符号）
    return jog(MECH_AXIS_PUSH, CFG_PUSH_OUT_STEPS, false);
}

bool MechanismLib::movePushBack() {
    // 回退是反方向：allowInvert=true 才放行（机械必须真的支持）
    return jog(MECH_AXIS_PUSH, -CFG_PUSH_RETRACT_STEPS, true);
}

// -----------------------------------------------------------------------------
// 急停
//
// ⚠️ 急停之后**不能**认为电机停在原目标位置：
//    · 这一步可能只走了一半；
//    · 28BYJ 是开环，停下来的时候丢了几步软件也不知道。
// 所以急停会把"位置是否有效"置为无效，并要求人工归零。
// -----------------------------------------------------------------------------
void MechanismLib::stopAll() {
    bool wasMoving = (_state == MECH_MOVING) ||
                     ((_lift != nullptr && _lift->busy()) ||
                      (_push != nullptr && _push->busy()));

    if (_lift != nullptr) {
        _lift->stop();
    }
    if (_push != nullptr) {
        _push->stop();
    }

    if (wasMoving) {
        // 被打断的位置不能再当作可靠位置
        _positionValid = false;
        _needHoming = true;
        _state = MECH_ESTOPPED;
        _lastError = MECH_ERR_ESTOP;
        snprintf(_lastErrorText, sizeof(_lastErrorText),
                 "急停：动作未完成，位置需重新归零");
        Serial.println("机械: 急停 —— 动作未正常完成, 位置已标记为需要人工归零");
    } else {
        Serial.println("机械: 急停（本来就没在动）");
    }
}

// -----------------------------------------------------------------------------
// 人工归零
//
// 【必须对用户讲清楚的事实】
//   这个操作**只是把当前软件步数记成 0**，电机不会动，也不会去找机械原点。
//   所以界面上必须写"清零软件步数不等于电机自动回原点"，
//   用户要先用调试页点动把机构挪到他认可的位置，再点这个按钮。
// -----------------------------------------------------------------------------
bool MechanismLib::setZeroHere() {
    if (!_inited) {
        _lastError = MECH_ERR_NOT_INITED;
        snprintf(_lastErrorText, sizeof(_lastErrorText), "机械没初始化");
        return false;
    }

    // 有轴在动的时候归零没有意义（零点会在走步过程中被继续改）
    if ((_lift != nullptr && _lift->busy()) || (_push != nullptr && _push->busy())) {
        _lastError = MECH_ERR_BUSY;
        snprintf(_lastErrorText, sizeof(_lastErrorText), "有轴在动，不能归零");
        Serial.println("机械: 拒绝人工归零 —— 还有轴在动, 先停止");
        return false;
    }

    if (_lift != nullptr) {
        _lift->resetPosition();
    }
    if (_push != nullptr) {
        _push->resetPosition();
    }

    _positionValid = true;
    _needHoming = false;
    _state = MECH_IDLE;
    _lastError = MECH_ERR_NONE;
    _lastErrorText[0] = 0;

    Serial.println("机械: 人工归零完成 —— 当前软件步数已置 0, 位置标记为有效");
    Serial.println("  提醒: 这只改了软件零点, 电机并没有移动到机械原点。");
    return true;
}

// -----------------------------------------------------------------------------
// 服务：由 stepperTask 高频调用
// -----------------------------------------------------------------------------
void MechanismLib::service() {
    if (!_inited) {
        return;
    }

    if (_state != MECH_MOVING) {
        return;  // 没在动就什么都不做（不阻塞、不空转）
    }

    // ---- 1) 推进选中轴的走步状态机 ----
    //      StepperLib::service() 到点才发一个脉冲，没到点立刻返回，
    //      所以这里绝不会阻塞，也不会占满 CPU。
    if (_activeAxis == MECH_AXIS_LIFT && _lift != nullptr) {
        _lift->service();
    } else if (_activeAxis == MECH_AXIS_PUSH && _push != nullptr) {
        _push->service();
    } else {
        // 轴对象丢了（不该发生）：按失败处理，别让状态机卡在 MOVING
        finishCurrent(false, MECH_ERR_PARAM);
        return;
    }

    // ---- 2) 判"走完没有" ----
    //      ⚠️ 判据里**必须**看 busy()：
    //         软限位触发时 StepperLib 会自己 stop()，那时 busy() 变 false、
    //         stepsLeft() 被清成 0，实际只走了一部分 —— 这是"到边了"的正常保护，
    //         但上层必须知道"停下来"不等于"走完了要求的步数"。
    bool stillBusy = false;
    if (_activeAxis == MECH_AXIS_LIFT && _lift != nullptr) {
        stillBusy = _lift->busy();
    } else if (_activeAxis == MECH_AXIS_PUSH && _push != nullptr) {
        stillBusy = _push->busy();
    }

    if (stillBusy) {
        // ---- 3) 还在走：只做超时兜底检查 ----
        //     正常动作靠"走完步数"结束；超时防的是"电机因为某种原因一直 busy"。
        if ((millis() - _actionStartMs) > CFG_MECH_ACTION_TIMEOUT_MS) {
            if (_lift != nullptr) {
                _lift->stop();
            }
            if (_push != nullptr) {
                _push->stop();
            }
            finishCurrent(false, MECH_ERR_TIMEOUT);
        }
        return;
    }

    // ---- 4) 不 busy 了：判"走够了没有" ----
    //      走到位了 -> DONE；被软限位提前停住 -> FAILED（宁可报失败让人看一眼，
    //      也不要默默把"少走了一半"当成成功，然后拿去改书单）。
    int32_t after = (_activeAxis == MECH_AXIS_LIFT)
                        ? (_lift != nullptr ? _lift->position() : 0)
                        : (_push != nullptr ? _push->position() : 0);
    int32_t expected = _actionStartPos + _targetSteps;

    if (after == expected) {
        finishCurrent(true, MECH_ERR_NONE);
    } else {
        Serial.printf("机械: 动作被提前中断 —— 期望到 %ld, 实际到 %ld (差 %ld 步)\n",
                      (long)expected, (long)after, (long)(expected - after));
        Serial.println("  最常见原因: 撞到软限位, 或者机构卡住导致丢步。");
        finishCurrent(false, MECH_ERR_PARAM);
    }
}

// -----------------------------------------------------------------------------
// 结束当前动作
// -----------------------------------------------------------------------------
void MechanismLib::finishCurrent(bool ok, uint8_t error) {
    _lastError = error;

    if (ok) {
        _state = MECH_DONE;
        Serial.printf("机械: 动作完成 (轴=%s, 要求 %ld 步, 现在位置=%ld)\n",
                      (_activeAxis == MECH_AXIS_LIFT) ? "升降" : "推书",
                      (long)_targetSteps,
                      (long)((_activeAxis == MECH_AXIS_LIFT)
                                 ? (_lift ? _lift->position() : 0)
                                 : (_push ? _push->position() : 0)));
        return;
    }

    if (error == MECH_ERR_ESTOP) {
        _state = MECH_ESTOPPED;
        snprintf(_lastErrorText, sizeof(_lastErrorText),
                 "急停打断，位置需重新归零");
    } else {
        _state = MECH_FAILED;
        if (error == MECH_ERR_TIMEOUT) {
            snprintf(_lastErrorText, sizeof(_lastErrorText),
                     "动作超时：电机可能堵转/丢步/线松了");
            Serial.println("机械: 动作超时 —— 检查供电、接线和机构有没有卡住");
        } else {
            snprintf(_lastErrorText, sizeof(_lastErrorText),
                     "动作被提前中断(可能是软限位或丢步)");
        }
    }

    // ⚠️ 任何非正常结束都要把位置判为无效：
    //    开环系统里"没走完"意味着真实位置已经不知道了，不能继续当可靠位置用。
    _positionValid = false;
    _needHoming = true;
}

// -----------------------------------------------------------------------------
// 读结果 / 清结果
// -----------------------------------------------------------------------------
bool MechanismLib::takeResult(MechResult &out) {
    if (_state != MECH_DONE && _state != MECH_FAILED && _state != MECH_ESTOPPED) {
        return false;   // 还没有"已经结束"的动作
    }

    memset(&out, 0, sizeof(out));
    out.ok = (_state == MECH_DONE);
    out.estop = (_state == MECH_ESTOPPED);
    out.axis = _activeAxis;
    out.error = _lastError;
    out.targetSteps = _targetSteps;
    out.posAfter = (_activeAxis == MECH_AXIS_LIFT)
                       ? (_lift ? _lift->position() : 0)
                       : (_push ? _push->position() : 0);
    out.actualSteps = out.posAfter - _actionStartPos;  // 本次实际走掉的步数

    // 读走就回 IDLE：下一个动作才能开始（这是"请求-完成-确认"三段式）
    _state = MECH_IDLE;
    _activeAxis = MECH_AXIS_NONE;
    _targetSteps = 0;
    return true;
}

void MechanismLib::clearResult() {
    if (_state == MECH_DONE || _state == MECH_FAILED || _state == MECH_ESTOPPED) {
        _state = MECH_IDLE;
        _activeAxis = MECH_AXIS_NONE;
        _targetSteps = 0;
    }
}

// -----------------------------------------------------------------------------
// 状态查询
// -----------------------------------------------------------------------------
uint8_t MechanismLib::state() const {
    return _state;
}

uint8_t MechanismLib::activeAxis() const {
    return _activeAxis;
}

uint8_t MechanismLib::lastError() const {
    return _lastError;
}

const char *MechanismLib::lastErrorText() const {
    return _lastErrorText;
}

const char *MechanismLib::stateText() const {
    switch (_state) {
        case MECH_IDLE:
            return "空闲";
        case MECH_MOVING:
            return "动作进行中";
        case MECH_DONE:
            return "动作已完成";
        case MECH_FAILED:
            return "动作失败";
        case MECH_ESTOPPED:
            return "已急停";
        default:
            return "未知";
    }
}

bool MechanismLib::busy() const {
    return (_state == MECH_MOVING);
}

bool MechanismLib::positionValid() const {
    return _positionValid;
}

bool MechanismLib::needHoming() const {
    return _needHoming;
}

int32_t MechanismLib::liftSteps() const {
    return (_lift != nullptr) ? _lift->position() : 0;
}

int32_t MechanismLib::liftRemaining() const {
    return (_lift != nullptr) ? _lift->stepsLeft() : 0;
}

int32_t MechanismLib::pushSteps() const {
    // 推书是"一次动作推一本"，所以显示"最近一次动作的步数"比累计位置更有意义
    return _pushLastSteps;
}

int32_t MechanismLib::pushRemaining() const {
    return (_push != nullptr) ? _push->stepsLeft() : 0;
}

int32_t MechanismLib::liftRange() const {
    return (_lift != nullptr) ? _lift->rangeSteps() : 0;
}

int32_t MechanismLib::pushRange() const {
    return (_push != nullptr) ? _push->rangeSteps() : 0;
}

int32_t MechanismLib::moveCount() const {
    return (int32_t)_moveCount;
}

bool MechanismLib::jogAllowed(uint8_t axis, int32_t steps, bool allowInvert, bool rawMode) const {
    // 只做"能不能走"的预判，不改变任何状态（调试页用它把按钮置灰）
    if (!_inited || _state == MECH_MOVING || steps == 0) {
        return false;
    }
    Stepper28BYJ *motor = nullptr;
    int32_t dirSign = 1;
    int32_t limit = CFG_LIFT_MAX_MOVE_STEPS;
    if (axis == MECH_AXIS_LIFT) {
        motor = _lift;
        dirSign = CFG_LIFT_DIR_SIGN;
        limit = CFG_LIFT_MAX_MOVE_STEPS;
    } else if (axis == MECH_AXIS_PUSH) {
        motor = _push;
        dirSign = CFG_PUSH_DIR_SIGN;
        limit = CFG_PUSH_MAX_MOVE_STEPS;
    } else {
        return false;
    }
    if (motor == nullptr || !motor->isWired()) {
        return false;
    }
    if (axis == MECH_AXIS_PUSH && !allowInvert && (steps < 0)) {
        return false;
    }

    if (rawMode) {
        // 调试模式：软限位和单次上限都不管，只确认参数合法（非 0、没超硬上限）
        int32_t mag = (steps >= 0) ? steps : -steps;
        return (mag > 0) && (mag <= CFG_STEPPER_MAX_STEPS);
    }

    int32_t signedSteps = (dirSign >= 0) ? steps : -steps;
    int32_t mag = (signedSteps >= 0) ? signedSteps : -signedSteps;
    if (mag > limit) {
        return false;
    }
    return clampToRange(motor, signedSteps) != 0;
}

// -----------------------------------------------------------------------------
// 从 Flash 恢复位置
//
// ⚠️ 这里必须诚实说明一个限制：
//    Stepper28BYJ 目前只有 resetPosition()（把位置清 0），没有"写入任意步数"的接口，
//    而用 run() 去补步数是会**真的驱动电机**的（绝不允许开机盲动）。
//    所以本函数只做一件事：
//      ① 把位置基准清 0（= 本次上电位置），并把位置状态置成"无效 + 需要人工归零"。
//    也就是说：**掉电后保存的步数会被读出来显示和打印，但不会直接变成新的位置基准**，
//    想要精确恢复必须人工归零一次。这一点已在 README 和调试页里写明。
// -----------------------------------------------------------------------------
void MechanismLib::restorePosition(int32_t liftSteps) {
    if (_lift != nullptr && _lift->isWired()) {
        _lift->resetPosition();   // 只改软件计数，不发脉冲、不碰 GPIO
    }
    if (_push != nullptr && _push->isWired()) {
        _push->resetPosition();
    }

    // ⚠️ 位置一律判为无效、需要人工归零。
    //    理由：Stepper28BYJ 现在只有"把位置清 0"，没有"写入任意步数"的接口；
    //    而用 run() 去补步数会**真的驱动电机**。开机盲动是明确禁止的。
    //    所以 Flash 里的步数只用于"显示 + 打印"，不当作可靠位置。
    _positionValid = false;
    _needHoming = true;

    _state = MECH_IDLE;
    _activeAxis = MECH_AXIS_NONE;
    _lastError = MECH_ERR_POS_INVALID;
    snprintf(_lastErrorText, sizeof(_lastErrorText),
             "上电后位置未知，请在调试页人工归零");

    Serial.printf("机械: Flash 里记录的升降步数=%ld (仅作参考显示)\n",
                  (long)liftSteps);
    Serial.println("机械: 上电位置一律判为无效 —— 请先在调试页人工归零，再执行自动取书");
}

// -----------------------------------------------------------------------------
// 串口打印
// -----------------------------------------------------------------------------
void MechanismLib::print() {
    Serial.printf("机械: 状态=%s, 当前轴=%s, 位置有效=%s, 需要归零=%s\n",
                  stateText(),
                  (_activeAxis == MECH_AXIS_LIFT)
                      ? "升降"
                      : ((_activeAxis == MECH_AXIS_PUSH) ? "推书" : "无"),
                  _positionValid ? "是" : "否", _needHoming ? "是" : "否");
    Serial.printf("  升降: 累计=%ld 步, 剩余=%ld, 软限位=±%ld, 方向符号=%d\n",
                  (long)liftSteps(), (long)liftRemaining(), (long)liftRange(),
                  CFG_LIFT_DIR_SIGN);
    Serial.printf("  推书: 最近动作=%ld 步, 剩余=%ld, 软限位=±%ld, 方向符号=%d\n",
                  (long)pushSteps(), (long)pushRemaining(), (long)pushRange(),
                  CFG_PUSH_DIR_SIGN);
    Serial.printf("  每本书=%d 步, 推书行程=%d 步, 累计动作=%ld 次\n",
                  CFG_LIFT_STEPS_PER_BOOK, CFG_PUSH_OUT_STEPS,
                  (long)_moveCount);
    if (_lastErrorText[0] != 0) {
        Serial.printf("  上次问题: %s\n", _lastErrorText);
    }
}

const char *mechanismLibVersion() {
    return MECHANISMLIB_VERSION;
}