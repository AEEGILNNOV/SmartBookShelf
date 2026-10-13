#include "DisplayLib.h"

// 全工程唯一配置来源（刷屏缓冲行数 / 消息停留时间 / 任务栈与核心都在这里）
#include "config.h"

// 开机中文字库自检（缺字会打到串口）
#include "FontCheck.h"

// =============================================================================
// DisplayLib 实现（LVGL 8.4 中文版）
//
// 刷新逻辑分三层：
//   1) 外部任务 setFrame()/setSystemInfo()/… 只写数据并置脏标志；
//   2) displayTask 里 update() 先加锁取一份快照，立刻放锁；
//   3) 拿快照在锁外更新 LVGL 控件（不能占着业务锁），最后 lv_timer_handler()
//      由 displayTask 统一跑，刷屏回调再把画好的区域推给 TFT_eSPI。
// 另外用“内容签名”判断内容是否真的变了，没变就不动控件，省 CPU 也不闪屏。
// =============================================================================

//------------------------------------ LVGL 与屏幕全局量 ------------------------------------//
static TFT_eSPI *s_tft = nullptr;              //刷屏回调用，指向 DisplayLib 里的屏幕对象
static lv_disp_draw_buf_t s_drawBuf;
static lv_disp_drv_t s_dispDrv;
static lv_color_t *s_buf1 = nullptr;
static lv_color_t *s_buf2 = nullptr;

//------------------------------------ 排错统计（只给串口命令 d 看） ------------------------------------//
// 刷屏回调被调了几次、最近一次花了多少微秒。
// 这两个数是排错的关键证据：
//   一直是 0        -> LVGL 根本没在刷屏（tick / 缓冲 / 驱动注册的问题）
//   在涨但屏是黑的  -> 数据已经发出去了，屏自己没反应（背光 / 接线 / 屏型号）
static volatile uint32_t s_flushCount = 0;
static volatile uint32_t s_lastFlushUs = 0;

// SPI 总线测速结果：填屏花的时间(us) 和写了多少像素
// 每像素理论下限 = 16bit / 27MHz ≈ 0.6us；实测只要 > 0.6us/像素 就说明数据真的发出去了。
// 反过来，如果“填满 240x320”只用了几十微秒，那就是 SPI 根本没在吐数据。
static uint32_t s_lastBusTestUs = 0;
static uint32_t s_lastBusTestPx = 0;

// SPI 总线锁：刷屏回调和“别的任务直接写屏”共用，保证同一时刻只有一个写者
static SemaphoreHandle_t s_busMutex = nullptr;

//------------------------------------ 界面控件（只在 displayTask 里碰） ------------------------------------//
static lv_obj_t *s_labelTitle = nullptr;
static lv_obj_t *s_labelPage = nullptr;
static lv_obj_t *s_labelBody = nullptr;
static lv_obj_t *s_barValue = nullptr;
static lv_obj_t *s_labelFooter = nullptr;
static lv_obj_t *s_msgPanel = nullptr;
static lv_obj_t *s_msgLabel = nullptr;

//每块 draw buffer 占几行：240 x 40 x 2B = 19.2KB，两块都在 PSRAM。
//行数由 config.h 的 CFG_LV_BUF_LINES 决定（原 DISPLAY_DRAW_LINES 已合并过去），
//屏上出现横向撕裂/闪烁时把 CFG_LV_BUF_LINES 往下调到 20。

//配色（改配色只改这几行）
#define COLOR_TITLE_BG 0x0088FF
#define COLOR_BODY_BG  0xFFFFFF
#define COLOR_TEXT     0x000000
#define COLOR_HINT     0x606060
#define COLOR_MSG_BG   0x101820

//布局
#define TITLE_H 32

// =============================================================================
// 内存：优先 PSRAM（可用 CFG_LV_BUF_IN_PSRAM 关掉），失败退回内部 RAM
// =============================================================================
static void *displayAlloc(size_t size) {
    void *p = nullptr;
#if CFG_LV_BUF_IN_PSRAM
    if (psramFound()) {
        p = ps_malloc(size);
    }
#endif
    if (p == nullptr) {
        p = malloc(size);  // 关掉 PSRAM 或 PSRAM 申请失败都走这里
    }
    return p;
}

// =============================================================================
// 背光：默认 digitalWrite（和验证过的 TFT_Test 一致）；
// 需要调亮度时在 config.h 里把 CFG_TFT_BL_USE_LEDC 置 1。
// =============================================================================
static void backlightInit() {
#if CFG_TFT_BL_USE_LEDC
    ledcSetup(CFG_TFT_BL_LEDC_CHANNEL, CFG_TFT_BL_LEDC_FREQ, 8);
    ledcAttachPin(PIN_TFT_BL, CFG_TFT_BL_LEDC_CHANNEL);
    ledcWrite(CFG_TFT_BL_LEDC_CHANNEL, CFG_TFT_BL_LEVEL);
#else
    pinMode(PIN_TFT_BL, OUTPUT);
    digitalWrite(PIN_TFT_BL, TFT_BACKLIGHT_ON ? HIGH : LOW);
#endif
}

#if CFG_LV_LOG_TO_SERIAL
// LVGL 的告警/信息转到 Serial（LV_LOG_PRINTF=0，不用 printf 是怕走错串口）
static void lvglLogCb(const char *buf) {
    Serial.print("[LVGL] ");
    Serial.print(buf);
}
#endif

