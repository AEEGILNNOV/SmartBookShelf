#include <Arduino.h>

// 自己的库（都在 lib/ 下，PlatformIO 会自动加进 include 路径）
#include "ButtonLib.h"
#include "DisplayLib.h"
#include "HuskyLib.h"
#include "SerialCmdLib.h"
#include "StepperLib.h"
#include "SysInfoLib.h"
#include "UltraLib.h"

// 全工程唯一配置来源（引脚/任务节奏/开关都在这里）
#include "config.h"

// =============================================================================
// SmartBookShelf / Main —— 主控整合程序
//
// 这个文件只干四件事：建全局对象、建队列/互斥锁、注册串口命令、建任务。
// 真正的功能都在 lib/ 里：
//   HuskyLib      二哈 I2C + 内置 CNN 识别结果读取
//   DisplayLib    ILI9341 状态页/系统信息页/书列表页（自带 displayTask）
//   SysInfoLib    RAM/PSRAM/Flash/运行时间采集
//   ButtonLib     按键消抖 + 事件队列（自带 buttonTask）
//   SerialCmdLib  串口命令行解析 + 分派（自带 serialCmdTask）
//   StepperLib    28BYJ-48/ULN2003 步进驱动（宏可切 42 步进），非阻塞走步 + 软限位
//   UltraLib      HC-SR04 超声波测距（非阻塞状态机，带超时/错误码）
//
// 任务与队列流向：
//   recogTask(核心1)   --HuskyFrame--> DisplayLib.setFrame()  （显示库内部加锁）
//   monitorTask(核心0) --SystemInfo--> DisplayLib.setSystemInfo() + 串口
//   buttonTask(库内)   --ButtonQueue--> loop() 翻译成串口命令
//   serialCmdTask(库内) --CmdQueue--> 各命令回调
//   cmdMotor/c  --MotorQueue--> stepperTask(核心1) --service()--> motor1/2/3
//   waveTask(核心0) --WaveState(waveMutex 保护)--> DisplayLib.setWave() + 串口
//   loop() 只做按键事件转发和短延时，不写业务
// =============================================================================

// -----------------------------------------------------------------------------
// 全局对象 / 句柄
// -----------------------------------------------------------------------------
static HuskyLib husky;
static DisplayLib display;
static ButtonLib buttons;
static SerialCmdLib cli;

// 系统信息共享结构体（infoMutex 保护）
static SemaphoreHandle_t infoMutex = nullptr;
static SystemInfo systemInfo = {};

// ---- 步进电机：三台各一个对象，引脚全部来自 config.h 第 9 节 ----
// ⚠️ 只有 stepperTask 能调 service()/run()，别的任务想动电机请发命令进 MotorQueue。
// 传 nullptr 表示这台电机没接线：begin() 不会碰引脚，service() 立刻返回。
static const uint8_t STEPPER_PINS_M1[4] = CFG_STEPPER_M1_PINS;
static const uint8_t STEPPER_PINS_M2[4] = CFG_STEPPER_M2_PINS;
static const uint8_t STEPPER_PINS_M3[4] = CFG_STEPPER_M3_PINS;

static Stepper28BYJ motor1(CFG_STEPPER_M1_ENABLE ? STEPPER_PINS_M1 : nullptr);
static Stepper28BYJ motor2(CFG_STEPPER_M2_ENABLE ? STEPPER_PINS_M2 : nullptr);
static Stepper28BYJ motor3(CFG_STEPPER_M3_ENABLE ? STEPPER_PINS_M3 : nullptr);

// 按下标取电机：0 = M1推出, 1 = M2夹层, 2 = M3升降；传 -1 表示不指定
static Stepper28BYJ *getMotor(int index) {
    switch (index) {
        case 0:
            return &motor1;
        case 1:
            return &motor2;
        case 2:
            return &motor3;
        default:
            return nullptr;
    }
}

// 步进命令队列：串口/按键投递，stepperTask 消费（单一消费者，不用锁）
static QueueHandle_t MotorQueue = nullptr;

