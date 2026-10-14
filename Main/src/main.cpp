#include <Arduino.h>

// 自己的库（都在 lib/ 下，PlatformIO 会自动加进 include 路径）
#include "BookDomain.h"
#include "ButtonLib.h"
#include "DisplayLib.h"
#include "HuskyLib.h"
#include "MechanismLib.h"
#include "SerialCmdLib.h"
#include "StepperLib.h"
#include "StorageLib.h"
#include "SysInfoLib.h"
#include "UltraLib.h"

// 全工程唯一配置来源（引脚/任务节奏/开关都在这里）
#include "config.h"

// =============================================================================
// SmartBookShelf / Main —— 主控整合程序（速定位书格）
//
// 这个文件只干五件事：建全局对象、建队列/互斥锁、注册串口命令、建任务、
// 在 stepperTask 里跑"机械动作 + 存书/取书流程"这一套状态机。
//
// 真正的功能都在 lib/ 里：
//   HuskyLib      二哈 I2C + 内置 CNN 分类结果读取（分类标签 = HuskyWord.id）
//   BookDomainLib 学科 ID 1~6 的映射 / 重复检查 / 书堆顺序（业务规则的唯一来源）
//   BookListLib   书单动态数组（带互斥锁 + 快照）
//   MechanismLib  升降 + 推书两个轴的非阻塞互斥状态机（软限位 / 急停 / 人工归零）
//   StepperLib    28BYJ-48 + ULN2003 底层走步（非阻塞 service()）
//   StorageLib    Flash(NVS) 保存书单 + 位置 + 掉电恢复标记
//   DisplayLib    ILI9341 + LVGL 中文界面（自带 displayTask，含第 6 页调试页）
//   UltraLib      HC-SR04 超声波测距（非阻塞状态机）
//   SysInfoLib    系统信息采集
//   ButtonLib     按键消抖 + 事件队列
//   SerialCmdLib  串口命令行解析 + 分派
//
// 【任务与数据流向（本次改造后）】
//   recogTask(核1)   唯一碰二哈：读 5 秒一帧的分类结果 -> 防抖 -> 请求入库
//   stepperTask(核1) 唯一碰电机引脚：
//                    ① 消费 MotorQueue（串口 m,/z 手动点动）
//                    ② 消费 MechQueue（调试页/按键/串口投进来的安全请求）
//                    ③ 调 mechanism.service() 推进机械状态机
//                    ④ 跑"存书/取书"流程状态机（机械动作 + 数据落盘）
//   waveTask(核0)    唯一碰 TRIG/ECHO（默认关，见 config.h 第 1 节的引脚冲突说明）
//   monitorTask(核0) 系统信息采集 -> 共享结构体 + 显示库
//   displayTask(库内) 唯一碰 LVGL；调试页的按钮只投队列，不碰 GPIO
//   serialCmdTask(库内) 命令分派；机器测试命令统一进队列，不绕过软限位
//   loop() 只做按键事件转发和短延时，不写业务
//
// 【为什么"存书/取书流程"要放在 stepperTask 里】
//   MechanisemLib 的状态变量（_state/_activeAxis/_targetSteps）只被"发起动作"和
//   "推进动作"两处读写。如果发起放在一个任务、推进放在另一个任务，就必须给每次
//   访问都加锁，还会出现"刚判断空闲、下一秒被另一个任务占用"的竞态。
//   把两者都放进 stepperTask 之后：机械状态**只有一个任务在碰**，不需要锁，
//   其它任务只能往 MechQueue 投请求。这是最简单、也最不容易出错的做法。
// =============================================================================

// -----------------------------------------------------------------------------
// 全局对象 / 句柄
// -----------------------------------------------------------------------------
static HuskyLib husky;
static DisplayLib display;
static ButtonLib buttons;
static SerialCmdLib cli;
static StorageLib storage;

// 书单：**main.cpp 是唯一所有者**（显示库用 attachBookList() 借用同一份数据）
// 这样"显示列表 / Flash 内容 / 机械位置计算"用的是同一份业务数据，不会各维护一份。
static BookList g_books;

// 系统信息共享结构体（infoMutex 保护）
static SemaphoreHandle_t infoMutex = nullptr;
static SystemInfo systemInfo = {};

// ---- 两台步进电机：升降 + 推书（引脚全部来自 config.h 第 9 节）----
// ⚠️ 只有 stepperTask 能调 service()，也只有 stepperTask 能调 mechanism 的动作函数。
//    别的任务想动电机，必须往 MechQueue / MotorQueue 投请求。
//    传 nullptr 表示这台电机没接线：begin() 不会碰引脚，service() 立刻返回。
static const uint8_t STEPPER_PINS_LIFT[4] = CFG_STEPPER_LIFT_PINS;
static const uint8_t STEPPER_PINS_PUSH[4] = CFG_STEPPER_PUSH_PINS;

static Stepper28BYJ motorLift(CFG_STEPPER_LIFT_ENABLE ? STEPPER_PINS_LIFT : nullptr);
static Stepper28BYJ motorPush(CFG_STEPPER_PUSH_ENABLE ? STEPPER_PINS_PUSH : nullptr);

// 机械层：把两个轴包成"一次只动一个"的状态机
static MechanismLib mechanism;

// 手动步进命令队列（串口 m / z 投递，stepperTask 消费；单一消费者，不用锁）
static QueueHandle_t MotorQueue = nullptr;

// ---- 机械请求队列：调试页 / 按键 / 串口 / 识别任务都往这里投 ----
// 元素是 MechRequest，长度 CFG_MECH_QUEUE_SIZE。
// ⚠️ 这里只传"请求"，不传电机对象：真正的动作由 stepperTask 执行。
struct MechRequest {
    uint8_t cmd;      // MechCmd
    int32_t arg;      // 步数 / 书单下标 / 分类 ID，按 cmd 解释
    // 1 = 这是"调试动作"：跳过软限位和单次步数上限（标定要用，见 CFG_DEBUG_IGNORE_LIMITS）
    // 存书/取书这些自动流程永远是 0，始终受软限位保护。
    uint8_t raw;
};
static QueueHandle_t MechQueue = nullptr;

// 串口/按键要发给 stepperTask 的机械命令
enum MechCmd : uint8_t {
    MECH_REQ_NONE = 0,
    MECH_REQ_JOG,            // arg = 带符号步数，步长由发起方决定；用 jogSteps 走哪一路见下
    MECH_REQ_JOG_LIFT,       // arg = 带符号步数（升降点动）
    MECH_REQ_JOG_PUSH,       // arg = 带符号步数（推书点动；负数=回退，需 CFG_PUSH_AUTO_RETRACT）
    MECH_REQ_STOP_ALL,       // 急停（arg 不用）
    MECH_REQ_SET_ZERO,       // 人工归零（arg 不用）
    MECH_REQ_SAVE_NOW,       // 保存当前位置到 Flash（arg 不用）
    MECH_REQ_CLEAR_BOOKS,    // 清空书籍数据（已二次确认；**不动机械位置**）
    MECH_REQ_STORE_BOOK,     // 存书：arg = 分类 ID（1~6）
    MECH_REQ_CONFIRM_STORE,  // 用户确认"书已经放好了"
    MECH_REQ_TAKE_BOOK,      // 取书：arg = 书单下标（-1 = 按下标不知道，用 takeById）
    MECH_REQ_TAKE_ID,        // 取书：arg = 分类 ID
};

// ---- 超声波测距（HC-SR04）----
// ⚠️ 引脚与升降电机的 IN 脚冲突，默认 CFG_ENABLE_WAVE=0（见 config.h 第 8 节）。
//    只有 waveTask 碰这个对象，别的任务读 waveMutex 保护的快照。
static HCSR04 waveSensor(PIN_WAVE_TRIG, PIN_WAVE_ECHO, CFG_WAVE_TIMEOUT_US);
static WaveState waveState = {};
static SemaphoreHandle_t waveMutex = nullptr;

// 滤波窗口：最近 CFG_WAVE_FILTER_N 次有效读数，取平均后清零重来
static uint32_t waveFilterBuf[CFG_WAVE_FILTER_N > 0 ? CFG_WAVE_FILTER_N : 1] = {};
static uint8_t waveFilterCount = 0;

// -----------------------------------------------------------------------------
// 识别结果 -> 入库请求：防抖 + "同一帧只处理一次"
// -----------------------------------------------------------------------------
// 为什么要防抖：需求要求"识别结果发生变化时必须重新验证有效性，
// 不能仅根据单次瞬时变化触发机械动作"。所以同一个分类 ID 要连续识别到
// CFG_BOOK_CONFIRM_FRAMES 次才认为稳定。
static int16_t lastRawId = -1;          // 上一帧的分类 ID（-1 = 没有有效结果）
static uint8_t stableCount = 0;         // 同一个 ID 连续出现了几次
static int16_t lastHandledId = -1;      // 已经"处理过（请求过入库）"的 ID

// ---- 识别链路的诊断信息（串口命令 j 打印，排查"识别到了但进不了数组"）----
//
// 为什么要有这几个：从"二哈看到书"到"动态数组里真的多一条"中间有好几道关卡
// （算法不对 / ID 无效 / 防抖没攒够 / 判定已存在 / 位置无效 / 流程忙 / 队列满），
// 任何一道没过，现象都是"数组里还是空的"，光看现象分不出是哪一步。
static int16_t recogLastId = -1;            // 最近一帧的分类 ID（-1 = 画面里没目标）
static int16_t recogLastWords = 0;          // 最近一帧识别到几个块
static uint8_t recogStable = 0;             // 这个 ID 连续稳定了几帧
static const char *recogModeText = "?";     // 最近一次读帧时的算法名
static int16_t recogModeIdx = -1;           // 最近一次读帧时的算法下标
static char recogNote[CFG_BOOK_NOTE_LEN] = "";  // 最后一次判定结果（中文）

// 二哈"读到第几帧"的序号：每次成功读到一帧就 +1。
// 取书流程回退后靠它判断"这次读到的到底是不是一帧新的识别结果"
// （跨任务读一个 32 位计数，最坏读到上一拍，不影响正确性）。
static volatile uint32_t recogFrameSeq = 0;

// 上一次识别/入库的结果提示（给界面显示，说明"为什么没入库"）
static char lastStoreNote[CFG_BOOK_NOTE_LEN] = "";

// 是否需要用户"确认放书"（存书流程里用它告诉界面"等你按确认"）
static volatile bool storeWaitingConfirm = false;
// 用户按下确认放书（串口 v,c / A 键 / 调试页都会置它，流程消费一次就清）
static volatile bool storeConfirmRequested = false;

// 电机名字，打印用
static const char *MOTOR_NAME[2] = { "升降", "推书" };

// ---- 位置自动落盘：机械"刚停下来"就把当前位置写进 Flash ----
//
// 为什么要有这个：用户要求"以后每次位置改变（电机移动、或点了归零）都自动写入 Flash"。
// 实现方式：stepperTask 每轮看一次 mechanism.busy()，
//   上升沿（开始动）-> 清标记；下降沿（刚停）-> 自动保存一次。
//
// ⚠️ 为什么用"busy 从 true 变 false"而不是 takeResult()：
//    takeResult() 会把结果读走，而存书/取书流程还要用它判断"动作完成没有"，
//    在这里读走就会把流程卡死。busy() 是只读的，不影响任何流程。
//
// ⚠️ 写入频率：一次动作只写一次，**不是每个脉冲都写**。
//    28BYJ 一分钟好几百步，每步都写 NVS 的话 Flash 很快就被写坏了。
static bool mechWasBusy = false;      // 上一轮机械是否在动（用来找下降沿）
static bool autoSavedThisMove = false;// 本次动作是否已经自动保存过（防重复写）
// =============================================================================
// 【位置持久化模型 —— 为什么不能直接存 mechanism.liftSteps()】
//
//   实机踩到的坑: 开机 / q,l 加载读到 "升降 300 步"，但紧接着自动保存把
//   mechanism.liftSteps() 写回去 —— 而 applyLoadedPosition() 刚把机构计数清成了 0，
//   于是 Flash 立刻被覆盖成 0。用户看到的就是"300 怎么变 0 了，根本没读进去"。
//
//   为什么 300 是对的、不该丢:
//     300 是上一次人工归零之后建立的位置基准（"这个位置我认作零点"，软件记 0）。
//     掉电重启后真实世界没变，机构还在同一个物理位置，所以 300 仍然是有效的位置信息。
//     applyLoadedPosition() 把软件计数清 0，是因为开环系统只能把"当前"当基准；
//     但 Flash 里那份历史值不该被自动保存覆盖掉。
//
//   模型: 要写进 Flash 的值 = base + offset
//     base   = 加载时从 Flash 读到的那个值（本次上电之前的历史）
//     offset = 加载之后、相对于"加载时刻机构位置"的累计位移（带符号）
//   机构每动一次 offset 就累加一次；每次成功保存后 offset 归零、base 变成刚存的值。
//   人工归零时 base 和 offset 都清 0（零点重建了）。
// =============================================================================
static int32_t g_persistBase = 0;      // 加载时的历史步数（或上次保存后的值）
static int32_t g_pendingPosOffset = 0; // 加载之后累计的机构位移
static int32_t g_mechPosAtLastSave = 0;// 上次保存时刻的 mechanism.liftSteps()（用来算 delta）
static int32_t lastSavedLiftSteps = 0; // = g_persistBase + g_pendingPosOffset（兜底比对用）
static uint32_t lastAutoSaveMs = 0;   // 上一次自动保存的时刻（兜底限流，别写太勤）

// -----------------------------------------------------------------------------
// 前置函数声明
// -----------------------------------------------------------------------------
static void recogTask(void *pvParameters);
static void monitorTask(void *pvParameters);
static void stepperTask(void *pvParameters);
static void waveTask(void *pvParameters);
static void handleButtonEvent(const ButtonEvent &event);

static void printBanner();
static void printStatus();
static void printStepperState();
static void printWaveState();
static void printBookList();

static void cmdHelp(const char *arg);
static void cmdScan(const char *arg);
static void cmdInfo(const char *arg);
static void cmdReconnect(const char *arg);
static void cmdNextPage(const char *arg);
static void cmdAlgo(const char *arg);
static void cmdLearn(const char *arg);
static void cmdForget(const char *arg);
static void cmdSaveSlot(const char *arg);
static void cmdLoadSlot(const char *arg);
static void cmdParam(const char *arg);
static void cmdBooks(const char *arg);
static void cmdClearBooks(const char *arg);
static void cmdMotor(const char *arg);
static void cmdStopMotor(const char *arg);
static void cmdWave(const char *arg);
static void cmdDisplay(const char *arg);
static void cmdTouch(const char *arg);
static void cmdDebug(const char *arg);
static void cmdFlash(const char *arg);
static void cmdBookFlow(const char *arg);
static void cmdBooksDiag(const char *arg);

static Stepper28BYJ *getMotor(int index);
static bool postMechRequest(uint8_t cmd, int32_t arg, uint8_t raw = 0);
static void handleDebugCmd(uint8_t cmd, int16_t arg);
static void handleDebugButton(uint8_t index, bool longPress);
static const char *pageName(uint8_t page);

// -----------------------------------------------------------------------------
// 工具函数
// -----------------------------------------------------------------------------
static void i2cScan() {
    Serial.println("--- I2C 总线扫描 ---");
    int found = 0;
    for (uint8_t a = 1; a < 127; a++) {
        Wire.beginTransmission(a);
        if (Wire.endTransmission() == 0) {
            Serial.printf("  发现设备 0x%02X%s\n", a,
                          (a == HUSKY_I2C_ADDR) ? "   <-- HUSKYLENS 2" : "");
            found++;
        }
    }
    if (found == 0) {
        Serial.println("  未发现任何 I2C 设备!");
        Serial.printf("  检查: 供电? GND 共地? SDA=GPIO%d, SCL=GPIO%d?\n",
                      PIN_HUSKY_SDA, PIN_HUSKY_SCL);
    }
    Serial.println("--- 扫描结束 ---");
}

// 按下标取电机：0 = 升降, 1 = 推书（本次改造只有两台）
static Stepper28BYJ *getMotor(int index) {
    switch (index) {
        case 0:
            return &motorLift;
        case 1:
            return &motorPush;
        default:
            return nullptr;
    }
}

// 往机械请求队列投一条请求。队列满就返回 false 并打日志（绝不阻塞等待）。
static bool postMechRequest(uint8_t cmd, int32_t arg, uint8_t raw) {
    if (MechQueue == nullptr) {
        Serial.println("机械: 请求队列没建好");
        return false;
    }
    MechRequest req;
    req.cmd = cmd;
    req.arg = arg;
    req.raw = raw;
    if (xQueueSend(MechQueue, &req, pdMS_TO_TICKS(20)) != pdPASS) {
        Serial.println("机械: 请求队列满, 请稍后再试");
        return false;
    }
    return true;
}

// 把当前书单顺序写成一句话（调试页/日志用）
static void buildBookOrderText(char *out, size_t outSize) {
    bookOrderText(g_books, out, outSize);
}