// =============================================================================
// LVGL 刷屏回调：把 LVGL 画好的区域推给 TFT_eSPI
//
// 加总线锁的原因：现在只有 displayTask 会刷屏，这把锁是“零成本保险”；
// 以后要加摄像头预览、或别的任务直接写屏（比如串口命令 d,1 的彩条），
// 两个写者同时动 SPI 就会花屏，所以从第一天就把锁留在这里。
// =============================================================================
static void displayFlushCb(lv_disp_drv_t *drv, const lv_area_t *area,
                           lv_color_t *colorP) {
    TFT_eSPI *tft = (TFT_eSPI *)drv->user_data;
    int32_t w = area->x2 - area->x1 + 1;
    int32_t h = area->y2 - area->y1 + 1;

    // 拿不到锁也要画（宁可花屏也不能整屏不刷），所以不阻塞等待
    bool locked = (s_busMutex != nullptr) &&
                  (xSemaphoreTake(s_busMutex, pdMS_TO_TICKS(200)) == pdTRUE);

    uint32_t t0 = micros();

    tft->startWrite();
    tft->setAddrWindow(area->x1, area->y1, w, h);
    tft->pushColors((uint16_t *)&colorP->full, w * h, CFG_LV_PUSH_SWAP != 0);
    tft->endWrite();

    s_lastFlushUs = micros() - t0;
    s_flushCount++;

    if (locked) {
        xSemaphoreGive(s_busMutex);
    }

    lv_disp_flush_ready(drv);
}

// =============================================================================
// 构造
// =============================================================================
DisplayLib::DisplayLib() {
    memset(&_frame, 0, sizeof(_frame));
    memset(&_info, 0, sizeof(_info));
    memset(&_wave, 0, sizeof(_wave));
    memset(_books, 0, sizeof(_books));
    _bookNum = 0;

    _mutex = nullptr;
    _taskHandle = nullptr;
    _page = PAGE_STATUS;
    _dirty = true;
    memset(_message, 0, sizeof(_message));
    _messageUntil = 0;
    _messageActive = false;
    _lastSig = 0;
    _sigValid = false;
    _testRequest = 0;
    _rawTestHoldMs = CFG_SELFTEST_BAR_HOLD_MS;
}

void DisplayLib::displayTaskEntry(void *pvParameters) {
    DisplayLib *self = (DisplayLib *)pvParameters;
    self->taskLoop();
}

// =============================================================================
// 初始化：屏幕 -> 彩条自检 -> LVGL -> 界面 -> 显示任务
// 排错开关 CFG_DISPLAY_BRINGUP 决定在哪一层停（见 config.h 第 3.1 节）
// =============================================================================
bool DisplayLib::begin() {
    if (_mutex == nullptr) {
        _mutex = xSemaphoreCreateMutex();
    }
    if (s_busMutex == nullptr) {
        s_busMutex = xSemaphoreCreateMutex();
    }
    if (_mutex == nullptr || s_busMutex == nullptr) {
        Serial.println("显示库: 互斥锁创建失败");
        return false;
    }

    // ---- 1) 屏幕 ----
    s_tft = &_tft;
    _tft.init();
    _tft.setRotation(TFT_ROTATION);

    // 清屏顺便做一次“SPI 总线测速”：填满 240x320 到底花了多久。
    // 这是排错最关键的一个数：它不依赖你能不能看见屏，只说明 MCU 有没有把像素吐出去。
    //   正常：约 0.6~3 us/像素（27MHz 下整屏 50~250ms 都算常见）
    //   异常：整屏只用几十 us（< 0.1us/像素）-> SPI 根本没在发数据
    {
        uint32_t us = busWriteTest(true);
        uint32_t px = s_lastBusTestPx ? s_lastBusTestPx : 1;
        uint32_t per100 = us * 100UL / px;  // 每像素耗时 x100，避免用浮点打印
        Serial.printf("显示库: 清屏(填满 %dx%d) 耗时 %luus = %lu.%02luus/像素 (%s)\n",
                      (int)SCR_W, (int)SCR_H, (unsigned long)us,
                      (unsigned long)(per100 / 100UL),
                      (unsigned long)(per100 % 100UL),
                      (us * 10UL >= px) ? "SPI 在发数据" : "❌太快! SPI 没在发数据");
    }

    // 背光：TFT_eSPI 只在部分驱动里自动初始化 TFT_BL，这里显式做一次。
    // 默认 digitalWrite（和验证过的 TFT_Test 一致）；要调亮度在 config.h 里开 LEDC。
    backlightInit();

    // ---- 1.5) 排错模式 1：只画彩条，完全不进 LVGL ----
    // 屏上出现彩条 => 接线/供电/背光/SPI/驱动型号这条链是好的，
    // 那“整屏没内容”就只可能在 LVGL 这一层，和字库无关。
#if CFG_DISPLAY_BRINGUP == 1
    drawRawBars("RAW ONLY");
    Serial.println("显示库: [排错模式1] 只画了 TFT_eSPI 彩条, 没有初始化 LVGL");
    Serial.println("  看到彩条 -> 硬件这条链是好的, 问题在 LVGL/刷屏/字库");
    Serial.println("  看不到   -> 跟 LVGL 和字库都无关, 查背光/接线/供电/屏型号");
    return true;
#endif

    // ---- 1.6) 排错模式 2/3：先画一次彩条，再进 LVGL ----
#if CFG_DISPLAY_BRINGUP >= 2
    drawRawBars("BRINGUP BARS");
    vTaskDelay(pdMS_TO_TICKS(CFG_SELFTEST_BAR_HOLD_MS));
#else
    // 正常固件：CFG_TFT_SPLASH_MS 默认 0，也就是不画（要一眼确认硬件就在 config.h 里调大）
    if (CFG_TFT_SPLASH_MS > 0) {
        drawRawBars("LCD SPLASH");
        vTaskDelay(pdMS_TO_TICKS(CFG_TFT_SPLASH_MS));
    }
#endif

    // ---- 2) LVGL ----
    lv_init();

#if CFG_LV_LOG_TO_SERIAL
    // 打开 LVGL 日志（config.h 的 CFG_LV_LOG_TO_SERIAL）后，它的告警会打到串口
    lv_log_register_print_cb(lvglLogCb);
#endif

    // LVGL 的“时钟”就是刷屏的心跳：它不走，lv_timer_handler() 永远不会去刷屏，
    // 表现就是“串口一切正常、屏上什么都没有”。这里开机就量一次，直接排除这个坑。
    {
        uint32_t tick0 = lv_tick_get();
        delay(20);
        uint32_t tick1 = lv_tick_get();
        Serial.printf("显示库: LVGL 时钟 %lu -> %lu (%s)\n", (unsigned long)tick0,
                      (unsigned long)tick1,
                      (tick1 != tick0) ? "正常" : "❌没走! 屏永远不会刷新");
    }

    size_t pixels = (size_t)SCR_W * CFG_LV_BUF_LINES;
    s_buf1 = (lv_color_t *)displayAlloc(pixels * sizeof(lv_color_t));
    s_buf2 = (lv_color_t *)displayAlloc(pixels * sizeof(lv_color_t));
    if (s_buf1 == nullptr || s_buf2 == nullptr) {
        Serial.println("显示库: LVGL 缓冲区申请失败");
        return false;
    }

    lv_disp_draw_buf_init(&s_drawBuf, s_buf1, s_buf2, (uint32_t)pixels);

    lv_disp_drv_init(&s_dispDrv);
    s_dispDrv.hor_res = SCR_W;
    s_dispDrv.ver_res = SCR_H;
    s_dispDrv.flush_cb = displayFlushCb;
    s_dispDrv.draw_buf = &s_drawBuf;
    s_dispDrv.full_refresh = 0;
    s_dispDrv.user_data = &_tft;
    lv_disp_drv_register(&s_dispDrv);

    // 中文字库自检：默认关（CFG_FONT_SELFCHECK），要用在 config.h 里打开。
    // 它同步跑在 setup 里，字库万一有问题会拖住启动，所以不默认开。
#if CFG_FONT_SELFCHECK
    fontSelfCheck();
#endif

    // ---- 3) 界面 ----
#if CFG_DISPLAY_BRINGUP == 2
    // 排错模式 2：只画纯色块，一个字体都不碰。
    // 色块能出来 -> LVGL 刷屏链路（tick/flush/字节序）是好的，问题回到字体；
    // 色块也出不来 -> 问题在 LVGL 刷屏链路，跟字库一点关系都没有。
    createColorTestUi();
    Serial.println("显示库: [排错模式2] LVGL 纯色块页 (未加载任何字体)");
#else
    createUi();
#endif

    _dirty = true;
    _sigValid = false;

    // 建显示任务：LVGL 只有这一个消费者，所以不需要业务锁
    BaseType_t created = xTaskCreatePinnedToCore(
        displayTaskEntry, "display", CFG_STACK_DISPLAY, this, CFG_PRIO_DISPLAY,
        &_taskHandle, CFG_CORE_DISPLAY);
    if (created != pdPASS) {
        Serial.println("显示库: 显示任务创建失败");
        return false;
    }

    Serial.printf(
        "显示库: ILI9341 %dx%d rotation=%d + LVGL8 已就绪 (缓冲 %uKB x2 %s)\n",
        (int)SCR_W, (int)SCR_H, (int)TFT_ROTATION,
        (unsigned)(pixels * sizeof(lv_color_t) / 1024),
        CFG_LV_BUF_IN_PSRAM ? "PSRAM" : "内部RAM");
    return true;
}