// ---- 超声波测距（HC-SR04）----
// ⚠️ 引脚与步进电机1 的 4/5/6/7 冲突，两者不要同时接（见 config.h 第 8 节）。
// 只有 waveTask 碰这个对象，别的任务读 waveMutex 保护的快照。
static HCSR04 waveSensor(PIN_WAVE_TRIG, PIN_WAVE_ECHO, CFG_WAVE_TIMEOUT_US);
static WaveState waveState = {};
static SemaphoreHandle_t waveMutex = nullptr;

// 滤波窗口：最近 CFG_WAVE_FILTER_N 次有效读数，取平均后清零重来
static uint32_t waveFilterBuf[CFG_WAVE_FILTER_N > 0 ? CFG_WAVE_FILTER_N : 1] = {};
static uint8_t waveFilterCount = 0;

// 电机名字，打印用；下标 0~2 对应 motor1~motor3
static const char *MOTOR_NAME[3] = { "M1推出", "M2夹层", "M3升降" };

// 软限位行程，下标 0~2 对应 motor1~motor3
static const int32_t MOTOR_RANGE[3] = { CFG_STEPPER_M1_RANGE_STEPS,
                                        CFG_STEPPER_M2_RANGE_STEPS,
                                        CFG_STEPPER_M3_RANGE_STEPS };

// 本帧是否已经把“新识别到的书”记进显示库（防止同一次识别重复计次）
static int32_t lastCountedId = -1;
static char lastCountedText[HUSKY_TEXT_LEN] = "";

// -----------------------------------------------------------------------------
// 前置函数声明
// -----------------------------------------------------------------------------
static void recogTask(void *pvParameters);
static void monitorTask(void *pvParameters);
static void handleButtonEvent(const ButtonEvent &event);

static void printBanner();
static void printStatus();
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
static Stepper28BYJ *getMotor(int index);
static char *parseMotorCmd(char *line, int *outIndex);
static void runMotor(int index, int32_t steps);
static void printStepperState();
static void stepperTask(void *pvParameters);
static void waveTask(void *pvParameters);
static void cmdWave(const char *arg);
static void printWaveState();
static void cmdDisplay(const char *arg);
static void cmdTouch(const char *arg);

// -----------------------------------------------------------------------------
// 工具
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
        Serial.println("  检查: 供电? GND 共地? 绿=SDA接GPIO1, 蓝=SCL接GPIO2?");
    }
    Serial.println("--- 扫描结束 ---");
}

static void printBanner() {
    Serial.println();
    Serial.println("==================================================");
    Serial.println(" SmartBookShelf / Main  (库化整合版)");
    Serial.println("==================================================");
    Serial.printf("库版本: husky=%s display=%s sysinfo=%s button=%s cmd=%s\n",
                  huskyLibVersion(), displayLibVersion(), sysInfoVersion(),
                  buttonLibVersion(), serialCmdLibVersion());
    Serial.printf("接线: 二哈 SDA=GPIO%d SCL=GPIO%d | 屏 MOSI=%d SCLK=%d CS=%d "
                  "DC=%d RST=%d BL=%d\n",
                  PIN_HUSKY_SDA, PIN_HUSKY_SCL, PIN_TFT_MOSI, PIN_TFT_SCLK,
                  PIN_TFT_CS, PIN_TFT_DC, PIN_TFT_RST, PIN_TFT_BL);
    Serial.printf("按键: BTN1=%d BTN2=%d BTN3=%d A键=%d B键=%d (按下为%s电平)\n",
                  BUTTON_PIN1, BUTTON_PIN2, BUTTON_PIN3, BUTTON_PIN4,
                  BUTTON_PIN5, (BUTTON_TRUE_VALUE != 0) ? "高" : "低");
    // 步进引脚是“宏展开成 {...} 初始化列表”，不能直接塞进 printf 的变参，
    // 所以先落成 static 数组（static 保证在 setup 之前就完成初始化）再逐个数。
    static const uint8_t m1pins[4] = CFG_STEPPER_M1_PINS;
    static const uint8_t m2pins[4] = CFG_STEPPER_M2_PINS;
    static const uint8_t m3pins[4] = CFG_STEPPER_M3_PINS;
    Serial.printf("测距: TRIG=%d ECHO=%d (ECHO 必须分压, 与M1冲突)\n",
                  PIN_WAVE_TRIG, PIN_WAVE_ECHO);
    // 触摸是独立 5 根线，和屏的 SPI、和 TFT_eSPI 的 TOUCH_CS 都没关系
    Serial.printf("触摸: T_CS=%d T_IRQ=%d T_CLK=%d T_DIN=%d T_DO=%d (开关 CFG_ENABLE_TOUCH=%d)\n",
                  PIN_TOUCH_CS, PIN_TOUCH_IRQ, PIN_TOUCH_CLK, PIN_TOUCH_DIN,
                  PIN_TOUCH_DO, CFG_ENABLE_TOUCH);
    Serial.printf("步进: M1=%d,%d,%d,%d M2=%d,%d,%d,%d M3=%d,%d,%d,%d\n",
                  m1pins[0], m1pins[1], m1pins[2], m1pins[3], m2pins[0],
                  m2pins[1], m2pins[2], m2pins[3], m3pins[0], m3pins[1],
                  m3pins[2], m3pins[3]);
    Serial.println("二哈屏幕: 系统设置 -> 协议类型 -> I2C");
}

