#include <Arduino.h>

#include "HCSR04.h"

//名称
//状态标志
#define MEASURE 0x01 //发起一次测距
#define GUIREFRESH 0x01 //GUI刷新（本测试暂未接屏幕，保留格式）

//------------------------------------ 全局队列 ------------------------------------//
// 测距结果队列：ultrasonicTask 发 -> 需要距离的任务收
QueueHandle_t ButtonValueQueue = nullptr;
QueueHandle_t GUIFlagValueQueue = nullptr;
QueueHandle_t DistanceValueQueue = nullptr;

//------------------------------------ 测距统计结构体 ------------------------------------//
struct DistanceReport {
    uint32_t distanceMm;
    uint32_t pulseUs;
    uint32_t totalCount;
    uint32_t invalidCount;
    uint32_t timeoutRiseCount;
    uint32_t timeoutFallCount;
    uint32_t minMm;
    uint32_t maxMm;
    uint64_t sumMm;
    uint8_t errorCode;
};

// 全局测距统计和互斥锁
static DistanceReport distanceReport = {};
static SemaphoreHandle_t distanceMutex = nullptr;

// HC-SR04 驱动对象
static HCSR04 hcsr04(US_TRIG_PIN, US_ECHO_PIN, US_TIMEOUT_US);

//------------------------------------ 队列收发 ------------------------------------//
static void MyQueueSend(uint8_t GUI_Flag, uint32_t DistanceMm) {
    if (GUI_Flag != 0) {
        xQueueSend(GUIFlagValueQueue, &GUI_Flag, 50);
    }
    xQueueSend(DistanceValueQueue, &DistanceMm, 50);
}

