#include <Arduino.h>
#include <TFT_eSPI.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>

//状态（测试项标志，FlagJudgeTask / 串口任务 -> TFT 测试任务）
#define TEST_COLORBAR 0x01 //8 条彩带
#define TEST_GRADIENT 0x02 //灰阶 + RGB 渐变
#define TEST_GRAPHIC  0x03 //基本图形
#define TEST_TEXT     0x04 //字体与排版
#define TEST_GRID     0x05 //网格标尺
#define TEST_SPRITE   0x06 //Sprite 画布（PSRAM）
#define TEST_SPEED    0x07 //刷屏速度测试
#define TEST_SYSINFO  0x08 //系统信息页（每秒刷新）
#define TEST_CLEAR    0x09 //清屏
#define TEST_COUNT    9

//状态（按键标志，ButtonTask -> FlagJudgeTask）
#define BUTTON1_OPENFLAG  0x01 //按钮 1 按下：上一个测试项
#define BUTTON2_OPENFLAG  0x02 //按钮 2 按下：下一个测试项
#define BUTTON3_OPENFLAG  0x03 //按钮 3 按下：重跑当前测试项
#define BUTTON1_CLOSEFLAG 0x04 //按钮 1 松开
#define BUTTON2_CLOSEFLAG 0x05 //按钮 2 松开
#define BUTTON3_CLOSEFLAG 0x06 //按钮 3 松开

//屏幕尺寸和标题栏
#define SCR_W TFT_WIDTH
#define SCR_H TFT_HEIGHT
#define TITLE_H 20 //标题栏高度

//------------------------------------ TFT_eSPI 对象 ------------------------------------//
TFT_eSPI tft;
static TFT_eSprite sprite(&tft);

//------------------------------------ 系统信息结构体 ------------------------------------//
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

// 全局系统信息结构体和互斥锁
static SystemInfo systemInfo = {};
static SemaphoreHandle_t infoMutex = nullptr;

//------------------------------------ 全局队列句柄 ------------------------------------//
QueueHandle_t ButtonValueQueue = nullptr;   //按键值队列
QueueHandle_t ToTFTTestFlagQueue = nullptr; //指向 TFT 测试任务的测试项队列

//------------------------------------ 全局状态 ------------------------------------//
static uint8_t currentTestFlag = TEST_COLORBAR; //当前测试项，供系统信息页和串口用
static uint32_t lastFillScreenUs = 0;           //最近一次全屏填充耗时（us）
static uint32_t lastSpriteFps = 0;              //最近一次 Sprite 推屏帧率

//测试项表：下标 -> 标志，下标 -> 名字
static const uint8_t TEST_TABLE[TEST_COUNT] = {
    TEST_COLORBAR, TEST_GRADIENT, TEST_GRAPHIC, TEST_TEXT, TEST_GRID,
    TEST_SPRITE, TEST_SPEED, TEST_SYSINFO, TEST_CLEAR
};

static const char *TEST_NAME[TEST_COUNT] = {
    "COLOR BAR", "GRADIENT", "GRAPHIC", "TEXT", "GRID",
    "SPRITE", "SPEED", "SYSINFO", "CLEAR"
};

//彩带颜色表
static const uint16_t COLOR_TABLE[8] = {
    TFT_RED, TFT_GREEN, TFT_BLUE, TFT_YELLOW,
    TFT_CYAN, TFT_MAGENTA, TFT_WHITE, TFT_BLACK
};

static const char *COLOR_NAME[8] = {
    "RED", "GREEN", "BLUE", "YELLOW", "CYAN", "MAGENTA", "WHITE", "BLACK"
};

//------------------------------------ 通用小函数 ------------------------------------//
// 背光开关（TFT_BL < 0 表示背光直接接 VCC，代码不控制）
static void backlightWrite(bool on) {
#if TFT_BL >= 0
    pinMode(TFT_BL, OUTPUT);
    digitalWrite(TFT_BL, on ? TFT_BACKLIGHT_ON : !TFT_BACKLIGHT_ON);
#else
    (void)on;
#endif
}

// 百分比计算，用 uint64_t 防溢出
static uint32_t percentOf(uint32_t used, uint32_t total) {
    if (total == 0) {
        return 0;
    }
    return (uint32_t)(((uint64_t)used * 100U) / total);
}

