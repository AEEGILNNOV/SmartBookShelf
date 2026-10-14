#ifndef MECHANISM_LIB_H
#define MECHANISM_LIB_H

#include <Arduino.h>

#include "BookList.h"
#include "StepperLib.h"
#include "config.h"

// =============================================================================
// MechanismLib —— 机械层：两台步进电机的"一次只动一个轴"状态机
//
// 【它解决的问题】
//   StepperLib 只管"让某台电机走 N 步"，它不知道机械结构是什么、
//   也不知道"升降和推书不能同时动"。本库负责这些规则：
//     1) 一次只允许一个轴在动（升降在动就不许推书，反之亦然）；
//     2) 软限位 + 单次最大步数双重保护，绝不硬顶机构；
//     3) 动作有明确的生命周期（空闲/在动/完成/失败），其它任务靠它判断进度；
//     4) 急停后把"位置是否有效"置为无效，并且**不假装动作完成**；
//     5) 位置有效性由"人工归零"建立（没有限位开关，不做假自动寻零）。
//
// 【谁来跑它】
//   service() 必须由 stepperTask（唯一碰电机引脚的任务）周期性调用。
//   别的任务只能调 jog()/moveLiftTo()/movePushOut()/stopAll()/setZeroHere()，
//   这些函数只登记请求，真正的脉冲还是在 service() 里发。
//   所以**不允许**任何任务写 while (motor.busy()); 这种忙等待。
//
// 【本次机械分工（不要再按旧工程的三台电机写）】
//   升降电机：齿轮齿条带动整叠书上下，让目标书对准出书口；
//   推书电机：齿轮齿条把对准出书口的书水平推出。
//   ⚠️ 升降电机不能单独选择书堆内部的某一本书：要先移动整叠书，再水平推出。
// =============================================================================

// 轴
enum MechAxis : uint8_t {
    MECH_AXIS_NONE = 0,
    MECH_AXIS_LIFT = 1,   // 升降（整叠书上下）
    MECH_AXIS_PUSH = 2,   // 推书（水平推出）
};

// 动作状态
enum MechState : uint8_t {
    MECH_IDLE = 0,        // 空闲，可以接受新动作
    MECH_MOVING,          // 正在走步（busy）
    MECH_DONE,            // 上一个动作正常走完（等调用方读走，读走就回 IDLE）
    MECH_FAILED,          // 上一个动作失败（超时/被急停/步数非法）
    MECH_ESTOPPED,        // 上一个动作是被急停打断的
};

// 失败原因（给串口日志和调试页显示用）
enum MechError : uint8_t {
    MECH_ERR_NONE = 0,
    MECH_ERR_NOT_INITED,       // begin() 没调过
    MECH_ERR_NOT_WIRED,        // 这个轴没接线（config.h 里 ENABLE=0）
    MECH_ERR_PARAM,            // 步数非法（0 / 超上限 / 超软限位）
    MECH_ERR_BUSY,             // 别的轴在动，请求被拒绝
    MECH_ERR_TIMEOUT,          // 动作超时（电机可能堵转/丢步/线松了）
    MECH_ERR_ESTOP,            // 被急停打断
    MECH_ERR_POS_INVALID,      // 位置无效，拒绝了依赖绝对位置的动作
};

// 一次动作的结果（给 bookTask / 串口 / 界面用）
struct MechResult {
    bool        ok;            // 动作是否正常完成（走完了目标步数并且驱动正常结束）
    bool        estop;         // 是不是被急停打断的
    uint8_t     axis;          // 哪一个轴（MechAxis）
    uint8_t     error;         // 失败原因（MechError）
    int32_t     targetSteps;   // 本次要求走的步数（带符号）
    int32_t     actualSteps;   // 实际走掉的步数（带符号）
    int32_t     posAfter;      // 走完后的累计位置
};

class MechanismLib {
public:
    MechanismLib();

    // 注册两个轴。传 nullptr 表示这个轴不存在（config.h 里 ENABLE=0）。
    // ⚠️ 本对象的指针由调用方（main.cpp）持有，必须活得比任务长（用 static 全局对象）。
    void begin(Stepper28BYJ *lift, Stepper28BYJ *push);

    // ---- 请求接口（别的任务可以调，只登记请求，不碰 GPIO）----

    // 点动。steps 是**带符号步数**（正 = 正方向，负 = 反方向）。
    //   allowInvert=false 时只接受"朝行程正方向"的点动（推书回退用这个）。
    //   被软限位/上限/其它轴占用拦下来时返回 false 并把 lastError 置上原因。
    // rawMode=true：**调试用**，跳过"软限位"和"单次步数上限"两道业务限制，
    //   只保留"步数不为 0 / 轴在不在 / 硬上限 CFG_STEPPER_MAX_STEPS"这些参数校验。
    //   为什么需要它：标定阶段必须能把机构往边上顶、去量真实行程，软限位会挡住这件事。
    //   ⚠️ 只有调试页/手动调试走这条路；STORE / TAKE 自动流程始终用 rawMode=false。
    bool jog(uint8_t axis, int32_t steps, bool allowInvert, bool rawMode = false);

    // 从当前位置走到指定的**绝对步数位置**（相对人工零点）。
    //   位置无效（没人工归零过）时直接拒绝：MECH_ERR_POS_INVALID。
    //   ⚠️ 这是"依赖绝对位置"的动作，掉电恢复后没归零之前不允许执行。
    bool moveLiftTo(int32_t targetPos);