// =============================================================================
// 纯 TFT_eSPI 彩条：完全不经过 LVGL
//
// 这是整个显示排错的“地基测试”——只要它出得来，
// 就说明 屏供电/背光/SPI 引脚/CS-DC-RST/驱动型号 全都是对的。
// =============================================================================
void DisplayLib::drawRawBars(const char *tag) {
    static const uint16_t BAR_COLOR[8] = {TFT_RED,  TFT_GREEN,   TFT_BLUE,
                                          TFT_YELLOW, TFT_CYAN, TFT_MAGENTA,
                                          TFT_WHITE, TFT_BLACK};
    int16_t barW = SCR_W / 8;

    bool locked = (s_busMutex != nullptr) &&
                  (xSemaphoreTake(s_busMutex, pdMS_TO_TICKS(200)) == pdTRUE);

    for (int i = 0; i < 8; i++) {
        _tft.fillRect(i * barW, 0, barW, SCR_H, BAR_COLOR[i]);
    }

    // 中间挖一条黑底，写两行 ASCII（TFT_eSPI 内置字体只有英文，正好够用）
    _tft.fillRect(0, SCR_H / 2 - 24, SCR_W, 48, TFT_BLACK);
    _tft.setTextDatum(MC_DATUM);
    _tft.setTextColor(TFT_WHITE, TFT_BLACK);
    _tft.drawString(tag, SCR_W / 2, SCR_H / 2 - 8, 2);   // 字号 2 = 16px
    _tft.setTextColor(TFT_GREEN, TFT_BLACK);
    _tft.drawString("RAW TFT_eSPI OK", SCR_W / 2, SCR_H / 2 + 12, 1);
    _tft.setTextDatum(TL_DATUM);

    if (locked) {
        xSemaphoreGive(s_busMutex);
    }
}

