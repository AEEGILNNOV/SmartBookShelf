#include <Arduino.h>

#include "HuskyCam.h"
#include "HuskyConfig.h"

// =============================================================================
// CNN_Test —— HUSKYLENS 2（二哈二代）内置 CNN 识别测试
//
// 二哈自己带 NPU，CNN 推理在它内部完成（物体识别/物体分类/自学习分类/OCR…），
// ESP32-S3 只干三件事：I2C 通信、结果搬运、串口打印。
//
// 任务分工（单一职责，别在一个任务里又收又打印）：
//   huskyTask  ：唯一接触二哈和 I2C 的任务。连接、切算法、读结果、执行命令。
//   printTask  ：只负责串口打印。结果快照、系统信息都从队列拿。
//   monitorTask：采集堆/PSRAM/运行时间，写共享结构体（互斥锁保护）。
//
// 队列流向：
//   huskyTask   --ResultQueue-->  printTask
//   monitorTask --systemInfo(互斥锁)--> printTask
//   串口 loop   --CmdQueue-->     huskyTask
//
// 串口命令（要按回车；固件只认换行结尾的输入，避免串口噪声误触发）：
//   ?=帮助  i=状态  s=重扫I2C  r=重新连接  a=下一个算法  l=学习  f=清空学习
//   w=保存知识库  o=读取知识库  p=算法参数  t=通信压测  y=系统信息
// =============================================================================

// -----------------------------------------------------------------------------
// 状态标志 / 命令码
// -----------------------------------------------------------------------------
// 打印类型
#define PRINT_RESULT 0x01  // 识别结果
#define PRINT_INFO 0x02    // 二哈状态
#define PRINT_SYSTEM 0x03  // 系统信息

// 压测次数
#define STRESS_TIMES 30

// -----------------------------------------------------------------------------
// 数据快照（任务之间传拷贝，不用复杂容器）
// -----------------------------------------------------------------------------
// 一帧识别结果快照
struct ResultSnapshot {
    bool online;
    int16_t algoIndex;
    uint32_t frameCount;
    uint8_t wordNum;
    HuskyWord words[WORD_MAX_NUM];
};

// 二哈状态快照（给 i 命令和状态行用）
struct HuskySnapshot {
    bool online;
    int16_t algoIndex;
    int16_t resultNum;
    int16_t learnedNum;
    int16_t maxId;
    uint32_t frameCount;
    uint32_t errorCount;
    uint32_t recoverCount;
};

// 系统信息快照
struct SystemInfo {
    uint32_t heapTotalKb;
    uint32_t heapFreeKb;
    uint32_t ramUsedKb;
    uint32_t ramUsedPct;
    uint32_t psramTotalKb;
    uint32_t psramFreeKb;
    uint32_t psramUsedKb;
    uint32_t psramUsedPct;
    uint32_t flashUsedKb;
    uint32_t flashTotalKb;
    uint32_t flashUsedPct;
    uint32_t uptimeSeconds;
    bool psramFound;
};

// 打印请求（不携带数据，只告诉打印任务“该打什么了”）
struct PrintRequest {
    uint8_t kind;
};

// -----------------------------------------------------------------------------
// 全局对象 / 句柄
// -----------------------------------------------------------------------------
static HuskyCam husky;

// 队列句柄
static QueueHandle_t ResultQueue = nullptr;
static QueueHandle_t PrintQueue = nullptr;
static QueueHandle_t CmdQueue = nullptr;

// 互斥锁保护的共享数据
static SemaphoreHandle_t infoMutex = nullptr;
static ResultSnapshot resultSnap = {};
static HuskySnapshot huskySnap = {};
static SystemInfo systemInfo = {};

// 当前算法在算法表里的下标（0 开始）
static int16_t algoIndex = 0;

// 标记已经打印过结果，避免开机先刷一屏 0
static bool firstResultPrinted = false;

// -----------------------------------------------------------------------------
// 前置函数声明
// -----------------------------------------------------------------------------
static void huskyTask(void *pvParameters);
static void printTask(void *pvParameters);
static void monitorTask(void *pvParameters);