// 测试项标志 -> 下标
static int8_t testIndexOf(uint8_t testFlag) {
    for (uint8_t i = 0; i < TEST_COUNT; i++) {
        if (TEST_TABLE[i] == testFlag) {
            return (int8_t)i;
        }
    }
    return 0;
}

// 测试项标志 -> 名字
static const char *testNameOf(uint8_t testFlag) {
    return TEST_NAME[(uint8_t)testIndexOf(testFlag)];
}

// 收集系统信息
static void collectSystemInfo(SystemInfo &out) {
    uint32_t heapTotal = ESP.getHeapSize();
    uint32_t heapFree = ESP.getFreeHeap();
    uint32_t psramTotal = 0;
    uint32_t psramFree = 0;

    out.psramFound = psramFound();
    if (out.psramFound) {
        psramTotal = ESP.getPsramSize();
        psramFree = ESP.getFreePsram();
    }

    uint32_t sketchUsed = ESP.getSketchSize();
    uint32_t sketchFree = ESP.getFreeSketchSpace();

    out.heapTotalKb = heapTotal / 1024;
    out.heapFreeKb = heapFree / 1024;
    out.ramUsedKb = (heapTotal - heapFree) / 1024;
    out.ramUsedPct = percentOf(heapTotal - heapFree, heapTotal);

    out.psramTotalKb = psramTotal / 1024;
    out.psramFreeKb = psramFree / 1024;
    out.psramUsedKb = (psramTotal - psramFree) / 1024;
    out.psramUsedPct = percentOf(psramTotal - psramFree, psramTotal);

    out.flashUsedKb = sketchUsed / 1024;
    out.flashTotalKb = (sketchUsed + sketchFree) / 1024;
    out.flashUsedPct = percentOf(sketchUsed, sketchUsed + sketchFree);

    out.uptimeSeconds = millis() / 1000UL;
}

// 打印系统信息到串口
static void printSystemInfo(const SystemInfo &info) {
    Serial.println("=== ESP32-S3 N16R8 系统信息 ===");
    Serial.printf("RAM: %u/%u KB (%u%%)\n",
                  info.ramUsedKb, info.heapTotalKb, info.ramUsedPct);

    if (info.psramFound) {
        Serial.printf("PSRAM: %u/%u KB (%u%%)\n",
                      info.psramUsedKb, info.psramTotalKb, info.psramUsedPct);
    } else {
        Serial.println("PSRAM: 未找到");
    }

    Serial.printf("Flash: %u/%u KB (%u%%)\n",
                  info.flashUsedKb, info.flashTotalKb, info.flashUsedPct);
    Serial.printf("当前测试项: %s\n", testNameOf(currentTestFlag));
}

// 打印芯片、引脚、串口命令
static void printHeader() {
    Serial.println("=====================================");
    Serial.println(" SmartBookShelf / TFT_Test");
    Serial.println("=====================================");
    Serial.printf("chip       : %s  cores=%d  freq=%u MHz\n",
                  ESP.getChipModel(), ESP.getChipCores(),
                  (unsigned)ESP.getCpuFreqMHz());
    Serial.printf("flash      : %u bytes (%u MB)\n",
                  (unsigned)ESP.getFlashChipSize(),
                  (unsigned)(ESP.getFlashChipSize() / (1024U * 1024U)));
    Serial.printf("psram found: %s  size=%u bytes\n",
                  psramFound() ? "YES" : "NO", (unsigned)ESP.getPsramSize());
    Serial.printf("tft pins   : MOSI=%d SCLK=%d CS=%d DC=%d RST=%d BL=%d\n",
                  TFT_MOSI, TFT_SCLK, TFT_CS, TFT_DC, TFT_RST, TFT_BL);
    Serial.printf("buttons    : BTN1=%d BTN2=%d BTN3=%d (按下为高电平)\n",
                  BUTTON_PIN1, BUTTON_PIN2, BUTTON_PIN3);
}

// 打印串口命令帮助
static void printHelp() {
    Serial.println("-------------------------------------");
    Serial.println(" 串口命令:");
    Serial.println("   n / p : 下一个 / 上一个测试项");
    Serial.println("   r     : 重跑当前测试项");
    Serial.println("   1-9   : 直接选测试项");
    Serial.println("   h     : 再打印一遍本帮助");
    Serial.println(" 测试项: 1 彩带 2 渐变 3 图形 4 文字 5 网格");
    Serial.println("         6 Sprite 7 速度 8 系统信息 9 清屏");
    Serial.println(" 按键(可选): BTN1(GPIO42) 上一个, BTN2(GPIO41) 下一个,");
    Serial.println("            BTN3(GPIO40) 重跑");
    Serial.println(" 屏上文字只能是英文：TFT_eSPI 内置字体不含中文");
    Serial.println("=====================================");
}