#if CFG_DISPLAY_BRINGUP == 2
// =============================================================================
// 排错模式 2 的页面：纯色块，不用任何字体
// =============================================================================
void DisplayLib::createColorTestUi() {
    lv_obj_t *scr = lv_scr_act();
    lv_obj_set_style_bg_color(scr, lv_color_hex(0xFF0000), 0);  // 红底
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

    lv_obj_t *box = lv_obj_create(scr);                          // 蓝方块
    lv_obj_set_size(box, SCR_W / 2, SCR_H / 3);
    lv_obj_align(box, LV_ALIGN_TOP_LEFT, 8, 8);
    lv_obj_set_style_radius(box, 0, 0);
    lv_obj_set_style_border_width(box, 0, 0);
    lv_obj_set_style_bg_color(box, lv_color_hex(0x0000FF), 0);
    lv_obj_set_style_bg_opa(box, LV_OPA_COVER, 0);
    lv_obj_clear_flag(box, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *box2 = lv_obj_create(scr);                         // 绿方块
    lv_obj_set_size(box2, SCR_W / 2, SCR_H / 3);
    lv_obj_align(box2, LV_ALIGN_BOTTOM_RIGHT, -8, -8);
    lv_obj_set_style_radius(box2, 0, 0);
    lv_obj_set_style_border_width(box2, 0, 0);
    lv_obj_set_style_bg_color(box2, lv_color_hex(0x00FF00), 0);
    lv_obj_set_style_bg_opa(box2, LV_OPA_COVER, 0);
    lv_obj_clear_flag(box2, LV_OBJ_FLAG_SCROLLABLE);
}
#endif

// =============================================================================
// 建界面（中文用 simsun_16_cjk，英文数字用 montserrat_12）
// =============================================================================
void DisplayLib::createUi() {
    lv_obj_t *scr = lv_scr_act();
    lv_obj_set_style_bg_color(scr, lv_color_hex(COLOR_BODY_BG), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

    // ---- 标题栏 ----
    lv_obj_t *titleBar = lv_obj_create(scr);
    lv_obj_set_size(titleBar, SCR_W, TITLE_H);
    lv_obj_align(titleBar, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_radius(titleBar, 0, 0);
    lv_obj_set_style_border_width(titleBar, 0, 0);
    lv_obj_set_style_pad_all(titleBar, 0, 0);
    lv_obj_set_style_bg_color(titleBar, lv_color_hex(COLOR_TITLE_BG), 0);
    lv_obj_clear_flag(titleBar, LV_OBJ_FLAG_SCROLLABLE);

    s_labelTitle = lv_label_create(titleBar);
    lv_obj_set_style_text_font(s_labelTitle, DISPLAY_FONT_CJK, 0);
    lv_obj_set_style_text_color(s_labelTitle, lv_color_white(), 0);
    lv_label_set_text(s_labelTitle, "速定位书格");
    lv_obj_align(s_labelTitle, LV_ALIGN_LEFT_MID, 6, 0);

    s_labelPage = lv_label_create(titleBar);
    lv_obj_set_style_text_font(s_labelPage, DISPLAY_FONT_ASCII, 0);
    lv_obj_set_style_text_color(s_labelPage, lv_color_white(), 0);
    lv_label_set_text(s_labelPage, "1/3");
    lv_obj_align(s_labelPage, LV_ALIGN_RIGHT_MID, -6, 0);

    // ---- 正文（中文多行，自动换行） ----
    s_labelBody = lv_label_create(scr);
    lv_obj_set_style_text_font(s_labelBody, DISPLAY_FONT_CJK, 0);
    lv_obj_set_style_text_color(s_labelBody, lv_color_hex(COLOR_TEXT), 0);
    lv_label_set_long_mode(s_labelBody, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(s_labelBody, SCR_W - 16);
    lv_obj_align(s_labelBody, LV_ALIGN_TOP_LEFT, 8, TITLE_H + 8);
    lv_label_set_text(s_labelBody, "启动中...");

    // ---- 进度条（系统信息页显示 RAM 占用） ----
    s_barValue = lv_bar_create(scr);
    lv_obj_set_size(s_barValue, SCR_W - 16, 10);
    lv_obj_align(s_barValue, LV_ALIGN_BOTTOM_MID, 0, -32);
    lv_bar_set_range(s_barValue, 0, 100);
    lv_bar_set_value(s_barValue, 0, LV_ANIM_OFF);
    lv_obj_add_flag(s_barValue, LV_OBJ_FLAG_HIDDEN);

    // ---- 页脚提示（英文，小字号） ----
    s_labelFooter = lv_label_create(scr);
    lv_obj_set_style_text_font(s_labelFooter, DISPLAY_FONT_ASCII, 0);
    lv_obj_set_style_text_color(s_labelFooter, lv_color_hex(COLOR_HINT), 0);
    lv_obj_align(s_labelFooter, LV_ALIGN_BOTTOM_LEFT, 8, -8);
    lv_label_set_text(s_labelFooter, CFG_FOOTER_HINT);

    // ---- 消息浮层（整屏，默认隐藏） ----
    s_msgPanel = lv_obj_create(scr);
    lv_obj_set_size(s_msgPanel, SCR_W, SCR_H);
    lv_obj_align(s_msgPanel, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_radius(s_msgPanel, 0, 0);
    lv_obj_set_style_border_width(s_msgPanel, 0, 0);
    lv_obj_set_style_pad_all(s_msgPanel, 0, 0);
    lv_obj_set_style_bg_color(s_msgPanel, lv_color_hex(COLOR_MSG_BG), 0);
    lv_obj_set_style_bg_opa(s_msgPanel, LV_OPA_COVER, 0);
    lv_obj_clear_flag(s_msgPanel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_msgPanel, LV_OBJ_FLAG_HIDDEN);

    s_msgLabel = lv_label_create(s_msgPanel);
    lv_obj_set_style_text_font(s_msgLabel, DISPLAY_FONT_CJK, 0);
    lv_obj_set_style_text_color(s_msgLabel, lv_color_white(), 0);
    lv_obj_set_style_text_align(s_msgLabel, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(s_msgLabel, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(s_msgLabel, SCR_W - 32);
    lv_obj_center(s_msgLabel);
    lv_label_set_text(s_msgLabel, "");
}

// =============================================================================
// 外部写数据（只写数据，不画屏）
// =============================================================================
void DisplayLib::setFrame(const HuskyFrame &frame) {
    if (_mutex == nullptr) {
        return;
    }
    if (xSemaphoreTake(_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        _frame = frame;
        _dirty = true;
        xSemaphoreGive(_mutex);
    }
}

void DisplayLib::setSystemInfo(const SystemInfo &info) {
    if (_mutex == nullptr) {
        return;
    }
    if (xSemaphoreTake(_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        _info = info;
        _dirty = true;
        xSemaphoreGive(_mutex);
    }
}

// 超声波快照：和别的页一样，只写数据不画屏
void DisplayLib::setWave(const WaveState &wave) {
    if (_mutex == nullptr) {
        return;
    }
    if (xSemaphoreTake(_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        _wave = wave;
        _dirty = true;
        xSemaphoreGive(_mutex);
    }
}

// 累计一本：先按 ID 找（ID 非 0 时），再按文字内容找，都没有就新增一条
void DisplayLib::countBook(const HuskyWord &word) {
    if (_mutex == nullptr) {
        return;
    }
    if (xSemaphoreTake(_mutex, pdMS_TO_TICKS(50)) != pdTRUE) {
        return;
    }

    const char *text = word.content;
    if (text[0] == '\0') {
        text = word.name;
    }

    int16_t found = -1;
    for (int16_t i = 0; i < _bookNum; i++) {
        if (word.id != 0 && _books[i].id == word.id) {
            found = i;
            break;
        }
        if (text[0] != '\0' && strcmp(_books[i].text, text) == 0) {
            found = i;
            break;
        }
    }

    if (found < 0 && _bookNum < MAX_BOOK_RECORDS) {
        found = _bookNum;
        _books[found].id = word.id;
        _books[found].count = 0;
        snprintf(_books[found].text, sizeof(_books[found].text), "%s", text);
        _bookNum++;
    }

    if (found >= 0 && _books[found].count < 0xFFFF) {
        _books[found].count++;
    }

    _dirty = true;
    xSemaphoreGive(_mutex);
}

void DisplayLib::clearBooks() {
    if (_mutex == nullptr) {
        return;
    }
    if (xSemaphoreTake(_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        memset(_books, 0, sizeof(_books));
        _bookNum = 0;
        _dirty = true;
        xSemaphoreGive(_mutex);
    }
}

int16_t DisplayLib::bookCount() const {
    int16_t n = 0;
    if (_mutex != nullptr &&
        xSemaphoreTake(_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        n = _bookNum;
        xSemaphoreGive(_mutex);
    }
    return n;
}

void DisplayLib::showMessage(const char *text, uint32_t ms) {
    if (_mutex == nullptr || text == nullptr) {
        return;
    }
#if CFG_DISPLAY_BRINGUP == 2
    // 排错模式 2 要保证一个字体都不碰，而消息页用的是中文字库，这里直接忽略
    (void)ms;
    return;
#endif
    if (xSemaphoreTake(_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        snprintf(_message, sizeof(_message), "%s", text);
        _messageUntil = millis() + ms;
        _messageActive = true;
        _dirty = true;
        xSemaphoreGive(_mutex);
    }
}

void DisplayLib::setPage(uint8_t page) {
    if (_mutex == nullptr) {
        return;
    }
    if (xSemaphoreTake(_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        if (page < PAGE_COUNT) {
            _page = page;
            _dirty = true;
            _sigValid = false;  // 换页必须重画
        }
        xSemaphoreGive(_mutex);
    }
}

// 翻页：必须在锁里读当前页再算下一页，否则两个任务同时翻会漏页
void DisplayLib::nextPage() {
    if (_mutex == nullptr) {
        return;
    }
    if (xSemaphoreTake(_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        _page = (uint8_t)((_page + 1) % PAGE_COUNT);
        _dirty = true;
        _sigValid = false;  // 换页必须重画
        xSemaphoreGive(_mutex);
    }
}

uint8_t DisplayLib::page() const {
    return _page;
}

void DisplayLib::printHelp() {
    Serial.println("屏幕页(中文界面, LVGL 8.4):");
    Serial.println("  0 = 状态页(二哈连接/算法/识别块/已记录书数)");
    Serial.println("  1 = 系统信息页(RAM/PSRAM/Flash/运行时间)");
    Serial.println("  2 = 已识别书列表页");
    Serial.println("  3 = 超声波测距页(距离/滤波/统计/测厚)");
}

const char *displayLibVersion() {
    return DISPLAYLIB_VERSION;
}

// =============================================================================
// 显示任务：刷新 + 跑 LVGL 定时器，5ms 一轮
// =============================================================================
void DisplayLib::taskLoop() {
    uint32_t lastLog = 0;

    for (;;) {
        update();

        // 串口 d,1 请求的彩条自检：必须在显示任务里画（LVGL 只能一个任务碰）
        handleSelfTest();

        lv_timer_handler();

        // 心跳（config.h 的 CFG_DISPLAY_FLUSH_LOG_MS，默认 0 关）：
        // 把“LVGL 到底有没有刷屏”打成一行串口，这是黑屏排错最直接的证据。
        if (CFG_DISPLAY_FLUSH_LOG_MS > 0) {
            uint32_t now = millis();
            if (now - lastLog >= (uint32_t)CFG_DISPLAY_FLUSH_LOG_MS) {
                lastLog = now;
                // ESP-IDF 的 uxTaskGetStackHighWaterMark 单位是字节
                Serial.printf(
                    "显示: 刷屏=%lu次 最近一次=%luus LVGL时钟=%lums 栈余量=%u字节\n",
                    (unsigned long)s_flushCount, (unsigned long)s_lastFlushUs,
                    (unsigned long)lv_tick_get(),
                    (unsigned)uxTaskGetStackHighWaterMark(nullptr));
            }
        }

        vTaskDelay(pdMS_TO_TICKS(CFG_DISPLAY_INTERVAL_MS));
    }
}

// =============================================================================
// 彩条自检 / SPI 总线测速：串口命令 d,1 和 d,2 用，不用重新烧录就能验硬件
//
// 流程：加总线锁写屏 -> 请求 LVGL 整屏重画（画面自己回来）。
// 写屏和 LVGL 刷屏都在本任务里做，所以不存在两个任务同时写 SPI 的问题。
// =============================================================================
void DisplayLib::requestRawTest(uint32_t holdMs) {
    _rawTestHoldMs = holdMs;
    _testRequest = 1;
}

void DisplayLib::requestBusTest() {
    _testRequest = 2;
}

void DisplayLib::handleSelfTest() {
    uint8_t req = _testRequest;
    if (req == 0) {
        return;
    }
    _testRequest = 0;  // 先清标志，免得测的过程中又被置上

    if (req == 1) {
        runRawTest(_rawTestHoldMs);
        return;
    }

    // ---- SPI 总线测速 ----
    // 只写顶部 240x40（9600 像素），够算出 us/像素，又不至于长时间盖住画面
    uint32_t us = busWriteTest(false);
    uint32_t px = s_lastBusTestPx ? s_lastBusTestPx : 1;
    uint32_t per100 = us * 100UL / px;
    Serial.printf("显示: SPI 总线测速 %lu 像素用时 %luus = %lu.%02luus/像素\n",
                  (unsigned long)px, (unsigned long)us,
                  (unsigned long)(per100 / 100UL), (unsigned long)(per100 % 100UL));
    if (us * 10UL >= px) {
        Serial.println("  -> SPI 确实在往屏上吐数据（问题在屏/背光/接线，不在软件刷屏）");
    } else {
        Serial.println("  -> ❌ 太快了，SPI 根本没在发数据（查 platformio.ini 的 "
                       "USER_SETUP/USE_FSPI_PORT 与引脚宏）");
    }

    restoreScreenAfterTest();
}

void DisplayLib::runRawTest(uint32_t holdMs) {
    drawRawBars("RAW TEST");

    if (holdMs > 0) {
        vTaskDelay(pdMS_TO_TICKS(holdMs));
    }

    restoreScreenAfterTest();
}

// 自检把画面盖掉之后，让 LVGL 整屏重画一次，画面自己回来
void DisplayLib::restoreScreenAfterTest() {
    if (xSemaphoreTake(_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        _sigValid = false;
        _dirty = true;
        xSemaphoreGive(_mutex);
    }
    lv_obj_invalidate(lv_scr_act());
}

// =============================================================================
// SPI 总线测速：写一块像素并计时（唯一写屏者 = 调用它的任务）
// fullScreen=true -> fillScreen 240x320；false -> 顶部 240x40 一条
// =============================================================================
uint32_t DisplayLib::busWriteTest(bool fullScreen) {
    bool locked = (s_busMutex != nullptr) &&
                  (xSemaphoreTake(s_busMutex, pdMS_TO_TICKS(200)) == pdTRUE);

    uint32_t t0 = micros();
    if (fullScreen) {
        _tft.fillScreen(TFT_BLACK);
        s_lastBusTestPx = (uint32_t)SCR_W * SCR_H;
    } else {
        _tft.fillRect(0, 0, SCR_W, CFG_LV_BUF_LINES, TFT_BLACK);
        s_lastBusTestPx = (uint32_t)SCR_W * CFG_LV_BUF_LINES;
    }
    s_lastBusTestUs = micros() - t0;

    if (locked) {
        xSemaphoreGive(s_busMutex);
    }
    return s_lastBusTestUs;
}

// =============================================================================
// 显示诊断（串口命令 d）：把排错要看的数一次性打全
// =============================================================================
void DisplayLib::printDiag() {
    Serial.println("--- 显示诊断 ---");
    Serial.printf("  刷屏回调: %lu 次, 最近一次 %lu us\n",
                  (unsigned long)s_flushCount, (unsigned long)s_lastFlushUs);
    if (s_lastBusTestPx > 0) {
        uint32_t per100 = s_lastBusTestUs * 100UL / s_lastBusTestPx;
        Serial.printf("  SPI 总线(开机测): %luus / %lu 像素 = %lu.%02luus/像素\n",
                      (unsigned long)s_lastBusTestUs, (unsigned long)s_lastBusTestPx,
                      (unsigned long)(per100 / 100UL), (unsigned long)(per100 % 100UL));
    }
    Serial.printf("  LVGL 时钟: %lu ms (millis=%lu)\n",
                  (unsigned long)lv_tick_get(), (unsigned long)millis());
    if (_taskHandle != nullptr) {
        Serial.printf("  显示任务: 已创建, 栈余量 %u 字节\n",
                      (unsigned)uxTaskGetStackHighWaterMark(_taskHandle));
    } else {
        Serial.println("  显示任务: 未创建（排错模式 1 或 begin() 失败）");
    }
    Serial.printf("  刷屏缓冲: %d 行 x2, 位置=%s\n", CFG_LV_BUF_LINES,
                  CFG_LV_BUF_IN_PSRAM ? "PSRAM" : "内部RAM");
    Serial.printf("  背光: GPIO%d, 方式=%s\n", PIN_TFT_BL,
                  CFG_TFT_BL_USE_LEDC ? "LEDC(PWM)" : "digitalWrite");
    Serial.printf("  字节交换 CFG_LV_PUSH_SWAP=%d (配 lv_conf.h 的 LV_COLOR_16_SWAP=%d)\n",
                  CFG_LV_PUSH_SWAP, LV_COLOR_16_SWAP);
    Serial.printf("  中文字库: %s\n",
                  CFG_FONT_CUSTOM_CJK ? "ui_font_16(自建全字库)" : "LVGL 自带 simsun(缺字)");
    Serial.printf("  排错模式: CFG_DISPLAY_BRINGUP=%d, 开机彩条=%dms\n",
                  CFG_DISPLAY_BRINGUP, CFG_TFT_SPLASH_MS);
    Serial.println("  判读: 刷屏次数一直是 0 -> LVGL 没在刷; 在涨但屏黑 -> 屏/背光/接线");
    Serial.println("  命令 d,1 = 画一次 TFT_eSPI 彩条; d,2 = 立刻测一次 SPI 总线速度");
}

// =============================================================================
// 内容签名：只取页面上真的会显示的值，内容没变就不动控件
// =============================================================================
static uint32_t computeSignature(const HuskyFrame &f, const SystemInfo &info,
                                 const WaveState &wave,
                                 const BookRecord *books, int16_t bookNum,
                                 uint8_t page) {
    uint32_t sig = 2166136261UL;

    sig = (sig ^ (uint32_t)(f.online ? 1 : 0)) * 16777619UL;
    sig = (sig ^ (uint32_t)(f.modeIndex & 0xFF)) * 16777619UL;
    sig = (sig ^ (uint32_t)(f.wordNum & 0xFF)) * 16777619UL;
    sig = (sig ^ (uint32_t)(f.learnedNum & 0xFF)) * 16777619UL;
    // 帧数只按“每 CFG_SIGNATURE_FRAME_STEP 帧”参与，避免每帧都重画
    sig = (sig ^ (f.frameCount / (uint32_t)CFG_SIGNATURE_FRAME_STEP)) *
          16777619UL;
    sig = (sig ^ f.errorCount) * 16777619UL;

    for (int16_t i = 0; i < f.wordNum && i < CFG_STATUS_WORD_SHOWN; i++) {
        sig = (sig ^ (uint32_t)(f.words[i].id & 0xFF)) * 16777619UL;
        sig = (sig ^ (uint32_t)(f.words[i].x & 0xFF)) * 16777619UL;
        const char *t = f.words[i].content;
        for (uint8_t c = 0; t[c] != '\0' && c < 8; c++) {
            sig = (sig ^ (uint8_t)t[c]) * 16777619UL;
        }
    }

    sig = (sig ^ wave.filteredMm) * 16777619UL;
    sig = (sig ^ (uint32_t)(wave.valid ? 1 : 0)) * 16777619UL;

    sig = (sig ^ info.ramUsedKb) * 16777619UL;
    sig = (sig ^ info.psramUsedKb) * 16777619UL;
    sig = (sig ^ info.flashUsedKb) * 16777619UL;
    sig = (sig ^ (info.uptimeSeconds / 10U)) * 16777619UL;

    sig = (sig ^ (uint32_t)(bookNum & 0xFF)) * 16777619UL;
    for (int16_t i = 0; i < bookNum && i < 4; i++) {
        sig = (sig ^ (uint32_t)books[i].id) * 16777619UL;
        sig = (sig ^ (uint32_t)books[i].count) * 16777619UL;
    }

    sig = (sig ^ (uint32_t)(page & 0xFF)) * 16777619UL;
    return sig;
}

// =============================================================================
// 刷新：加锁取快照 -> 锁外更新控件
// =============================================================================
void DisplayLib::update() {
    if (_mutex == nullptr) {
        return;
    }

#if CFG_DISPLAY_BRINGUP == 2
    // 排错模式 2：屏幕上只有纯色块，业务控件一个都没建，
    // 这里必须直接返回，否则会去写空指针。
    return;
#endif

    // ---- 1) 加锁取快照，马上放锁 ----
    HuskyFrame frame;
    SystemInfo info;
    WaveState wave;
    BookRecord books[MAX_BOOK_RECORDS];
    int16_t bookNum = 0;
    uint8_t page = PAGE_STATUS;
    bool messageActive = false;
    char message[sizeof(_message)];
    uint32_t messageUntil = 0;

    memset(&frame, 0, sizeof(frame));
    memset(&info, 0, sizeof(info));
    memset(&wave, 0, sizeof(wave));
    memset(books, 0, sizeof(books));
    memset(message, 0, sizeof(message));

    if (xSemaphoreTake(_mutex, pdMS_TO_TICKS(50)) != pdTRUE) {
        return;
    }
    frame = _frame;
    info = _info;
    wave = _wave;
    memcpy(books, _books, sizeof(books));
    bookNum = _bookNum;
    page = _page;
    messageActive = _messageActive;
    snprintf(message, sizeof(message), "%s", _message);
    messageUntil = _messageUntil;
    xSemaphoreGive(_mutex);

    // ---- 2) 消息到期就撤掉 ----
    if (messageActive && (int32_t)(millis() - messageUntil) >= 0) {
        if (xSemaphoreTake(_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
            _messageActive = false;
            _dirty = true;
            _sigValid = false;
            xSemaphoreGive(_mutex);
        }
        messageActive = false;
    }

    // ---- 3) 有消息就画消息页，别的都不管 ----
    if (messageActive) {
        showMessagePage(message);
        return;
    }
    if (s_msgPanel != nullptr &&
        !lv_obj_has_flag(s_msgPanel, LV_OBJ_FLAG_HIDDEN)) {
        lv_obj_add_flag(s_msgPanel, LV_OBJ_FLAG_HIDDEN);
        _dirty = true;
        _sigValid = false;
    }

    // ---- 4) 正常页：内容变了才更新控件 ----
    uint32_t sig = computeSignature(frame, info, wave, books, bookNum, page);
    if (_sigValid && !_dirty && sig == _lastSig) {
        return;
    }
    _lastSig = sig;
    _sigValid = true;
    _dirty = false;

    // ---- 5) 页码 + 对应页面 ----
    char pageText[16];
    snprintf(pageText, sizeof(pageText), "%u/%u", (unsigned)(page + 1),
             (unsigned)PAGE_COUNT);
    lv_label_set_text(s_labelPage, pageText);

    switch (page) {
        case PAGE_SYSINFO:
            showSysInfoPage(info);
            break;
        case PAGE_WAVE:
            showWavePage(wave);
            break;
        case PAGE_BOOKS:
            showBooksPage(books, bookNum);
            break;
        case PAGE_STATUS:
        default:
            showStatusPage(frame, bookNum);
            break;
    }
}

// =============================================================================
// 三个页面 + 消息页
// =============================================================================
void DisplayLib::showStatusPage(const HuskyFrame &f, int16_t bookNum) {
    char body[CFG_STATUS_TEXT_LEN];
    char recent[HUSKY_TEXT_LEN + 8];
    const char *modeName = "未知";

    int16_t mi = f.modeIndex;
    if (mi >= 0 && mi < (int16_t)HUSKY_MODE_COUNT) {
        modeName = HUSKY_MODE_TABLE[mi].name;
    }

    // 取第一个有文字内容的识别块
    recent[0] = '\0';
    for (int16_t i = 0; i < f.wordNum && i < HUSKY_WORD_MAX; i++) {
        const char *t = f.words[i].content;
        if (t[0] == '\0') {
            t = f.words[i].name;
        }
        if (t[0] != '\0') {
            snprintf(recent, sizeof(recent), "%s", t);
            break;
        }
    }
    if (recent[0] == '\0') {
        snprintf(recent, sizeof(recent), "（画面里没有目标）");
    }

    snprintf(body, sizeof(body),
             "二哈: %s\n"
             "算法: %s\n"
             "识别块: %d   已学: %d\n"
             "累计帧: %lu  错误: %lu\n"
             "\n最近识别:\n%s\n"
             "\n已记录书数: %d",
             f.online ? "在线" : "离线", modeName, (int)f.wordNum,
             (int)f.learnedNum, (unsigned long)f.frameCount,
             (unsigned long)f.errorCount, recent, (int)bookNum);

    lv_label_set_text(s_labelBody, body);
    lv_obj_add_flag(s_barValue, LV_OBJ_FLAG_HIDDEN);
}

void DisplayLib::showSysInfoPage(const SystemInfo &info) {
    char body[256];
    char ram[32];
    char psram[32];
    char flash[32];
    char uptime[16];

    sysInfoFormatRam(info, ram, sizeof(ram));
    sysInfoFormatPsram(info, psram, sizeof(psram));
    sysInfoFormatFlash(info, flash, sizeof(flash));
    sysInfoFormatUptime(info, uptime, sizeof(uptime));

    snprintf(body, sizeof(body),
             "内存 RAM\n  %s\n"
             "\nPSRAM\n  %s\n"
             "\nFlash\n  %s\n"
             "\n运行时间: %s\n"
             "芯片: %u 核 %u MHz",
             ram, psram, flash, uptime, (unsigned)info.chipCores,
             (unsigned)info.cpuFreqMhz);

    lv_label_set_text(s_labelBody, body);

    // 进度条 = RAM 占用百分比
    lv_bar_set_value(s_barValue, (int32_t)info.ramUsedPct, LV_ANIM_OFF);
    lv_obj_clear_flag(s_barValue, LV_OBJ_FLAG_HIDDEN);
}

void DisplayLib::showBooksPage(const BookRecord *books, int16_t bookNum) {
    char body[CFG_STATUS_TEXT_LEN];
    int off = 0;
    int16_t shown = bookNum;

    if (shown > MAX_BOOK_RECORDS) {
        shown = MAX_BOOK_RECORDS;
    }

    off += snprintf(body + off, sizeof(body) - (size_t)off, "已记录 %d 本：\n",
                    (int)bookNum);

    if (shown == 0) {
        off += snprintf(body + off, sizeof(body) - (size_t)off,
                        "\n（还没扫到书）");
    }

    for (int16_t i = 0; i < shown; i++) {
        if (off > (int)sizeof(body) - 48) {
            break;  // 屏幕放不下就截断，不越界
        }
        const char *t = (books[i].text[0] != '\0') ? books[i].text : "（无文字）";
        off += snprintf(body + off, sizeof(body) - (size_t)off, "%d. %s x%u\n",
                        (int)(i + 1), t, (unsigned)books[i].count);
    }

    lv_label_set_text(s_labelBody, body);
    lv_obj_add_flag(s_barValue, LV_OBJ_FLAG_HIDDEN);
}

// 超声波测距页：距离(mm/cm) + 滤波值 + 统计 + 测厚信息
void DisplayLib::showWavePage(const WaveState &wave) {
    char body[CFG_STATUS_TEXT_LEN];
    char lineDist[48];
    char lineState[48];

    if (!wave.valid && wave.distanceMm == 0) {
        snprintf(lineDist, sizeof(lineDist), "等待测距...");
    } else {
        snprintf(lineDist, sizeof(lineDist), "%u mm  (%u.%u cm)",
                 (unsigned)wave.distanceMm, (unsigned)(wave.distanceMm / 10U),
                 (unsigned)(wave.distanceMm % 10U));
    }

    if (wave.errorCode == 1) {
        snprintf(lineState, sizeof(lineState), "超时: ECHO 没拉高(查TRIG/5V)");
    } else if (wave.errorCode == 2) {
        snprintf(lineState, sizeof(lineState), "超时: ECHO 没拉低(查分压)");
    } else if (!wave.valid) {
        snprintf(lineState, sizeof(lineState), "无效读数(超量程或无目标)");
    } else {
        snprintf(lineState, sizeof(lineState), "读数正常");
    }

    int off = 0;
    off += snprintf(body + off, sizeof(body) - (size_t)off,
                    "距离: %s\n"
                    "状态: %s\n"
                    "滤波: %u mm\n",
                    lineDist, lineState, (unsigned)wave.filteredMm);

    // 测厚：B 键记起点，A 键算厚度
    if (wave.asnapValid) {
        off += snprintf(body + off, sizeof(body) - (size_t)off,
                        "\n起点: %u mm (%.1f 秒前)\n",
                        (unsigned)wave.asnapMm,
                        (double)((millis() - wave.asnapMs) / 1000.0));
    }
    if (wave.thicknessValid) {
        off += snprintf(body + off, sizeof(body) - (size_t)off,
                        "厚度: %d mm\n", (int)wave.thicknessMm);
    }

    snprintf(body + off, sizeof(body) - (size_t)off,
             "\n统计: 共%u 无效%u\n范围: %u~%u mm",
             (unsigned)wave.totalCount, (unsigned)wave.invalidCount,
             (unsigned)wave.minMm, (unsigned)wave.maxMm);

    lv_label_set_text(s_labelBody, body);
    lv_obj_add_flag(s_barValue, LV_OBJ_FLAG_HIDDEN);
}

void DisplayLib::showMessagePage(const char *text) {
    lv_label_set_text(s_msgLabel, text);
    lv_obj_clear_flag(s_msgPanel, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(s_msgPanel);
}