static void printHelp();
static void printSystemInfo(const SystemInfo &info);
static void printHuskyInfo();
static void printResultSnapshot(const ResultSnapshot &snap, bool force);
static void printAlgoParam();
static void busStress();
static void i2cScan();
static void handleCommand(char c);
static void pollSerial();
static void postPrint(uint8_t kind);
static void myQueueSend(uint8_t kind);

// -----------------------------------------------------------------------------
// 工具函数
// -----------------------------------------------------------------------------
// 百分比计算，用 uint64_t 防溢出
static uint32_t percentOf(uint32_t used, uint32_t total) {
    if (total == 0) {
        return 0;
    }
    return (uint32_t)(((uint64_t)used * 100U) / total);
}

// 取算法中文名。下标越界时返回“未知”，防止访问算法表越界。
static const char *algoName(int16_t index) {
    if (index < 0 || index >= (int16_t)HUSKY_ALGO_COUNT) {
        return "未知";
    }
    return HUSKY_ALGO_TABLE[index].name;
}

// 提交一个打印请求。打印队列只是“闹钟”，满了直接覆盖，不阻塞。
static void postPrint(uint8_t kind) {
    if (PrintQueue == nullptr) {
        return;
    }
    PrintRequest req;
    req.kind = kind;
    xQueueOverwrite(PrintQueue, &req);
}

// 统一队列发送封装（和 MacTry 项目里的 MyQueueSend 一个用法）
static void myQueueSend(uint8_t kind) {
    postPrint(kind);
}