//------------------------------------ 测试图案 ------------------------------------//
// 画页面标题栏（会先清屏）
static void drawTitle(uint8_t testFlag) {
    char title[48];

    snprintf(title, sizeof(title), "TFT TEST %u/%u  %s",
             (unsigned)(testIndexOf(testFlag) + 1), (unsigned)TEST_COUNT,
             testNameOf(testFlag));

    tft.fillScreen(TFT_BLACK);
    tft.fillRect(0, 0, SCR_W, TITLE_H, TFT_NAVY);
    tft.setTextDatum(TL_DATUM);
    tft.setTextFont(2);
    tft.setTextColor(TFT_WHITE, TFT_NAVY);
    tft.drawString(title, 4, 2);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);

    Serial.printf("[TFT] %s\n", title);
}

// 画一条百分比进度条
static void drawBar(int16_t x, int16_t y, int16_t w, int16_t h,
                    uint32_t pct, uint16_t color) {
    int16_t fillW;

    if (pct > 100U) {
        pct = 100U;
    }
    fillW = (int16_t)(((uint32_t)(w - 4) * pct) / 100U);

    tft.drawRect(x, y, w, h, TFT_DARKGREY);
    tft.fillRect(x + 2, y + 2, fillW, h - 4, color);
}

// 8 条彩带
static void drawColorBars() {
    int16_t y0 = TITLE_H + 2;
    int16_t barH = (SCR_H - y0) / 8;

    drawTitle(TEST_COLORBAR);

    for (uint8_t i = 0; i < 8; i++) {
        int16_t y = y0 + i * barH;
        int16_t h = (i == 7) ? (int16_t)(SCR_H - y) : barH;

        tft.fillRect(0, y, SCR_W, h, COLOR_TABLE[i]);
        // 黑条上用白字，其余用黑字
        tft.setTextColor((COLOR_TABLE[i] == TFT_BLACK) ? TFT_WHITE : TFT_BLACK);
        tft.setTextFont(2);
        tft.setTextDatum(ML_DATUM);
        tft.drawString(COLOR_NAME[i], 6, y + h / 2);
    }

    tft.setTextDatum(TL_DATUM);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
}

// 灰阶 + RGB 渐变
static void drawGradient() {
    int16_t y0 = TITLE_H + 2;
    int16_t grayH = (SCR_H - y0) / 2;

    drawTitle(TEST_GRADIENT);

    // 上半：黑白灰阶，看有没有色阶断层
    for (int16_t y = 0; y < grayH; y++) {
        uint8_t g = (uint8_t)(((uint32_t)y * 255U) / (uint32_t)(grayH - 1));
        tft.drawFastHLine(0, y0 + y, SCR_W, tft.color565(g, g, g));
    }

    // 下半：RGB 渐变
    for (int16_t x = 0; x < SCR_W; x++) {
        uint8_t r = (uint8_t)(((uint32_t)x * 255U) / (uint32_t)(SCR_W - 1));
        tft.drawFastVLine(x, y0 + grayH, SCR_H - y0 - grayH,
                          tft.color565(r, 40, (uint8_t)(255 - r)));
    }
}

// 基本图形
static void drawGraphic() {
    drawTitle(TEST_GRAPHIC);

    tft.drawRect(4, 26, 110, 56, TFT_RED);
    tft.fillRect(124, 26, 112, 56, TFT_DARKGREEN);
    tft.drawRoundRect(4, 88, 110, 56, 8, TFT_CYAN);
    tft.fillRoundRect(124, 88, 112, 56, 8, TFT_ORANGE);
    tft.drawCircle(60, 188, 34, TFT_YELLOW);
    tft.fillCircle(180, 188, 34, TFT_PURPLE);
    tft.drawTriangle(10, 240, 110, 240, 60, 300, TFT_GREEN);
    tft.fillTriangle(130, 240, 230, 240, 180, 300, TFT_BLUE);
    // 对角斜线：看有没有断线、错位
    tft.drawLine(4, 308, 236, 314, TFT_WHITE);
}