    // 把书推出：往前走 CFG_PUSH_OUT_STEPS（乘以方向符号）。
    //   仅在系统空闲时接受；推书机构重复推出会重复顶机构的，调用方负责判断。
    bool movePushOut();

    // 推书回退 CFG_PUSH_RETRACT_STEPS。
    //   ⚠️ 只有实际机械结构允许时才该调用（见 config.h 的 CFG_PUSH_AUTO_RETRACT）。
    bool movePushBack();

    // 急停：两个轴立刻停，清掉剩余步数并释放线圈。
    //   ⚠️ 急停后**不能**认为电机停在原目标位置：位置有效性会被置为无效。
    void stopAll();

    // 人工归零：把"当前位置"设为软件零点。
    //   · 两个轴都必须在空闲状态（在动的时候归零没有意义）
    //   · 清零软件步数 != 电机已经自动移动到机械原点，这一点必须在界面上写清楚
    //   返回 false = 有轴在动 / 没初始化
    bool setZeroHere();

    // ---- 由 stepperTask 调用 ----

    // 周期性服务：发脉冲、判完成、判超时。必须高频调用（STEPPER_SERVICE_INTERVAL_MS）。
    // ⚠️ 里面**没有任何 while 等待**：到点发一个脉冲就返回。
    void service();

    // 动作结束后把"完成/失败"状态读走，读走就回到 IDLE（这样下一个动作才能开始）。
    // 返回 false = 当前没有已经结束的动作可读。
    bool takeResult(MechResult &out);

    // 手动清掉已完成/失败的状态（急停后想重新开始、或调用方不想读结果时用）
    void clearResult();

    // ---- 状态查询（加锁/只读快照）----
    uint8_t     state() const;
    uint8_t     activeAxis() const;
    uint8_t     lastError() const;
    const char *lastErrorText() const;
    const char *stateText() const;
    bool        busy() const;              // 有轴在动
    bool        positionValid() const;     // 位置是否有效（人工归零过并且没被打断）
    bool        needHoming() const;        // 是否需要人工归零
    int32_t     liftSteps() const;         // 升降累计步数
    int32_t     liftRemaining() const;     // 升降剩余步数
    int32_t     pushSteps() const;         // 推书本次动作步数（走完就是最后一次的步数）
    int32_t     pushRemaining() const;     // 推书剩余步数
    int32_t     liftRange() const;
    int32_t     pushRange() const;
    int32_t     moveCount() const;         // 累计执行过多少次动作（诊断用）

    // 掉电恢复用：开机加载 Flash 后告诉机械层"Flash 里记的步数是多少"。
    //   ⚠️ 它**不会**把那个步数当成新的位置基准（Stepper28BYJ 没有"直接写位置"的接口，
    //      用 run() 补步数会真的驱动电机，绝不允许开机盲动）。
    //   实际效果：位置一律判为无效 + 需要人工归零，另加一条串口日志说明。
    //   保守是刻意的：宁可多要一次人工归零，也不要拿一个不知道对不对的位置去自动取书。
    // ⚠️ 已由 applyLoadedPosition() 取代（保留接口防止外部老调用编译不过）：
    void restorePosition(int32_t liftSteps);

    // 运行中从 Flash 重新加载位置时用（串口 q,l）。
    //   ⚠️ 和 restorePosition() 的区别：
    //     restorePosition() 是**开机**用的，一律判"位置无效+需归零"（那时候确实不知道位置）；
    //     applyLoadedPosition() 是**运行中**用户明确执行"从 Flash 加载"时用的，
    //       如果 Flash 里记的是"位置有效且不需要归零"，就把它恢复成有效 ——
    //       否则会出现"q,l 明明加载成功、位置还是无效、存书一直被拒"的怪现象。
    //   两个都会把当前位置计数清 0（因为机构上电/加载后就在现在这个位置，
    //   软件只能把"当前"当基准），这一点是开环系统的固有限制。
    void applyLoadedPosition(int32_t liftSteps, bool positionValid, bool needHoming);

    // 软限位：单次点动是否会被拦（调试页显示"能不能再走"）
    bool jogAllowed(uint8_t axis, int32_t steps, bool allowInvert, bool rawMode = false) const;

    // 串口打印当前机械状态（中文）
    void print();

private:
    // 按软限位把请求步数夹到允许范围（变 0 就说明已经到边了）
    int32_t clampToRange(Stepper28BYJ *motor, int32_t steps) const;
    // 结束当前动作（ok=false 时按原因分类成失败/急停）
    void finishCurrent(bool ok, uint8_t error);

    Stepper28BYJ *_lift;
    Stepper28BYJ *_push;
    bool         _inited;

    uint8_t      _state;         // MechState
    uint8_t      _activeAxis;    // MechAxis：当前/最近一个动作的轴
    uint8_t      _lastError;     // MechError

    bool         _positionValid; // 位置是否有效
    bool         _needHoming;    // 是否需要人工归零

    // 本次动作的上下文
    int32_t      _targetSteps;   // 要求走的步数（带符号）
    int32_t      _actionStartPos;// 动作开始时的累计位置（判"有没有走够"用）
    uint32_t     _actionStartMs; // 动作开始时刻（判超时用）
    int32_t      _pushLastSteps; // 推书最近一次动作的步数（界面显示用）

    uint32_t     _moveCount;     // 累计动作次数

    // 上一次失败原因文本（给调试页显示，中文，重新开始动作时清空）
    char         _lastErrorText[64];   // 留够一条完整中文（UTF-8 一个汉字 3 字节）
};

const char *mechanismLibVersion();

#endif