// -----------------------------------------------------------------------------
// 开机横幅
// -----------------------------------------------------------------------------
static void printBanner() {
    Serial.println();
    Serial.println("==================================================");
    Serial.println(" SmartBookShelf / Main  (速定位书格 · 两台电机版)");
    Serial.println("==================================================");
    Serial.printf("库版本: husky=%s display=%s sysinfo=%s button=%s cmd=%s\n",
                  huskyLibVersion(), displayLibVersion(), sysInfoVersion(),
                  buttonLibVersion(), serialCmdLibVersion());
    Serial.printf("       booklist=%s domain=%s mech=%s storage=%s\n",
                  bookListLibVersion(), bookDomainLibVersion(),
                  mechanismLibVersion(), storageLibVersion());
    Serial.printf("接线: 二哈 SDA=GPIO%d SCL=GPIO%d | 屏 MOSI=%d SCLK=%d CS=%d "
                  "DC=%d RST=%d BL=%d\n",
                  PIN_HUSKY_SDA, PIN_HUSKY_SCL, PIN_TFT_MOSI, PIN_TFT_SCLK,
                  PIN_TFT_CS, PIN_TFT_DC, PIN_TFT_RST, PIN_TFT_BL);
    Serial.printf("按键: BTN1=%d BTN2=%d BTN3=%d A键=%d B键=%d (按下为%s电平)\n",
                  BUTTON_PIN1, BUTTON_PIN2, BUTTON_PIN3, BUTTON_PIN4,
                  BUTTON_PIN5, (BUTTON_TRUE_VALUE != 0) ? "高" : "低");

    // 步进引脚宏展开成 {...} 初始化列表，不能直接塞 printf 变参，先落成数组再逐个数
    static const uint8_t liftPins[4] = CFG_STEPPER_LIFT_PINS;
    static const uint8_t pushPins[4] = CFG_STEPPER_PUSH_PINS;
    Serial.printf("升降电机: %d,%d,%d,%d   推书电机: %d,%d,%d,%d\n",
                  liftPins[0], liftPins[1], liftPins[2], liftPins[3],
                  pushPins[0], pushPins[1], pushPins[2], pushPins[3]);
    Serial.printf("测距: TRIG=%d ECHO=%d (开关=%d, ECHO 必须分压且与升降电机冲突)\n",
                  PIN_WAVE_TRIG, PIN_WAVE_ECHO, CFG_ENABLE_WAVE);
    Serial.printf("触摸: T_CS=%d T_IRQ=%d T_CLK=%d T_DIN=%d T_DO=%d (开关=%d)\n",
                  PIN_TOUCH_CS, PIN_TOUCH_IRQ, PIN_TOUCH_CLK, PIN_TOUCH_DIN,
                  PIN_TOUCH_DO, CFG_ENABLE_TOUCH);
    Serial.printf("机械参数(待实机标定): 每本书=%d 步, 升降软限位=±%d, 推书行程=%d\n",
                  CFG_LIFT_STEPS_PER_BOOK, CFG_STEPPER_LIFT_RANGE_STEPS,
                  CFG_PUSH_OUT_STEPS);
    Serial.printf("识别周期=%dms, 测距周期=%dms, 入库防抖=%d 帧\n",
                  HUSKY_READ_INTERVAL_MS, WAVE_MEASURE_INTERVAL_MS,
                  CFG_BOOK_CONFIRM_FRAMES);
    Serial.println("二哈屏幕: 系统设置 -> 协议类型 -> I2C; 分类算法用 a,2(物体分类) 或 a,3(自学习分类)");
}