// 字体与排版
static void drawTextDemo() {
    drawTitle(TEST_TEXT);
    tft.setTextDatum(TL_DATUM);

    tft.setTextFont(1);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.drawString("font 1 : 8px  0123456789", 4, 26);

    tft.setTextFont(2);
    tft.drawString("font 2 : 16px ABCDEFGHIJ", 4, 40);

    tft.setTextFont(4);
    tft.drawString("font 4 : 26px", 4, 62);

    tft.setTextFont(6);
    tft.setTextColor(TFT_GREEN, TFT_BLACK);
    tft.drawString("ABC 123", 4, 96);

    tft.setTextFont(2);
    tft.setTextColor(TFT_YELLOW, TFT_BLACK);
    tft.drawString("colour: R=red  G=green  B=blue", 4, 160);

    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(TFT_CYAN, TFT_BLACK);
    tft.drawString("MC_DATUM middle centre", SCR_W / 2, 200);

    tft.setTextDatum(TR_DATUM);
    tft.setTextColor(TFT_MAGENTA, TFT_BLACK);
    tft.drawString("TR_DATUM", SCR_W - 4, 224);

    tft.setTextDatum(BL_DATUM);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.drawString("BL_DATUM", 4, SCR_H - 4);

    tft.setTextDatum(BR_DATUM);
    tft.drawString("BR_DATUM", SCR_W - 4, SCR_H - 4);
    tft.setTextDatum(TL_DATUM);
}

// 网格标尺
static void drawGridRuler() {
    int16_t y0 = TITLE_H + 2;

    drawTitle(TEST_GRID);

    for (int16_t x = 0; x < SCR_W; x += 20) {
        tft.drawFastVLine(x, y0, SCR_H - y0,
                          ((x % 100) == 0) ? TFT_RED : TFT_DARKGREY);
    }
    for (int16_t y = y0; y < SCR_H; y += 20) {
        tft.drawFastHLine(0, y, SCR_W,
                          (((y - y0) % 100) == 0) ? TFT_RED : TFT_DARKGREY);
    }

    // 四角标记 + 边框：判断画面有没有整体偏移或被裁掉
    tft.fillRect(0, y0, 12, 12, TFT_GREEN);
    tft.fillRect(SCR_W - 12, y0, 12, 12, TFT_GREEN);
    tft.fillRect(0, SCR_H - 12, 12, 12, TFT_GREEN);
    tft.fillRect(SCR_W - 12, SCR_H - 12, 12, 12, TFT_GREEN);
    tft.drawRect(0, y0, SCR_W, SCR_H - y0, TFT_WHITE);

    tft.setTextFont(2);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.setTextDatum(TL_DATUM);
    tft.drawString("0", 4, y0 + 16);
    tft.drawString("120", SCR_W / 2 - 12, y0 + 16);
    tft.drawString("240x320 grid 20px", 4, SCR_H - 40);
}

// Sprite 画布：先画在 PSRAM 画布上，再一次性推屏
static void drawSpriteDemo() {
    int16_t w = 0;
    int16_t h = 0;

    drawTitle(TEST_SPRITE);

    if (sprite.getPointer() == nullptr) {
        tft.setTextFont(2);
        tft.setTextColor(TFT_RED, TFT_BLACK);
        tft.drawString("sprite alloc FAILED", 4, 40);
        Serial.println("[TFT] sprite 申请失败");
        return;
    }

    w = sprite.width();
    h = sprite.height();

    sprite.fillSprite(TFT_BLACK);

    // 画布内：竖直灰阶
    for (int16_t y = 0; y < h; y++) {
        uint8_t g = (uint8_t)(((uint32_t)y * 255U) / (uint32_t)(h - 1));
        sprite.drawFastHLine(0, y, w, sprite.color565(g, g, g));
    }

    // 画布内：几个基本图形
    sprite.fillRect(10, (int16_t)(h / 6), 60, 60, TFT_RED);
    sprite.fillCircle(w / 4, (int16_t)(h / 2), 36, TFT_BLUE);
    sprite.drawRect((int16_t)(w / 2), (int16_t)(h / 6),
                    (int16_t)(w / 2 - 10), 60, TFT_GREEN);

    // 画布内：文字
    sprite.setTextDatum(MC_DATUM);
    sprite.setTextFont(4);
    sprite.setTextColor(TFT_YELLOW, TFT_BLACK);
    sprite.drawString("SPRITE", w / 2, (int16_t)(h * 3 / 4));
    sprite.setTextFont(2);
    sprite.setTextColor(TFT_WHITE, TFT_BLACK);
    sprite.drawString("16bpp canvas", w / 2, (int16_t)(h * 3 / 4 + 24));
    sprite.setTextDatum(TL_DATUM);

    // 一次性推屏
    sprite.pushSprite(0, 0);
    Serial.printf("[TFT] sprite: %dx%d %dbpp push done\n",
                  w, h, (int)sprite.getColorDepth());
}