// 只读快照，读完马上放锁
static void readDistanceReport(DistanceReport &snap) {
    if (distanceMutex != nullptr &&
        xSemaphoreTake(distanceMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        snap = distanceReport;
        xSemaphoreGive(distanceMutex);
    }
}

// 打印一条测距结果
static void printDistance(const DistanceReport &snap) {
    if (snap.errorCode == 1) {
        Serial.printf("[%lu] 超时：ECHO 一直没有拉高（检查 TRIG 接线 / 模块 5V）\n",
                      (unsigned long)(millis() / 1000UL));
        return;
    }
    if (snap.errorCode == 2) {
        Serial.printf("[%lu] 超时：ECHO 一直没有拉低（检查 ECHO 分压 / 是否超量程）\n",
                      (unsigned long)(millis() / 1000UL));
        return;
    }

    Serial.printf("[%lu] 距离 %u mm  (%u.%u cm)  回波 %u us  %s\n",
                  (unsigned long)(millis() / 1000UL),
                  snap.distanceMm,
                  snap.distanceMm / 10U,
                  snap.distanceMm % 10U,
                  snap.pulseUs,
                  ((snap.distanceMm < US_MIN_MM) || (snap.distanceMm > US_MAX_MM))
                      ? "超出量程" : "OK");
}

// 打印统计信息
static void printStatistics(const DistanceReport &snap) {
    uint32_t avg = 0;
    uint32_t valid = snap.totalCount - snap.invalidCount;

    if (valid > 0) {
        avg = (uint32_t)(snap.sumMm / valid);
    }

    Serial.println("=== HC-SR04 测距统计 ===");
    Serial.printf("总数 %u  有效 %u  无效 %u  (未收到回波 %u / 未结束 %u)\n",
                  snap.totalCount, valid, snap.invalidCount,
                  snap.timeoutRiseCount, snap.timeoutFallCount);
    Serial.printf("最小 %u mm  最大 %u mm  平均 %u mm  可用堆 %u KB\n",
                  snap.minMm, snap.maxMm, avg,
                  (uint32_t)(ESP.getFreeHeap() / 1024));
}

// 串口上报任务：队列来一条打印一条，另外每秒补一条
static void reportTask(void *pvParameters) {
    uint32_t lastPrintMs = 0;
    uint32_t lastStatMs = 0;
    uint8_t flag = 0x00;
    uint32_t distanceMm = 0;
    DistanceReport snap;

    for (;;) {
        uint32_t now = millis();

        // 队列里来一条就打印一条
        if (xQueueReceive(DistanceValueQueue, &distanceMm, pdMS_TO_TICKS(10)) == pdPASS) {
            snap = {};
            readDistanceReport(snap);
            printDistance(snap);
            lastPrintMs = now;
        }
        if (xQueueReceive(GUIFlagValueQueue, &flag, 0) == pdPASS) {
            // 本测试没有屏幕，标志位先收下不用
        }

        // 兜底：一直没新数据也每秒打一条，方便看接线死活
        if (now - lastPrintMs >= 1000UL) {
            lastPrintMs = now;
            snap = {};
            readDistanceReport(snap);
            printDistance(snap);
        }
        if (now - lastStatMs >= 10000UL) {
            lastStatMs = now;
            snap = {};
            readDistanceReport(snap);
            printStatistics(snap);
        }

        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

// HC-SR04 测距任务
static void ultrasonicTask(void *pvParameters) {
    static HCSR04 *sensor = &hcsr04;
    uint32_t lastMeasureMs = 0;
    uint8_t measureFlag = 0x00;
    uint32_t distanceMm = 0;
    uint8_t errorCode = 0;
    bool valid = false;

    for (;;) {
        // 按周期发起测量，前一次没结束就下一轮再试
        uint32_t now = millis();
        if (now - lastMeasureMs >= US_INTERVAL_MS) {
            if (sensor->startMeasure()) {
                lastMeasureMs = now;
                measureFlag = MEASURE;
            }
        }
        if (xQueueReceive(GUIFlagValueQueue, &measureFlag, 0) == pdPASS) {
            // 保留：以后可以由按钮/串口触发单次测量
        }

        // 推进状态机，这里不阻塞
        sensor->update();

        if (sensor->isReady()) {
            distanceMm = sensor->getDistanceMm();
            errorCode = sensor->getErrorCode();
            valid = sensor->isValid();

            if (valid && (distanceMm < US_MIN_MM || distanceMm > US_MAX_MM)) {
                valid = false;
                errorCode = 0;
            }

            // 写共享统计，快照方式给别的任务读
            if (distanceMutex != nullptr &&
                xSemaphoreTake(distanceMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
                distanceReport.distanceMm = distanceMm;
                distanceReport.pulseUs = sensor->getPulseUs();
                distanceReport.errorCode = errorCode;
                distanceReport.totalCount++;

                if (errorCode == 1) {
                    distanceReport.timeoutRiseCount++;
                } else if (errorCode == 2) {
                    distanceReport.timeoutFallCount++;
                }

                if (!valid) {
                    distanceReport.invalidCount++;
                } else {
                    distanceReport.sumMm += distanceMm;
                    if (distanceReport.minMm == 0 || distanceMm < distanceReport.minMm) {
                        distanceReport.minMm = distanceMm;
                    }
                    if (distanceMm > distanceReport.maxMm) {
                        distanceReport.maxMm = distanceMm;
                    }
                }
                xSemaphoreGive(distanceMutex);
            }

            MyQueueSend(GUIREFRESH, distanceMm);
        }

        vTaskDelay(pdMS_TO_TICKS(1));
    }
}

void setup() {
    //创建队列
    ButtonValueQueue = xQueueCreate(US_DISTANCE_QUEUE_SIZE, sizeof(uint8_t));
    GUIFlagValueQueue = xQueueCreate(US_DISTANCE_QUEUE_SIZE, sizeof(uint8_t));
    DistanceValueQueue = xQueueCreate(US_DISTANCE_QUEUE_SIZE, sizeof(uint32_t));

    //开启串口
    Serial.begin(SERIAL_BAUD);
    delay(200);//等待串口稳定

    Serial.println();
    Serial.println("=== HC-SR04 非阻塞测试 (Wave_Test) ===");
    Serial.printf("TRIG=GPIO%d  ECHO=GPIO%d  超时=%luus  周期=%ums\n",
                  US_TRIG_PIN, US_ECHO_PIN,
                  (unsigned long)US_TIMEOUT_US, US_INTERVAL_MS);
    Serial.printf("有效范围 %umm ~ %umm，ECHO 需分压到 3.3V\n", US_MIN_MM, US_MAX_MM);

    distanceMutex = xSemaphoreCreateMutex();

    xTaskCreatePinnedToCore(ultrasonicTask, "ultrasonic", US_TASK_STACK,
                            nullptr, US_TASK_PRIORITY, nullptr, 1);
    xTaskCreatePinnedToCore(reportTask, "report", REPORT_TASK_STACK,
                            nullptr, REPORT_TASK_PRIORITY, nullptr, 0);
}

void loop() {
    vTaskDelay(pdMS_TO_TICKS(1000));
}