// -----------------------------------------------------------------------------
// 状态打印：二哈 + 系统信息 + 机械 + 测距 + 书单
// -----------------------------------------------------------------------------
static void printStatus() {
    husky.printState();

    SystemInfo snap = {};
    if (infoMutex != nullptr &&
        xSemaphoreTake(infoMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        snap = systemInfo;
        xSemaphoreGive(infoMutex);
    }
    sysInfoPrint(snap);

    Serial.printf("书单: 共 %d 本 (每个学科最多一本)\n", (int)display.bookCount());
    printBookList();
    printStepperState();
    printWaveState();
    // 书单为空时，直接指路到全链路诊断，省得用户不知道下一步敲什么
    if (g_books.count() == 0) {
        Serial.println("书单是空的 -> 敲 j 看 识别->入库 全链路诊断（会指出卡在哪一步）");
    }

    StorageStatus st = {};
    storage.status(st);
    storage.print();
    (void)st;
}

// 打印整张书单（含逻辑书堆顺序）
static void printBookList() {
    char order[CFG_BOOK_NOTE_LEN];
    buildBookOrderText(order, sizeof(order));
    Serial.printf("  书堆顺序(顶->底): %s\n", order);
    display.printBooks();
}

// 把"机构自己变了多少"折算进 pendingPosOffset。
//
// ⚠️⚠️ 【这是本模块最关键的一步，必须从机构位置自己推导】
//   一开始我是在"调试页 jog / 按键 jog"那两条分支里手动累加的 —— 结果漏掉了
//   串口 m,1,125 那条路（它走 runMotor() -> mechanism.jog()，不经过那两条分支），
//   于是用户看到"机构明明走到 250，保存却是 0"。
//   现在改成：不管是谁让机构动的，只要 mechanism.liftSteps() 变了，这里就算得出来。
//   这样"位置变了就一定落盘"才是真的，而不是看运气走哪条命令。
static void syncPosOffsetFromMechanism() {
    int32_t now = mechanism.liftSteps();
    g_pendingPosOffset += (now - g_mechPosAtLastSave);
    g_mechPosAtLastSave = now;
}

// 当前应该写进 Flash 的升降步数（= 历史基准 + 加载后的累计位移）。
// 每次调用都先把"机构实际位置"折算进去，所以任何调用路径看到的都是最新值。
static int32_t persistedLiftSteps() {
    syncPosOffsetFromMechanism();
    return g_persistBase + g_pendingPosOffset;
}

// 保存成功后统一记账：base 变成刚写进去的值，offset 归零。
static void commitPersistAccounting(int32_t savedValue) {
    g_persistBase = savedValue;
    g_pendingPosOffset = 0;
    g_mechPosAtLastSave = mechanism.liftSteps();
    lastSavedLiftSteps = savedValue;
}

// -----------------------------------------------------------------------------
// Flash 保存：书单 + 升降步数 + 位置状态 一次全写
//
// 【什么时候调它】
//   新书入库后 / 取书正常完成后 / 升降动作正常完成后 / 人工归零后 / 用户显式保存。
//   **绝不**每走一步就写一次 Flash（那样几百步就把 NVS 写坏）。
// -----------------------------------------------------------------------------
static bool saveAllToFlash(const char *why) {
    bool needHoming = mechanism.needHoming();
    bool positionValid = mechanism.positionValid();

    // ⚠️ 存的是 persistedLiftSteps()，**不是** mechanism.liftSteps()：
    //    后者在加载之后是 0（软件计数被清过），直接存会把 Flash 里的历史值抹掉。
    int32_t steps = persistedLiftSteps();
    bool ok = storage.saveAll(g_books, steps, positionValid, needHoming);
    if (ok) {
        commitPersistAccounting(steps);
        Serial.printf("存储: 已保存 (%s) 书单=%d 本, 升降步数=%ld, 位置有效=%s, 需要归零=%s",
                      why, (int)display.bookCount(), (long)steps,
                      positionValid ? "是" : "否", needHoming ? "是" : "否");
        Serial.printf("        (记账: base=%ld offset=%ld 机构=%ld)\n",
                      (long)g_persistBase, (long)g_pendingPosOffset,
                      (long)mechanism.liftSteps());
    } else {
        Serial.printf("存储: ❌ 保存失败 (%s), 看上面 StorageLib 的日志\n", why);
    }
    return ok;
}

// 界面提示：把一条消息同时写到屏幕和串口。
// ⚠️ 原设计是"错误状态不能只打串口"，所以用户可见的问题都走这里。
// ⚠️ 用户要求（本次）：**界面弹窗全部禁掉**（config.h 的 CFG_ENABLE_NOTIFY_POPUP = 0）——
//    包括"已把当前位置设为零点"这类弹窗都不再出现，只保留串口日志。
//    把那个宏改回 1 就恢复弹窗，不用动这里的代码。
static void notify(const char *text, uint32_t ms) {
#if CFG_ENABLE_NOTIFY_POPUP
    if (CFG_RUN_DISPLAY) {
        display.showMessage(text, ms);
    }
#else
    (void)ms;   // 关掉弹窗后 ms 没人用，显式忽略，避免编译告警
#endif
    Serial.printf("提示: %s\n", text);
}

// -----------------------------------------------------------------------------
// 串口命令实现
//
// 【本次改造保留的原则】
//   · 老命令字母和含义一律不动（?/h/s/i/r/n/a/l/f/w/o/p/b/c/m/z/u/d/t）；
//   · 新增命令用没被占用的字母：g(调试页) / v(取书存书) / q(Flash 持久化)；
//   · 机械相关的命令**全部走 stepperTask 的队列**，不直接调电机，
//     这样软限位、忙碌判断、急停都能生效，串口也绕不过去。
// -----------------------------------------------------------------------------
static void cmdHelp(const char *arg) {
    cli.printHelp();
    DisplayLib::printHelp();
    Serial.println("算法切换也可以直接按 1~8 号按键字符。");
}

static void cmdScan(const char *arg) {
    i2cScan();
}

static void cmdInfo(const char *arg) {
    printStatus();
}

static void cmdReconnect(const char *arg) {
    husky.markOffline();
    Serial.println("已标记掉线, 正在重连...");
}

static void cmdNextPage(const char *arg) {
    display.nextPage();
    Serial.printf("屏幕切到第 %u 页\n", (unsigned)display.page() + 1);
}

// 切算法：可以直接给数字(在模式表里的位置 1~8 字符)，不给就切下一个
// ⚠️ 本项目要做"分类 ID 1~6"，所以要用分类类算法：a,2（物体分类）或 a,3（自学习分类）。
static void cmdAlgo(const char *arg) {
    if (arg != nullptr && arg[0] != 0) {
        if (husky.setModeByAlpha(arg[0])) {
            Serial.printf("切换算法 -> %s (正在重连...)\n", husky.modeName());
        } else {
            Serial.println("没这个算法编号, 输入 ? 看列表");
        }
        return;
    }
    husky.setModeNext();
    Serial.printf("切换算法 -> %s (正在重连...)\n", husky.modeName());
}

static void cmdLearn(const char *arg) {
    if (!husky.online()) {
        Serial.println("未连接, 不能学习");
        return;
    }
    Serial.println("学习当前画面中心目标(镜头对准目标)...");
    uint8_t id = husky.learn();
    if (id == 0) {
        Serial.println("  学习失败(画面里没目标, 或二哈正忙)");
        return;
    }
    Serial.printf("  学习成功: ID=%u -> 学科名 %s\n", (unsigned)id,
                  getBookNameById((int16_t)id));
    Serial.println("  提醒: 学习结果断电会丢, 用 w 或 w,槽位 保存到知识库。");
}

static void cmdForget(const char *arg) {
    Serial.println(husky.forget() ? "已清空当前算法的学习内容" : "清空失败");
}

static void cmdSaveSlot(const char *arg) {
    uint8_t slot = 0;
    if (arg != nullptr && arg[0] != 0) {
        slot = (uint8_t)atoi(arg);
    }
    if (slot > 3) {
        slot = 3;  // 二哈只有 0~3 四个槽位
    }
    Serial.println(husky.saveKnowledge(slot) ? "知识库已保存(断电不丢)"
                                             : "保存失败");
}

static void cmdLoadSlot(const char *arg) {
    uint8_t slot = 0;
    if (arg != nullptr && arg[0] != 0) {
        slot = (uint8_t)atoi(arg);
    }
    if (slot > 3) {
        slot = 3;
    }
    Serial.println(husky.loadKnowledge(slot) ? "已读取知识库" : "读取失败");
}

static void cmdParam(const char *arg) {
    husky.dumpAlgoParam();
}


// 串口命令 b：书列表页 + 书单操作
//   b          跳到书列表页并打印条数
//   b,a        加一条测试书（没有二哈也能把列表/滑动试出来）
//   b,a,8      加 8 条测试书
//   b,p        串口打印整张书单 + 书堆顺序
// ⚠️ 测试书用的是 id=0（不是 1~6），所以它们只影响界面测试，
//    不会和"每个学科最多一本"的真实业务数据混淆。
static void cmdBooks(const char *arg) {
    char sub = (arg != nullptr && arg[0] != 0) ? arg[0] : 0;

    if (sub == 'a' || sub == 'A') {
        int want = 1;
        const char *comma = strchr(arg, ',');
        if (comma != nullptr) {
            want = atoi(comma + 1);
        }
        if (want < 1) {
            want = 1;
        }
        if (want > 40) {
            want = 40;  // 一次别加太多，够测滑动就行
        }

        static int16_t testSeq = 0;  // 测试数据编号，一直往上加
        int16_t added = 0;
        for (int i = 0; i < want; i++) {
            char name[32];
            testSeq++;
            snprintf(name, sizeof(name), "测试书 %d", (int)testSeq);
            if (display.addBook(name) >= 0) {
                added++;
            }
        }
        Serial.printf("书单: 已加 %d 条测试数据, 现在共 %d 条 (输入 b 看书列表页)\n",
                      (int)added, (int)display.bookCount());
        return;
    }

    if (sub == 'p' || sub == 'P') {
        printBookList();
        return;
    }

    Serial.printf("已记录书数: %d\n", (int)display.bookCount());
    display.setPage(PAGE_BOOKS);
    Serial.println("屏幕切到书列表页: 每行右边是“取出”按钮(走取书流程), 列表可以上下滑动");
}

// 串口命令 c：清空书籍数据
// ⚠️ **只清书单，绝对不清机械位置**（需求明确要求两个操作互相独立）
static void cmdClearBooks(const char *arg) {
    display.clearBooks();
    Serial.println("书单: 已清空书籍数据");
    saveAllToFlash("清空书籍数据");
    Serial.println("  注意: 机械位置没有被动过 —— 位置要重新归零请用 g,zero 或调试页按钮。");
}

// 解析步进命令：[电机号,]动作。line 形如 "1,300" / "1,-300" / "z" / "s1500"
// 返回去掉“电机号,”之后的动作串，*outIndex 回填 0~1，没写电机号就保持原值。
static char *parseMotorCmd(char *line, int *outIndex) {
    int first = atoi(line);  // 字符串开头不是数字时 atoi 返回 0
    if (line[0] >= '1' && line[0] <= '2' && first >= 1 && first <= 2) {
        char *comma = strchr(line, ',');
        if (comma != nullptr) {
            *outIndex = first - 1;
            return comma + 1;
        }
    }
    return line;
}

// 让某台电机走 steps 步（**只在 stepperTask 里调**）
//
// 和旧版本的区别：现在只有升降/推书两个轴，而且动作统一走 MechanismLib，
// 这样软限位、互斥、急停、方向符号都对得上。
static void runMotor(int index, int32_t steps) {
    if (index < 0) {
        index = 0;  // 默认升降
    }
    if (index > 1) {
        Serial.println("步进: 电机号只能是 1(升降) / 2(推书)");
        return;
    }

    // 手动调试命令允许推书往回走（allowInvert=true），因为调试的目的就是看机构
    uint8_t axis = (index == 0) ? MECH_AXIS_LIFT : MECH_AXIS_PUSH;
    if (!mechanism.jog(axis, steps, true)) {
        Serial.printf("步进: %s 命令被拒绝 —— %s\n", MOTOR_NAME[index],
                      mechanism.lastErrorText());
    }
}

// 打印两个电机 + 机械层的状态（串口命令 m,? 和 i 都用它）
static void printStepperState() {
    Serial.printf("步进(驱动=%s, 模式=%s, 步/圈=%d):\n",
                  (CFG_STEPPER_DRIVER == 0) ? "28BYJ+ULN2003" : "42步进+驱动器",
                  (CFG_STEPPER_WAVE_MODE == 0) ? "八拍半步" : "四拍整步",
                  STEPPER_STEPS_PER_REV);

    for (int i = 0; i < 2; i++) {
        Stepper28BYJ *m = getMotor(i);
        if (m == nullptr || !m->isWired()) {
            Serial.printf("  %s: 未接线/未启用\n", MOTOR_NAME[i]);
            continue;
        }
        Serial.printf("  %s: pos=%ld 步, %s, 剩余=%ld, 速度=%luus/步, 软限位=±%ld\n",
                      MOTOR_NAME[i], (long)m->position(),
                      m->busy() ? "在走" : "停止", (long)m->stepsLeft(),
                      (unsigned long)m->stepDelayUs(), (long)m->rangeSteps());
    }

    mechanism.print();
}

// 串口命令 m：步进电机手动调试
//   m              看用法
//   m,?            打印两个电机 + 机械层状态
//   m,1,300        升降正转 300 步
//   m,1,-300       升降反转 300 步
//   m,300          不写电机号，默认升降
//   m,z            全部急停
//   m,s1500        两个电机速度都改成 1500us/步
// ⚠️ 这些命令不直接动电机：它们走 MotorQueue 交给 stepperTask，
//    所以软限位、忙碌互斥、急停都照样生效，串口绕不过去。
static void cmdMotor(const char *arg) {
    if (arg == nullptr || arg[0] == 0) {
        Serial.println("步进命令用法:");
        Serial.println("  m            看这段用法");
        Serial.println("  m,?          打印两个电机状态(位置/行程/速度)");
        Serial.println("  m,1,300      升降电机正转 300 步");
        Serial.println("  m,1,-300     升降电机反转 300 步");
        Serial.println("  m,2,600      推书电机推出 600 步");
        Serial.println("  m,2,-600     推书电机回退(机械允许才用!)");
        Serial.println("  m,300        不写电机号, 默认升降");
        Serial.println("  m,z          全部急停");
        Serial.println("  m,s1500      两个电机速度都改成 1500us/步");
        Serial.println("  电机号: 1=升降(整叠书上下)  2=推书(水平推出)");
        Serial.println("  注意: 上电位置就是零点, 没有限位开关, 先小步数试方向!");
        Serial.println("        正式标定请用调试页 g (小/中/大步长 + 归零按钮)。");
        return;
    }
    if (MotorQueue == nullptr) {
        Serial.println("步进: 队列没建好");
        return;
    }
    if (xQueueSend(MotorQueue, arg, pdMS_TO_TICKS(50)) == pdPASS) {
        Serial.printf("步进: 已下发 [m,%s]\n", arg);
    } else {
        Serial.println("步进: 命令队列满, 稍后再试");
    }
}

// 串口命令 z：步进急停
// ⚠️ 急停走队列交给 stepperTask 执行（保证只有它碰电机引脚）。
//    急停后位置会被判为无效，需要重新人工归零，这是故意的。
static void cmdStopMotor(const char *arg) {
    Serial.println("步进: 急停请求已下发 (急停后位置需要重新人工归零)");
    postMechRequest(MECH_REQ_STOP_ALL, 0);
}

// 串口命令 g：电机调试页 + 机械快捷操作
//
//   g            跳到第 6 页（电机调试页）
//   g,zero       把当前位置设为软件零点（人工归零）
//   g,save       保存当前位置到 Flash
//   g,up / g,down / g,out / g,back   按当前步长点动
//   g,stop       急停
//
// ⚠️⚠️ 【这里踩过一个真 bug，别再踩回去】
//   原来是用 `char sub = arg[0]` 只取**第一个字母**来分派：
//     "zero" 的首字母是 z -> 命中了 `sub == 'z'` 分支 -> 被当成"急停"处理了！
//   结果就是 `g,zero` 只停机、**永远不会去设零点**，现象是"归零后没有效果"，
//   位置一直是"无效"，于是存书请求一直被拒绝，书永远进不了动态数组。
//   教训：子命令必须**按整串**比较，不能只看首字母（"save" 和 "stop" 也撞首字母）。
// -----------------------------------------------------------------------------
static void cmdDebug(const char *arg) {
    // SerialCmdLib 已经把第一个逗号切掉了：g,zero -> arg = "zero"
    const char *sub = (arg != nullptr) ? arg : "";

    // ---- 不带参数：进调试页 ----
    if (sub[0] == 0) {
        display.setPage(PAGE_DEBUG);
        Serial.printf("屏幕切到电机调试页(第 %u 页)\n",
                      (unsigned)display.page() + 1);
        Serial.println("  页面上有: 升降上/下、推书前/后、停止、步长小/中/大、设零点、保存、清空书籍");
        Serial.println("  ⚠️ 所有点动都走 stepperTask，软限位和急停照样生效。");
        return;
    }

    uint16_t jog = debugJogSteps();
    if (jog == 0) {
        jog = CFG_DEBUG_JOG_MID;
    }

    // ---- 按**整串**比较，绝不能只看首字母（见上面那段 bug 说明）----
    if (strcmp(sub, "zero") == 0) {
        // 人工归零：只把当前软件步数记成 0，电机不会动，也不会去找机械原点
        postMechRequest(MECH_REQ_SET_ZERO, 0);
        Serial.println("调试: 已请求把当前位置设为零点（看下面 stepperTask 的执行结果）");
    } else if (strcmp(sub, "save") == 0) {
        postMechRequest(MECH_REQ_SAVE_NOW, 0);
    } else if (strcmp(sub, "up") == 0) {
        postMechRequest(MECH_REQ_JOG_LIFT, (int32_t)jog, CFG_DEBUG_IGNORE_LIMITS);
        Serial.printf("调试: 升降上移点动 %u 步\n", (unsigned)jog);
    } else if (strcmp(sub, "down") == 0) {
        postMechRequest(MECH_REQ_JOG_LIFT, -(int32_t)jog, CFG_DEBUG_IGNORE_LIMITS);
        Serial.printf("调试: 升降下移点动 %u 步\n", (unsigned)jog);
    } else if (strcmp(sub, "out") == 0) {
        postMechRequest(MECH_REQ_JOG_PUSH, (int32_t)jog, CFG_DEBUG_IGNORE_LIMITS);
        Serial.printf("调试: 推书前进点动 %u 步\n", (unsigned)jog);
    } else if (strcmp(sub, "back") == 0) {
        postMechRequest(MECH_REQ_JOG_PUSH, -(int32_t)jog, CFG_DEBUG_IGNORE_LIMITS);
        Serial.printf("调试: 推书后退点动 %u 步（机械允许才用）\n", (unsigned)jog);
    } else if (strcmp(sub, "stop") == 0) {
        cmdStopMotor(nullptr);
    } else if (strcmp(sub, "clear") == 0) {
        // 清空书籍数据：**不动机械位置**（两个操作永远独立）
        postMechRequest(MECH_REQ_CLEAR_BOOKS, 0);
    } else {
        Serial.printf("调试: 看不懂 \"g,%s\"\n", sub);
        Serial.println("  用法: g 进调试页 | g,zero 设零点 | g,save 保存 | g,clear 清空书籍");
        Serial.println("        g,up / g,down 升降点动 | g,out / g,back 推书点动 | g,stop 急停");
    }
}

// =============================================================================
// 存书 / 取书 流程状态机
//
// 【为什么放在 stepperTask 里】
//   MechanisemLib 的状态变量只被"发起动作"和"推进动作"两处读写。如果发起在一个任务、
//   推进在另一个任务，就得给每次访问加锁，还会出现"刚判断空闲、下一秒被占用"的竞态。
//   两者都放进 stepperTask 之后，机械状态只有一个任务在碰，**不需要锁**。
//   识别任务/界面/串口只往队列投"请求"。
//
// 【流程状态】
//   空闲 --收到 MECH_REQ_STORE_BOOK--> 存书检查(ID/重复/位置) --通过--> 升降到待存位
//        --不动机械--> 等用户从顶部放书并按确认(A键 / v,c) --确认--> 记录插到顶部 + 落盘
//        --超时(CFG_BOOK_PLACE_TIMEOUT_MS)--> 取消, 书单不动
//
//   空闲 --收到 MECH_REQ_TAKE_BOOK--> 取书检查(存在/位置有效) --> 升降对准出书口
//        --升降完成--> 推书推出 --推出完成--> (可选回退) --> **这时才**删记录 + 落盘
//
// 【铁律：取书不许提前删记录】
//   升降或推书任何一步没正常完成（超时/急停/被软限位截断），
//   都必须**保留原书单记录**，并把位置判为需要人工归零，让用户去看机构。
// =============================================================================

// 流程状态（用一个 uint8_t 加一组宏，作者看起来最直接）
#define FLOW_IDLE              0
#define FLOW_STORE_CHECK       1   // 存书前检查（ID 合法 / 没重复 / 位置有效）
// ⚠️ 已废弃（保留编号不删，防止以后加状态时编号对不上）：
//    原来存书要先把升降移到"待存书位置"，现在**取消了这一步** ——
//    摄像头在整叠书正上方、书从顶部开口放进去，存书时升降不该动。
#define FLOW_STORE_LIFT_HOME   2   // （废弃，不再使用）
#define FLOW_STORE_WAIT_BOOK   3   // 等用户把书放好并确认（自动模式下靠二哈判断，基本是瞬间）
#define FLOW_STORE_MOVE_DOWN   7   // 存完一本后：整叠书向下让 125 步，保持投书位置不变
#define FLOW_TAKE_LIFT         4   // 升降把目标书对准出书口
#define FLOW_TAKE_PUSH         5   // 推书推出
#define FLOW_TAKE_RETRACT      6   // 推书回退
#define FLOW_TAKE_HOLD         8   // 推出后停 CFG_PUSH_HOLD_MS（等书被接走/机械停稳）
#define FLOW_TAKE_VERIFY_WAIT  9   // 回退后再等 CFG_TAKE_VERIFY_DELAY_MS，然后读二哈校验
#define FLOW_TAKE_HOME        10   // 最后把升降送回"标准位"(-125 x 剩余书数)

static uint8_t flowState = FLOW_IDLE;
static bool flowStartRequested = false;   // 有没有新的存书/取书请求
static uint8_t flowStartCmd = MECH_REQ_NONE;
static int32_t flowStartArg = 0;

static int16_t flowStoreId = -1;          // 存书：要入库的分类 ID
static int16_t flowTakeIndex = -1;        // 取书：目标在书单里的下标
static uint8_t flowRetry = 0;             // 通用重试/阶段计数

// =============================================================================
// 存书 / 取书 流程状态机
//
// 【为什么放在 stepperTask 里】
//   MechanisemLib 的状态变量只被"发起动作"和"推进动作"两处读写。如果发起在一个任务、
//   推进在另一个任务，就得给每次访问加锁，还会出现"刚判断空闲、下一秒被占用"的竞态。
//   两者都放进 stepperTask 之后，机械状态只有一个任务在碰，**不需要锁**。
//   识别任务/界面/串口只往队列投"请求"。
//
// 【流程状态】
//   空闲 --收到 MECH_REQ_STORE_BOOK--> 存书检查(ID/重复/位置) --通过--> 升降到待存位
//        --升降完成--> 等用户按确认(A键 / 串口 v,c / 调试页) --确认--> 记录插到顶部 + 落盘
//        --超时(CFG_BOOK_PLACE_TIMEOUT_MS)--> 取消, 书单不动
//
//   空闲 --收到 MECH_REQ_TAKE_BOOK--> 取书检查(存在/位置有效) --> 升降对准出书口
//        --升降完成--> 推书推出 --推出完成--> (可选回退) --> **这时才**删记录 + 落盘
//
// 【铁律：取书不许提前删记录】
//   升降或推书任何一步没正常完成（超时/急停/被软限位截断），
//   都必须**保留原书单记录**，并把位置判为需要人工归零，让用户去看机构。
// =============================================================================

static uint32_t flowDeadlineMs = 0;       // 流程级超时
static uint32_t flowWaitStartMs = 0;      // 开始"等放书确认"的时刻（给界面算倒计时）
static bool flowDownStarted = false;      // 存书后"向下让一本书"是否已经发过请求

// ---- 取书流程新增的三个小状态量 ----
static uint32_t flowHoldDeadlineMs = 0;   // "推出后停住" / "回退后再等一拍" 的到期时刻
static uint32_t flowVerifyStartSeq = 0;   // 开始等二哈新帧时的 recogFrameSeq（用来判断有没有新帧）
static bool flowHomeStarted = false;      // 取书最后"回标准位"是否已经发过请求

// 把"目标书的升降位置"算出来
//
// 【位置模型（本次改造的核心约定，一定要看懂这段）】
//
// ✅ 已实机确认：**投书口和取书口是同一个位置**（同一个开口，进书出书都从这里）。
//    所以"把书推进去的高度"和"把书推出来的高度"是同一个高度，不需要任何偏移。
//    这正是下面公式里没有额外常量、直接 i × 每本书步数 的原因。
//
// ⚠️ 如果以后机械改成"进书口和出书口不是同一个位置"，不要去改公式本体，
//    把两者的高度差填到 CFG_LIFT_INDEX0_OFFSET_STEPS 里即可（那个宏就是为这种情况留的）。
//   【坐标系（用户实机确认过，改公式前一定要看懂）】
//     · 零点(0) = 空平台：平台面正好在推出口的高度，**最下面那本书**可以被推出来。
//     · 放一本 -> 位置 +125；放 n 本 -> 停在 **+n × 每本书步数**，这就是"标准位"：
//       书堆顶面正对投书口，下一本能从同一个口放进来。
//       ⚠️ 是**正**的（越放越大），和 CFG_STORE_DOWN_SIGN = -1 是配套的 —— 这一条
//          已被用户实机日志证实：3 本时位置 = +375。
//     · 由此得出：**从最下面数第 j 本**（j = 0,1,2...）能被推出时，平台位置 = **+j × 每本书步数**。
//     · 数组是"顶 -> 底"，下标 0 = 最上面（最新放的那本），
//       所以下标 i 对应从下面数 j = (总数 - 1 - i)。
//   于是：
//     目标位置 = CFG_LIFT_INDEX0_OFFSET_STEPS + (总数 - 1 - 下标) × 每本书步数
//   ⚠️ 符号改过两次，别再凭直觉改：
//      最早写成 `+下标 × 每本书步数`（错），后来被我改成负号（也错，取书时平台朝反方向走），
//      2026-xx 按用户实测日志改回**正号**：3 本、标准位 +375 时，取下标0 -> +250（只走 -125）。
//   ⚠️ CFG_LIFT_STEPS_PER_BOOK 必须是**实机标定**出来的（见 config.h 9.1 节）。
//   ⚠️ 开环系统：这个公式算的是"软件以为的位置"，堵转/丢步/齿轮间隙都会让它偏。
//
// 【符号不再靠猜：和 CFG_STORE_DOWN_SIGN 联动】
//   存书让位时每本书把位置改动多少，就决定了整个升降坐标系的正负：
//     CFG_STORE_DOWN_SIGN < 0 -> 每存一本位置 +125 -> 深度符号 +1（用户实机就是这一档）
//     CFG_STORE_DOWN_SIGN > 0 -> 每存一本位置 -125 -> 深度符号 -1
//   取书公式一律乘这个符号，所以"存书是正的、取书按负的算"这种错位不可能再出现。
static int32_t liftDepthSign() {
    return (CFG_STORE_DOWN_SIGN < 0) ? 1 : -1;
}

static int32_t calcLiftTargetForIndex(int16_t index, int16_t total) {
    if (index < 0) {
        index = 0;
    }
    if (total < 1) {
        total = 1;
    }
    if (index >= total) {
        index = (int16_t)(total - 1);
    }
    int32_t j = (int32_t)(total - 1 - index);   // 从最下面数第几本（0 = 最下面那本）
    return CFG_LIFT_INDEX0_OFFSET_STEPS +
           liftDepthSign() * j * (int32_t)CFG_LIFT_STEPS_PER_BOOK;
}

// 取书最后要停的"标准位"：书堆顶面正对投书口，新书能从同一个口顺利投入。
// ⚠️ 书数 = **取完之后剩下**的数量（所以流程里要先删记录、再调它）。
static int32_t calcLiftStandardPos(int16_t bookCount) {
    if (bookCount < 0) {
        bookCount = 0;
    }
    return CFG_LIFT_INDEX0_OFFSET_STEPS +
           liftDepthSign() * (int32_t)bookCount * (int32_t)CFG_LIFT_STEPS_PER_BOOK;
}

// 存书前检查：ID 合法 + 没重复 + 位置有效，全部通过返回 BOOK_OK
static BookResult checkStoreRequest(int16_t id, char *note, size_t noteSize) {
    // ① 分类 ID 必须是 1~6。无效：不入库，**也不触发任何机械动作**。
    if (!bookIdIsValid(id)) {
        snprintf(note, noteSize, "分类 ID %d 无效(必须是 1~6)，未入库", (int)id);
        return BOOK_ERR_ID_INVALID;
    }

    // ② 方案 A：同一个学科最多一条记录
    if (bookExists(g_books, id)) {
        snprintf(note, noteSize, "%s 的书籍已存在，未重复入库", getBookNameById(id));
        return BOOK_ERR_DUPLICATE;
    }

    // ③ 位置必须有效（人工归零过，并且上次动作正常完成）
    if (!mechanism.positionValid()) {
        snprintf(note, noteSize, "位置无效，请先在调试页人工归零");
        return BOOK_ERR_NOT_INITED;   // 借用这个"前置条件不满足"的结果码
    }

    note[0] = 0;
    return BOOK_OK;
}

// 存书流程推进（每轮调一次，不阻塞）
static void flowStepStore() {
    switch (flowState) {
        // ---- 存书第一步：检查 ID / 查重 / 位置是否有效 ----
        case FLOW_STORE_CHECK: {
            char note[CFG_BOOK_NOTE_LEN];
            BookResult r = checkStoreRequest(flowStoreId, note, sizeof(note));
            snprintf(lastStoreNote, sizeof(lastStoreNote), "%s", note);
            if (r != BOOK_OK) {
                // 检查没过：**不动机械、不改书单**，只提示
                Serial.printf("存书: 拒绝 —— %s\n", note);
                notify(note, CFG_MESSAGE_HOLD_MS);
                flowState = FLOW_IDLE;
                return;
            }

            // =================================================================
            // ⚠️ 这里**不移动升降机构**（这是本次修正的一个理解偏差）
            //
            //   实际情况：摄像头固定在**整叠书的正上方**，书是从顶部开口直接放进去的，
            //   放进去那一本就是书堆最顶上的一本。所以：
            //     · 存书时升降**不需要动**，机构保持不动就行；
            //     · 新书记录插到数组下标 0（= 最顶部），和物理位置天然对应；
            //     · 后面再取这本书，下标 0 的公式算出来就是"不动"，也是自洽的。
            //
            //   （升降机构只在**取书**时才需要动：把目标书移到出书口高度。
            //     如果存书时反而把书堆挪走，书放进去的位置就和数组记录对不上了。）
            // =================================================================
            Serial.printf("存书: 检查通过 —— 请把《%s》从顶部放进书堆\n",
                          getBookNameById(flowStoreId));
            Serial.println("      放好后: 串口敲 v,c 确认 / 按 A 键 / 用调试页确认按钮");
            Serial.printf("      %lu 秒内没有确认就自动取消(不会入库)\n",
                          (unsigned long)(CFG_BOOK_PLACE_TIMEOUT_MS / 1000UL));
            Serial.println("      注意: 存书不动升降机构, 书直接从顶部开口放进去");
            notify("请把书从顶部放入，然后确认入库", CFG_MESSAGE_HOLD_MS);

            // ⚠️ 【"书已放入"是怎么判断的】
            //    硬件上没有对射/微动开关，但有**二哈摄像头固定在整叠书正上方**。
            //    用户把书从顶部开口放进去 -> 二哈看到它 -> 识别到合法分类 ID。
            //    所以这里的判断就是：**等待"识别到分类 ID"**，而不是等用户按键。
            //    识别任务看到稳定 ID 后会投 MECH_REQ_CONFIRM_STORE，把下面这个标志置上。
            //
            //    ⚠️ 诚实说明：这不是"检测到书这个物体"，而是"二哈认出了这本书的学科标签"。
            //       如果放进一本二哈认不出来的书，流程会一直等到超时然后自动取消。
            storeWaitingConfirm = true;
            // 注意：这里**不**写 markActionPending() —— 此刻还没有任何机械动作；
            //       真正的动作（下移 125 步）在 FLOW_STORE_MOVE_DOWN 里，那里才打标记。
            flowWaitStartMs = millis();
            flowDeadlineMs = flowWaitStartMs + CFG_BOOK_PLACE_TIMEOUT_MS;
            flowState = FLOW_STORE_WAIT_BOOK;
            break;
        }

        case FLOW_STORE_WAIT_BOOK: {
            // 等用户确认（storeConfirmRequested 由队列处理那里置上）
            if (storeConfirmRequested) {
                storeConfirmRequested = false;
                storeWaitingConfirm = false;

                int16_t newIndex = -1;
                BookResult r = bookAddAtTop(g_books, flowStoreId, &newIndex);
                if (r != BOOK_OK) {
                    Serial.printf("存书: ❌ 写入书单失败 —— %s\n",
                                  bookResultText(r));
                    notify(bookResultText(r), CFG_MESSAGE_HOLD_MS);
                } else {
                    Serial.printf("存书: ✅ 入库成功 —— %s 插到书堆顶部(下标 %d), 共 %d 本\n",
                                  getBookNameById(flowStoreId), (int)newIndex,
                                  (int)display.bookCount());
                    notify("入库成功", CFG_MESSAGE_HOLD_MS);
                    saveAllToFlash("新书入库");

                    // ---- 存完一本，整叠书向下让 125 步 ----
                    // 为什么必须让：平台向下走一本书的距离后，**投书/出书开口的位置不变**，
                    // 下一本还能从同一个开口放进来；同时新书稳稳落在书堆最顶上（下标 0）。
                    // 方向用独立的 CFG_STORE_DOWN_SIGN，避免被 CFG_LIFT_DIR_SIGN 的两次相乘绕晕。
                    storage.markActionPending();   // 机械动作前先落盘"未完成"
                    storeConfirmRequested = false;
                    flowState = FLOW_STORE_MOVE_DOWN;
                    break;
                }
                // 入库失败：不改机械位置，直接收尾
                flowStoreId = -1;
                flowState = FLOW_IDLE;
                break;
            }

            // 超时取消：机械位置不动（机构已经在待存位置），书单也没改
            if ((int32_t)(millis() - flowDeadlineMs) >= 0) {
                storeWaitingConfirm = false;
                storage.clearActionPending();   // 取消也算正常收尾
                Serial.println("存书: 等确认超时，已取消入库（书单和机械位置都没变）");
                Serial.println("      如果书其实已经放进去了，请再用 v,s,<分类ID> 重新走一遍流程。");
                notify("存书超时，已取消", CFG_MESSAGE_HOLD_MS);
                snprintf(lastStoreNote, sizeof(lastStoreNote), "等待放书确认超时，已取消");
                flowStoreId = -1;
                flowState = FLOW_IDLE;
            }
            break;
        }

        // ---- 存书收尾：整叠书向下让一本书，保持投书位置不变 ----
        case FLOW_STORE_MOVE_DOWN: {
            // 第一次进来：把下移请求发出去（用点动接口，方向由 CFG_STORE_DOWN_SIGN 决定）
            if (!flowDownStarted) {
                flowDownStarted = true;
                int32_t downSteps = -(int32_t)CFG_LIFT_STEPS_PER_BOOK;
                if (CFG_STORE_DOWN_SIGN < 0) {
                    downSteps = -downSteps;
                }
                Serial.printf("存书: 整叠书下移一本书 %d 步（让出投书位置）\n",
                              CFG_LIFT_STEPS_PER_BOOK);
                // allowInvert=true：这里是刻意反方向走，不是"误操作回退"
                if (!mechanism.jog(MECH_AXIS_LIFT, downSteps, true)) {
                    Serial.printf("存书: ⚠️ 下移动作被拒绝 —— %s\n",
                                  mechanism.lastErrorText());
                    Serial.println("      书已经记进数组了，但这一本没有让位；");
                    Serial.println("      下一本请先手动 g,down 让位，或检查软限位。");
                    notify("书已入库，但下移失败，请手动让位", CFG_MESSAGE_HOLD_MS);
                    storage.clearActionPending();
                    flowDownStarted = false;
                    flowStoreId = -1;
                    flowState = FLOW_IDLE;
                    break;
                }
                break;   // 等它走完，下一轮再看
            }

            // 等升降走完
            MechResult res;
            if (!mechanism.takeResult(res)) {
                break;   // 还在走，下一轮再看（不阻塞）
            }
            if (!res.ok) {
                // ⚠️ 下移没走完：书已经入库了，但机构位置和记录可能对不上
                Serial.printf("存书: ❌ 下移没有正常完成 —— %s\n",
                              mechanism.lastErrorText());
                notify("下移异常，位置需重新归零", CFG_MESSAGE_HOLD_MS);
                storage.clearActionPending();
                flowDownStarted = false;
                flowStoreId = -1;
                flowState = FLOW_IDLE;
                break;
            }

            storage.clearActionPending();
            saveAllToFlash("存书下移到位");
            Serial.printf("存书: 下移完成, 当前升降位置=%ld 步, 准备接下一本\n",
                          (long)mechanism.liftSteps());
            flowDownStarted = false;
            flowStoreId = -1;
            flowState = FLOW_IDLE;
            break;
        }

        default:
            flowState = FLOW_IDLE;
            break;
    }
}

// 取书最后一步：机械动作都正常完成了，**这时才**删记录 + 落盘
static void flowFinishTake() {
    BookItem removed;
    memset(&removed, 0, sizeof(removed));

    BookResult r = bookRemoveAt(g_books, flowTakeIndex, &removed);
    if (r != BOOK_OK) {
        Serial.printf("取书: ⚠️ 机械已完成但书单删除失败 —— %s\n",
                      bookResultText(r));
        notify("机械完成但书单更新失败，请检查", CFG_MESSAGE_HOLD_MS);
    } else {
        Serial.printf("取书: ✅ 已取出 %s (原下标 %d), 剩 %d 本\n",
                      getBookNameById(removed.id), (int)flowTakeIndex,
                      (int)display.bookCount());
        // ⚠️ 删除后，原来在下标 i+1..N-1 的书各自往前挪一格，
        //    数组下标会自动和真实书堆顺序保持一致；机械位置不变，
        //    因为那几本书物理上并没有动（是它们上面的那本被推走了）。
        Serial.println("      剩余书籍的逻辑顺序已自动前移，机械位置不需要改动。");
        notify("取书完成", CFG_MESSAGE_HOLD_MS);
    }

    saveAllToFlash("取书完成");
    flowTakeIndex = -1;
    flowState = FLOW_IDLE;
}

// 取书校验：回退完成后，二哈（固定在书堆正上方）应该能认出"现在最顶上那本"。
//   · 取下标 i>0（中间/最下面）时：最上面那本没动 -> 期望 = 数组下标 0；
//   · 取下标 0（最上面）时      ：新的最上面是原来的下标 1。
//   ⚠️ 此刻记录还没删，所以数组内容和"物理上现在最顶上那本"差一位。
//   ⚠️ 只提示、**绝不阻断取书**：二哈离线或不在分类算法(a,2/a,3)下都会不合格，
//      绝不能因为它把整条取书流程卡住（用户已确认这一点）。
static void takeVerifyNextBook() {
    int16_t expectIdx = (flowTakeIndex == 0) ? 1 : 0;
    BookItem expect;
    memset(&expect, 0, sizeof(expect));
    bool haveExpect = g_books.at(expectIdx, &expect);

    if (!haveExpect) {
        Serial.println("取书校验: 书堆已经空了，没有下一本可以对照（跳过）");
        return;
    }
    if (recogFrameSeq == flowVerifyStartSeq) {
        Serial.println("取书校验: ⚠️ 二哈没有给出新的一帧（很可能离线）—— 跳过校验");
        Serial.printf("          期望看到 %s(ID=%d)，但没读到新帧\n",
                      getBookNameById(expect.id), (int)expect.id);
        notify("取书完成，但二哈未回数据(未校验)", CFG_MESSAGE_HOLD_MS);
        return;
    }
    if (recogLastId == expect.id) {
        Serial.printf("取书校验: ✅ 二哈看到 %s(ID=%d)，和期望一致\n",
                      getBookNameById(expect.id), (int)expect.id);
        notify("取书校验通过", CFG_MESSAGE_HOLD_MS);
    } else {
        Serial.printf("取书校验: ⚠️ 二哈看到 ID=%d(%s)，期望 ID=%d(%s) —— 请人工确认\n",
                      (int)recogLastId, getBookNameById(recogLastId),
                      (int)expect.id, getBookNameById(expect.id));
        Serial.printf("          （当前算法=%s；OCR 模式下 ID 不可信）\n", recogModeText);
        notify("取书完成，但校验不一致，请看串口", CFG_MESSAGE_HOLD_MS);
    }
}

// 推书回退到位之后（或配置成不回退时）的统一去处：
//   要校验就先等一拍再读二哈；不校验就直接收尾 + 回标准位。
static void takeAfterRetract() {
#if CFG_TAKE_VERIFY_ENABLE
    // 二哈 1 秒一帧：回退后立刻读会读到"推出之前"的旧帧，所以先等一段再读
    flowVerifyStartSeq = recogFrameSeq;
    flowHoldDeadlineMs = millis() + (uint32_t)CFG_TAKE_VERIFY_DELAY_MS;
    flowState = FLOW_TAKE_VERIFY_WAIT;
    Serial.printf("取书: 等 %d ms 后读二哈校验\n", (int)CFG_TAKE_VERIFY_DELAY_MS);
#else
    Serial.println("取书: CFG_TAKE_VERIFY_ENABLE=0, 不读二哈");
    flowFinishTake();
    flowState = FLOW_TAKE_HOME;
    flowHomeStarted = false;
#endif
}

// 取书流程推进（每轮调一次，不阻塞）
static void flowStepTake() {
    switch (flowState) {
        case FLOW_TAKE_LIFT: {
            MechResult res;
            if (!mechanism.takeResult(res)) {
                break;   // 升降还在走
            }
            if (!res.ok) {
                // ⚠️ 升降没正常完成：**保留原书单记录**，位置判为需要人工归零
                Serial.printf("取书: ❌ 升降没有正常完成 —— %s\n",
                              mechanism.lastErrorText());
                notify("升降异常，书仍在架上，请检查机构后人工归零", CFG_MESSAGE_HOLD_MS);
                flowState = FLOW_IDLE;
                return;
            }

            storage.clearActionPending();
            saveAllToFlash("升降到位(取书)");

            Serial.println("取书: 升降已对准出书口, 开始推书...");
            storage.markActionPending();   // 推书也会改变机械位置
            if (!mechanism.movePushOut()) {
                Serial.printf("取书: ❌ 推书动作下发失败 —— %s\n",
                              mechanism.lastErrorText());
                storage.clearActionPending();
                notify("推书无法启动，请检查机构后重试", CFG_MESSAGE_HOLD_MS);
                flowState = FLOW_IDLE;
                return;
            }
            flowState = FLOW_TAKE_PUSH;
            break;
        }

        case FLOW_TAKE_PUSH: {
            MechResult res;
            if (!mechanism.takeResult(res)) {
                break;   // 推书还在走
            }
            if (!res.ok) {
                // 推书没走完：书可能只推出一半。记录**必须保留**。
                Serial.printf("取书: ❌ 推书没有正常完成 —— %s\n",
                              mechanism.lastErrorText());
                notify("推书异常，书单未改动，请检查机构后人工恢复", CFG_MESSAGE_HOLD_MS);
                flowState = FLOW_IDLE;
                return;
            }

            storage.clearActionPending();

            // ⚠️ 诚实说明：28BYJ 是开环、没有推出检测开关，
            //    软件能确认的只有"推书电机走完了预设步数"，
            //    **不能证明实体书一定完全推出了**。这一步按需求"默认认为推出完成"。
            Serial.println("取书: 推书动作正常结束(预设步数走完)");
            Serial.printf("      推到接书位置, 停 %d ms 再收回推书板\n",
                          (int)CFG_PUSH_HOLD_MS);
            flowHoldDeadlineMs = millis() + (uint32_t)CFG_PUSH_HOLD_MS;
            flowState = FLOW_TAKE_HOLD;
            break;
        }

        // ---- 推出后停住 CFG_PUSH_HOLD_MS ----
        // ⚠️ 非阻塞：只看 deadline，绝不 delay（delay 会卡死整个 stepperTask，连急停都按不动）
        case FLOW_TAKE_HOLD: {
            if ((int32_t)(millis() - flowHoldDeadlineMs) < 0) {
                break;   // 还没停够
            }
#if CFG_PUSH_AUTO_RETRACT
            Serial.printf("取书: 收回推书板 %d 步\n", (int)CFG_PUSH_RETRACT_STEPS);
            storage.markActionPending();
            if (!mechanism.movePushBack()) {
                Serial.printf("取书: ❌ 推书回退下发失败 —— %s\n",
                              mechanism.lastErrorText());
                storage.clearActionPending();
                notify("推书回退失败，书单未改动", CFG_MESSAGE_HOLD_MS);
                flowState = FLOW_IDLE;
                return;
            }
            flowState = FLOW_TAKE_RETRACT;
#else
            Serial.println("取书: CFG_PUSH_AUTO_RETRACT=0，不回退推书板");
            takeAfterRetract();
#endif
            break;
        }

        case FLOW_TAKE_RETRACT: {
            MechResult res;
            if (!mechanism.takeResult(res)) {
                break;
            }
            if (!res.ok) {
                Serial.printf("取书: ❌ 推书回退没有正常完成 —— %s\n",
                              mechanism.lastErrorText());
                notify("推书回退异常，书单未改动", CFG_MESSAGE_HOLD_MS);
                flowState = FLOW_IDLE;
                return;
            }
            storage.clearActionPending();
            Serial.println("取书: 推书板已收回");
            takeAfterRetract();
            break;
        }

        // ---- 回退完成后等一拍，再读二哈校验"下一本是不是它" ----
        case FLOW_TAKE_VERIFY_WAIT: {
            if ((int32_t)(millis() - flowHoldDeadlineMs) < 0) {
                break;   // 还没等到
            }
            takeVerifyNextBook();   // 只提示、不阻断
            flowFinishTake();       // 机械动作都成功了 -> 这时才删记录 + 落盘
            flowState = FLOW_TAKE_HOME;
            flowHomeStarted = false;
            break;
        }

        // ---- 最后把升降送回"标准位"：书堆顶面正对投书口，新书能从同一个口投进来 ----
        case FLOW_TAKE_HOME: {
            if (!flowHomeStarted) {
                // ⚠️ 用"取完之后剩下的书数"：记录已经在进入本状态前删掉了
                int16_t left = display.bookCount();
                int32_t target = calcLiftStandardPos(left);
                flowHomeStarted = true;
                Serial.printf("取书: 升降回标准位 (剩 %d 本 -> %ld 步, 当前 %ld 步)\n",
                              (int)left, (long)target, (long)mechanism.liftSteps());
                storage.markActionPending();
                if (!mechanism.moveLiftTo(target)) {
                    Serial.printf("取书: ⚠️ 回标准位下发失败 —— %s（记录已经更新好了）\n",
                                  mechanism.lastErrorText());
                    storage.clearActionPending();
                    notify("取书完成，但升降没回到标准位", CFG_MESSAGE_HOLD_MS);
                    flowState = FLOW_IDLE;
                    break;
                }
                break;   // 等它走完，下一轮再看
            }
            MechResult res;
            if (!mechanism.takeResult(res)) {
                break;   // 还在走
            }
            if (!res.ok) {
                Serial.printf("取书: ⚠️ 回标准位没有正常完成 —— %s\n",
                              mechanism.lastErrorText());
                notify("取书完成，但升降没回到标准位，请检查", CFG_MESSAGE_HOLD_MS);
                storage.clearActionPending();
                flowState = FLOW_IDLE;
                break;
            }
            storage.clearActionPending();
            saveAllToFlash("取书后回标准位");
            Serial.printf("取书: ✅ 全部完成, 升降停在标准位 %ld 步\n",
                          (long)mechanism.liftSteps());
            flowState = FLOW_IDLE;
            break;
        }

        default:
            flowState = FLOW_IDLE;
            break;
    }
}

// =============================================================================
// 电机任务：唯一接触电机引脚的任务
//
// 每一轮循环做四件事（都是非阻塞的）：
//   ① 收手动命令（MotorQueue：串口 m,/z 的原始步数调试）
//   ② 收机械请求（MechQueue：调试页/按键/串口/识别的安全请求）
//   ③ 推进机械状态机（mechanism.service() 里到点才发一个脉冲）
//   ④ 推进存书/取书流程（flowStepStore / flowStepTake，只做状态判断）
//
// ⚠️ 里面**没有任何 while(motor.busy()) 或长延时**：一轮最多几十微秒，
//    没动作的时候立刻 vTaskDelay 让出 CPU。
// =============================================================================
static void stepperTask(void *pvParameters) {
    Serial.println("步进任务: 已启动 (升降 + 推书, 唯一接触电机引脚的任务)");

    for (;;) {
        // ---- ① 手动步进命令（串口 m / z）----
        char line[CFG_STEPPER_CMD_LEN];
        if (MotorQueue != nullptr &&
            xQueueReceive(MotorQueue, line, 0) == pdPASS) {
            int midx = -1;
            char *body = parseMotorCmd(line, &midx);
            char action = body[0];

            if (action == 0) {
                Serial.println("步进: 命令为空, 用法 m,<电机>,<步数> | m,z | m,?");
            } else if (action == 'z' || action == 'Z') {
                mechanism.stopAll();          // 急停
                storeWaitingConfirm = false;
                flowState = FLOW_IDLE;        // 流程也要停，不能继续往下走
                Serial.println("步进: 急停完成, 流程已复位 (位置需要重新人工归零)");
            } else if (action == '?') {
                printStepperState();
            } else if (action == 's' || action == 'S') {
                long v = atol(body + 1);
                if (v < 0) {
                    v = -v;
                }
                if (v == 0) {
                    Serial.println("步进: 速度参数不对, 例 m,2,s1500");
                } else {
                    motorLift.setStepDelayUs((uint32_t)v);
                    motorPush.setStepDelayUs((uint32_t)v);
                    Serial.printf("步进: 两个电机速度都改成 %ldus/步\n", v);
                }
            } else {
                long steps = atol(body);
                if (steps == 0 && body[0] != '0') {
                    Serial.println("步进: 看不懂这条命令, 输入 m 看用法");
                } else {
                    runMotor(midx, (int32_t)steps);
                }
            }
        }

        // ---- ② 机械请求（调试页 / 按键 / 串口 / 识别任务）----
        MechRequest req;
        if (MechQueue != nullptr &&
            xQueueReceive(MechQueue, &req, 0) == pdPASS) {
            switch (req.cmd) {
                case MECH_REQ_JOG_LIFT:
                    // 点动只在没有流程在跑的时候有意义
                    if (flowState != FLOW_IDLE) {
                        Serial.println("机械: 拒绝点动 —— 存书/取书流程正在进行");
                        snprintf(lastStoreNote, sizeof(lastStoreNote), "%s", "流程进行中，暂不接受点动");
                    } else if (!mechanism.jog(MECH_AXIS_LIFT, req.arg, true,
                                              req.raw != 0)) {
                        // 位移记账不在这里做：统一由 syncPosOffsetFromMechanism() 从机构位置推导，
                        // 这样串口 m,1,N / 调试页 / 按键 / 存书下移 走的都是同一套逻辑。
                        Serial.printf("机械: 升降点动被拒绝 —— %s\n",
                                      mechanism.lastErrorText());
                        snprintf(lastStoreNote, sizeof(lastStoreNote), "%s", mechanism.lastErrorText());
                    }
                    break;

                case MECH_REQ_JOG_PUSH:
                    if (flowState != FLOW_IDLE) {
                        Serial.println("机械: 拒绝点动 —— 存书/取书流程正在进行");
                        snprintf(lastStoreNote, sizeof(lastStoreNote), "%s", "流程进行中，暂不接受点动");
                    } else if (!mechanism.jog(MECH_AXIS_PUSH, req.arg, true,
                                              req.raw != 0)) {
                        Serial.printf("机械: 推书点动被拒绝 —— %s\n",
                                      mechanism.lastErrorText());
                        snprintf(lastStoreNote, sizeof(lastStoreNote), "%s", mechanism.lastErrorText());
                    }
                    break;

                case MECH_REQ_STOP_ALL:
                    mechanism.stopAll();
                    storeWaitingConfirm = false;
                    flowState = FLOW_IDLE;
                    Serial.println("机械: 急停, 流程已复位 (位置需要重新人工归零)");
                    break;

                case MECH_REQ_SET_ZERO:
                    if (mechanism.setZeroHere()) {
                        // 归零后立刻落盘：这是"位置基准"的来源，必须马上记住
                        // 归零 = 零点重建：基准和累计位移都清 0，记账点对齐到机构当前(0)
                        g_persistBase = 0;
                        g_pendingPosOffset = 0;
                        g_mechPosAtLastSave = mechanism.liftSteps();
                        saveAllToFlash("人工归零");
                        lastAutoSaveMs = millis();
                        notify("已把当前位置设为零点", CFG_MESSAGE_HOLD_MS);
                    } else {
                        Serial.printf("机械: ❌ 归零失败 —— %s\n",
                                      mechanism.lastErrorText());
                        notify("归零失败(有轴在动?)", CFG_MESSAGE_HOLD_MS);
                    }
                    break;

                case MECH_REQ_SAVE_NOW:
                    if (saveAllToFlash("用户手动保存")) {
                        notify("已保存到 Flash", CFG_MESSAGE_HOLD_MS);
                    } else {
                        notify("保存失败，看串口日志", CFG_MESSAGE_HOLD_MS);
                    }
                    break;

                case MECH_REQ_CLEAR_BOOKS:
                    // ⚠️ 只清书单，**绝不动机械位置**（需求明确要求两个操作独立）
                    display.clearBooks();
                    saveAllToFlash("清空书籍数据");
                    notify("书籍数据已清空(位置未改)", CFG_MESSAGE_HOLD_MS);
                    break;

                case MECH_REQ_STORE_BOOK:
                case MECH_REQ_TAKE_BOOK:
                case MECH_REQ_TAKE_ID:
                    if (flowState != FLOW_IDLE) {
                        Serial.println("流程: ❌ 拒绝 —— 已经有一个存书/取书流程在跑");
                        snprintf(lastStoreNote, sizeof(lastStoreNote), "%s", "已有流程在跑，拒绝新请求");
                    } else {
                        flowStartRequested = true;
                        flowStartCmd = req.cmd;
                        flowStartArg = req.arg;
                    }
                    break;

                case MECH_REQ_CONFIRM_STORE:
                    if (flowState == FLOW_STORE_WAIT_BOOK) {
                        storeConfirmRequested = true;
                    } else {
                        Serial.println("存书: 现在没有在等放书确认");
                    }
                    break;

                default:
                    break;
            }
        }

        // ---- ③ 推进机械状态机（到点才发脉冲，绝不阻塞）----
        mechanism.service();

        // ---- ③.5 位置自动落盘：机械刚停下来就保存 ----
        // 只看 busy() 的下降沿（在动 -> 停），不消费任何结果，所以不会影响流程。
        // ⚠️ 每轮都先 syncPosOffsetFromMechanism()：这样不管机构是被哪条命令驱动的都会记账。
#if CFG_AUTO_SAVE_ON_MOVE
        {
            syncPosOffsetFromMechanism();   // 无条件把机构真实位移折算进记账
            bool busyNow = mechanism.busy();
            uint32_t nowMs = millis();
            bool needSave = false;
            const char *saveWhy = "";

            if (busyNow) {
                autoSavedThisMove = false;   // 新一轮动作开始，允许下次停稳再存一次
            } else if (mechWasBusy && !autoSavedThisMove) {
                // ---- 路径 1：机械刚停下来（正常路径）----
                autoSavedThisMove = true;
                needSave = true;
                saveWhy = "机械已停止";
            } else if (!autoSavedThisMove &&
                       persistedLiftSteps() != lastSavedLiftSteps &&
                       (nowMs - lastAutoSaveMs) >= CFG_AUTO_SAVE_MIN_INTERVAL_MS) {
                // ---- 路径 2（兜底）：位置和"上次已经写进 Flash 的值"不一样 ----
                //
                // 为什么要有这条：路径 1 依赖"busy 从 true 变 false"这个边沿。
                // 万一因为某种原因没捕捉到那个边沿（动作被拒绝、状态变化太快等），
                // 位置就会一直停在内存里、掉电就丢了。这里直接比对"当前位置 vs 上次已保存的位置"，
                // 只要不一样就补写一次 —— 这才是"位置变了就一定落盘"的真正保证。
                // 限流用 CFG_AUTO_SAVE_MIN_INTERVAL_MS，避免在连续动作时反复写。
                needSave = true;
                saveWhy = "检测到位置变化(兜底)";
            }

            if (needSave) {
                int32_t pos = persistedLiftSteps();
                storage.saveLiftSteps(pos);
                storage.savePositionState(mechanism.positionValid(),
                                          mechanism.needHoming());
                commitPersistAccounting(pos);
                lastAutoSaveMs = nowMs;
                Serial.printf("存储: %s, 自动保存位置 (%ld 步, 位置有效=%s)\n",
                              saveWhy, (long)pos,
                              mechanism.positionValid() ? "是" : "否");
            }
            mechWasBusy = busyNow;
        }
#endif

        // ---- ④ 启动新的存书/取书流程 ----
        if (flowState == FLOW_IDLE && flowStartRequested) {
            flowStartRequested = false;

            if (flowStartCmd == MECH_REQ_STORE_BOOK) {
                flowStoreId = (int16_t)flowStartArg;
                flowRetry = 0;
                flowState = FLOW_STORE_CHECK;
                Serial.printf("存书: 收到请求, 分类 ID=%d (%s)\n",
                              (int)flowStoreId, getBookNameById(flowStoreId));
            } else if (flowStartCmd == MECH_REQ_TAKE_BOOK) {
                int16_t idx = (int16_t)flowStartArg;
                int16_t total = display.bookCount();
                if (idx < 0 || idx >= total) {
                    Serial.printf("取书: ❌ 下标 %d 不合法 (书单只有 %d 本)\n",
                                  (int)idx, (int)total);
                    notify("取书失败：书单里没有这一本", CFG_MESSAGE_HOLD_MS);
                } else if (!mechanism.positionValid()) {
                    // ⚠️ 位置无效时不允许"依赖绝对位置"的自动取书
                    Serial.println("取书: ❌ 位置无效 —— 请先在调试页人工归零");
                    notify("位置无效，请先人工归零", CFG_MESSAGE_HOLD_MS);
                } else {
                    flowTakeIndex = idx;
                    BookItem item;
                    memset(&item, 0, sizeof(item));
                    g_books.at(idx, &item);

                    // 目标 = -(总数-1-下标) x 每本书步数；见 calcLiftTargetForIndex 的坐标系说明
                    int32_t target = calcLiftTargetForIndex(idx, total);
                    Serial.printf("取书: 请求《%s》(下标 %d, 共 %d 本, 从最下面数第 %d 本)\n",
                                  getBookNameById(item.id), (int)idx, (int)total,
                                  (int)(total - 1 - idx));
                    Serial.printf("      升降目标 = %ld 步, 当前 = %ld 步, 需要走 %ld 步\n",
                                  (long)target, (long)mechanism.liftSteps(),
                                  (long)(target - mechanism.liftSteps()));

                    storage.markActionPending();   // 动作前先记"未完成"
                    if (!mechanism.moveLiftTo(target)) {
                        Serial.printf("取书: ❌ 升降无法启动 —— %s\n",
                                      mechanism.lastErrorText());
                        storage.clearActionPending();
                        notify("升降无法启动，请检查机构和零点", CFG_MESSAGE_HOLD_MS);
                        flowTakeIndex = -1;
                    } else {
                        flowState = FLOW_TAKE_LIFT;
                    }
                }
            }
        }

        // ---- ⑤ 推进流程状态机 ----
        if (flowState == FLOW_STORE_CHECK || flowState == FLOW_STORE_WAIT_BOOK ||
            flowState == FLOW_STORE_MOVE_DOWN) {
            flowStepStore();
        } else if (flowState == FLOW_TAKE_LIFT || flowState == FLOW_TAKE_PUSH ||
                   flowState == FLOW_TAKE_RETRACT || flowState == FLOW_TAKE_HOLD ||
                   flowState == FLOW_TAKE_VERIFY_WAIT || flowState == FLOW_TAKE_HOME) {
            flowStepTake();
        }

        // 有动作在走就快点轮询，都空闲就慢一点，省 CPU
        bool busy = mechanism.busy() || (flowState != FLOW_IDLE);
        vTaskDelay(pdMS_TO_TICKS(busy ? STEPPER_SERVICE_INTERVAL_MS : 10));
    }
}

// 串口命令 q：Flash 持久化（q 在 ASCII 符号里靠后，肯定没被占用；README 里同步说明）
//   q            看 Flash 里存了什么
//   q,s          立刻保存当前状态（书单 + 步数 + 位置）
//   q,l          重新从 Flash 加载书单（会覆盖内存里的书单！）
//   q,c          只清空 Flash 里的书籍数据（**不动机械位置**）
//   q,r          只重置 Flash 里的位置数据（步数 0 + 有效，**不清书单**）
//   q,f          两个都清（恢复出厂，危险）
// ⚠️ 清书单和重置位置是两个**独立**操作，任何时候都不许合并。
static void cmdFlash(const char *arg) {
    char sub = (arg != nullptr && arg[0] != 0) ? arg[0] : 0;

    if (sub == 0) {
        storage.print();
        Serial.println("  q,s 保存 | q,l 重新加载 | q,k 看 NVS 键 | q,c 只清书单 | q,r 只重置位置 | q,f 全清");
        return;
    }

    if (sub == 's' || sub == 'S') {
        saveAllToFlash("用户命令 q,s");
        return;
    }

    if (sub == 'l' || sub == 'L') {
        int32_t steps = 0;
        bool posValid = false;
        bool needHoming = true;
        if (storage.loadBooks(g_books, steps, posValid, needHoming)) {
            Serial.printf("存储: 已加载 %d 本书, 步数=%ld, 位置有效=%s, 需要归零=%s\n",
                          (int)display.bookCount(), (long)steps,
                          posValid ? "是" : "否", needHoming ? "是" : "否");

            // ⚠️⚠️ 【这里必须同步给机械层，否则"加载成功但书还是进不去"】
            //   以前 q,l 只把书单和几个回填值交给 main.cpp，机械层里的 needHoming 还是 true，
            //   于是 checkStoreRequest() 一直以"位置无效"拒绝存书请求 —— 用户看到的就是
            //   "书单能加载、但新书永远加不进去"。
            mechanism.applyLoadedPosition(steps, posValid, needHoming);
            // 记账: base = 刚加载到的历史值，offset = 0（此刻机构位置就是基准点）
            g_persistBase = steps;
            g_pendingPosOffset = 0;
            g_mechPosAtLastSave = mechanism.liftSteps();
            lastSavedLiftSteps = steps;
            lastAutoSaveMs = millis();
            Serial.printf("存储: 位置基准=%ld 步（加载值）, 后续位移在此基础上累加\n",
                          (long)steps);
            printBookList();
        } else {
            Serial.println("存储: ❌ 加载失败(没数据/损坏/版本不匹配)，内存里的书单没有被动过");
        }
        return;
    }

    if (sub == 'c' || sub == 'C') {
        // ⚠️ 只清书单，**绝不清位置**
        if (storage.clearBooks()) {
            Serial.println("存储: 已清空 Flash 里的书籍数据(机械位置未动)");
        } else {
            Serial.println("存储: ❌ 清空书籍数据失败");
        }
        return;
    }

    if (sub == 'r' || sub == 'R') {
        // ⚠️ 只重置位置，**绝不清书单**
        if (storage.resetPosition()) {
            Serial.println("存储: 已重置 Flash 里的位置数据(书单未动); 位置步数 0 + 标记有效");
        } else {
            Serial.println("存储: ❌ 重置位置失败");
        }
        return;
    }

    if (sub == 'f' || sub == 'F') {
        if (storage.eraseAll()) {
            Serial.println("存储: ⚠️ 已清空全部(书单 + 位置) —— 这是危险操作");
        } else {
            Serial.println("存储: ❌ 全清失败");
        }
        return;
    }

    if (sub == 'k' || sub == 'K') {
        // 排查"保存成功但开机读不到"：把 NVS 里实际存在的键和值打出来
        storage.printKeys();
        Serial.println("  如果 ver 存在=否 或 值=0 -> 版本号没写进去，看看是不是写到了别的命名空间");
        return;
    }

    Serial.println("存储命令: q 看状态 | q,k 看 NVS 键 | q,s 保存 | q,l 加载 | q,c 清书单 | q,r 重置位置 | q,f 全清");
}

// =============================================================================
// 串口命令 j：识别 -> 入库 全链路诊断
//
// 为什么要有它：从"二哈看到书"到"动态数组里真的多一条"中间有好几道关卡：
//   算法不对 -> 分类 ID 无效 -> 防抖没攒够 -> 判定已存在 -> 位置无效 -> 流程忙 -> 队列满
// 任何一道没过，现象都是"数组里还是空的"，光看现象分不出是哪一步。
// 这个命令把每一道关卡的当前值一次全打出来。
// =============================================================================
static void cmdBooksDiag(const char *arg) {
    (void)arg;

    Serial.println("----- 识别 -> 入库 全链路诊断 -----");

    // ① 二哈连接与算法
    Serial.printf("1) 二哈: %s, 当前算法=%s(下标 %d)\n",
                  husky.online() ? "在线" : "离线", husky.modeName(),
                  (int)husky.modeIndex());
    // 分类识别必须用"分类类"算法；如果是"物体识别/OCR"，ID 不是 1~6，永远进不了库。
    const char *modeName = husky.modeName();
    bool modeOk = (strstr(modeName, "分类") != nullptr);
    if (!modeOk) {
        Serial.println("   ⚠️ 当前不是分类算法！请敲 a,2(物体分类) 或 a,3(自学习分类)");
    }

    // ② 最近一帧的原始识别结果
    Serial.printf("2) 最近一帧: 识别块=%d, 分类 ID=%d", (int)recogLastWords,
                  (int)recogLastId);
    if (recogLastId >= 0) {
        Serial.printf(" -> %s", getBookNameById(recogLastId));
    }
    Serial.printf("  (算法=%s)\n", recogModeText);

    // ③ 防抖进度
    Serial.printf("3) 防抖: 同一个 ID 已连续稳定 %u / 需要 %d 帧\n",
                  (unsigned)recogStable, CFG_BOOK_CONFIRM_FRAMES);

    // ④ 分类 ID 是否有效
    if (recogLastId < 0) {
        Serial.println("4) 分类 ID: 画面里没有目标（或没读到帧）");
    } else if (!bookIdIsValid(recogLastId)) {
        Serial.printf("4) 分类 ID: ❌ %d 不在 1~6 —— 这个 ID 永远不会入库\n",
                      (int)recogLastId);
        Serial.println("   常见原因: ① 算法不是分类类(a,2/a,3)；② 二哈里的标签 ID 不是 1~6");
    } else {
        Serial.printf("4) 分类 ID: ✅ %d = %s\n", (int)recogLastId,
                      getBookNameById(recogLastId));
        if (bookExists(g_books, recogLastId)) {
            Serial.println("   ⚠️ 这个学科已经在书单里了 —— 方案A 规定不重复入库");
        }
    }

    // ⑤ 动态数组现状
    Serial.printf("5) 动态数组: 共 %d 条 (容量 %d, 版本 %lu)\n",
                  (int)g_books.count(), (int)g_books.capacity(),
                  (unsigned long)g_books.version());
    if (g_books.count() > 0) {
        printBookList();
    } else {
        Serial.println("   （空）还没有任何书进去");
    }

    // ⑥ 存书流程的门禁
    Serial.printf("6) 存书流程: 状态=%u, 等放书确认=%s\n", (unsigned)flowState,
                  storeWaitingConfirm ? "是" : "否");
    Serial.printf("   机械位置有效=%s, 需要人工归零=%s\n",
                  mechanism.positionValid() ? "是" : "否",
                  mechanism.needHoming() ? "是" : "否");
    if (!mechanism.positionValid()) {
        Serial.println("   ⚠️⚠️ 位置无效 -> 存书请求会被拒绝，书永远进不了数组！");
        Serial.println("        先人工归零: 串口 g,zero（或进调试页按按键3）");
    }

    // ⑦ 最后一次判定结果
    Serial.printf("7) 最近判定: %s\n",
                  (recogNote[0] != 0) ? recogNote : "（还没有结果）");
    if (lastStoreNote[0] != 0) {
        Serial.printf("   界面提示: %s\n", lastStoreNote);
    }

    Serial.println("----- 排查顺序: 1(算法) -> 4(ID) -> 3(防抖) -> 6(位置) -> 5(数组) -----");
}
// 串口命令 v：存书 / 取书流程（v = Volume，取"书本"的意思）
//   v            看当前流程状态和用法
//   v,s,<分类ID> 请求存书。例: v,s,2 = 把《数学》入库
//   v,c          确认"书已经放到书堆顶部了"（存书流程等这一步）
//   v,x          取消当前流程（机械位置不动，书单也不改）
//   v,t,<下标>   按书单下标取书（下标从 0 开始，0 = 书堆最顶上）
//   v,d,<分类ID> 按学科 ID 取书。例: v,d,2 = 取《数学》
//   v,a          打印整个书堆顺序
static void cmdBookFlow(const char *arg) {
    char sub = (arg != nullptr && arg[0] != 0) ? arg[0] : 0;

    if (sub == 0) {
        Serial.printf("流程: 状态=%u (0空闲/1-3存书/4-6取书), 等待放书确认=%s\n",
                      (unsigned)flowState, storeWaitingConfirm ? "是" : "否");
        Serial.println("  v,s,<分类ID>  请求存书 (例 v,s,2)");
        Serial.println("  v,c           确认书已放到顶部");
        Serial.println("  v,x           取消当前流程");
        Serial.println("  v,t,<下标>    按下标取书 (0 = 书堆最顶上)");
        Serial.println("  v,d,<分类ID>  按学科取书 (例 v,d,2)");
        Serial.println("  v,a           打印书堆顺序");
        return;
    }

    if (sub == 'a' || sub == 'A') {
        printBookList();
        return;
    }

    if (sub == 'c' || sub == 'C') {
        if (flowState == FLOW_STORE_WAIT_BOOK) {
            storeConfirmRequested = true;
            Serial.println("存书: 已收到放书确认, 正在入库...");
        } else {
            Serial.println("存书: 现在没有在等放书确认, 忽略这条命令");
        }
        return;
    }

    if (sub == 'x' || sub == 'X') {
        if (flowState == FLOW_IDLE) {
            Serial.println("流程: 本来就是空闲的");
        } else {
            // ⚠️ 取消**不会**把书单或机械位置改回去（机械已经动过了）
            storeWaitingConfirm = false;
            flowState = FLOW_IDLE;
            flowStoreId = -1;
            flowTakeIndex = -1;
            Serial.println("流程: 已取消。注意: 机械位置可能已经改变, 位置有效性看调试页。");
        }
        return;
    }

    if (sub == 's' || sub == 'S') {
        // 参数形如 "s" 或 "s,2"：取逗号后面的分类 ID
        int id = 0;
        const char *comma = strchr(arg, ',');
        if (comma != nullptr) {
            id = atoi(comma + 1);
        }
        if (id < BOOK_ID_MIN || id > BOOK_ID_MAX) {
            Serial.printf("存书: ❌ 分类 ID %d 无效, 必须是 %d~%d\n", id,
                          BOOK_ID_MIN, BOOK_ID_MAX);
            return;
        }
        Serial.printf("存书: 请求入库 %s (ID=%d)\n", getBookNameById((int16_t)id), id);
        postMechRequest(MECH_REQ_STORE_BOOK, (int32_t)id);
        return;
    }

    if (sub == 't' || sub == 'T') {
        int idx = 0;
        const char *comma = strchr(arg, ',');
        if (comma != nullptr) {
            idx = atoi(comma + 1);
        }
        Serial.printf("取书: 请求取下标 %d\n", idx);
        postMechRequest(MECH_REQ_TAKE_BOOK, (int32_t)idx);
        return;
    }

    if (sub == 'd' || sub == 'D') {
        int id = 0;
        const char *comma = strchr(arg, ',');
        if (comma != nullptr) {
            id = atoi(comma + 1);
        }
        if (!bookIdIsValid((int16_t)id)) {
            Serial.printf("取书: ❌ 分类 ID %d 无效\n", id);
            return;
        }
        Serial.printf("取书: 请求取 %s (ID=%d)\n", getBookNameById((int16_t)id), id);
        postMechRequest(MECH_REQ_TAKE_ID, (int32_t)id);
        return;
    }

    Serial.println("流程命令: v 看用法 | v,s,<ID> 存书 | v,c 确认放书 | v,x 取消 | v,t,<下标> 取书 | v,d,<ID> 取书");
}

// 注册所有串口命令
//
// ⚠️ 命令表是"第一个注册的生效"，重复注册 SerialCmdLib 会报警并在串口拒绝。
//    本次改造**没有改动任何老命令的字母和含义**，新功能全部用没占用的字母：
//      g = 电机调试页 / 机械快捷操作
//      v = 存书 / 取书流程
//      q = Flash 持久化
//    （w 仍然是"保存知识库"，超声波仍然是 u，这两条历史上的坑不要踩回去）
static void registerCommands() {
    cli.registerCommand('?', "帮助", cmdHelp);
    cli.registerCommand('h', "帮助", cmdHelp);
    cli.registerCommand('s', "重扫 I2C 总线", cmdScan);
    cli.registerCommand('i', "状态(二哈+系统信息+机械+测距+书单)", cmdInfo);
    cli.registerCommand('r', "重新连接二哈", cmdReconnect);
    cli.registerCommand('n', "屏幕下一页", cmdNextPage);
    cli.registerCommand('a', "切换算法(分类用 a,2 物体分类 或 a,3 自学习分类)", cmdAlgo);
    cli.registerCommand('l', "学习当前画面中心目标", cmdLearn);
    cli.registerCommand('f', "清空学习内容", cmdForget);
    cli.registerCommand('w', "保存知识库(默认槽位0, 支持 w,1)", cmdSaveSlot);
    cli.registerCommand('o', "读取知识库(默认槽位0, 支持 o,1)", cmdLoadSlot);
    cli.registerCommand('p', "读算法参数(调试OCR)", cmdParam);
    cli.registerCommand('j', "识别->入库 全链路诊断(数组为空时先敲这个)",
                        cmdBooksDiag);
    cli.registerCommand('b', "书列表页: b 看列表页, b,a[,N] 加N条测试书, b,p 打印书单",
                        cmdBooks);
    cli.registerCommand('c', "清空书籍数据(不动机械位置)", cmdClearBooks);
    cli.registerCommand('m', "步进电机调试: m 看用法, m,1,300 / m,2,-300 / m,z",
                        cmdMotor);
    cli.registerCommand('z', "步进急停(急停后需要重新人工归零)", cmdStopMotor);
    cli.registerCommand('u', "超声波: u 看状态, u,s 记起点, u,e 算厚度, u,c 清统计",
                        cmdWave);
    cli.registerCommand('d', "显示诊断: d 状态, d,1 彩条, d,2 SPI测速, d,3 触摸采样, d,4/5 背光, d,6 触摸原始值",
                        cmdDisplay);
    cli.registerCommand('t', "触摸测试页(第5页)", cmdTouch);
    cli.registerCommand('g', "电机调试页(第6页): g 进入, g,zero 设零点, g,save 保存, g,up/down/out/back 点动, g,stop 急停",
                        cmdDebug);
    cli.registerCommand('v', "存书/取书流程: v 看用法, v,s,<ID> 存书, v,c 确认放书, v,t,<下标> 取书",
                        cmdBookFlow);
    cli.registerCommand('q', "Flash 持久化: q 看状态, q,s 保存, q,l 加载, q,c 清书单, q,r 重置位置, q,f 全清",
                        cmdFlash);
}

// -----------------------------------------------------------------------------
// 长按判定时间(ms)：超过这个时间算长按
//
// ⚠️ 为什么不用 config.h 里的 BUTTON_LONG_PRESS_MS：
//    那个宏是 ButtonLib 的，而当前 ButtonLib 实现里短按和长按**都只发一个"按下"事件**，
//    没有把时长传出来。所以这里自己按"按下时刻 -> 松开时刻"的差值来判长按。
//    按键事件在 loop() 里收，松开事件也会进队列，判长按不需要阻塞等待。
#ifndef CFG_BUTTON_LONG_PRESS_MS
#define CFG_BUTTON_LONG_PRESS_MS 700
#endif

// 每个按键"按下时刻"的暂存，用来算按了多久（0 = 当前没有按着）
static uint32_t buttonDownMs[6] = { 0, 0, 0, 0, 0, 0 };   // 下标 1~5 对应按键 1~5


// 页码 -> 中文名，给按键切页时打印用
static const char *pageName(uint8_t page) {
    switch (page) {
        case PAGE_STATUS:
            return "状态页";
        case PAGE_SYSINFO:
            return "系统信息页";
        case PAGE_BOOKS:
            return "书列表页";
        case PAGE_WAVE:
            return "测距页";
        case PAGE_TOUCH:
            return "触摸测试页";
        case PAGE_DEBUG:
            return "电机调试页";
        default:
            return "未知页";
    }
}

// -----------------------------------------------------------------------------
// 调试页的按键操作（**没有触摸屏时调试页也能用**）
//
// 为什么要有这一段：
//   第 6 页调试页上的按钮都是 LVGL 触摸按钮。一旦触摸没接/没通，
//   调试页就变成"只能看不能动"的死页面。所以这里给实体按键一套等价操作。
//
// 按键分工（只在**当前就在调试页**时生效）：
//   按键1 短按 = 翻下一页（翻出调试页）
//   按键1 长按 = 切换点动步长  小10 -> 中100 -> 大500 -> 小10
//   按键2     = 急停
//   按键3     = 把当前位置设为零点（人工归零）
//   按键4(A)  = 升降点动（按当前步长）
//   按键5(B)  = 推书点动（按当前步长）
//
// ⚠️ 所有动作都是"投请求进 MechQueue"，由 stepperTask 执行，
//    这里绝不直接操作 GPIO，也绝不等电机走完。
// -----------------------------------------------------------------------------
static void handleDebugButton(uint8_t index, bool longPress) {
    uint16_t jogSteps = debugJogSteps();
    if (jogSteps == 0) {
        jogSteps = CFG_DEBUG_JOG_MID;
    }

    switch (index) {
        case 1:
            if (longPress) {
                // 切换点动步长：三档循环  小 -> 中 -> 大 -> 小
                uint16_t next = CFG_DEBUG_JOG_SMALL;
                if (jogSteps == CFG_DEBUG_JOG_SMALL) {
                    next = CFG_DEBUG_JOG_MID;
                } else if (jogSteps == CFG_DEBUG_JOG_MID) {
                    next = CFG_DEBUG_JOG_LARGE;
                }
                setDebugJogSteps(next);   // 只写数据，LVGL 那边自己会刷新
                Serial.printf("调试页: 点动步长 -> %u 步\n", (unsigned)next);
#if CFG_ENABLE_NOTIFY_POPUP
                display.showMessage("点动步长已切换（看串口）", 1200);
#endif
            } else {
                display.nextPage();
                Serial.printf("调试页: 下一页 -> %s\n", pageName(display.page()));
            }
            break;

        case 2:
            // 急停：安全操作，调试页里随时可用
            postMechRequest(MECH_REQ_STOP_ALL, 0);
            Serial.println("调试页按键2: 急停已下发");
            break;

        case 3:
            postMechRequest(MECH_REQ_SET_ZERO, 0);
            Serial.println("调试页按键3: 请求把当前位置设为零点");
            break;

        case 4:
            postMechRequest(MECH_REQ_JOG_LIFT, (int32_t)jogSteps, CFG_DEBUG_IGNORE_LIMITS);
            Serial.printf("调试页按键4: 升降点动 %u 步\n", (unsigned)jogSteps);
            break;

        case 5:
            postMechRequest(MECH_REQ_JOG_PUSH, (int32_t)jogSteps, CFG_DEBUG_IGNORE_LIMITS);
            Serial.printf("调试页按键5: 推书点动 %u 步\n", (unsigned)jogSteps);
            break;

        default:
            break;
    }
}

// -----------------------------------------------------------------------------
// 按键事件 -> 动作
//
// 【按键分工（**不依赖触摸屏**）】
//   按键1 短按 = 下一页            按键1 长按 = 进/出电机调试页(第6页)
//   按键2 短按 = 重新连接二哈       按键2 在调试页里 = 急停
//   按键3 短按 = 学习当前目标       按键3 长按 = 进/出电机调试页(备用入口)
//   按键4(A)  = 存书确认 / 调试页里升降点动
//   按键5(B)  = 取书请求 / 调试页里推书点动
//
// 长按怎么判的：
//   按下时把 millis() 记进 buttonDownMs[]，松开时算差值，够 CFG_BUTTON_LONG_PRESS_MS 就算长按。
//   短按的动作在**按下那一刻**就执行（按键手感更跟手），长按的动作在**松开时**执行。
//   整个过程不阻塞，loop() 照常按 CFG_LOOP_DELAY_MS 转。
// -----------------------------------------------------------------------------
static void handleButtonEvent(const ButtonEvent &event) {
    // 下标保护：ButtonEvent.index 是 1~5
    if (event.index < 1 || event.index > 5) {
        return;
    }

    // ---- 松开事件：算按了多久，够长就执行长按动作 ----
    if (event.pressed == 0) {
        uint32_t downMs = buttonDownMs[event.index];
        buttonDownMs[event.index] = 0;
        if (downMs == 0) {
            return;   // 没有对应的按下记录（比如上电前就按着），忽略
        }
        uint32_t heldMs = millis() - downMs;
        if (heldMs < CFG_BUTTON_LONG_PRESS_MS) {
            return;   // 短按：动作在按下那一刻已经做过了
        }

        // 只有按键1 / 按键3 有长按动作：进/出调试页
        if (event.index != 1 && event.index != 3) {
            return;
        }

        if (display.page() == PAGE_DEBUG) {
            display.setPage(PAGE_STATUS);   // 再长按一次 = 退出调试页
            Serial.printf("按键%u 长按(%lums): 退出调试页 -> %s\n",
                          (unsigned)event.index, (unsigned long)heldMs,
                          pageName(display.page()));
        } else {
            display.setPage(PAGE_DEBUG);    // 长按 = 进调试页
            Serial.printf("按键%u 长按(%lums): 进入电机调试页\n",
                          (unsigned)event.index, (unsigned long)heldMs);
            Serial.println("  没接触摸屏也能操作: 按键2=急停 按键3=设零点 按键4=升降点动 按键5=推书点动");
            Serial.println("  按键1长按=切换点动步长; 或者用串口: g / g,up / g,down / g,out / g,back / g,zero / g,save");
        }
        return;
    }

    // ---- 按下事件：先记时刻，再执行短按动作 ----
    buttonDownMs[event.index] = millis();

    // 当前就在调试页：按键交给调试页（短按动作立刻执行）
    if (display.page() == PAGE_DEBUG) {
        handleDebugButton(event.index, false);
        return;
    }

    // ---- 其它页面的短按动作 ----
    switch (event.index) {
        case 1:
            display.nextPage();
            Serial.printf("按键1: 下一页 -> %s\n", pageName(display.page()));
            break;
        case 2:
            cli.sendCommand("r");
            Serial.println("按键2: 重新连接二哈");
            break;
        case 3:
            cli.sendCommand("l");
            Serial.println("按键3: 学习当前目标");
            break;
        case 4:
            // A 键：存书流程里是"确认放书"
            if (storeWaitingConfirm) {
                cli.sendCommand("v,c");
                Serial.println("按键A: 确认放书");
            } else {
                Serial.println("按键A: 没有在等放书确认（用 v,s,<ID> 存书；长按可进调试页）");
            }
            break;
        case 5:
            // B 键：取书。默认取书堆最顶上那一本（下标 0）
            cli.sendCommand("v,t,0");
            Serial.println("按键B: 请求取书堆最顶上那一本");
            break;
        default:
            break;
    }
}

// =============================================================================
// 识别任务：唯一接触二哈和 I2C 的任务
//
// 【本次改造后的入库判断链（顺序不能错）】
//   读一帧 -> 有结果吗? -> 分类 ID 合法(1~6)吗? -> 同一个 ID 连续稳定 N 帧吗?
//   -> 这个 ID 处理过了吗? -> 投"存书请求"给 stepperTask
//
// ⚠️ 分类 ID 用的是 HuskyWord.id（= 底层 Result::ID），不是 classID！
//    classID 和 xCenter 是同一个 union 槽位，读它会读到坐标。
//
// ⚠️ 识别任务**只负责识别和投请求**，绝不直接动电机、绝不直接写书单：
//    真正的入库在 stepperTask 的存书流程里（要先确认位置有效、再提示用户放书）。
// =============================================================================
static void recogTask(void *pvParameters) {
    HuskyFrame frame;
    uint32_t lastRead = 0;
    uint32_t lastTry = 0;

    husky.begin(PIN_HUSKY_SDA, PIN_HUSKY_SCL);
    Serial.printf("识别任务: I2C 初始化完成, 默认算法=%s\n", husky.modeName());
    Serial.println("  提醒: 本项目要用分类 ID 1~6, 请切到分类类算法: a,2(物体分类) 或 a,3(自学习分类)");

    for (;;) {
        uint32_t now = millis();

        // ---- 1) 没连上就周期性重连 ----
        if (!husky.online()) {
            if (now - lastTry >= HUSKY_RECONNECT_INTERVAL_MS) {
                lastTry = now;
                Serial.println(
                    "等待二哈... (检查接线 / 协议类型设为 I2C / 按 s 重扫总线)");
                if (husky.connect()) {
                    Serial.printf("二哈就绪: %s\n", husky.modeName());
                    frame.online = true;
                    frame.modeIndex = husky.modeIndex();
                    display.setFrame(frame);
                    lastRead = now - HUSKY_READ_INTERVAL_MS;
                }
            }
            vTaskDelay(pdMS_TO_TICKS(20));
            continue;
        }

        // ---- 2) 周期性读一帧（正常周期 = HUSKY_READ_INTERVAL_MS = 5 秒）----
        if (now - lastRead >= HUSKY_READ_INTERVAL_MS) {
            lastRead = now;

            int16_t ret = husky.readFrame(frame);
            display.setFrame(frame);  // 显示库自己判断内容变没变
            if (ret >= 0) {
                recogFrameSeq++;   // 取书校验靠它判断"这是不是一帧新的识别结果"
            }

            if (ret < 0) {
                // 通信超时：累计出错就判掉线，交给上面重连
                if ((frame.errorCount % HUSKY_ERROR_LIMIT) == 0) {
                    Serial.printf("读取超时, 累计 %lu 次, 重新初始化二哈...\n",
                                  (unsigned long)frame.errorCount);
                    husky.markOffline();
                }
                // ⚠️ 通信失败时**必须**把"稳定计数"清掉，否则恢复后可能直接触发入库
                stableCount = 0;
                lastRawId = -1;
            } else if (frame.wordNum <= 0) {
                // 画面里没有目标：清防抖计数，不入库（符合"无有效识别内容不添加书籍"）
                stableCount = 0;
                lastRawId = -1;
                recogLastId = -1;
                recogLastWords = 0;
                recogStable = 0;
                recogModeText = husky.modeName();
                recogModeIdx = husky.modeIndex();
                snprintf(recogNote, sizeof(recogNote), "画面里 0 个识别块(wordNum=0)");
            } else {
                // 取第一个识别块（分类算法下通常只有 1 个）
                int16_t id = (int16_t)frame.words[0].id;

                // =============================================================
                // ⚠️ 【"书已放入"的自动判断就在这里】
                //
                //   用户把书从顶部放进书堆 -> 二哈（固定在整叠书正上方）看到它。
                //   所以"正在等放书确认" + "识别到合法分类 ID" == 书已经放进去了。
                //   这里立刻投一条自动确认，**不需要用户按任何键**。
                //
                //   ⚠️ 诚实说明：这不是"检测到书这个物体"，而是"二哈认出了它的学科标签"。
                //      放进一本二哈认不出的书，流程会等到超时自动取消（不会乱记录）。
                //   ⚠️ flowState 由 stepperTask 写、这里读，是"跨任务读一个字节"。
                //      最坏情况是读到上一拍的旧值 -> 晚一帧确认，不会做错事，所以没有加锁。
                // =============================================================
#if CFG_BOOK_AUTO_CONFIRM
                if (flowState == FLOW_STORE_WAIT_BOOK && bookIdIsValid(id)) {
                    // 队列满就等下一帧再投，不影响已有流程
                    if (postMechRequest(MECH_REQ_CONFIRM_STORE, (int32_t)id)) {
                        Serial.printf("识别: 自动判断为\"书已放入\" -> 自动确认入库 %s(ID=%d)\n",
                                      getBookNameById(id), (int)id);
                    }
                }
#else
                // CFG_BOOK_AUTO_CONFIRM=0：不自动确认，等用户按 A 键或串口 v,c
                (void)id;
#endif

                // 每帧都记诊断信息，串口命令 j 一眼看出卡在哪一步
                recogLastId = id;
                recogLastWords = frame.wordNum;
                recogModeText = husky.modeName();
                recogModeIdx = husky.modeIndex();

                if (!bookIdIsValid(id)) {
                    // 无效分类 ID：不入库、不触发机械动作，只在 ID 变化时提示一次
                    if (id != lastRawId) {
                        Serial.printf("识别: 分类 ID %d 无效(不在 1~6)，忽略\n",
                                      (int)id);
                        Serial.printf("  当前算法=%s(下标 %d)。分类识别要切到 分类类算法：a,2 物体分类 或 a,3 自学习分类\n",
                                      husky.modeName(), (int)husky.modeIndex());
                        snprintf(lastStoreNote, sizeof(lastStoreNote),
                                 "识别到无效 ID=%d，未入库", (int)id);
                        snprintf(recogNote, sizeof(recogNote),
                                 "ID=%d 无效(不在 1~6)：算法不对或标签没学习", (int)id);
                    }
                    stableCount = 0;
                    lastRawId = id;
                    lastHandledId = -1;   // 允许下次重新触发
                } else {
                    // 分类 ID 合法：做"连续稳定"防抖
                    if (id == lastRawId) {
                        if (stableCount < 255) {
                            stableCount++;
                        }
                    } else {
                        stableCount = 1;
                        lastRawId = id;
                        Serial.printf("识别: 分类 ID=%d -> %s (等 %d 帧稳定)\n",
                                      (int)id, getBookNameById(id),
                                      CFG_BOOK_CONFIRM_FRAMES);
                    }

                    recogStable = stableCount;

                    // 稳定够了，而且这个 ID 还没处理过 -> 投存书请求（只投一次）
                    if (stableCount >= CFG_BOOK_CONFIRM_FRAMES && id != lastHandledId) {
                        lastHandledId = id;

                        // 已经存在就先提示，不发请求（省得机械白动一趟）
                        if (bookExists(g_books, id)) {
                            Serial.printf("识别: %s 已经在书单里，不重复入库\n",
                                          getBookNameById(id));
                            snprintf(lastStoreNote, sizeof(lastStoreNote),
                                     "%s 已存在，未重复入库", getBookNameById(id));
                            snprintf(recogNote, sizeof(recogNote),
                                     "%s 已在书单里(ID=%d)，按方案A不重复入库",
                                     getBookNameById(id), (int)id);
                            notify("该学科书籍已存在，未入库", CFG_MESSAGE_HOLD_MS);
                        } else {
                            Serial.printf("识别: %s 稳定 %d 帧, 请求入库\n",
                                          getBookNameById(id), CFG_BOOK_CONFIRM_FRAMES);
                            snprintf(lastStoreNote, sizeof(lastStoreNote),
                                     "请求入库 %s", getBookNameById(id));
                            snprintf(recogNote, sizeof(recogNote),
                                     "已投出存书请求 %s(ID=%d)，等 stepperTask 处理",
                                     getBookNameById(id), (int)id);
                            if (postMechRequest(MECH_REQ_STORE_BOOK, (int32_t)id)) {
                                // ⚠️ 原来这里会**自动跳到调试页**；用户要求"自动弹出测试界面的功能全删"，
                                //    所以现在只打串口、不切页面（想看流程敲 g 进调试页，或敲 j 看诊断）。
                            } else {
                                // 队列满：清掉"处理过"的标记，下一帧还能重试，
                                // 否则这个 ID 就永远不再请求了，看起来就是"识别到但进不了数组"
                                lastHandledId = -1;
                                snprintf(recogNote, sizeof(recogNote),
                                         "存书请求队列满，下一帧重试");
                                Serial.println("识别: 存书请求队列满，下一帧重试");
                            }
                        }
                    }
                }
            }
        }

        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

// =============================================================================
// 测距任务（HC-SR04）：唯一接触 TRIG/ECHO 的任务
//
// 非阻塞流程：每 WAVE_MEASURE_INTERVAL_MS（默认 5 秒）发起一次测量，
// 每 1ms 推进一次状态机（update 里从不死等），出结果后做量程过滤 + 滑动平均。
// ⚠️ 测距结果**不会**直接触发任何机械动作，只做显示和辅助观察。
// =============================================================================
static void waveTask(void *pvParameters) {
    Serial.printf("测距任务: TRIG=GPIO%d ECHO=GPIO%d 超时=%luus 周期=%dms\n",
                  PIN_WAVE_TRIG, PIN_WAVE_ECHO,
                  (unsigned long)CFG_WAVE_TIMEOUT_US, WAVE_MEASURE_INTERVAL_MS);

    uint32_t lastMeasure = 0;
    uint32_t lastInvalid = 0;

    for (;;) {
        uint32_t now = millis();

        // ---- 1) 到点就发起一次测量 ----
        if (now - lastMeasure >= WAVE_MEASURE_INTERVAL_MS) {
            if (waveSensor.startMeasure()) {
                lastMeasure = now;
            }
        }

        // ---- 2) 推进状态机 ----
        waveSensor.update();

        // ---- 3) 有结果就处理 ----
        if (waveSensor.isReady()) {
            uint32_t mm = waveSensor.getDistanceMm();
            uint8_t err = waveSensor.getErrorCode();
            bool valid = waveSensor.isValid();

            // 超出量程的也当无效：HC-SR04 手册范围 20~4000mm
            if (valid && (mm < CFG_WAVE_MIN_MM || mm > CFG_WAVE_MAX_MM)) {
                valid = false;
            }

            uint32_t filtered = 0;
            if (valid) {
                // 滑动平均：攒够 CFG_WAVE_FILTER_N 个就取平均，然后重新攒
#if CFG_WAVE_FILTER_N > 1
                if (waveFilterCount < CFG_WAVE_FILTER_N) {
                    waveFilterBuf[waveFilterCount++] = mm;
                }
                uint64_t sum = 0;
                for (uint8_t i = 0; i < waveFilterCount; i++) {
                    sum += waveFilterBuf[i];
                }
                filtered = (uint32_t)(sum / (waveFilterCount ? waveFilterCount : 1));
                if (waveFilterCount >= CFG_WAVE_FILTER_N) {
                    waveFilterCount = 0;
                }
#else
                filtered = mm;
#endif
            }

            if (waveMutex != nullptr &&
                xSemaphoreTake(waveMutex, pdMS_TO_TICKS(20)) == pdTRUE) {
                // 跳变检查：和上一次有效值差太多就不更新（防止多路径反射直接改显示）
                bool jump = false;
#if CFG_WAVE_JUMP_MM > 0
                if (valid && waveState.valid && waveState.distanceMm > 0) {
                    int32_t diff = (int32_t)mm - (int32_t)waveState.distanceMm;
                    if (diff < 0) {
                        diff = -diff;
                    }
                    if (diff > CFG_WAVE_JUMP_MM) {
                        jump = true;
                    }
                }
#endif
                if (jump) {
                    // 跳变太大：本次读数不采纳（只统计，不覆盖有效值）
                    waveState.invalidCount++;
                    valid = false;
                }

                waveState.pulseUs = waveSensor.getPulseUs();
                waveState.errorCode = err;
                waveState.valid = valid;
                if (valid) {
                    waveState.distanceMm = mm;
                    if (filtered != 0) {
                        waveState.filteredMm = filtered;
                    }
                }
                waveState.totalCount++;
                if (!valid) {
                    waveState.invalidCount++;
                }
                if (err == 1) {
                    waveState.timeoutRiseCount++;
                } else if (err == 2) {
                    waveState.timeoutFallCount++;
                }
                if (valid) {
                    waveState.lastMs = now;
                    if (waveState.minMm == 0 || mm < waveState.minMm) {
                        waveState.minMm = mm;
                    }
                    if (mm > waveState.maxMm) {
                        waveState.maxMm = mm;
                    }
                }
                xSemaphoreGive(waveMutex);
            }

            // ---- 4) 推给屏幕（显示库内部判断要不要重画）----
#if CFG_WAVE_SHOW_ON_SCREEN
            if (CFG_RUN_DISPLAY) {
                WaveState snap = {};
                if (waveMutex != nullptr &&
                    xSemaphoreTake(waveMutex, pdMS_TO_TICKS(20)) == pdTRUE) {
                    snap = waveState;
                    xSemaphoreGive(waveMutex);
                }
                display.setWave(snap);
            }
#endif

            // ---- 5) 无效读数提醒（不覆盖有效值、不触发机械动作）----
            if (!valid) {
                if (lastInvalid == 0 ||
                    (now - lastInvalid) >= (uint32_t)CFG_WAVE_INVALID_LIMIT * WAVE_MEASURE_INTERVAL_MS) {
                    lastInvalid = now;
                    Serial.printf("测距无效: err=%u (1=ECHO没拉高 2=ECHO没拉低), "
                                  "检查接线/分压/量程\n", (unsigned)err);
                }
            } else {
                lastInvalid = 0;
            }
        }

        vTaskDelay(pdMS_TO_TICKS(WAVE_UPDATE_INTERVAL_MS));
    }
}

// 打印一次测距状态（串口命令 u 和 i 都用它）
static void printWaveState() {
    WaveState snap = {};
    if (waveMutex != nullptr &&
        xSemaphoreTake(waveMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        snap = waveState;
        xSemaphoreGive(waveMutex);
    }

    // ⚠️ 先判"测距到底有没有在跑"：CFG_ENABLE_WAVE=0 时任务不建、引脚不配，
    //    串口一条日志都没有，很容易被误判成"传感器坏/接线错"。
    if (!CFG_RUN_WAVE) {
        Serial.println("测距: ❌ 没启用（config.h 的 CFG_ENABLE_WAVE=0）");
        Serial.println("      后果: 不建 waveTask、TRIG/ECHO 连 pinMode 都没配 -> 永远测不到");
        Serial.println("      改法: 把 CFG_ENABLE_WAVE 改成 1（并确认测距引脚不和升降电机抢）");
        return;
    }
    if (snap.totalCount == 0) {
        Serial.printf("测距: 还没测过（TRIG=GPIO%d ECHO=GPIO%d, 周期 %dms）\n",
                      PIN_WAVE_TRIG, PIN_WAVE_ECHO, WAVE_MEASURE_INTERVAL_MS);
        Serial.println("      刚上电要等一个测量周期；一直是这样就是任务没跑起来");
    }

    Serial.printf("测距: %u mm (%u.%u cm), %s, err=%u\n",
                  (unsigned)snap.distanceMm, (unsigned)(snap.distanceMm / 10U),
                  (unsigned)(snap.distanceMm % 10U),
                  snap.valid ? "有效" : "无效", (unsigned)snap.errorCode);
    Serial.printf("  滤波后=%u mm, 共%u 次, 无效%u (没拉高%u / 没拉低%u)\n",
                  (unsigned)snap.filteredMm, (unsigned)snap.totalCount,
                  (unsigned)snap.invalidCount,
                  (unsigned)snap.timeoutRiseCount,
                  (unsigned)snap.timeoutFallCount);
    Serial.printf("  范围 %u~%u mm, TRIG=GPIO%d ECHO=GPIO%d (ECHO 必须分压)\n",
                  (unsigned)snap.minMm, (unsigned)snap.maxMm, PIN_WAVE_TRIG,
                  PIN_WAVE_ECHO);
    if (snap.asnapValid) {
        Serial.printf("  测厚起点: %u mm (%.1f 秒前)\n", (unsigned)snap.asnapMm,
                      (double)((millis() - snap.asnapMs) / 1000.0));
    }
    if (snap.thicknessValid) {
        Serial.printf("  测出的厚度: %d mm\n", (int)snap.thicknessMm);
    }
}

// 串口命令 u：超声波测距（u = Ultrasonic，不能用 w，w 是保存知识库）
//   u        打印一次当前测距状态
//   u,s      记录"开始测量厚度"的起点
//   u,e      结束测量：厚度 = 起点距离 - 当前距离
//   u,c      清空统计
static void cmdWave(const char *arg) {
    char sub = (arg != nullptr && arg[0] != 0) ? arg[0] : 0;

    if (sub == 0) {
        printWaveState();
        return;
    }

    if (sub == 's' || sub == 'S') {
        WaveState snap = {};
        if (waveMutex != nullptr &&
            xSemaphoreTake(waveMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
            snap = waveState;
            if (snap.valid) {
                waveState.asnapValid = true;
                waveState.asnapMm = snap.filteredMm ? snap.filteredMm
                                                    : snap.distanceMm;
                waveState.asnapMs = millis();
                waveState.thicknessValid = false;
            }
            xSemaphoreGive(waveMutex);
        }
        if (!snap.valid) {
            Serial.println("测厚: 当前读数无效, 先把书放好再看状态");
            return;
        }
        Serial.printf("测厚: 已记起点 %u mm, 现在把书放上去, 然后 u,e\n",
                      (unsigned)snap.distanceMm);
        return;
    }

    if (sub == 'e' || sub == 'E') {
        int32_t diff = 0;
        bool ok = false;
        if (waveMutex != nullptr &&
            xSemaphoreTake(waveMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
            if (waveState.asnapValid && waveState.valid) {
                uint32_t cur = waveState.filteredMm ? waveState.filteredMm
                                                    : waveState.distanceMm;
                diff = (int32_t)waveState.asnapMm - (int32_t)cur;
                waveState.thicknessMm = diff;
                waveState.thicknessValid = true;
                ok = true;
            }
            xSemaphoreGive(waveMutex);
        }
        if (!ok) {
            Serial.println("测厚: 没有起点快照或当前读数无效, 先 u,s 再 u,e");
            return;
        }
        Serial.printf("测厚: 厚度 = %d mm (容差 ±%d mm)\n", (int)diff,
                      CFG_THICK_TOLERANCE_MM);
        Serial.printf("      可以把标定出来的厚度填到 config.h 的 CFG_LIFT_STEPS_PER_BOOK 参考\n");
        return;
    }

    if (sub == 'c' || sub == 'C') {
        if (waveMutex != nullptr &&
            xSemaphoreTake(waveMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
            waveState.totalCount = 0;
            waveState.invalidCount = 0;
            waveState.timeoutRiseCount = 0;
            waveState.timeoutFallCount = 0;
            waveState.minMm = 0;
            waveState.maxMm = 0;
            waveState.asnapValid = false;
            waveState.thicknessValid = false;
            xSemaphoreGive(waveMutex);
        }
        Serial.println("测距: 统计已清空");
        return;
    }

    Serial.println("测距命令: u 看状态 | u,s 记起点 | u,e 算厚度 | u,c 清统计");
}

// 串口命令 d：显示排错（屏幕没内容时先敲这个）
static void cmdDisplay(const char *arg) {
    if (arg != nullptr && arg[0] == '1') {
        display.requestRawTest(CFG_SELFTEST_BAR_HOLD_MS);
        Serial.printf("显示: 已请求彩条自检(停留 %dms, 由显示任务执行, 之后自动回界面)\n",
                      CFG_SELFTEST_BAR_HOLD_MS);
        return;
    }
    if (arg != nullptr && arg[0] == '2') {
        display.requestBusTest();
        Serial.println("显示: 已请求 SPI 总线测速(由显示任务执行, 结果打印在下面)");
        return;
    }
    if (arg != nullptr && arg[0] == '3') {
        display.requestTouchSample();
        Serial.println("触摸: 已请求原始值采样/查看结果（第一次开始采样, 第二次打印结果）");
        return;
    }
    if (arg != nullptr && arg[0] == '4') {
        display.backlightBlink(3);
        return;
    }
    if (arg != nullptr && arg[0] == '5') {
        display.backlightSet(!display.backlightIsOn());
        return;
    }
    if (arg != nullptr && arg[0] == '6') {
        display.requestTouchDump();
        Serial.println("触摸: 已请求打印原始值（由显示任务执行，结果在下面）");
        return;
    }
    display.printDiag();
}

// 串口命令 t：跳到触摸测试页（第 5 页）
static void cmdTouch(const char *arg) {
    display.setPage(PAGE_TOUCH);
    Serial.printf("屏幕切到触摸测试页(第 %u 页): 点屏上的按钮应该加 1\n",
                  (unsigned)display.page() + 1);
    Serial.println("  没反应就看串口有没有 触摸: 测试按钮第 N 次按下");
    Serial.println("  d    = 显示诊断(含触摸状态/坐标/原始值)");
    Serial.println("  d,3  = 采样触摸原始值(连按两次: 第一次开始, 第二次看结果)");
}

// =============================================================================
// 调试页的命令回调
//
// 这个函数由 displayTask 在"从调试页队列取出命令"时调用，运行在**显示任务**里。
// 所以它必须**立刻返回**：只做"往 MechQueue 投一条请求"，绝不等电机、绝不碰 GPIO。
// 真正执行的是 stepperTask，那里才有软限位/互斥/急停的全套保护。
// =============================================================================
static void handleDebugCmd(uint8_t cmd, int16_t arg) {
    // 调试页的点动用不到 arg（步长从 debugJogSteps() 读）；
    // 书列表页的"取出"按钮要用 arg = 书籍下标，所以这里不能 (void)arg。

    uint16_t jog = debugJogSteps();
    if (jog == 0) {
        jog = CFG_DEBUG_JOG_MID;
    }

    switch (cmd) {
        // 调试页的点动一律用 raw=1：解除软限位和单次上限（CFG_DEBUG_IGNORE_LIMITS）
        // 标定必须能把机构往边上顶去量真实行程，软限位在这里只会碍事。
        // 急停、互斥、参数校验、步数硬上限这些都还在。
        case DEBUG_CMD_LIFT_UP:
            postMechRequest(MECH_REQ_JOG_LIFT, (int32_t)jog, CFG_DEBUG_IGNORE_LIMITS);
            break;
        case DEBUG_CMD_LIFT_DOWN:
            postMechRequest(MECH_REQ_JOG_LIFT, -(int32_t)jog, CFG_DEBUG_IGNORE_LIMITS);
            break;
        case DEBUG_CMD_PUSH_FORWARD:
            postMechRequest(MECH_REQ_JOG_PUSH, (int32_t)jog, CFG_DEBUG_IGNORE_LIMITS);
            break;
        case DEBUG_CMD_PUSH_BACK:
            postMechRequest(MECH_REQ_JOG_PUSH, -(int32_t)jog, CFG_DEBUG_IGNORE_LIMITS);
            break;
        case DEBUG_CMD_STOP_ALL:
            postMechRequest(MECH_REQ_STOP_ALL, 0);
            break;
        case DEBUG_CMD_SET_ZERO:
            postMechRequest(MECH_REQ_SET_ZERO, 0);
            break;
        case DEBUG_CMD_SAVE_NOW:
            postMechRequest(MECH_REQ_SAVE_NOW, 0);
            break;
        case DEBUG_CMD_CLEAR_BOOKS:
            // 界面上已经做过"二次确认"，这里直接投
            postMechRequest(MECH_REQ_CLEAR_BOOKS, 0);
            break;
        case DEBUG_CMD_TAKE_BOOK:
            // 书列表页的"取出"按钮：只投请求，真正执行在 stepperTask（取书流程）
            if (!postMechRequest(MECH_REQ_TAKE_BOOK, (int32_t)arg)) {
                Serial.println("取书: 请求队列满，请再点一次");
            }
            break;
        default:
            break;
    }
}

// =============================================================================
// 系统信息任务：采集 -> 共享结构体 + 显示库 + 调试页快照 + 周期打印
// =============================================================================
static void monitorTask(void *pvParameters) {
    uint32_t lastPrint = 0;

    for (;;) {
        SystemInfo snap = {};
        sysInfoCollect(snap);

        if (infoMutex != nullptr &&
            xSemaphoreTake(infoMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
            systemInfo = snap;
            xSemaphoreGive(infoMutex);
        }

        if (CFG_RUN_DISPLAY) {
            display.setSystemInfo(snap);
        }

        // ---- 调试页快照：把机械/书单/测距/Flash 的状态拼成一块纯数据交给显示任务 ----
        // ⚠️ 业务任务只写数据，**不碰任何 LVGL 控件**（LVGL 只有 displayTask 能碰）。
        if (CFG_RUN_DISPLAY) {
            DebugState dbg;
            memset(&dbg, 0, sizeof(dbg));

            dbg.liftSteps = mechanism.liftSteps();
            dbg.liftLeft = mechanism.liftRemaining();
            dbg.liftMoving = mechanism.busy();
            dbg.liftRange = mechanism.liftRange();
            dbg.pushSteps = mechanism.pushSteps();
            dbg.pushMoving = (mechanism.state() == MECH_MOVING) &&
                             (mechanism.activeAxis() == MECH_AXIS_PUSH);
            dbg.positionValid = mechanism.positionValid();
            dbg.needHoming = mechanism.needHoming();
            dbg.jogSteps = debugJogSteps();
            dbg.lastActionOk = (mechanism.state() != MECH_FAILED) &&
                               (mechanism.state() != MECH_ESTOPPED);
            snprintf(dbg.lastError, sizeof(dbg.lastError), "%s",
                     mechanism.lastErrorText());

            StorageStatus st = {};
            storage.status(st);
            dbg.flashSaved = st.savedOnce;
            snprintf(dbg.lastSavedInfo, sizeof(dbg.lastSavedInfo), "%s",
                     st.ready ? (st.loadOk ? "已加载" : "已就绪") : "未就绪");

            dbg.bookNum = (uint16_t)display.bookCount();
            buildBookOrderText(dbg.bookOrder, sizeof(dbg.bookOrder));

            // 测距快照（waveMutex 保护）
            if (waveMutex != nullptr &&
                xSemaphoreTake(waveMutex, pdMS_TO_TICKS(20)) == pdTRUE) {
                dbg.waveMm = waveState.filteredMm ? waveState.filteredMm
                                                  : waveState.distanceMm;
                dbg.waveValid = waveState.valid;
                dbg.waveError = waveState.errorCode;
                xSemaphoreGive(waveMutex);
            }

            // 额外提示：优先显示"正在等放书确认"，其次显示上一次识别/入库说明
            if (storeWaitingConfirm) {
                snprintf(dbg.note, sizeof(dbg.note), "请把书放到顶部, 然后按确认");
            } else if (lastStoreNote[0] != 0) {
                snprintf(dbg.note, sizeof(dbg.note), "%s", lastStoreNote);
            } else {
                snprintf(dbg.note, sizeof(dbg.note), "就绪");
            }

            setDebugState(dbg);
        }

        uint32_t now = millis();
        if (now - lastPrint >= (uint32_t)SYSINFO_INTERVAL_MS * SYSINFO_PRINT_EVERY) {
            lastPrint = now;
            Serial.printf("SYS ram=%u/%uKB psram=%u/%uKB up=%lus heap=%u\n",
                          (unsigned)snap.ramUsedKb, (unsigned)snap.heapTotalKb,
                          (unsigned)snap.psramUsedKb,
                          (unsigned)snap.psramTotalKb,
                          (unsigned long)snap.uptimeSeconds,
                          (unsigned)snap.heapFreeKb);
        }

        vTaskDelay(pdMS_TO_TICKS(SYSINFO_INTERVAL_MS));
    }
}

// =============================================================================
// 初始化
//
// 顺序（不要随意调整，尤其是 Flash 加载必须在机械层 begin 之前）：
//   串口 -> 互斥锁 -> 书单(显示库挂同一份) -> 显示 -> Flash 打开并加载
//   -> 机械层 begin + 从 Flash 恢复位置状态 -> 队列 -> 电机 begin -> 命令 -> 建任务
//
// ⚠️ 开机**绝不驱动电机**：位置一律判为无效，等用户在调试页人工归零。
// =============================================================================
void setup() {
    Serial.begin(CFG_SERIAL_BAUD);
    delay(CFG_SERIAL_BOOT_DELAY_MS);  // 等串口稳定

    // ---- 互斥锁 ----
    infoMutex = xSemaphoreCreateMutex();
    if (infoMutex == nullptr) {
        Serial.println("初始化失败: 互斥锁创建不成功");
        return;
    }

    printBanner();

    // ---- 1) 书单：main.cpp 是唯一所有者，显示库只是借用 ----
    if (CFG_RUN_DISPLAY) {
        // 先把书单挂进显示库，这样"显示列表 / Flash 内容 / 机械位置计算"用同一份数据
        display.attachBookList(&g_books);
        if (!g_books.begin()) {
            Serial.println("初始化失败: 书单创建不成功");
            return;
        }
        Serial.printf("书单: 就绪 (初始 %d 条, 上限 %d 条)\n",
                      g_books.capacity(), CFG_BOOKLIST_MAX);
    }

    // ---- 2) 显示：begin() 里会建 displayTask ----
    if (CFG_RUN_DISPLAY) {
        display.begin();
        display.setDebugCmdHandler(handleDebugCmd);    // 调试页命令 -> MechQueue
        display.setDebugJogSteps(CFG_DEBUG_JOG_MID);   // 默认中等步长，最安全
#if CFG_ENABLE_NOTIFY_POPUP
        display.showMessage("SmartBookShelf BOOT", (uint32_t)CFG_MESSAGE_HOLD_MS);
#endif
    }

    // ---- 3) Flash：打开 NVS 并加载书单/位置 ----
    bool flashLoaded = false;
    int32_t savedLiftSteps = 0;
    bool savedPosValid = false;
    bool savedNeedHoming = true;
    if (storage.begin()) {
        if (CFG_STORAGE_AUTO_LOAD) {
            flashLoaded = storage.loadBooks(g_books, savedLiftSteps,
                                            savedPosValid, savedNeedHoming);
            if (flashLoaded) {
                Serial.printf("存储: 已恢复 %d 本书\n", (int)display.bookCount());
                storage.printKeys();   // books 长度应该 = 2 + 条数 x 每条字节数
                printBookList();
            } else {
                Serial.println("存储: 没有可用的历史数据(或数据需要人工处理)，从空书单开始");
                // 直接打键清单：一眼看出到底是 books 没写进去，还是写进去了但读不出来
                storage.printKeys();
            }
        }
    } else {
        Serial.println("存储: ❌ NVS 打不开，本次运行不保存(界面功能不受影响)");
    }

    // ---- 4) 机械层：注册两个轴（没接线的轴传 nullptr）----
    mechanism.begin(motorLift.isWired() ? &motorLift : nullptr,
                    motorPush.isWired() ? &motorPush : nullptr);

    // 从 Flash 恢复"位置状态"。
    // ⚠️ 这里**只把 Flash 里记的步数读出来显示 + 把位置判为无效需要归零**。
    //    Stepper28BYJ 没有"直接写位置"的接口，用 run() 补步数会真的驱动电机，
    //    开机盲动是明确禁止的。所以掉电后必须人工归零一次（见 README 第 7 节）。
    if (flashLoaded) {
        // 开机加载：用 applyLoadedPosition，如果 Flash 里记的是"位置有效且不需要归零"，
        // 就把它恢复成有效 —— 这样"上次正常关机"的情况下开机不用再归零一次。
        // ⚠️ 位数的语义：步数基准会被清 0（开环系统只能把"当前"当基准），
        //    所以这里恢复的"有效"指的是"零点基准可用"，不是"机构回到了 300 步那个位置"。
        mechanism.applyLoadedPosition(savedLiftSteps, savedPosValid, savedNeedHoming);
        // 记账: base = 开机加载到的历史值，offset = 0
        // ⚠️ 这样"开机后什么都不做"时，兜底自动保存不会把 Flash 里的历史值抹成 0。
        g_persistBase = savedLiftSteps;
        g_pendingPosOffset = 0;
        g_mechPosAtLastSave = mechanism.liftSteps();
        lastSavedLiftSteps = savedLiftSteps;
        lastAutoSaveMs = millis();
    }

    // ---- 5) 队列 ----
    MechQueue = xQueueCreate(CFG_MECH_QUEUE_SIZE, sizeof(MechRequest));
    if (MechQueue == nullptr) {
        Serial.println("机械: ❌ 请求队列创建失败");
    }

    // ---- 6) 按键 ----
    if (CFG_RUN_BUTTON) {
        buttons.begin();
        buttons.startTask();
    }

    // ---- 7) 串口命令 ----
    cli.begin();
    registerCommands();
    cli.startTask();

    // ---- 8) 超声波测距初始化 ----
    if (CFG_RUN_WAVE) {
        waveMutex = xSemaphoreCreateMutex();
        if (waveMutex == nullptr) {
            Serial.println("测距: 互斥锁创建失败");
        }
        waveSensor.begin();
    }

    // ---- 9) 步进电机初始化 ----
    // 两个轴共用一套速度，每台按 config.h 的行程设软限位。
    // 没接线的电机（ENABLE=0）这里会跳过，不会碰 GPIO。
    if (CFG_RUN_STEPPER) {
        MotorQueue = xQueueCreate(CFG_STEPPER_QUEUE_SIZE, CFG_STEPPER_CMD_LEN);
        if (MotorQueue == nullptr) {
            Serial.println("步进: 命令队列创建失败");
        }

        if (motorLift.isWired()) {
            motorLift.setStepDelayUs(CFG_STEPPER_STEP_DELAY_US);
            motorLift.setRangeSteps(CFG_STEPPER_LIFT_RANGE_STEPS);
            motorLift.begin();
        }
        if (motorPush.isWired()) {
            motorPush.setStepDelayUs(CFG_STEPPER_STEP_DELAY_US);
            motorPush.setRangeSteps(CFG_STEPPER_PUSH_RANGE_STEPS);
            motorPush.begin();
        }
        Serial.printf("步进: 已初始化(驱动=%s, 速度=%dus/步)\n",
                      (CFG_STEPPER_DRIVER == 0) ? "28BYJ+ULN2003" : "42步进+驱动器",
                      CFG_STEPPER_STEP_DELAY_US);
        printStepperState();
    }

    // ---- 10) 首次采集系统信息，让屏幕第一页就有数据 ----
    sysInfoCollect(systemInfo);
    if (CFG_RUN_DISPLAY) {
        display.setSystemInfo(systemInfo);
    }
    sysInfoPrint(systemInfo);

    // ---- 11) 建任务 ----
#if CFG_ENABLE_HUSKY
    xTaskCreatePinnedToCore(recogTask, "recog", CFG_STACK_RECOG, nullptr,
                            CFG_PRIO_RECOG, nullptr, CFG_CORE_RECOG);
#else
    Serial.println("识别任务: 已在 config.h 里关掉 (CFG_ENABLE_HUSKY=0)");
#endif

#if CFG_ENABLE_SYSINFO
    xTaskCreatePinnedToCore(monitorTask, "monitor", CFG_STACK_MONITOR, nullptr,
                            CFG_PRIO_MONITOR, nullptr, CFG_CORE_MONITOR);
#endif

    // 步进任务独占电机引脚，和 recogTask 一起放核心 1（互不抢总线）
#if CFG_ENABLE_STEPPER
    xTaskCreatePinnedToCore(stepperTask, "stepper", CFG_STACK_STEPPER, nullptr,
                            CFG_PRIO_STEPPER, nullptr, CFG_CORE_STEPPER);
#else
    Serial.println("步进任务: 已在 config.h 里关掉 (CFG_ENABLE_STEPPER=0)");
#endif

#if CFG_ENABLE_WAVE
    xTaskCreatePinnedToCore(waveTask, "wave", CFG_STACK_WAVE, nullptr,
                            CFG_PRIO_WAVE, nullptr, CFG_CORE_WAVE);
#endif

    Serial.printf("模块开关: husky=%d display=%d sysinfo=%d button=%d cmd=%d stepper=%d wave=%d\n",
                  CFG_RUN_HUSKY, CFG_RUN_DISPLAY, CFG_RUN_SYSINFO,
                  CFG_RUN_BUTTON, CFG_RUN_SERIALCMD, CFG_RUN_STEPPER,
                  CFG_RUN_WAVE);

    // ---- 12) 明确告诉用户现在能不能自动取书 ----
    if (mechanism.needHoming()) {
        Serial.println("⚠️ 机械位置无效: 自动取书已被禁止。请先在调试页人工归零。");
        Serial.println("   步骤: g 进调试页 -> 小步长点动把机构调到参考位置 -> 点[设为零点]");
    }
    Serial.println("提示: 机械参数(每本书步数/推书行程/软限位)都是待标定值,");
    Serial.println("      标定方法见 README 第 8 节和 config.h 第 9.1 节。");

    Serial.println("BOOT_DONE");
    cli.printHelp();
}

// =============================================================================
// 主循环：只做按键事件转发，不写业务
// =============================================================================
void loop() {
    ButtonEvent event;
    while (buttons.queue() != nullptr &&
           xQueueReceive(buttons.queue(), &event, 0) == pdPASS) {
        handleButtonEvent(event);
    }
    vTaskDelay(pdMS_TO_TICKS(CFG_LOOP_DELAY_MS));
}