// 刷屏速度测试
static void runSpeedTest() {
    char buf[48];
    uint32_t t0 = 0;
    uint32_t us = 0;
    uint32_t fps = 0;
    int16_t spriteW = 0;
    int16_t spriteH = 0;

    drawTitle(TEST_SPEED);

    // 1) 全屏纯色填充 10 次取平均
    t0 = micros();
    for (uint8_t i = 0; i < 10; i++) {
        tft.fillScreen(COLOR_TABLE[i & 0x07]);
    }
    us = micros() - t0;
    lastFillScreenUs = us / 10U;

    // 2) Sprite 整屏推屏 20 帧
    if (sprite.getPointer() != nullptr) {
        spriteW = sprite.width();
        spriteH = sprite.height();
        sprite.fillSprite(TFT_BLACK);
        t0 = micros();
        for (uint8_t i = 0; i < 20; i++) {
            sprite.fillSprite(COLOR_TABLE[i & 0x07]);
            sprite.pushSprite(0, 0);
        }
        us = micros() - t0;
        if (us > 0) {
            fps = (uint32_t)(((uint64_t)20U * 1000000ULL) / us);
        }
        lastSpriteFps = fps;
    }

    // 前面的填充把标题盖掉了，这里重画一遍再写结果
    drawTitle(TEST_SPEED);
    tft.setTextDatum(TL_DATUM);
    tft.setTextFont(2);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);

    snprintf(buf, sizeof(buf), "fillScreen %dx%d", SCR_W, SCR_H);
    tft.drawString(buf, 4, 34);
    snprintf(buf, sizeof(buf), "  %u us  (%u ms)",
             (unsigned)lastFillScreenUs, (unsigned)(lastFillScreenUs / 1000U));
    tft.drawString(buf, 4, 56);

    snprintf(buf, sizeof(buf), "sprite %dx%d 16bpp", spriteW, spriteH);
    tft.drawString(buf, 4, 90);
    snprintf(buf, sizeof(buf), "  %u fps", (unsigned)lastSpriteFps);
    tft.drawString(buf, 4, 112);

    tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
    tft.drawString("27 MHz SPI, 240x320, rotation 2", 4, 150);

    Serial.printf("[TFT] speed: fillScreen %u us, sprite %dx%d %u fps\n",
                  (unsigned)lastFillScreenUs, spriteW, spriteH,
                  (unsigned)lastSpriteFps);
}

// 刷新系统信息页正文（不动标题栏，每秒调一次）
static void updateSysInfoBody() {
    char buf[48];
    SystemInfo snap;

    if (infoMutex == nullptr ||
        xSemaphoreTake(infoMutex, pdMS_TO_TICKS(50)) != pdTRUE) {
        return;
    }
    snap = systemInfo;
    xSemaphoreGive(infoMutex);

    tft.fillRect(0, TITLE_H + 2, SCR_W, SCR_H - TITLE_H - 2, TFT_BLACK);
    tft.setTextFont(2);
    tft.setTextDatum(TL_DATUM);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);

    snprintf(buf, sizeof(buf), "RAM  : %u/%u KB (%u%%)",
             snap.ramUsedKb, snap.heapTotalKb, snap.ramUsedPct);
    tft.drawString(buf, 4, 28);
    drawBar(4, 46, SCR_W - 8, 8, snap.ramUsedPct, TFT_GREEN);

    if (snap.psramFound) {
        snprintf(buf, sizeof(buf), "PSRAM: %u/%u KB (%u%%)",
                 snap.psramUsedKb, snap.psramTotalKb, snap.psramUsedPct);
    } else {
        snprintf(buf, sizeof(buf), "PSRAM: N/A");
    }
    tft.drawString(buf, 4, 62);
    drawBar(4, 80, SCR_W - 8, 8, snap.psramUsedPct, TFT_CYAN);

    snprintf(buf, sizeof(buf), "FLASH: %u/%u KB (%u%%)",
             snap.flashUsedKb, snap.flashTotalKb, snap.flashUsedPct);
    tft.drawString(buf, 4, 96);
    drawBar(4, 114, SCR_W - 8, 8, snap.flashUsedPct, TFT_ORANGE);

    snprintf(buf, sizeof(buf), "UPTIME: %02u:%02u:%02u",
             (unsigned)(snap.uptimeSeconds / 3600U),
             (unsigned)((snap.uptimeSeconds / 60U) % 60U),
             (unsigned)(snap.uptimeSeconds % 60U));
    tft.drawString(buf, 4, 134);

    snprintf(buf, sizeof(buf), "TEST : %u/%u %s",
             (unsigned)(testIndexOf(currentTestFlag) + 1), (unsigned)TEST_COUNT,
             testNameOf(currentTestFlag));
    tft.drawString(buf, 4, 158);

    snprintf(buf, sizeof(buf), "FILL : %u us", (unsigned)lastFillScreenUs);
    tft.drawString(buf, 4, 182);

    snprintf(buf, sizeof(buf), "SPRITE: %u fps", (unsigned)lastSpriteFps);
    tft.drawString(buf, 4, 206);

    tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
    tft.drawString("PINS : MOSI=11 SCLK=12 CS=10", 4, 238);
    tft.drawString("       DC=9 RST=8 BL=47", 4, 258);
    tft.drawString("CMD  : n/p/r or 1-9", 4, 286);
}

