#include <Arduino.h>

// 自己的库（都在 lib/ 下，PlatformIO 会自动加进 include 路径）
#include "ButtonLib.h"
#include "DisplayLib.h"
#include "HuskyLib.h"
#include "SerialCmdLib.h"
#include "SysInfoLib.h"

// 全工程唯一引脚表
#include "BoardConfig.h"

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
//
// 任务与队列流向：
//   recogTask(核心1)   --HuskyFrame--> DisplayLib.setFrame()  （显示库内部加锁）
//   monitorTask(核心0) --SystemInfo--> DisplayLib.setSystemInfo() + 串口
//   buttonTask(库内)   --ButtonQueue--> loop() 翻译成串口命令
//   serialCmdTask(库内) --CmdQueue--> 各命令回调
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
    Serial.printf("按键: BTN1=%d BTN2=%d BTN3=%d (按下为%s电平, 可选)\n",
                  BUTTON_PIN1, BUTTON_PIN2, BUTTON_PIN3,
                  (BUTTON_TRUE_VALUE != 0) ? "高" : "低");
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
        display.setSystemInfo(snap);

        uint32_t now = millis();
        if (now - lastPrint >= SYSINFO_INTERVAL_MS * 10U) {
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
    Serial.begin(115200);
    delay(200);  // 等串口稳定

    // ---- 互斥锁 ----
    infoMutex = xSemaphoreCreateMutex();
    if (infoMutex == nullptr) {
        Serial.println("初始化失败: 互斥锁创建不成功");
        return;
    }

    printBanner();

    // ---- 各库初始化 ----
    display.begin();
    display.showMessage("SmartBookShelf BOOT", 2000);

    buttons.begin();
    buttons.startTask();

    cli.begin();
    registerCommands();
    cli.startTask();

    // ---- 首次采集系统信息，让屏幕第一页就有数据 ----
    sysInfoCollect(systemInfo);
    display.setSystemInfo(systemInfo);
    sysInfoPrint(systemInfo);

    // ---- 建任务 ----
    // recogTask 独占二哈 I2C，放核心 1；其它都在核心 0
    xTaskCreatePinnedToCore(recogTask, "recog", 8192, nullptr, 3, nullptr, 1);
    xTaskCreatePinnedToCore(monitorTask, "monitor", 4096, nullptr, 1, nullptr, 0);

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
    vTaskDelay(pdMS_TO_TICKS(20));
}