// 状态打印：二哈 + 系统信息一起给全
static void printStatus() {
    husky.printState();
    SystemInfo snap = {};
    if (infoMutex != nullptr &&
        xSemaphoreTake(infoMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        snap = systemInfo;
        xSemaphoreGive(infoMutex);
    }
    sysInfoPrint(snap);
    Serial.printf("已记录书数: %d\n", (int)display.bookCount());
    printStepperState();
    printWaveState();
}

// -----------------------------------------------------------------------------
// 串口命令实现
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
    Serial.printf("屏幕切到第 %u 页\n", (unsigned)display.page());
}

// 切算法：可以直接给数字(在模式表里的位置 1~8 字符)，不给就切下一个
static void cmdAlgo(const char *arg) {
    if (arg != nullptr && arg[0] != '\0') {
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
    Serial.printf("  学习成功: ID=%u\n", (unsigned)id);
    Serial.println("  提醒: 学习结果断电会丢, 用 w 或 w,槽位 保存到知识库。");
}

static void cmdForget(const char *arg) {
    Serial.println(husky.forget() ? "已清空当前算法的学习内容" : "清空失败");
}

static void cmdSaveSlot(const char *arg) {
    uint8_t slot = 0;
    if (arg != nullptr && arg[0] != '\0') {
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
    if (arg != nullptr && arg[0] != '\0') {
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

static void cmdBooks(const char *arg) {
    Serial.printf("已记录书数: %d\n", (int)display.bookCount());
    display.nextPage();
    display.setPage(PAGE_BOOKS);
}

static void cmdClearBooks(const char *arg) {
    display.clearBooks();
    Serial.println("已清空书列表记录");
}

// 解析步进命令：[电机号,]动作。line 形如 "1,300" / "1,-300" / "z" / "s1500"
// 返回去掉“电机号,”之后的动作串；*outIndex 回填 0~2，没写电机号就保持原值。
static char *parseMotorCmd(char *line, int *outIndex) {
    int first = atoi(line);  // 字符串开头不是数字时 atoi 返回 0
    if (line[0] >= '1' && line[0] <= '3' && first >= 1 && first <= 3) {
        char *comma = strchr(line, ',');
        if (comma != nullptr) {
            *outIndex = first - 1;
            return comma + 1;
        }
    }
    return line;
}

// 让某台电机走 steps 步：先把其它电机停掉，避免同时走步
static void runMotor(int index, int32_t steps) {
    if (index < 0) {
        index = 0;  // 默认电机1（推出小车）
    }
    if (index > 2) {
        Serial.println("步进: 电机号只能是 1/2/3");
        return;
    }

    Stepper28BYJ *m = getMotor(index);
    if (m == nullptr || !m->isWired()) {
        Serial.printf("步进: %s 没接线/没启用, 先在 config.h 里打开对应的 ENABLE 宏\n",
                      MOTOR_NAME[index]);
        return;
    }

    // 同一时间只动一台：别的先停
    for (int i = 0; i < 3; i++) {
        if (i != index) {
            Stepper28BYJ *other = getMotor(i);
            if (other != nullptr) {
                other->stop();
            }
        }
    }

    if (!m->run(steps)) {
        Serial.printf("步进: %s 步数 %ld 不合法（0 或超过 CFG_STEPPER_MAX_STEPS=%d）\n",
                      MOTOR_NAME[index], (long)steps, CFG_STEPPER_MAX_STEPS);
        return;
    }

    // 估算耗时：步数 × 每步延时，串口上给个心理准备
    uint32_t us = (uint32_t)(steps > 0 ? steps : -steps) * m->stepDelayUs();
    Serial.printf("步进: %s %s %ld 步, 速度 %luus/步, 预计 %lu.%01lus\n",
                  MOTOR_NAME[index], (steps > 0) ? "正转" : "反转",
                  (long)(steps > 0 ? steps : -steps),
                  (unsigned long)m->stepDelayUs(), (unsigned long)(us / 1000000UL),
                  (unsigned long)((us / 100000UL) % 10UL));
    Serial.println("  提示: 上电位置就是零点, 没有限位开关; 软限位到边会自动停。");
}

// 打印三台电机状态（串口命令 m,? 和 i 都用它）
static void printStepperState() {
    Serial.printf("步进(驱动=%s, 模式=%s, 步/圈=%d):\n",
                  (CFG_STEPPER_DRIVER == 0) ? "28BYJ+ULN2003" : "42步进+驱动器",
                  (CFG_STEPPER_WAVE_MODE == 0) ? "八拍半步" : "四拍整步",
                  STEPPER_STEPS_PER_REV);

    for (int i = 0; i < 3; i++) {
        Stepper28BYJ *m = getMotor(i);
        if (m == nullptr || !m->isWired()) {
            Serial.printf("  %s: 未接线/未启用\n", MOTOR_NAME[i]);
            continue;
        }
        Serial.printf("  %s: pos=%ld 步, %s, 剩余=%ld, 速度=%luus/步, 软限位=%ld\n",
                      MOTOR_NAME[i], (long)m->position(),
                      m->busy() ? "在走" : "停止", (long)m->stepsLeft(),
                      (unsigned long)m->stepDelayUs(), (long)m->rangeSteps());
    }
}

// =============================================================================
// 步进电机任务：唯一接触电机引脚的任务
//
// 串口/按键只往 MotorQueue 投命令，本任务消费命令并推进电机状态机。
// 一次只服务一台电机（runMotor 里会先停掉其它电机），避免三台同时走步把
// 电流和 CPU 都拉满；真要同时动再改成每台一个 service()。
// =============================================================================
static void stepperTask(void *pvParameters) {
    Serial.println("步进任务: 已启动");

    for (;;) {
        // ---- 1) 收命令（非阻塞，队列空就往下走）----
        char line[CFG_STEPPER_CMD_LEN];
        if (MotorQueue != nullptr &&
            xQueueReceive(MotorQueue, line, 0) == pdPASS) {
            int midx = -1;
            char *body = parseMotorCmd(line, &midx);

            char action = body[0];
            if (action == '\0') {
                Serial.println("步进: 命令为空, 用法 m,<电机>,<步数> | m,z | m,?");
            } else if (action == 'z' || action == 'Z') {
                cmdStopMotor(nullptr);  // 参数为 nullptr = 停全部
            } else if (action == '?') {
                printStepperState();
            } else if (action == 's' || action == 'S') {
                // m,2,s500  或直接 m,s500：改速度（两步之间的延时，单位 us）
                long v = atol(body + 1);
                if (v < 0) {
                    v = -v;
                }
                if (v == 0) {
                    Serial.println("步进: 速度参数不对, 例 m,2,s1500");
                } else {
                    motor1.setStepDelayUs((uint32_t)v);
                    motor2.setStepDelayUs((uint32_t)v);
                    motor3.setStepDelayUs((uint32_t)v);
                    Serial.printf("步进: 三台速度都改成 %ldus/步\n", v);
                }
            } else {
                // 剩下的按“步数”处理：m,1,-300 / m,-300 / m,300
                long steps = atol(body);
                if (steps == 0 && body[0] != '0') {
                    Serial.println("步进: 看不懂这条命令, 输入 m 看用法");
                } else {
                    runMotor(midx, (int32_t)steps);
                }
            }
        }

        // ---- 2) 推进三台电机的状态机（没在走的立刻返回）----
        motor1.service();
        motor2.service();
        motor3.service();

        // 还有电机在走就快点轮询，都停了就慢点，省 CPU
        bool anyMoving = motor1.busy() || motor2.busy() || motor3.busy();
        vTaskDelay(pdMS_TO_TICKS(anyMoving ? STEPPER_SERVICE_INTERVAL_MS : 10));
    }
}

// 串口命令 m：步进电机手动调试
//   m              看用法
//   m,?            打印三台电机的状态
//   m,1,300        电机1 正转 300 步
//   m,1,-300       电机1 反转 300 步
//   m,300          不写电机号，默认电机1
//   m,z            全部急停
//   m,2,z          只停电机2
//   m,s1500        三台速度都改成 1500us/步
static void cmdMotor(const char *arg) {
    if (arg == nullptr || arg[0] == '\0') {
        Serial.println("步进命令用法:");
        Serial.println("  m            看这段用法");
        Serial.println("  m,?          打印三台电机状态(位置/行程/速度)");
        Serial.println("  m,1,300      电机1 正转 300 步");
        Serial.println("  m,1,-300     电机1 反转 300 步");
        Serial.println("  m,300        不写电机号, 默认电机1");
        Serial.println("  m,z          全部急停");
        Serial.println("  m,2,z        只停电机2");
        Serial.println("  m,s1500      三台速度都改成 1500us/步");
        Serial.println("  电机号: 1=推出小车(Y)  2=中间夹层  3=升降平台");
        Serial.println("  注意: 上电位置就是零点, 没有限位开关, 先小步数试方向!");
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

static void cmdStopMotor(const char *arg) {
    int index = -1;
    if (arg != nullptr && arg[0] != '\0') {
        index = atoi(arg);
        if (index != 1 && index != 2 && index != 3) {
            Serial.println("步进: 电机号只能是 1/2/3, 不给就是全部");
            return;
        }
    }

    if (index < 0) {
        motor1.stop();
        motor2.stop();
        motor3.stop();
        Serial.println("步进: 三台全部停机");
        return;
    }

    Stepper28BYJ *m = getMotor(index - 1);
    if (m == nullptr) {
        Serial.println("步进: 电机号不对");
        return;
    }
    m->stop();
    Serial.printf("步进: %s 已停机\n", MOTOR_NAME[index - 1]);
}

// 注册所有串口命令
static void registerCommands() {
    cli.registerCommand('?', "帮助", cmdHelp);
    cli.registerCommand('h', "帮助", cmdHelp);
    cli.registerCommand('s', "重扫 I2C 总线", cmdScan);
    cli.registerCommand('i', "状态(二哈+系统信息)", cmdInfo);
    cli.registerCommand('r', "重新连接二哈", cmdReconnect);
    cli.registerCommand('n', "屏幕下一页", cmdNextPage);
    cli.registerCommand('a', "切换算法(可不带参数, 也支持 a,4)", cmdAlgo);
    cli.registerCommand('l', "学习当前画面中心目标", cmdLearn);
    cli.registerCommand('f', "清空学习内容", cmdForget);
    cli.registerCommand('w', "保存知识库(默认槽位0, 支持 w,1)", cmdSaveSlot);
    cli.registerCommand('o', "读取知识库(默认槽位0, 支持 o,1)", cmdLoadSlot);
    cli.registerCommand('p', "读算法参数(调试OCR)", cmdParam);
    cli.registerCommand('b', "看书列表页并打印数量", cmdBooks);
    cli.registerCommand('c', "清空书列表记录", cmdClearBooks);
    cli.registerCommand('m', "步进电机(手动调试): m 看用法, m,1,300 / m,z", cmdMotor);
    cli.registerCommand('z', "步进急停: z 全停, z,2 停电机2", cmdStopMotor);
    // ⚠️ 超声波必须用 u（Ultrasonic），不能用 w：
    //    w 已经被“保存知识库”占了，而命令表是“第一个注册的生效”，
    //    以前两条都写 w，结果超声波那条永远进不来（A/B 键会变成保存知识库）。
    //    SerialCmdLib 现在也会拦重复注册并在串口报警。
    cli.registerCommand('u', "超声波测距: u 看状态, u,s 记起点, u,e 算厚度, u,c 清统计",
                        cmdWave);
    cli.registerCommand('d', "显示诊断: d 看状态, d,1 彩条, d,2 SPI测速, d,3 触摸采样",
                        cmdDisplay);
    cli.registerCommand('t', "触摸测试页(第5页, 页上有按钮做点击计数)", cmdTouch);
}

// -----------------------------------------------------------------------------
// 按键事件 -> 屏幕/命令
// 按键定义：
//   按键1 按下 = 切下一页
//   按键2 按下 = 搜索/重新连接（r 命令）
//   按键3 按下 = 学习当前目标（l 命令）
// -----------------------------------------------------------------------------
static void handleButtonEvent(const ButtonEvent &event) {
    if (event.pressed == 0) {
        return;  // 松开事件先不处理
    }

    switch (event.index) {
        case 1:
            cli.sendCommand("n");
            Serial.println("按键1: 下一页");
            break;
        case 2:
            cli.sendCommand("r");
            Serial.println("按键2: 重新连接二哈");
            break;
        case 3:
            cli.sendCommand("l");
            Serial.println("按键3: 学习当前目标");
            break;
        // 说明书里的 A/B 两个键：A 取书、B 还书。
        // 现在机械还没接，先把它们接到测距的“记起点 / 算厚度”上，
        // 等 stepperTask 能整套动作时，把 sendCommand 换成取书/还书流程即可。
        case 4:
            cli.sendCommand("u,s");
            Serial.println("按键A: 记测厚起点");
            break;
        case 5:
            cli.sendCommand("u,e");
            Serial.println("按键B: 算书本厚度");
            break;
        default:
            break;
    }
}

// =============================================================================
// 识别任务：唯一接触二哈和 I2C 的任务
// =============================================================================
static void recogTask(void *pvParameters) {
    HuskyFrame frame;
    uint32_t lastRead = 0;
    uint32_t lastTry = 0;

    husky.begin(PIN_HUSKY_SDA, PIN_HUSKY_SCL);
    Serial.printf("识别任务: I2C 初始化完成, 默认算法=%s\n", husky.modeName());

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

        // ---- 2) 周期性读一帧 CNN 结果 ----
        if (now - lastRead >= HUSKY_READ_INTERVAL_MS) {
            lastRead = now;

            int16_t ret = husky.readFrame(frame);
            display.setFrame(frame);  // 显示库自己判断内容变没变

            if (ret < 0) {
                // 通信超时：连续出错就判掉线，交给上面重连
                if ((frame.errorCount % 3) == 0) {
                    Serial.printf("读取超时, 累计 %lu 次, 重新初始化二哈...\n",
                                  (unsigned long)frame.errorCount);
                    husky.markOffline();
                }
            } else if (frame.wordNum > 0) {
                // 只有“识别内容真的变了”才计一次，避免同一本书被反复计次
                const HuskyWord *w = &frame.words[0];
                const char *text = w->content;
                if (text[0] == '\0') {
                    text = w->name;
                }
                bool same = (w->id == lastCountedId) &&
                            (strcmp(text, lastCountedText) == 0);
                if (!same) {
                    lastCountedId = w->id;
                    snprintf(lastCountedText, sizeof(lastCountedText), "%s",
                             text);
                    display.countBook(*w);
                    Serial.printf("记到一本书: id=%d text='%s' (共 %d 本)\n",
                                  (int)w->id, text, (int)display.bookCount());
                }
            }
        }

        vTaskDelay(pdMS_TO_TICKS(5));
    }
}

// =============================================================================
// 超声波测距任务（HC-SR04）：唯一接触 TRIG/ECHO 的任务
//
// 非阻塞流程：
//   1) 每 WAVE_MEASURE_INTERVAL_MS 发起一次测量（手册建议间隔 >60ms）；
//   2) 每 1ms 推进一次状态机（update 里从不死等）；
//   3) 出结果后做量程过滤 + 滑动平均，写进 waveState（waveMutex 保护）。
// =============================================================================
static void waveTask(void *pvParameters) {
    Serial.printf("测距任务: TRIG=GPIO%d ECHO=GPIO%d 超时=%luus\n",
                  PIN_WAVE_TRIG, PIN_WAVE_ECHO,
                  (unsigned long)CFG_WAVE_TIMEOUT_US);

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
                waveState.distanceMm = mm;
                waveState.pulseUs = waveSensor.getPulseUs();
                waveState.errorCode = err;
                waveState.valid = valid;
                if (filtered != 0) {
                    waveState.filteredMm = filtered;
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

            // ---- 5) 连续读不到就提醒一次 ----
            if (!valid) {
                if (lastInvalid == 0 ||
                    (now - lastInvalid) >= CFG_WAVE_INVALID_LIMIT * 1000UL) {
                    lastInvalid = now;
                    Serial.printf("测距无效: err=%u (1=ECHO没拉高 2=ECHO没拉低), "
                                  "检查接线/分压/量程\n",
                                  (unsigned)err);
                }
            } else {
                lastInvalid = 0;
            }
        }

        vTaskDelay(pdMS_TO_TICKS(WAVE_UPDATE_INTERVAL_MS));
    }
}

// 打印一次测距状态（串口命令 w 和 i 都用它）
static void printWaveState() {
    WaveState snap = {};
    if (waveMutex != nullptr &&
        xSemaphoreTake(waveMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        snap = waveState;
        xSemaphoreGive(waveMutex);
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

// 串口命令 u：超声波测距（u = Ultrasonic；不能用 w，w 是保存知识库）
//   u        打印一次当前测距状态
//   u,s      记录“开始测量厚度”的起点（把书放上去之前先按）
//   u,e      结束测量：厚度 = 起点距离 - 当前距离
//   u,c      清空统计
static void cmdWave(const char *arg) {
    char sub = (arg != nullptr && arg[0] != '\0') ? arg[0] : '\0';

    if (sub == '\0') {
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
//   d        打印显示诊断：刷屏次数/LVGL 时钟/任务栈余量/缓冲位置/各排错开关
//   d,1      立刻用 TFT_eSPI 画一次彩条（停留约 1.5 秒，然后自动回到界面）
//   d,2      立刻测一次 SPI 总线速度（看 MCU 到底有没有在往屏上吐像素）
//
// 判读：彩条能出来 -> 屏/背光/接线/SPI 都是好的，问题在 LVGL 这一层
//       彩条也没有 -> 和 LVGL、字库都无关，查背光/接线/供电/屏型号
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
    display.printDiag();
}

// 串口命令 t：跳到触摸测试页（第 5 页），页上有一个大按钮做点击计数
static void cmdTouch(const char *arg) {
    display.setPage(PAGE_TOUCH);
    Serial.printf("屏幕切到触摸测试页(第 %u 页): 点屏上的按钮应该加 1\n",
                  (unsigned)display.page() + 1);
    Serial.println("  没反应就看串口有没有 '触摸: 测试按钮第 N 次按下'");
    Serial.println("  d    = 显示诊断(含触摸状态/坐标/原始值)");
    Serial.println("  d,3  = 采样触摸原始值(连按两次: 第一次开始, 第二次看结果)");
}

// =============================================================================
// 系统信息任务：采集 -> 共享结构体 + 显示库 + 周期打印
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

        // 显示库只关心自己那一页，内部会判断要不要重画
        if (CFG_RUN_DISPLAY) {
            display.setSystemInfo(snap);
        }

        uint32_t now = millis();
        if (now - lastPrint >= SYSINFO_INTERVAL_MS * SYSINFO_PRINT_EVERY) {
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

    // ---- 各库初始化 ----
    // 用编译期常量判断，别用 #if 把函数切成碎片（配置项一变，编译器会直接优化掉）
    if (CFG_RUN_DISPLAY) {
        display.begin();
        display.showMessage("SmartBookShelf BOOT",
                            (uint32_t)CFG_MESSAGE_HOLD_MS);
    }

    if (CFG_RUN_BUTTON) {
        buttons.begin();
        buttons.startTask();
    }

    cli.begin();
    registerCommands();
    cli.startTask();

    // ---- 超声波测距初始化 ----
    // ⚠️ 默认配置下 TRIG/ECHO(4/5) 与步进电机1(4,5,6,7) 冲突，两个都开会导致
    //    电机工作时干扰回波。要用测距就把 CFG_ENABLE_STEPPER 置 0（或挪 ECHO 引脚）。
    if (CFG_RUN_WAVE) {
        waveMutex = xSemaphoreCreateMutex();
        if (waveMutex == nullptr) {
            Serial.println("测距: 互斥锁创建失败");
        }
        waveSensor.begin();
        if (CFG_RUN_STEPPER) {
            Serial.println("⚠️ 警告: 测距(4/5)与步进电机1(4,5,6,7)引脚重叠, "
                           "建议把 CFG_ENABLE_STEPPER 置 0 或挪 ECHO 引脚");
        }
    }

    // ---- 步进电机初始化 ----
    // 三台电机共用一套速度；每台按 config.h 的行程设软限位。
    // 没接线的电机（ENABLE=0）这里会直接跳过，不会碰 GPIO。
    if (CFG_RUN_STEPPER) {
        MotorQueue = xQueueCreate(CFG_STEPPER_QUEUE_SIZE, CFG_STEPPER_CMD_LEN);
        if (MotorQueue == nullptr) {
            Serial.println("步进: 命令队列创建失败");
        }

        for (int i = 0; i < 3; i++) {
            Stepper28BYJ *m = getMotor(i);
            if (m == nullptr || !m->isWired()) {
                continue;
            }
            m->setStepDelayUs(CFG_STEPPER_STEP_DELAY_US);
            m->setRangeSteps(MOTOR_RANGE[i]);
            m->begin();
        }
        Serial.printf("步进: 已初始化(驱动=%s, 速度=%dus/步)\n",
                      (CFG_STEPPER_DRIVER == 0) ? "28BYJ+ULN2003"
                                                : "42步进+驱动器",
                      CFG_STEPPER_STEP_DELAY_US);
        printStepperState();
    }

    // ---- 首次采集系统信息，让屏幕第一页就有数据 ----
    sysInfoCollect(systemInfo);
    if (CFG_RUN_DISPLAY) {
        display.setSystemInfo(systemInfo);
    }
    sysInfoPrint(systemInfo);

    // ---- 建任务 ----
    // recogTask 独占二哈 I2C，放核心 1；其它都在核心 0。
    // 栈/优先级/核心全部来自 config.h 的 CFG_STACK_xxx / CFG_PRIO_xxx / CFG_CORE_xxx，
    // 不要在这里写死数字。
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
#else
    Serial.println("测距任务: 已在 config.h 里关掉 (CFG_ENABLE_WAVE=0)");
#endif

    Serial.printf("模块开关: husky=%d display=%d sysinfo=%d button=%d cmd=%d "
                  "stepper=%d wave=%d\n",
                  CFG_RUN_HUSKY, CFG_RUN_DISPLAY, CFG_RUN_SYSINFO,
                  CFG_RUN_BUTTON, CFG_RUN_SERIALCMD, CFG_RUN_STEPPER,
                  CFG_RUN_WAVE);

    Serial.println("BOOT_DONE");
    cli.printHelp();
}

// =============================================================================
// 主循环：只做按键事件转发，不写业务
// =============================================================================
void loop() {
    ButtonEvent event;
    while (xQueueReceive(buttons.queue(), &event, 0) == pdPASS) {
        handleButtonEvent(event);
    }
    vTaskDelay(pdMS_TO_TICKS(CFG_LOOP_DELAY_MS));
}