// 系统信息页（带标题栏）
static void drawSysInfoPage() {
    drawTitle(TEST_SYSINFO);
    updateSysInfoBody();
}

// 清屏
static void drawClearPage() {
    tft.fillScreen(TFT_BLACK);
    Serial.println("[TFT] 清屏");

    tft.setTextDatum(MC_DATUM);
    tft.setTextFont(2);
    tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
    tft.drawString("screen cleared", SCR_W / 2, SCR_H / 2);
    tft.setTextDatum(TL_DATUM);
}

// 按测试项标志画对应页面
static void drawTestPage(uint8_t testFlag) {
    switch (testFlag) {
        case TEST_COLORBAR:
            drawColorBars();
            break;
        case TEST_GRADIENT:
            drawGradient();
            break;
        case TEST_GRAPHIC:
            drawGraphic();
            break;
        case TEST_TEXT:
            drawTextDemo();
            break;
        case TEST_GRID:
            drawGridRuler();
            break;
        case TEST_SPRITE:
            drawSpriteDemo();
            break;
        case TEST_SPEED:
            runSpeedTest();
            break;
        case TEST_SYSINFO:
            drawSysInfoPage();
            break;
        case TEST_CLEAR:
            drawClearPage();
            break;
        default:
            break;
    }
}

//------------------------------------ 队列收发 ------------------------------------//
// 统一往 TFT 测试任务发测试项
static void MyQueueSend(uint8_t testFlag) {
    xQueueSend(ToTFTTestFlagQueue, &testFlag, pdMS_TO_TICKS(50));
}

//------------------------------------ 任务 ------------------------------------//
// TFT 测试任务：独占屏幕，收到测试项标志就画对应页面
static void tftTestTask(void *pvParameters) {
    uint8_t testFlag = 0x00;
    uint32_t lastInfoRefresh = 0;

    for (;;) {
        if (xQueueReceive(ToTFTTestFlagQueue, &testFlag,
                          pdMS_TO_TICKS(10)) == pdPASS) {
            currentTestFlag = testFlag;
            drawTestPage(testFlag);
            lastInfoRefresh = millis();
        }

        // 系统信息页每秒刷新一次，短延时，不阻塞
        if (currentTestFlag == TEST_SYSINFO &&
            (millis() - lastInfoRefresh) >= 1000) {
            lastInfoRefresh = millis();
            updateSysInfoBody();
        }

        vTaskDelay(pdMS_TO_TICKS(5));
    }
}

