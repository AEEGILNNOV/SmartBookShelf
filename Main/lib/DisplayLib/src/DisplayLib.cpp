#include "DisplayLib.h"

// 全工程唯一配置来源（刷屏缓冲行数 / 消息停留时间 / 任务栈与核心都在这里）
#include "config.h"

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
// 内存：优先 PSRAM，失败退回内部 RAM
// =============================================================================
static void *displayAlloc(size_t size) {
    void *p = nullptr;
    if (psramFound()) {
        p = ps_malloc(size);
    }
    if (p == nullptr) {
        p = malloc(size);
    }
    return p;
}

// =============================================================================
// LVGL 刷屏回调：把 LVGL 画好的区域推给 TFT_eSPI
// 注意：只有 displayTask 会走到这里，所以不需要额外的总线锁。
// =============================================================================
static void displayFlushCb(lv_disp_drv_t *drv, const lv_area_t *area,
                           lv_color_t *colorP) {
    TFT_eSPI *tft = (TFT_eSPI *)drv->user_data;
    int32_t w = area->x2 - area->x1 + 1;
    int32_t h = area->y2 - area->y1 + 1;

    tft->startWrite();
    tft->setAddrWindow(area->x1, area->y1, w, h);
    tft->pushColors((uint16_t *)&colorP->full, w * h, true);
    tft->endWrite();

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
    _page = PAGE_STATUS;
    _dirty = true;
    memset(_message, 0, sizeof(_message));
    _messageUntil = 0;
    _messageActive = false;
    _lastSig = 0;
    _sigValid = false;
}

void DisplayLib::displayTaskEntry(void *pvParameters) {
    DisplayLib *self = (DisplayLib *)pvParameters;
    self->taskLoop();
}

// =============================================================================
// 初始化：屏幕 -> LVGL -> 界面 -> 显示任务
// =============================================================================
bool DisplayLib::begin() {
    if (_mutex == nullptr) {
        _mutex = xSemaphoreCreateMutex();
    }
    if (_mutex == nullptr) {
        Serial.println("显示库: 互斥锁创建失败");
        return false;
    }

    // ---- 1) 屏幕 ----
    s_tft = &_tft;
    _tft.init();
    _tft.setRotation(TFT_ROTATION);
    _tft.fillScreen(TFT_BLACK);

    // 背光：TFT_eSPI 只在部分驱动里自动初始化 TFT_BL，这里显式做一次。
    // 亮度用 LEDC 控制，别把 BL 直接接 VCC（config.h 的 CFG_TFT_BL_LEVEL 可调）。
    pinMode(PIN_TFT_BL, OUTPUT);
    digitalWrite(PIN_TFT_BL, TFT_BACKLIGHT_ON);

    // ---- 2) LVGL ----
    lv_init();

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

    createUi();

    _dirty = true;
    _sigValid = false;

    // 建显示任务：LVGL 只有这一个消费者，所以不需要总线锁
    xTaskCreatePinnedToCore(displayTaskEntry, "display", CFG_STACK_DISPLAY, this,
                            CFG_PRIO_DISPLAY, nullptr, CFG_CORE_DISPLAY);

    Serial.printf(
        "显示库: ILI9341 %dx%d rotation=%d + LVGL8 已就绪 (缓冲 %uKB x2 %s)\n",
        (int)SCR_W, (int)SCR_H, (int)TFT_ROTATION,
        (unsigned)(pixels * sizeof(lv_color_t) / 1024),
        psramFound() ? "PSRAM" : "RAM");
    return true;
}

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
    for (;;) {
        update();
        lv_timer_handler();
        vTaskDelay(pdMS_TO_TICKS(CFG_DISPLAY_INTERVAL_MS));
    }
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