// =============================================================================
// 二哈任务：唯一接触 I2C 和二哈库的任务
// =============================================================================
static void huskyTask(void *pvParameters) {
    HuskyWord words[WORD_MAX_NUM];
    uint8_t wordNum = 0;
    uint32_t lastRead = 0;
    uint32_t lastTry = 0;
    char c = 0;

    husky.begin(PIN_HUSKY_SDA, PIN_HUSKY_SCL);
    Serial.printf("I2C 初始化: SDA=GPIO%d SCL=GPIO%d 地址=0x50\n", PIN_HUSKY_SDA,
                  PIN_HUSKY_SCL);

    for (;;) {
        uint32_t now = millis();

        // ---- 1) 命令处理：放在连接之前，离线时也能按键 ----
        while (xQueueReceive(CmdQueue, &c, 0) == pdPASS) {
            handleCommand(c);

            // 命令里可能换了算法，切到当前下标对应的算法
            if (algoIndex >= 0 && algoIndex < (int16_t)HUSKY_ALGO_COUNT) {
                husky.setAlgorithm(HUSKY_ALGO_TABLE[algoIndex].algo);
            }

            // 学习/清空之后立刻刷新一次结果，不用等下一个周期
            lastRead = now - HUSKY_READ_INTERVAL_MS;
        }

        // ---- 2) 还没连上就周期性重连 ----
        if (!husky.connected()) {
            if (now - lastTry >= HUSKY_RECONNECT_INTERVAL_MS) {
                lastTry = now;
                Serial.println(
                    "等待二哈... (检查接线 / 协议类型设为 I2C / 按 s 重扫总线)");
                if (husky.connect()) {
                    if (xSemaphoreTake(infoMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
                        huskySnap.online = true;
                        huskySnap.algoIndex = algoIndex;
                        huskySnap.recoverCount++;
                        huskySnap.resultNum = 0;
                        huskySnap.learnedNum = 0;
                        huskySnap.maxId = 0;
                        xSemaphoreGive(infoMutex);
                    }
                    Serial.printf("二哈就绪: %s\n",
                                  HUSKY_ALGO_TABLE[algoIndex].name);
                    lastRead = now - HUSKY_READ_INTERVAL_MS;
                }
            }
            vTaskDelay(pdMS_TO_TICKS(20));
            continue;
        }

        // ---- 3) 周期性读一帧 CNN 结果 ----
        if (now - lastRead >= HUSKY_READ_INTERVAL_MS) {
            lastRead = now;

            int16_t ret = husky.readWords(words, WORD_MAX_NUM, wordNum);
            if (ret < 0) {
                // 通信超时，累计错误；连续出错就判掉线，交给上面重连
                uint32_t err = 0;
                if (xSemaphoreTake(infoMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
                    huskySnap.errorCount++;
                    err = huskySnap.errorCount;
                    xSemaphoreGive(infoMutex);
                }
                if (err == 1 || (err % 5) == 0) {
                    Serial.printf("读取超时(通信错误), 累计 %u 次\n", (unsigned)err);
                }
                if (err % 3 == 0) {
                    husky.markOffline();
                    Serial.println("连续通信超时, 重新初始化二哈...");
                }
            } else {
                // 读到结果（0 个也算正常，说明画面里没目标）
                if (xSemaphoreTake(infoMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
                    resultSnap.online = true;
                    resultSnap.algoIndex = algoIndex;
                    resultSnap.wordNum = wordNum;
                    resultSnap.frameCount++;
                    for (uint8_t i = 0; i < wordNum; i++) {
                        resultSnap.words[i] = words[i];
                    }

                    huskySnap.online = true;
                    huskySnap.algoIndex = algoIndex;
                    huskySnap.resultNum = husky.cachedResultNum();
                    huskySnap.learnedNum = husky.cachedLearnedNum();
                    huskySnap.maxId = husky.cachedMaxId();
                    huskySnap.frameCount = resultSnap.frameCount;
                    xSemaphoreGive(infoMutex);
                }

                // 结果快照投给打印任务：邮箱队列，只保留最新一帧
                ResultSnapshot snap = {};
                if (xSemaphoreTake(infoMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
                    snap = resultSnap;
                    xSemaphoreGive(infoMutex);
                }
                xQueueOverwrite(ResultQueue, &snap);
                myQueueSend(PRINT_RESULT);
            }
        }

        vTaskDelay(pdMS_TO_TICKS(5));
    }
}

// =============================================================================
// 打印任务：只负责串口输出，不碰 I2C
// =============================================================================
static void printTask(void *pvParameters) {
    PrintRequest req;
    ResultSnapshot snap;
    uint32_t lastPrint = 0;

    for (;;) {
        // 结果行：每 PRINT_INTERVAL_MS 刷一次，用 \r 原地覆盖，不刷屏
        if (xQueueReceive(ResultQueue, &snap, pdMS_TO_TICKS(10)) == pdPASS) {
            uint32_t now = millis();
            if (!firstResultPrinted || now - lastPrint >= PRINT_INTERVAL_MS) {
                lastPrint = now;
                if (!firstResultPrinted) {
                    Serial.println();
                }
                firstResultPrinted = true;
                printResultSnapshot(snap, false);
            }
        }

        // 命令触发的打印：二哈状态 / 系统信息
        if (xQueueReceive(PrintQueue, &req, pdMS_TO_TICKS(10)) == pdPASS) {
            if (req.kind == PRINT_INFO) {
                Serial.println();
                printHuskyInfo();
                firstResultPrinted = false;
                lastPrint = millis();
            } else if (req.kind == PRINT_SYSTEM) {
                SystemInfo infoSnap = {};
                if (xSemaphoreTake(infoMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
                    infoSnap = systemInfo;
                    xSemaphoreGive(infoMutex);
                }
                Serial.println();
                printSystemInfo(infoSnap);
                firstResultPrinted = false;
                lastPrint = millis();
            }
        }

        vTaskDelay(pdMS_TO_TICKS(5));
    }
}

// =============================================================================
// 系统信息任务：定时采集堆/PSRAM/Flash/运行时间，写共享结构体
// =============================================================================
static void monitorTask(void *pvParameters) {
    for (;;) {
        SystemInfo snap = {};

        uint32_t heapTotal = ESP.getHeapSize();
        uint32_t heapFree = ESP.getFreeHeap();
        uint32_t psramTotal = 0;
        uint32_t psramFree = 0;

        snap.psramFound = psramFound();
        if (snap.psramFound) {
            psramTotal = ESP.getPsramSize();
            psramFree = ESP.getFreePsram();
        }

        uint32_t sketchUsed = ESP.getSketchSize();
        uint32_t sketchFree = ESP.getFreeSketchSpace();

        snap.heapTotalKb = heapTotal / 1024;
        snap.heapFreeKb = heapFree / 1024;
        snap.ramUsedKb = (heapTotal - heapFree) / 1024;
        snap.ramUsedPct = percentOf(heapTotal - heapFree, heapTotal);

        snap.psramTotalKb = psramTotal / 1024;
        snap.psramFreeKb = psramFree / 1024;
        snap.psramUsedKb = (psramTotal - psramFree) / 1024;
        snap.psramUsedPct = percentOf(psramTotal - psramFree, psramTotal);

        snap.flashUsedKb = sketchUsed / 1024;
        snap.flashTotalKb = (sketchUsed + sketchFree) / 1024;
        snap.flashUsedPct = percentOf(sketchUsed, sketchUsed + sketchFree);

        snap.uptimeSeconds = millis() / 1000UL;

        if (xSemaphoreTake(infoMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
            systemInfo = snap;
            xSemaphoreGive(infoMutex);
        }

        vTaskDelay(pdMS_TO_TICKS(MONITOR_INTERVAL_MS));
    }
}

// =============================================================================
// 打印：帮助 / 系统信息 / 二哈状态 / 识别结果
// =============================================================================
static void printHelp() {
    Serial.println("================ 串口命令 ================");
    Serial.println("  s = 重扫 I2C 总线      r = 重新连接二哈");
    Serial.println("  i = 二哈状态           t = I2C 通信压测");
    Serial.println("  l = 学习当前目标       f = 清空学习内容");
    Serial.println("  w = 保存知识库         o = 读取知识库");
    Serial.println("  p = 读算法参数(调试)   y = 系统信息");
    Serial.println("  a = 切换到下一个算法");
    Serial.println("  数字 1~8 = 直接切到对应算法");
    Serial.println("  ? = 显示帮助");
    Serial.println("  (发命令后要按回车; 固件只认换行结尾的输入)");
    Serial.println("=========================================");
    Serial.print("可选算法: ");
    for (uint8_t i = 0; i < HUSKY_ALGO_COUNT; i++) {
        Serial.printf("%c=%s  ", HUSKY_ALGO_TABLE[i].alpha,
                      HUSKY_ALGO_TABLE[i].name);
    }
    Serial.println();
}

static void printSystemInfo(const SystemInfo &info) {
    Serial.println("=== ESP32-S3 N16R8 系统信息 ===");
    Serial.printf("RAM: %u/%u KB (%u%%)\n", info.ramUsedKb, info.heapTotalKb,
                  info.ramUsedPct);

    if (info.psramFound) {
        Serial.printf("PSRAM: %u/%u KB (%u%%)\n", info.psramUsedKb,
                      info.psramTotalKb, info.psramUsedPct);
    } else {
        Serial.println(
            "PSRAM: 未找到(检查 board_build.arduino.memory_type = qio_opi)");
    }

    Serial.printf("Flash: %u/%u KB (%u%%)\n", info.flashUsedKb,
                  info.flashTotalKb, info.flashUsedPct);

    uint32_t s = info.uptimeSeconds;
    Serial.printf("运行时间: %02u:%02u:%02u\n", (unsigned)(s / 3600U),
                  (unsigned)((s / 60U) % 60U), (unsigned)(s % 60U));
}

static void printHuskyInfo() {
    HuskySnapshot snap = {};
    ResultSnapshot res = {};

    if (xSemaphoreTake(infoMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        snap = huskySnap;
        res = resultSnap;
        xSemaphoreGive(infoMutex);
    }

    Serial.println("=== 二哈 HUSKYLENS 2 状态 ===");
    if (!snap.online) {
        Serial.println("连接: 未连接(检查接线/协议类型/供电)");
        return;
    }

    Serial.printf("连接: 在线   算法: %s\n", algoName(snap.algoIndex));
    Serial.printf("当前画面: 识别=%d 其中已学习=%d\n", snap.resultNum,
                  snap.learnedNum);
    Serial.printf("设备已学习: 最大ID=%d\n", snap.maxId);
    Serial.printf("累计: 帧=%u 通信错误=%u 重连=%u\n", (unsigned)snap.frameCount,
                  (unsigned)snap.errorCount, (unsigned)snap.recoverCount);
    Serial.println("提示: OCR 只识别十字准心(画面中央)那个文字块的文字;");
    Serial.println("      对准书名, 未学习过的文字 ID=0。");
    if (res.wordNum > 0) {
        Serial.printf("上一帧识别到 %u 个块:\n", (unsigned)res.wordNum);
    }
}

// 识别结果：拼成一行用 \r 覆盖刷新，不刷屏
static void printResultSnapshot(const ResultSnapshot &snap, bool force) {
    char line[220];
    int len = 0;

    if (!snap.online) {
        snprintf(line, sizeof(line), "二哈未连接  等待重连...");
        Serial.printf("\r%-100s", line);
    } else if (snap.wordNum == 0) {
        snprintf(line, sizeof(line),
                 "[%s] 已连接, 画面无识别目标  帧=%u   (把目标对准镜头)",
                 algoName(snap.algoIndex),
                 (unsigned)snap.frameCount);
        Serial.printf("\r%-100s", line);
    } else {
        len += snprintf(line + len, sizeof(line) - len, "[%s] 识别到 %u 个: ",
                        algoName(snap.algoIndex),
                        (unsigned)snap.wordNum);
        for (uint8_t i = 0; i < snap.wordNum && len < (int)sizeof(line) - 40;
             i++) {
            const HuskyWord *w = &snap.words[i];
            const char *text = w->content;
            if (text[0] == '\0') {
                text = w->name;
            }
            len += snprintf(line + len, sizeof(line) - len, "#%d '%s'(%d,%d) ",
                            (int)w->id, text, (int)w->x, (int)w->y);
        }
        Serial.printf("\r%-160s", line);
    }

    if (force) {
        Serial.println();
    }
}

// =============================================================================
// 命令实现
// =============================================================================

// 学习当前画面中心目标（二哈 CNN 提特征入库），顺便把读到的文字打出来
static void doLearn() {
    if (!husky.connected()) {
        Serial.println("未连接, 不能学习");
        return;
    }
    Serial.println("学习当前画面中心目标(镜头对准目标)...");

    uint8_t id = husky.learn();
    if (id == 0) {
        Serial.println("  学习失败(画面里没目标, 或二哈正忙)");
        return;
    }

    // 学习完再读一次，看看这次学到的文字/名称
    HuskyWord words[WORD_MAX_NUM];
    uint8_t wordNum = 0;
    husky.readWords(words, WORD_MAX_NUM, wordNum);

    char text[WORD_TEXT_LEN];
    memset(text, 0, sizeof(text));
    if (wordNum > 0) {
        if (words[0].content[0] != '\0') {
            snprintf(text, sizeof(text), "%s", words[0].content);
        } else {
            snprintf(text, sizeof(text), "%s", words[0].name);
        }
    }
    if (text[0] == '\0') {
        snprintf(text, sizeof(text), "ID%u", (unsigned)id);
    }

    // 不调用 setNameByID：设了名字可能抑制 content，而且中文名容易重复字
    Serial.printf("  学习成功: ID=%u 读到内容='%s'\n", (unsigned)id, text);
    Serial.println("  提醒: 学习结果断电会丢, 用 w 保存到知识库槽位。");
}

static void doForget() {
    if (!husky.connected()) {
        Serial.println("未连接, 不能清空");
        return;
    }
    Serial.println(husky.forget() ? "已清空当前算法的全部学习内容"
                                  : "清空失败");
}

static void doSaveKnowledge() {
    if (!husky.connected()) {
        Serial.println("未连接");
        return;
    }
    // 槽位固定用 0，够测试用；要多槽位自己加命令
    Serial.println(husky.saveKnowledge(0) ? "知识库已保存到槽位 0(断电不丢)"
                                          : "保存失败");
}

static void doLoadKnowledge() {
    if (!husky.connected()) {
        Serial.println("未连接");
        return;
    }
    Serial.println(husky.loadKnowledge(0) ? "已从槽位 0 读取知识库"
                                          : "读取失败");
}

// 探测 OCR 等算法的参数（当初查“content 为空”就是靠这个）
static void printAlgoParam() {
    if (!husky.connected()) {
        Serial.println("未连接");
        return;
    }
    static const char *keys[] = {"lang",     "language", "dict_type", "text_type",
                                 "ocr_lang", "show_name", "det_thres",
                                 "ocr_type"};
    Serial.printf("算法参数(%s):\n", algoName(algoIndex));
    for (uint8_t i = 0; i < sizeof(keys) / sizeof(keys[0]); i++) {
        String s = husky.getAlgoParam(keys[i]);
        Serial.printf("  %-10s = '%s'\n", keys[i], s.c_str());
        delay(20);
    }
}

// 通信压测：连续做 N 次协议往返，看成功率。
// 成功率低 = 总线电气问题(上拉弱/线太长) 或二哈没在服务状态。
static void busStress() {
    if (!husky.connected()) {
        Serial.println("未连接, 不能压测");
        return;
    }
    const int n = STRESS_TIMES;

    int ackOk = 0;
    for (int i = 0; i < n; i++) {
        if (husky.probe()) {
            ackOk++;
        }
    }
    Serial.printf("1) 地址 ACK      : %d/%d 成功\n", ackOk, n);

    int grOk = 0;
    int grErr = 0;
    int grZero = 0;
    HuskyWord words[WORD_MAX_NUM];
    uint8_t wordNum = 0;
    for (int i = 0; i < n; i++) {
        int16_t r = husky.readWords(words, WORD_MAX_NUM, wordNum);
        if (r < 0) {
            grErr++;
        } else if (wordNum == 0) {
            grZero++;
        } else {
            grOk++;
        }
        delay(50);
    }
    Serial.printf("2) 读结果 getResult: 有目标%d 无目标%d 通信错误%d / %d\n", grOk,
                  grZero, grErr, n);
    Serial.printf("   (I2C %d Hz, 事务超时 %d ms, 库 retry=%d)\n", HUSKY_I2C_HZ,
                  HUSKY_I2C_TIMEOUT_MS, HUSKY_RETRY);
    if (grErr > 0) {
        Serial.println("   => 有通信错误: 二哈可能忙于 NPU 识别, 或总线电气质量差");
    }
}

// 扫描 I2C 总线，确认二哈在不在
static void i2cScan() {
    Serial.println("--- I2C 总线扫描 ---");
    int found = 0;
    for (uint8_t a = 1; a < 127; a++) {
        Wire.beginTransmission(a);
        if (Wire.endTransmission() == 0) {
            Serial.printf("  发现设备 0x%02X%s\n", a,
                          (a == 0x50) ? "   <-- HUSKYLENS 2" : "");
            found++;
        }
    }
    if (found == 0) {
        Serial.println("  未发现任何 I2C 设备!");
        Serial.println("  检查: 供电? GND 共地? 绿=SDA接GPIO1, 蓝=SCL接GPIO2?");
    }
    Serial.println("--- 扫描结束 ---");
}

static void handleCommand(char c) {
    // 数字 1~8：直接切到算法表里对应的算法
    for (uint8_t i = 0; i < HUSKY_ALGO_COUNT; i++) {
        if (c == HUSKY_ALGO_TABLE[i].alpha) {
            algoIndex = (int16_t)i;
            husky.markOffline();  // 换算法要重新连接
            Serial.printf("切换算法 -> %s (正在重新加载 CNN 模型...)\n",
                          HUSKY_ALGO_TABLE[i].name);
            return;
        }
    }

    switch (c) {
        case '?':
            printHelp();
            break;
        case 'i':
        case 'I':
            postPrint(PRINT_INFO);
            break;
        case 's':
        case 'S':
            i2cScan();
            break;
        case 'r':
        case 'R':
            husky.markOffline();
            Serial.println("重新连接二哈...");
            break;
        case 'a':
        case 'A':
            algoIndex++;
            if (algoIndex >= (int16_t)HUSKY_ALGO_COUNT) {
                algoIndex = 0;
            }
            husky.markOffline();
            Serial.printf("切换算法 -> %s\n", algoName(algoIndex));
            break;
        case 'l':
        case 'L':
            doLearn();
            break;
        case 'f':
        case 'F':
            doForget();
            break;
        case 'w':
        case 'W':
            doSaveKnowledge();
            break;
        case 'o':
        case 'O':
            doLoadKnowledge();
            break;
        case 'p':
        case 'P':
            printAlgoParam();
            break;
        case 't':
        case 'T':
            busStress();
            break;
        case 'y':
        case 'Y':
            postPrint(PRINT_SYSTEM);
            break;
        default:
            break;
    }
}

// 读串口：只在收到换行时执行命令，避免串口噪声误触发
static void pollSerial() {
    static char buf[CMD_BUF_LEN];
    static uint8_t len = 0;

    while (Serial.available()) {
        char c = Serial.read();
        if (c == '\n' || c == '\r') {
            if (len > 0) {
                buf[len] = '\0';
                xQueueSend(CmdQueue, &buf[0], 0);
                len = 0;
            }
        } else if (len < sizeof(buf) - 1) {
            buf[len++] = c;
        }
    }
}

// =============================================================================
// 初始化
// =============================================================================
void setup() {
    Serial.begin(115200);
    delay(200);  // 等串口稳定

    Serial.println();
    Serial.println("===== CNN_Test: HUSKYLENS 2 内置 CNN 识别测试 =====");
    Serial.printf("板型: ESP32-S3 DevKitC-1 N16R8  Flash=%uMB  PSRAM=%s\n",
                  (unsigned)(ESP.getFlashChipSize() / (1024 * 1024)),
                  psramFound() ? "已找到" : "未找到");
    Serial.printf("接线: 绿线SDA=GPIO%d  蓝线SCL=GPIO%d  VCC=5V/3V3  GND共地\n",
                  PIN_HUSKY_SDA, PIN_HUSKY_SCL);
    Serial.println("二哈屏幕: 系统设置 -> 协议类型 -> I2C");

    // ---- 创建队列 ----
    ResultQueue = xQueueCreate(SNAPSHOT_QUEUE_SIZE, sizeof(ResultSnapshot));
    PrintQueue = xQueueCreate(1, sizeof(PrintRequest));
    CmdQueue = xQueueCreate(CMD_QUEUE_SIZE, sizeof(char));

    // ---- 创建互斥锁 ----
    infoMutex = xSemaphoreCreateMutex();

    if (ResultQueue == nullptr || PrintQueue == nullptr || CmdQueue == nullptr ||
        infoMutex == nullptr) {
        Serial.println("队列/互斥锁创建失败, 请重启");
        return;
    }

    // 默认算法：OCR 文字识别（在算法表里找）
    algoIndex = 0;
    for (uint8_t i = 0; i < HUSKY_ALGO_COUNT; i++) {
        if (HUSKY_ALGO_TABLE[i].algo == ALGORITHM_OCR_RECOGNITION) {
            algoIndex = (int16_t)i;
            break;
        }
    }
    Serial.printf("默认算法: %s\n", algoName(algoIndex));

    printHelp();

    // 开一次系统信息，确认 PSRAM/Flash 配置对不对
    postPrint(PRINT_SYSTEM);

    // ---- 创建任务 ----
    // huskyTask 独占 I2C，放核心 1；打印和采集放核心 0，互不打扰
    xTaskCreatePinnedToCore(huskyTask, "husky", 8192, nullptr, 3, nullptr, 1);
    xTaskCreatePinnedToCore(printTask, "print", 8192, nullptr, 2, nullptr, 0);
    xTaskCreatePinnedToCore(monitorTask, "monitor", 4096, nullptr, 1, nullptr, 0);

    Serial.println("BOOT_DONE");
}

void loop() {
    // loop 里只做简单的事：收串口命令，不写业务逻辑
    pollSerial();
    vTaskDelay(pdMS_TO_TICKS(20));
}