// 按键任务：移位历史消抖，按下发标志
static void ButtonTask(void *pvParameters) {
    uint8_t button_flag = 0x00;
    uint8_t currentButtonState1 = 0x00;
    uint8_t currentButtonState2 = 0x00;
    uint8_t currentButtonState3 = 0x00;
    uint8_t lastButtonState1 = 0x00;
    uint8_t lastButtonState2 = 0x00;
    uint8_t lastButtonState3 = 0x00;

    pinMode(BUTTON_PIN1, INPUT_PULLDOWN);
    pinMode(BUTTON_PIN2, INPUT_PULLDOWN);
    pinMode(BUTTON_PIN3, INPUT_PULLDOWN);

    for (;;) {
        // 读取按钮状态
        currentButtonState1 = (uint8_t)digitalRead(BUTTON_PIN1);
        currentButtonState2 = (uint8_t)digitalRead(BUTTON_PIN2);
        currentButtonState3 = (uint8_t)digitalRead(BUTTON_PIN3);

        // 更新按钮状态历史
        lastButtonState1 <<= 1;
        lastButtonState1 |= (currentButtonState1 == HIGH) ? 0x01 : 0x00;
        lastButtonState2 <<= 1;
        lastButtonState2 |= (currentButtonState2 == HIGH) ? 0x01 : 0x00;
        lastButtonState3 <<= 1;
        lastButtonState3 |= (currentButtonState3 == HIGH) ? 0x01 : 0x00;

        // 连续 4 次读到高电平才算按下
        if ((lastButtonState1 & 0x0F) == 0x0F) {
            if (button_flag != BUTTON1_OPENFLAG) {
                button_flag = BUTTON1_OPENFLAG;
                xQueueSend(ButtonValueQueue, &button_flag, 0);
            }
        } else if ((lastButtonState2 & 0x0F) == 0x0F) {
            if (button_flag != BUTTON2_OPENFLAG) {
                button_flag = BUTTON2_OPENFLAG;
                xQueueSend(ButtonValueQueue, &button_flag, 0);
            }
        } else if ((lastButtonState3 & 0x0F) == 0x0F) {
            if (button_flag != BUTTON3_OPENFLAG) {
                button_flag = BUTTON3_OPENFLAG;
                xQueueSend(ButtonValueQueue, &button_flag, 0);
            }
        }
        // 连续 4 次读到低电平算松开（只记状态，不发队列）
        else if ((lastButtonState1 & 0x0F) == 0x00) {
            button_flag = BUTTON1_CLOSEFLAG;
        } else if ((lastButtonState2 & 0x0F) == 0x00) {
            button_flag = BUTTON2_CLOSEFLAG;
        } else if ((lastButtonState3 & 0x0F) == 0x00) {
            button_flag = BUTTON3_CLOSEFLAG;
        }

        vTaskDelay(pdMS_TO_TICKS(5));
    }
}

// 逻辑判断任务：按键标志 -> 具体测试项
static void FlagJudgeTask(void *pvParameters) {
    uint8_t button_flag = 0x00;
    uint8_t index = 0;

    for (;;) {
        if (xQueueReceive(ButtonValueQueue, &button_flag,
                          portMAX_DELAY) == pdTRUE) {
            // 和屏上当前页保持同步（串口可以直接跳页）
            index = (uint8_t)testIndexOf(currentTestFlag);

            switch (button_flag) {
                case BUTTON1_OPENFLAG:
                    Serial.println("按钮 1 按下：上一个测试项");
                    index = (uint8_t)((index + TEST_COUNT - 1) % TEST_COUNT);
                    MyQueueSend(TEST_TABLE[index]);
                    break;
                case BUTTON2_OPENFLAG:
                    Serial.println("按钮 2 按下：下一个测试项");
                    index = (uint8_t)((index + 1) % TEST_COUNT);
                    MyQueueSend(TEST_TABLE[index]);
                    break;
                case BUTTON3_OPENFLAG:
                    Serial.println("按钮 3 按下：重跑当前测试项");
                    MyQueueSend(TEST_TABLE[index]);
                    break;
                default:
                    break;
            }

            vTaskDelay(pdMS_TO_TICKS(50)); //简单防抖
        }
    }
}

// 串口命令任务：n/p/r 走按键队列复用翻页逻辑，数字 1-9 直接选测试项
static void serialCmdTask(void *pvParameters) {
    uint8_t button_flag = 0x00;
    uint8_t testFlag = 0x00;

    for (;;) {
        while (Serial.available() > 0) {
            char ch = (char)Serial.read();

            if (ch == 'n' || ch == 'N') {
                button_flag = BUTTON2_OPENFLAG;
                xQueueSend(ButtonValueQueue, &button_flag, pdMS_TO_TICKS(50));
            } else if (ch == 'p' || ch == 'P') {
                button_flag = BUTTON1_OPENFLAG;
                xQueueSend(ButtonValueQueue, &button_flag, pdMS_TO_TICKS(50));
            } else if (ch == 'r' || ch == 'R') {
                button_flag = BUTTON3_OPENFLAG;
                xQueueSend(ButtonValueQueue, &button_flag, pdMS_TO_TICKS(50));
            } else if (ch >= '1' && ch <= '9') {
                uint8_t idx = (uint8_t)(ch - '1');
                if (idx < TEST_COUNT) {
                    testFlag = TEST_TABLE[idx];
                    MyQueueSend(testFlag);
                }
            } else if (ch == 'h' || ch == 'H' || ch == '?') {
                printHelp();
            }
        }

        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

// 系统信息采集任务
static void systemInfoTask(void *pvParameters) {
    for (;;) {
        SystemInfo snap;
        collectSystemInfo(snap);

        if (xSemaphoreTake(infoMutex, portMAX_DELAY) == pdTRUE) {
            systemInfo = snap;
            xSemaphoreGive(infoMutex);
        }

        printSystemInfo(snap);
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

//------------------------------------ setup / loop ------------------------------------//
void setup() {
    //创建队列
    ButtonValueQueue = xQueueCreate(BUTTON_QUEUE_SIZE, sizeof(uint8_t));
    ToTFTTestFlagQueue = xQueueCreate(TESTFLAG_QUEUE_SIZE, sizeof(uint8_t));

    //开启串口
    Serial.begin(115200);
    delay(200); //等待串口稳定

    printHeader();
    printHelp();

    //[1] 背光闪 2 次：能看到闪就说明 MCU 在跑
    Serial.println("[1] backlight blink");
    for (uint8_t i = 0; i < 2; i++) {
        backlightWrite(false);
        delay(150);
        backlightWrite(true);
        delay(150);
    }

    //[2] 初始化屏幕
    Serial.println("[2] tft.init()");
    tft.init();

    //[3] 旋转方向
    Serial.println("[3] tft.setRotation(TFT_ROTATION)");
    tft.setRotation(TFT_ROTATION);
    Serial.printf("TFT_eSPI: width=%d height=%d rotation=%u\n",
                  tft.width(), tft.height(), (unsigned)tft.getRotation());

    //[4] 读 ID：MISO 没接时返回 0 属正常
    Serial.println("[4] readcommand8");
    Serial.printf("readcommand8(RDMADCTL)=%u (0 when MISO is not wired)\n",
                  (unsigned)tft.readcommand8(ILI9341_RDMADCTL));

    //[5] 全屏填充计时
    Serial.println("[5] fillScreen timing");
    {
        uint32_t t0 = micros();
        tft.fillScreen(TFT_BLACK);
        lastFillScreenUs = micros() - t0;
        Serial.printf("fillScreen(%dx%d) took %u us\n", SCR_W, SCR_H,
                      (unsigned)lastFillScreenUs);
    }

    //[6] Sprite 画布：16 位色，显存放在 PSRAM
    Serial.println("[6] sprite create");
    sprite.setColorDepth(16);
    if (sprite.createSprite(SCR_W, SCR_H) == nullptr) {
        Serial.println("sprite: 全屏申请失败，退回 240x60");
        if (sprite.createSprite(SCR_W, 60) == nullptr) {
            Serial.println("sprite: 申请失败，Sprite 测试页会显示错误");
        }
    } else {
        Serial.printf("sprite: %dx%d 16bpp in %s\n", sprite.width(),
                      sprite.height(), psramFound() ? "PSRAM" : "RAM");
    }

    //[7] 系统信息和互斥锁
    infoMutex = xSemaphoreCreateMutex();
    collectSystemInfo(systemInfo);
    printSystemInfo(systemInfo);

    //[8] 先画第一页，再启动任务
    currentTestFlag = TEST_COLORBAR;
    drawTestPage(currentTestFlag);

    xTaskCreatePinnedToCore(systemInfoTask, "sysinfo", 8192, nullptr, 1, nullptr, 0);
    xTaskCreatePinnedToCore(tftTestTask, "tfttest", 8192, nullptr, 2, nullptr, 1);
    xTaskCreatePinnedToCore(ButtonTask, "button", 8192, nullptr, 1, nullptr, 0);
    xTaskCreatePinnedToCore(FlagJudgeTask, "flagjudge", 8192, nullptr, 1, nullptr, 0);
    xTaskCreatePinnedToCore(serialCmdTask, "serialcmd", 4096, nullptr, 1, nullptr, 0);

    Serial.println("ready: n/p/r or 1-9 over serial, or the three buttons");
}

void loop() {
    vTaskDelay(pdMS_TO_TICKS(1000));
}
