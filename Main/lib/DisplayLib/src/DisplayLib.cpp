#include "DisplayLib.h"

// 全工程唯一配置来源（刷屏缓冲行数 / 消息停留时间 / 任务栈与核心都在这里）
#include "config.h"

// 触摸：独立 5 根线的 XPT2046（见 config.h 第 3.2 节）
#include "TouchXPT2046.h"

// 开机中文字库自检（缺字会打到串口）
#include "FontCheck.h"

// =============================================================================
// 调试页要用、但 include/config.h 里可能还没有的配置项
//
// config.h 第 9.2 节已经有 CFG_DEBUG_JOG_SMALL/MID/LARGE 了，这里的 #ifndef 只是兜底：
// 万一别人拿这份库单独编译（或者 config.h 又被整理），本库也不会因为缺宏编不过。
// 一旦 config.h 里有了，这里的默认值自动失效。
//
// 为什么要分"点动步长"三档：手动调机械时一步走多远决定了能不能微调——
// 10 步 ≈ 0.9°（输出轴 4096 步/圈），100 步 ≈ 8.8°，500 步 ≈ 44°。
// =============================================================================
#ifndef CFG_DEBUG_JOG_SMALL
#define CFG_DEBUG_JOG_SMALL 10
#endif
#ifndef CFG_DEBUG_JOG_MID
#define CFG_DEBUG_JOG_MID 100
#endif
#ifndef CFG_DEBUG_JOG_LARGE
#define CFG_DEBUG_JOG_LARGE 500
#endif
#ifndef CFG_DEBUG_QUEUE_LEN
#define CFG_DEBUG_QUEUE_LEN 8      // 调试页 -> 业务任务的命令队列长度（需求定死 8 条）
#endif

// =============================================================================
// DisplayLib 实现（LVGL 8.4 中文版）
//
// 刷新逻辑分三层：
//   1) 外部任务 setFrame()/setSystemInfo()/… 只写数据并置脏标志，
//   2) displayTask 里 update() 先加锁取一份快照，立刻放锁，
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
// 刷屏回调被调了几次，最近一次花了多少微秒。
// 这两个数是排错的关键证据：
//   一直是 0        -> LVGL 根本没在刷屏（tick / 缓冲 / 驱动注册的问题）
//   在涨但屏是黑的  -> 数据已经发出去了，屏自己没反应（背光 / 接线 / 屏型号）
static volatile uint32_t s_flushCount = 0;
static volatile uint32_t s_lastFlushUs = 0;

// SPI 总线测速结果：填屏花的时间(us) 和写了多少像素
// 每像素理论下限 = 16bit / 27MHz ≈ 0.6us，实测只要 > 0.6us/像素 就说明数据真的发出去了。
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

//------------------------------------ 触摸（XPT2046）------------------------------------//
// 触摸测试页的控件 + 当前触摸状态。
// ⚠️ 这些变量只在 displayTask 里读写：LVGL 的输入设备回调也是被
//    lv_timer_handler() 调起来的，和刷屏在同一个任务里，不需要额外加锁。
static lv_obj_t *s_touchPanel = nullptr;   // 按钮面板（只在触摸页显示）
static lv_obj_t *s_btnTest = nullptr;      // 大测试按钮
static lv_obj_t *s_labelBtn = nullptr;     // 按钮上的字（显示点击次数）
static lv_obj_t *s_btnPrev = nullptr;      // 上一页
static lv_obj_t *s_btnReset = nullptr;     // 清零
static lv_obj_t *s_btnNext = nullptr;      // 下一页
static lv_indev_drv_t s_indevDrv;

//------------------------------------ 书列表页（动态数组书单的界面） ------------------------------------//
static lv_obj_t *s_bookPanel = nullptr;    // 可滚动的书单容器（每行带"取出"按钮）

static bool s_touchPressed = false;        // 当前是否有手指按着
static uint16_t s_touchX = 0;              // 换算后的屏幕坐标
static uint16_t s_touchY = 0;
static uint16_t s_touchRawX = 0;           // XPT2046 原始值（校准/排错看它）
static uint16_t s_touchRawY = 0;
static uint16_t s_touchZ = 0;              // 压力值（灵敏度门限就是比它）
static uint32_t s_touchPressCount = 0;     // 累计按下次数（原始触摸）
static uint32_t s_btnClickCount = 0;       // 测试按钮被点了几次

// 原始值采样（串口命令 d,3）：用来核对轴方向/量程，采完在串口打印四角极值
static bool s_touchSampling = false;
static uint16_t s_rawMinX = 0xFFFF;
static uint16_t s_rawMaxX = 0;
static uint16_t s_rawMinY = 0xFFFF;
static uint16_t s_rawMaxY = 0;

// 触摸驱动：独立 5 根线的位操作 XPT2046（和参考工程同一套），不占屏的 SPI
static TouchXPT2046 s_touch;

// 屏幕上的按钮回调要用到 DisplayLib 实例（LVGL 的事件回调是普通函数，没有 this）
static DisplayLib *s_self = nullptr;

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

// 调试页 / 调试入口的布局与时序（要调布局只改这几个数）
// ⚠️ 这几个 #define 必须放在使用它们的函数**之前**（放在文件靠后的"布局"段会编不过）。
#define DEBUG_ROW_H 34          // 调试页一行按钮的高度（行高 34，按钮 30，留 4 间距）
#define DEBUG_BTN_W 62          // 状态页/书列表页右下角"调试"入口的宽
#define DEBUG_BTN_H 24          // 它的高
// 它的底边离屏幕底部多远：主动躲开最下方 CFG_TOUCH_BOTTOM_MASK_PX 那段不响应触摸的屏蔽区
#define DEBUG_BTN_BOTTOM (CFG_TOUCH_BOTTOM_MASK_PX + 4)
// 书列表页的滚动容器要主动给"调试"入口让出的高度（按钮 24 + 6 间距），
// 不然入口会压在最后几行的"取出"按钮上面，那边就点不着了。
#define DEBUG_BTN_RESERVE (DEBUG_BTN_H + 6)
// 清空书单的二次确认时限(ms)：超时自动收回，免得"上膛"以后被下一次误点清库
#define DEBUG_CLEAR_CONFIRM_MS 3000

// =============================================================================
// 显示/隐藏一个控件：状态没变就不动它
// 为什么要有这个小函数：lv_obj_add_flag/clear_flag 即使状态没变，也会让那块区域
// 重画一遍。隐藏的触摸按钮面板有 31KB 像素，每轮刷新都白重画一次，会白白多刷很多次。
// =============================================================================
static void objSetHidden(lv_obj_t *obj, bool hidden) {
    if (obj == nullptr) {
        return;
    }
    if (lv_obj_has_flag(obj, LV_OBJ_FLAG_HIDDEN) == hidden) {
        return;
    }
    if (hidden) {
        lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_clear_flag(obj, LV_OBJ_FLAG_HIDDEN);
    }
}

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
// 背光：默认 digitalWrite（和验证过的 TFT_Test 一致），
// 需要调亮度时在 config.h 里把 CFG_TFT_BL_USE_LEDC 置 1。
// 电平方向由 CFG_TFT_BL_ACTIVE_LOW 决定（默认高电平点亮）。
//
// 背光不亮时的排查顺序（硬件占大多数）：
//   1) 串口敲 d,4 让背光闪 3 次：会闪 -> 引脚和线是通的，问题在别处，
//      不闪 -> 用万用表量 GPIO47 对 GND：约 3.3V 说明引脚在输出，
//      那就是 LED 线/模块供电/LED 本身的问题，0V 才是主控这边的问题。
//   2) 敲 d,5 把电平对调试试（有些模块是低有效）。
//   3) 确认模块 LED/BL 脚确实接在 GPIO47，且模块 GND 与主控共地。
// =============================================================================
static void backlightWrite(bool on) {
#if CFG_TFT_BL_USE_LEDC
    ledcWrite(CFG_TFT_BL_LEDC_CHANNEL, on ? CFG_TFT_BL_LEVEL : 0);
#else
    bool level = on;
#if CFG_TFT_BL_ACTIVE_LOW
    level = !level;
#endif
    digitalWrite(PIN_TFT_BL, level ? HIGH : LOW);
#endif
}

static void backlightInit() {
#if CFG_TFT_BL_USE_LEDC
    ledcSetup(CFG_TFT_BL_LEDC_CHANNEL, CFG_TFT_BL_LEDC_FREQ, 8);
    ledcAttachPin(PIN_TFT_BL, CFG_TFT_BL_LEDC_CHANNEL);
#else
    pinMode(PIN_TFT_BL, OUTPUT);
#endif
    backlightWrite(true);
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
// 加总线锁的原因：现在只有 displayTask 会刷屏，这把锁是“零成本保险”，
// 以后要加摄像头预览，或别的任务直接写屏（比如串口命令 d,1 的彩条），
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
// 触摸（XPT2046）：LVGL 输入设备
//
// LVGL 需要有人告诉它“现在有没有按下，按在哪”，下面这个回调就是干这个的。
// 它由 lv_timer_handler() 按 LV_INDEV_DEF_READ_PERIOD（= CFG_TOUCH_READ_MS）调起，
// 和刷屏，按钮事件都跑在 displayTask 里，所以天然不会并发。
//
// 原始值 -> 屏幕坐标的换算照抄参考工程 LVGLin240320TFTLCD：
//   先按 SWAP/INVERT 调轴方向，再按 4095 满量程线性映射到 SCR_W/SCR_H。
// =============================================================================
#if CFG_RUN_TOUCH
static void touchToScreen(uint16_t rawX, uint16_t rawY, int16_t *sx, int16_t *sy) {
    int32_t x = rawX;
    int32_t y = rawY;

#if CFG_TOUCH_SWAP_XY
    int32_t tmp = x;
    x = y;
    y = tmp;
#endif
#if CFG_TOUCH_INVERT_X
    x = 4095 - x;
#endif
#if CFG_TOUCH_INVERT_Y
    y = 4095 - y;
#endif

    *sx = (int16_t)((int64_t)x * (SCR_W - 1) / 4095);
    *sy = (int16_t)((int64_t)y * (SCR_H - 1) / 4095);
}

static void touchReadCb(lv_indev_drv_t *drv, lv_indev_data_t *data) {
    (void)drv;

    // 松手后 LVGL 还会继续调用本回调，所以要记住最后一次坐标
    static int16_t lastX = 0;
    static int16_t lastY = 0;

    uint16_t rawX = 0;
    uint16_t rawY = 0;

    if (s_touch.read(&rawX, &rawY)) {
        s_touchRawX = rawX;
        s_touchRawY = rawY;
        s_touchZ = s_touch.lastPressure();

        int16_t sx = 0;
        int16_t sy = 0;
        touchToScreen(rawX, rawY, &sx, &sy);

        // ---- 屏幕最下方 CFG_TOUCH_BOTTOM_MASK_PX 像素不响应触摸 ----
        // 手指划到这个区域容易误触（也容易被外壳/手势区蹭到），直接当成"没按"。
        if (CFG_TOUCH_BOTTOM_MASK_PX > 0 && sy >= (SCR_H - CFG_TOUCH_BOTTOM_MASK_PX)) {
            s_touchPressed = false;
            data->state = LV_INDEV_STATE_RELEASED;
            data->point.x = lastX;
            data->point.y = lastY;
            return;
        }

        if (!s_touchPressed) {
            s_touchPressCount++;  // 只统计“按下”这一瞬间，不按读取次数计
        }

        s_touchX = (uint16_t)sx;
        s_touchY = (uint16_t)sy;
        lastX = sx;
        lastY = sy;

        // 采样模式：记录原始值极值，给校准/查轴方向用（不会打印，避免刷屏）
        if (s_touchSampling) {
            if (rawX < s_rawMinX) s_rawMinX = rawX;
            if (rawX > s_rawMaxX) s_rawMaxX = rawX;
            if (rawY < s_rawMinY) s_rawMinY = rawY;
            if (rawY > s_rawMaxY) s_rawMaxY = rawY;
        }

        data->state = LV_INDEV_STATE_PRESSED;
        data->point.x = sx;
        data->point.y = sy;
        s_touchPressed = true;
        return;
    }

    s_touchPressed = false;
    data->state = LV_INDEV_STATE_RELEASED;
    data->point.x = lastX;
    data->point.y = lastY;
}

// 测试按钮：点一下计数 +1，并直接写在按钮上（顺便打串口）
static void onBtnTestClicked(lv_event_t *e) {
    (void)e;
    if (s_btnClickCount < CFG_TOUCH_TEST_MAX) {
        s_btnClickCount++;
    }
    if (s_labelBtn != nullptr) {
        lv_label_set_text_fmt(s_labelBtn, "测试按钮 按下 %lu 次",
                              (unsigned long)s_btnClickCount);
    }
    Serial.printf("触摸: 测试按钮第 %lu 次按下 (屏幕 %u,%u | 原始 %u,%u)\n",
                  (unsigned long)s_btnClickCount, (unsigned)s_touchX,
                  (unsigned)s_touchY, (unsigned)s_touchRawX,
                  (unsigned)s_touchRawY);
}

// 清零按钮：把计数归零，方便反复测
static void onBtnResetClicked(lv_event_t *e) {
    (void)e;
    s_btnClickCount = 0;
    s_touchPressCount = 0;
    if (s_labelBtn != nullptr) {
        lv_label_set_text(s_labelBtn, "测试按钮 按下 0 次");
    }
    Serial.println("触摸: 计数已清零");
}

// 上一页 / 下一页：点屏幕就能换页，用来看触摸是不是真的在起作用
// （翻页本身走和串口命令 n 完全一样的那条路：DisplayLib::prevPage()/nextPage()）
static void onBtnPrevPage(lv_event_t *e) {
    (void)e;
    if (s_self != nullptr) {
        s_self->prevPage();
        Serial.printf("触摸: 上一页 -> 第 %u 页\n", (unsigned)s_self->page() + 1);
    }
}

static void onBtnNextPage(lv_event_t *e) {
    (void)e;
    if (s_self != nullptr) {
        s_self->nextPage();
        Serial.printf("触摸: 下一页 -> 第 %u 页\n", (unsigned)s_self->page() + 1);
    }
}

// 书列表页每一行右边的"取出"按钮
// 只把"第几条"取出来交给 DisplayLib::removeBook()：
//   · 它只在 BookList 里删数据（加锁，很快），不碰任何控件，
//   · 行控件的重建交给 update() 里的 rebuildBookRows()，
//     绝不能在这里删自己（LVGL 不允许在事件回调里删掉正在处理的控件）。
static void onBtnTakeOut(lv_event_t *e) {
    int16_t index = (int16_t)(intptr_t)lv_event_get_user_data(e);
    if (s_self != nullptr) {
        s_self->removeBook(index);
    }
}
#endif  // CFG_RUN_TOUCH

// =============================================================================
// 调试页（第 6 页）：控件、内部命令队列、数据快照、按钮回调
//
// 这一段放在构造函数之前，是为了让后面所有用到它的地方（构造/begin/taskLoop/update/
// createDebugUi/showDebugPage）都能直接看到；也保证 s_self / 布局宏已经声明过。
// =============================================================================

// ⚠️ 这些控件和别的页面一样，只在 displayTask 里碰（LVGL 不是线程安全的）
static lv_obj_t *s_debugPanel = nullptr;    // 调试页的竖直滚动容器（只能上下滑）
static lv_obj_t *s_debugText = nullptr;     // 调试页正文（中文多行，跟着容器一起滚）
static lv_obj_t *s_btnDebugEnter = nullptr; // 状态页/书列表页右下角的"调试"入口
static lv_obj_t *s_btnClearBooks = nullptr; // "清空书籍"按钮（要二次确认，文字会变）
static lv_obj_t *s_jogBtn[3] = {nullptr, nullptr, nullptr};  // 小/中/大 三个步长按钮

// 清空书单的二次确认：armed = "已经点过一次，正等第二次确认"
static bool s_clearArmed = false;
static uint32_t s_clearArmUntil = 0;        // 过了这个时刻就自动取消确认

// ---- 调试意图队列：界面 -> 业务任务 ----
// 为什么不让事件回调直接调 handler：LVGL 的事件回调跑在 displayTask 的调用链里，
// 在那里做业务（哪怕只是往 MotorQueue 投一条）都可能变慢，直接拖慢刷屏。
// 多一层几十字节的内部队列：回调只入队（不阻塞），taskLoop() 每轮再取出来交给 handler。
static QueueHandle_t s_debugQueue = nullptr;
static void (*s_debugCmdHandler)(uint8_t cmd, int16_t arg) = nullptr;

// ---- 调试页数据快照：业务任务写、显示任务读 ----
static SemaphoreHandle_t s_debugMutex = nullptr;
static DebugState s_debugState;                        // 显示用的纯数据快照（构造里给默认值）
// 当前点动步长（调试页按钮改它）。
// 这里用 int16_t 存"权威值"：jog 命令的 arg 就是 int16_t，一路传给 MotorQueue 不用来回转。
// 对外的 DebugState.jogSteps / debugJogSteps() 是 uint16_t（lead 已确认改类型），
// 三档 10/100/500 都能原样表示，不再有"500 被截断成 244"的问题。
static int16_t s_jogSteps = CFG_DEBUG_JOG_MID;

// =============================================================================
// 事件回调里"投意图"：入队就返回，绝不阻塞、绝不碰 GPIO
// 队列满就丢并打日志——宁可丢一次点动，也不能让显示任务在这里等。
// =============================================================================
static void debugPostCmd(uint8_t cmd, int16_t arg) {
    if (s_debugQueue == nullptr) {
        Serial.printf("⚠️ 调试: 命令队列没建好，命令 %u 丢弃\n", (unsigned)cmd);
        return;
    }
    DisplayDebugMsg msg;
    msg.cmd = cmd;
    msg.arg = arg;
    if (xQueueSend(s_debugQueue, &msg, 0) != pdTRUE) {
        Serial.printf("⚠️ 调试: 命令队列满，命令 %u 丢弃\n", (unsigned)cmd);
    }
}

// =============================================================================
// 清空书单的二次确认开关
//
// 为什么必须有：清空书籍数据是不可逆的破坏性操作，调试时手指又容易连点。
// 第一次点只"上膛"（按钮文字变成"再点一次确认清空"），第二次点才真的投命令；
// 超过 DEBUG_CLEAR_CONFIRM_MS 没再点就自动收回（serviceDebugConfirm() 每轮查一眼）。
// =============================================================================
static void debugClearArm(bool armed) {
    s_clearArmed = armed;
    s_clearArmUntil = armed ? (millis() + DEBUG_CLEAR_CONFIRM_MS) : 0;
    if (s_btnClearBooks != nullptr) {
        // 按钮里只有一个 label（makeDebugButton 建的），改它的第 0 个子控件就是改按钮文字
        lv_obj_t *label = lv_obj_get_child(s_btnClearBooks, 0);
        if (label != nullptr) {
            lv_label_set_text(label, armed ? "再点一次确认清空" : "清空书籍");
        }
    }
}

// =============================================================================
// taskLoop 每轮把队列里攒下的"意图"交给注册的 handler
//
// 为什么不直接在事件回调里调：handler 是 main.cpp 注册的业务代码，
// 让它跑在 LVGL 的调用链上，一旦它变慢就会连带卡住刷屏。多一次出队，代价可以忽略。
// =============================================================================
static void debugDispatch() {
    if (s_debugQueue == nullptr) {
        return;
    }
    DisplayDebugMsg msg;
    // 一轮最多取 CFG_DEBUG_QUEUE_LEN 条：队列就这么长，取完自然结束，不会把刷屏饿死
    for (uint8_t i = 0; i < CFG_DEBUG_QUEUE_LEN; i++) {
        if (xQueueReceive(s_debugQueue, &msg, 0) != pdTRUE) {
            return;
        }
        if (s_debugCmdHandler != nullptr) {
            s_debugCmdHandler(msg.cmd, msg.arg);
        } else {
            Serial.printf("⚠️ 调试: 没有注册动作处理函数，命令 %u(参数 %d) 被丢弃\n",
                          (unsigned)msg.cmd, (int)msg.arg);
        }
    }
}

// 上报用的无符号视图：s_jogSteps 只会被设成三个正数，这里只是把类型收敛干净
static uint16_t debugJogStepsU16() {
    return (uint16_t)((s_jogSteps < 0) ? 0 : s_jogSteps);
}

// =============================================================================
// 内部设置点动步长（int16_t 入参）
//
// 对外的 setDebugJogSteps(uint16_t) 是它的一层薄封装，调试页的三个按钮也调这里。
// 非法值一律拒绝：点动步长直接决定电机走多少步，乱给值可能一步就撞到机构。
// =============================================================================
static void debugApplyJogSteps(int16_t steps) {
    if (steps != CFG_DEBUG_JOG_SMALL && steps != CFG_DEBUG_JOG_MID &&
        steps != CFG_DEBUG_JOG_LARGE) {
        Serial.printf("⚠️ 调试: 非法点动步长 %d，只接受 %d/%d/%d 步\n", (int)steps,
                      (int)CFG_DEBUG_JOG_SMALL, (int)CFG_DEBUG_JOG_MID,
                      (int)CFG_DEBUG_JOG_LARGE);
        return;
    }

    s_jogSteps = steps;
    if (s_debugMutex != nullptr &&
        xSemaphoreTake(s_debugMutex, pdMS_TO_TICKS(20)) == pdTRUE) {
        s_debugState.jogSteps = (uint16_t)steps;   // jogSteps 已是 uint16_t，500 能存下
        xSemaphoreGive(s_debugMutex);
    }
    Serial.printf("调试: 点动步长 = %d 步/次\n", (int)steps);
}

// =============================================================================
// 调试页的按钮回调：**只投意图，绝不做业务**
//
// 每个回调只做一件事：debugPostCmd()/setDebugJogSteps()（都是几微秒就返回的）。
// 真正碰电机引脚的代码只存在于 stepperTask（全工程唯一碰电机的任务）。
// 这些回调故意放在 CFG_RUN_TOUCH 之外：它们不碰触摸驱动，
// 没有触摸时按钮照样建出来（只是没人点），串口命令仍是唯一入口。
// =============================================================================
static void onBtnDebugLiftUp(lv_event_t *e) {
    (void)e;
    debugPostCmd(DEBUG_CMD_LIFT_UP, (int16_t)s_jogSteps);
}

static void onBtnDebugLiftDown(lv_event_t *e) {
    (void)e;
    debugPostCmd(DEBUG_CMD_LIFT_DOWN, (int16_t)s_jogSteps);
}

static void onBtnDebugPushForward(lv_event_t *e) {
    (void)e;
    debugPostCmd(DEBUG_CMD_PUSH_FORWARD, (int16_t)s_jogSteps);
}

static void onBtnDebugPushBack(lv_event_t *e) {
    (void)e;
    debugPostCmd(DEBUG_CMD_PUSH_BACK, (int16_t)s_jogSteps);
}

static void onBtnDebugStop(lv_event_t *e) {
    (void)e;
    debugPostCmd(DEBUG_CMD_STOP_ALL, 0);
}

static void onBtnDebugSetZero(lv_event_t *e) {
    (void)e;
    debugPostCmd(DEBUG_CMD_SET_ZERO, 0);
}

static void onBtnDebugSaveNow(lv_event_t *e) {
    (void)e;
    debugPostCmd(DEBUG_CMD_SAVE_NOW, 0);
}

// 清空书籍数据：第一次点只上膛，第二次（3 秒内）才真发命令
static void onBtnDebugClearBooks(lv_event_t *e) {
    (void)e;
    if (!s_clearArmed) {
        debugClearArm(true);
        Serial.println("调试: 清空书籍需要二次确认，3 秒内再点一次才执行");
        return;
    }
    debugClearArm(false);
    debugPostCmd(DEBUG_CMD_CLEAR_BOOKS, 0);
    Serial.println("调试: 已确认清空书籍，命令已投给业务任务");
}

static void onBtnDebugJogSmall(lv_event_t *e) {
    (void)e;
    debugApplyJogSteps(CFG_DEBUG_JOG_SMALL);
}

static void onBtnDebugJogMid(lv_event_t *e) {
    (void)e;
    debugApplyJogSteps(CFG_DEBUG_JOG_MID);
}

// "大"档走内部 int16 接口：500 装不进对外的 uint8_t 签名（已报 lead）
static void onBtnDebugJogLarge(lv_event_t *e) {
    (void)e;
    debugApplyJogSteps(CFG_DEBUG_JOG_LARGE);
}

// 调试页的"返回"：回状态页。页面切换只认按钮和串口命令，没有任何左右滑动手势。
static void onBtnDebugBack(lv_event_t *e) {
    (void)e;
    if (s_self != nullptr) {
        s_self->setPage(PAGE_STATUS);
        Serial.println("调试: 返回状态页");
    }
}

// 状态页/书列表页右下角"调试"入口
static void onBtnEnterDebug(lv_event_t *e) {
    (void)e;
    if (s_self != nullptr) {
        s_self->setPage(PAGE_DEBUG);
        Serial.println("调试: 进入电机调试页(第 6 页)");
    }
}

// =============================================================================
// 构造
// =============================================================================
DisplayLib::DisplayLib() {
    memset(&_frame, 0, sizeof(_frame));
    memset(&_info, 0, sizeof(_info));
    memset(&_wave, 0, sizeof(_wave));
    memset(_bookSnap, 0, sizeof(_bookSnap));
    _bookSnapNum = 0;
    _bookSnapVersion = 0;
    _booksDirty = true;

    // 书单：默认没有外部挂载（main.cpp 会在 begin() 之前调 attachBookList()），
    // 没人挂时 books() 会惰性启用兜底的 _booksOwn。
    _booksPtr = nullptr;
    _booksOwnReady = false;

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
    _blOn = true;   // begin() 里 backlightInit() 会把背光打开

    // 调试页的默认值：先当"位置无效、需要人工归零"，
    // 免得开机第一次进调试页就看到一个骗人的"位置有效"。
    // lastActionOk 默认 true：还没有任何动作时不该报"上次失败"。
    s_jogSteps = CFG_DEBUG_JOG_MID;
    memset(&s_debugState, 0, sizeof(s_debugState));
    s_debugState.jogSteps = (uint16_t)s_jogSteps;
    s_debugState.lastActionOk = true;
    s_debugState.positionValid = false;
    s_debugState.needHoming = true;
    // 升降软限位只是先给个参考值：真正的行程由 stepperTask 通过 setDebugState() 填。
    // 用 LIFT 这个宏而不是旧的 M3：新机械定义里 升降 = LIFT 轴。
    s_debugState.liftRange = CFG_STEPPER_LIFT_RANGE_STEPS;
    snprintf(s_debugState.bookOrder, sizeof(s_debugState.bookOrder), "无");
    snprintf(s_debugState.lastError, sizeof(s_debugState.lastError), "无");
    s_clearArmed = false;
    s_clearArmUntil = 0;
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

    // 调试页的内部小队列 + 快照锁：要在别的任务开始注册调试命令之前就建好。
    // 建不起来不返回 false——丢的是"调试页按钮"，其它页面照常工作。
    if (s_debugQueue == nullptr) {
        s_debugQueue = xQueueCreate(CFG_DEBUG_QUEUE_LEN, sizeof(DisplayDebugMsg));
    }
    if (s_debugMutex == nullptr) {
        s_debugMutex = xSemaphoreCreateMutex();
    }
    if (s_debugQueue == nullptr || s_debugMutex == nullptr) {
        Serial.println("⚠️ 显示库: 调试页队列/锁创建失败（调试页按钮会失效，其它页不受影响）");
    }

    // 书单：动态数组，先申请 CFG_BOOKLIST_INIT_CAP 条，不够会自动扩容
    if (!books().begin()) {
        Serial.println("显示库: 书单初始化失败（内存不够?）");
    } else {
        Serial.printf("显示库: 书单就绪（动态数组, 初始 %d 条, 上限 %d 条）\n",
                      books().capacity(), CFG_BOOKLIST_MAX);
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
    // 默认 digitalWrite（和验证过的 TFT_Test 一致），要调亮度在 config.h 里开 LEDC。
    // 排错：背光不亮先敲 d,4（闪 3 次）和 d,5（电平对调），见本文件 backlightWrite 上面的注释。
    backlightInit();
    _blOn = true;
    Serial.printf("显示库: 背光 GPIO%d 已设为 %s（方式=%s, CFG_TFT_BL_ACTIVE_LOW=%d）\n",
                  PIN_TFT_BL, "开",
                  CFG_TFT_BL_USE_LEDC ? "LEDC(PWM)" : "digitalWrite",
                  CFG_TFT_BL_ACTIVE_LOW);

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
    // 表现就是“串口一切正常，屏上什么都没有”。这里开机就量一次，直接排除这个坑。
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

    // ---- 2.5) 触摸：独立 5 根线的 XPT2046 + 注册成 LVGL 输入设备 ----
    // 这一步以前整个工程都没有，所以就算接好了线也没有任何反应。
    s_self = this;   // 屏上按钮的事件回调要用到本实例（翻页等）
#if CFG_RUN_TOUCH
    s_touch.begin(PIN_TOUCH_CS, PIN_TOUCH_IRQ, PIN_TOUCH_CLK, PIN_TOUCH_DIN,
                  PIN_TOUCH_DO);
    lv_indev_drv_init(&s_indevDrv);
    s_indevDrv.type = LV_INDEV_TYPE_POINTER;
    s_indevDrv.read_cb = touchReadCb;
    s_indevDrv.scroll_limit = CFG_TOUCH_SCROLL_LIMIT;  // 0 = 解除滑动限制（config.h）
    lv_indev_t *indev = lv_indev_drv_register(&s_indevDrv);
    s_touch.printConfig();
    if (indev == nullptr) {
        Serial.println("显示库: LVGL 输入设备注册失败（lv_indev_drv_register 返回空）");
    } else {
        Serial.printf("显示库: 触摸已注册为 LVGL 输入设备 (轮询 %dms), 敲 t 到第 5 页点按钮\n",
                      CFG_TOUCH_READ_MS);
    }
#elif CFG_ENABLE_TOUCH
    Serial.println("显示库: 触摸引脚没配全（config.h 第 3.2 节的 PIN_TOUCH_*）");
#else
    Serial.println("显示库: 触摸已在 config.h 里关掉 (CFG_ENABLE_TOUCH=0)");
#endif

    // ---- 3) 界面 ----
#if CFG_DISPLAY_BRINGUP == 2
    // 排错模式 2：只画纯色块，一个字体都不碰。
    // 色块能出来 -> LVGL 刷屏链路（tick/flush/字节序）是好的，问题回到字体，
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

    // ===== 全工程只允许上下滑，禁止左右滑 =====
    // 根屏幕**完全禁止滚动**：页面切换只认屏幕按钮和串口命令，
    // 绝不能出现"手指左右一划就换页"——调机械时那种误触最要命。
    // 需要滚动的内容一律放进各自的容器，容器显式设成 LV_DIR_VER（见下面各 xxxUi()）。
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

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
    objSetHidden(s_barValue, true);

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

    // 触摸测试页的按钮面板（默认隐藏，翻到触摸页才显示）
    createTouchUi();

    // 书列表页的滚动容器（默认隐藏，翻到书列表页才显示）
    createBookUi();

    // 调试页（第 6 页）的竖直滚动容器 + 按钮，以及状态页/书列表页右下角的"调试"入口
    createDebugUi();
}

// =============================================================================
// 触摸测试页：一个能按的大按钮 + 下一排“上一页 / 清零 / 下一页”
// 面板在 footer 之后创建，所以它会盖住页脚提示，不用另外处理可见性。
// =============================================================================
// 小按钮工厂：三处写法一样，抽一个函数出来，免得复制粘贴改漏一处
static lv_obj_t *makeTouchButton(lv_obj_t *parent, const char *text, int16_t x,
                                 uint32_t colorHex) {
    lv_obj_t *btn = lv_btn_create(parent);
    lv_obj_set_size(btn, 66, 34);
    lv_obj_align(btn, LV_ALIGN_BOTTOM_MID, x, -4);
    lv_obj_set_style_bg_color(btn, lv_color_hex(colorHex), 0);
    lv_obj_set_style_radius(btn, 6, 0);

    lv_obj_t *label = lv_label_create(btn);
    lv_obj_set_style_text_font(label, DISPLAY_FONT_CJK, 0);
    lv_obj_set_style_text_color(label, lv_color_white(), 0);
    lv_label_set_text(label, text);
    lv_obj_center(label);
    return btn;
}

void DisplayLib::createTouchUi() {
    s_touchPanel = lv_obj_create(lv_scr_act());
    lv_obj_set_size(s_touchPanel, SCR_W - 16, 138);
    lv_obj_align(s_touchPanel, LV_ALIGN_TOP_MID, 0, 172);
    lv_obj_set_style_radius(s_touchPanel, 6, 0);
    lv_obj_set_style_border_width(s_touchPanel, 1, 0);
    lv_obj_set_style_border_color(s_touchPanel, lv_color_hex(0x90A0B0), 0);
    lv_obj_set_style_pad_all(s_touchPanel, 6, 0);
    lv_obj_set_style_bg_color(s_touchPanel, lv_color_hex(0xF0F4F8), 0);
    lv_obj_set_style_bg_opa(s_touchPanel, LV_OPA_COVER, 0);
    lv_obj_clear_flag(s_touchPanel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_touchPanel, LV_OBJ_FLAG_HIDDEN);

    // 大按钮：手指点得到就行，字用中文字库
    s_btnTest = lv_btn_create(s_touchPanel);
    lv_obj_set_size(s_btnTest, SCR_W - 44, 58);
    lv_obj_align(s_btnTest, LV_ALIGN_TOP_MID, 0, 2);
    lv_obj_set_style_bg_color(s_btnTest, lv_color_hex(0x0088FF), 0);
    lv_obj_set_style_radius(s_btnTest, 8, 0);

    s_labelBtn = lv_label_create(s_btnTest);
    lv_obj_set_style_text_font(s_labelBtn, DISPLAY_FONT_CJK, 0);
    lv_obj_set_style_text_color(s_labelBtn, lv_color_white(), 0);
    lv_label_set_text(s_labelBtn, "测试按钮 按下 0 次");
    lv_obj_center(s_labelBtn);

#if CFG_RUN_TOUCH
    lv_obj_add_event_cb(s_btnTest, onBtnTestClicked, LV_EVENT_CLICKED, nullptr);
#endif

    // 底下一排三个小按钮：上一页 / 清零 / 下一页
    // 上一页，下一页走的就是串口命令 n / t 用的那两个翻页函数，点一下就能验证触摸是否生效
    s_btnPrev = makeTouchButton(s_touchPanel, "上一页", -70, 0x0088FF);
    s_btnReset = makeTouchButton(s_touchPanel, "清零", 0, 0x607080);
    s_btnNext = makeTouchButton(s_touchPanel, "下一页", 70, 0x0088FF);

#if CFG_RUN_TOUCH
    lv_obj_add_event_cb(s_btnPrev, onBtnPrevPage, LV_EVENT_CLICKED, nullptr);
    lv_obj_add_event_cb(s_btnReset, onBtnResetClicked, LV_EVENT_CLICKED, nullptr);
    lv_obj_add_event_cb(s_btnNext, onBtnNextPage, LV_EVENT_CLICKED, nullptr);
#endif
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

// 累计一本：去重/计数的逻辑在 BookListLib 里（动态数组），这里只管转交 + 置脏标志
void DisplayLib::countBook(const HuskyWord &word) {
    const char *text = word.content;
    if (text[0] == '\0') {
        text = word.name;
    }
    if (books().addOrCount(word.id, text) >= 0) {
        _booksDirty = true;   // 列表变了，书列表页的行控件要重建
        _dirty = true;
    }
}

// 直接加一条（串口测试用，不去重）：没有二哈也能把书列表页/取出按钮/滚动试出来
int16_t DisplayLib::addBook(const char *text, int16_t id) {
    int16_t index = books().add(text, id);
    if (index >= 0) {
        _booksDirty = true;
        _dirty = true;
    }
    return index;
}

// 取出第 index 条：书列表页每行右边的"取出"按钮最终走到这里
// ⚠️ 这个函数是在 LVGL 的事件回调里被调的，所以**只能改数据，不能删控件**
//    （删掉正在处理事件的按钮会崩）。行控件的重建交给 update() 里的 rebuildBookRows()。
int16_t DisplayLib::removeBook(int16_t index) {
    // 先把要取出的那条读出来：日志要打名字，以后接机械也要拿它定位
    BookItem item;
    memset(&item, 0, sizeof(item));
    if (!books().at(index, &item)) {
        Serial.printf("书单: 取出失败, 下标 %d 不合法\n", (int)index);
        return -1;
    }

    int16_t left = books().removeAt(index);
    if (left < 0) {
        return -1;
    }

    Serial.printf("书单: 已取出第 %d 条 '%s', 剩 %d 条\n", (int)(index + 1),
                  item.text, (int)left);
    // TODO(机械接上以后): 这里就是"取书流程"的入口——
    //   把这条的书号/位置交给 stepperTask（M1 推出小车 / M2 中间夹层 / M3 升降平台）。
    _booksDirty = true;
    _dirty = true;
    return left;
}

void DisplayLib::clearBooks() {
    books().clear();
    _booksDirty = true;
    _dirty = true;
    Serial.println("书单: 已清空");
}

int16_t DisplayLib::bookCount() {
    return books().count();
}

void DisplayLib::printBooks() {
    books().print();
}

// =============================================================================
// 书单归属：main.cpp 在 begin() 之前把自己那份 BookList 挂进来
//
// 为什么要挂载而不是各建一份：显示列表 / Flash 内容 / 机械位置计算必须看同一份数据，
// 不然会出现"屏上显示 3 本、业务认为 2 本"这种查不出来的问题。
// 本库只在没人挂载时（串口测试、单独编译本库）才启用自己的兜底数组。
// =============================================================================
void DisplayLib::attachBookList(BookList *booksIn) {
    // ⚠️ 传空指针直接返回：绝不能把已经挂好的那份弄丢（把显示切成空列表是最糟的失败方式）
    if (booksIn == nullptr) {
        Serial.println("显示库: attachBookList(nullptr) 被忽略，继续用当前书单");
        return;
    }
    _booksPtr = booksIn;
    _booksPtr->begin();     // begin() 是幂等的：已经建过就只补锁，不会清数据
    _booksDirty = true;     // 列表可能换了内容，下次刷新重建行控件
    _dirty = true;
    Serial.println("显示库: 已挂载外部书单（显示/业务共用同一份数据）");
}

BookList &DisplayLib::books() {
    if (_booksPtr != nullptr) {
        return *_booksPtr;
    }
    // 没人挂载：惰性启用兜底数组，保证本库单独跑（串口测试）也有书单可用
    if (!_booksOwnReady) {
        _booksOwn.begin();
        _booksOwnReady = true;
    }
    return _booksOwn;
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

// 上一页：和 nextPage 对称（触摸页上的“上一页”按钮用它）
void DisplayLib::prevPage() {
    if (_mutex == nullptr) {
        return;
    }
    if (xSemaphoreTake(_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        _page = (uint8_t)((_page + PAGE_COUNT - 1) % PAGE_COUNT);
        _dirty = true;
        _sigValid = false;
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
    Serial.println("  4 = 触摸测试页(按钮计数 + 坐标/原始值)，命令 t 直接跳过去");
    Serial.println("  5 = 电机调试页(升降/推书点动, 停止, 步长小/中/大, 设零点/保存/清空书单)");
    Serial.println("      调试页按钮只投命令, 真正动电机的是 stepperTask; 所有页面只能上下滑");
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

        // 调试页按钮投进来的"意图"在这里出队，交给 main.cpp 注册的 handler。
        // 放在刷屏之外做：handler 万一变慢，也只是晚一帧刷屏，不会卡在 LVGL 的调用链里。
        debugDispatch();

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

    if (req == 3) {
        // 触摸原始值采样（d,3）：不画屏，只记极值/打印
        runTouchSample();
        return;
    }

    if (req == 6) {
        // 触摸原始值直接打印（d,6）：不画屏
        runTouchDump();
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
// fullScreen=true -> fillScreen 240x320，false -> 顶部 240x40 一条
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
// 背光排错：直接开关 / 闪烁（串口命令 d,4，d,5 用）
// =============================================================================
void DisplayLib::backlightSet(bool on) {
    backlightWrite(on);
    _blOn = on;

#if CFG_TFT_BL_ACTIVE_LOW
    const char *levelText = on ? "LOW" : "HIGH";
#else
    const char *levelText = on ? "HIGH" : "LOW";
#endif

    Serial.printf("背光: GPIO%d 设为 %s (实际电平 %s, 方式 %s, CFG_TFT_BL_ACTIVE_LOW=%d)\n",
                  PIN_TFT_BL, on ? "开" : "关", levelText,
                  CFG_TFT_BL_USE_LEDC ? "LEDC(PWM)" : "digitalWrite",
                  CFG_TFT_BL_ACTIVE_LOW);
}

void DisplayLib::backlightBlink(uint8_t times, uint16_t onMs, uint16_t offMs) {
    Serial.printf("背光: 闪烁 %u 次（亮 %ums / 灭 %ums）—— 盯着屏看有没有跟着闪\n",
                  (unsigned)times, (unsigned)onMs, (unsigned)offMs);

    for (uint8_t i = 0; i < times; i++) {
        backlightWrite(false);
        vTaskDelay(pdMS_TO_TICKS(offMs));
        backlightWrite(true);
        vTaskDelay(pdMS_TO_TICKS(onMs));
    }
    _blOn = true;

    Serial.println("背光: 闪完了。");
    Serial.println("  会闪 -> GPIO47 和 LED 线是通的，背光不亮是别的原因（比如被盖住的画面）");
    Serial.println("  一点不闪 -> 量 GPIO47 对 GND 的电压：约3.3V=引脚在输出，查LED线/模块供电，0V=主控这边");
    Serial.println("  也可以敲 d,5 把电平对调（少数模块 LED 是低有效）");
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
    Serial.printf("  背光: GPIO%d, 方式=%s, 电平=%s, 当前=%s\n", PIN_TFT_BL,
                  CFG_TFT_BL_USE_LEDC ? "LEDC(PWM)" : "digitalWrite",
                  CFG_TFT_BL_ACTIVE_LOW ? "低电平点亮" : "高电平点亮",
                  _blOn ? "开" : "关");
    Serial.printf("  字节交换 CFG_LV_PUSH_SWAP=%d (配 lv_conf.h 的 LV_COLOR_16_SWAP=%d)\n",
                  CFG_LV_PUSH_SWAP, LV_COLOR_16_SWAP);
    Serial.printf("  中文字库: %s\n",
                  CFG_FONT_CUSTOM_CJK ? "ui_font_16(自建全字库)" : "LVGL 自带 simsun(缺字)");
#if CFG_RUN_TOUCH
    Serial.printf("  触摸: T_CS=%d T_IRQ=%d T_CLK=%d T_DIN=%d T_DO=%d, 轮询 %dms\n",
                  PIN_TOUCH_CS, PIN_TOUCH_IRQ, PIN_TOUCH_CLK, PIN_TOUCH_DIN,
                  PIN_TOUCH_DO, CFG_TOUCH_READ_MS);
    Serial.printf("        状态=%s 屏幕=%u,%u 原始=%u,%u 压力Z=%u\n",
                  s_touchPressed ? "按下" : "松开", (unsigned)s_touchX,
                  (unsigned)s_touchY, (unsigned)s_touchRawX,
                  (unsigned)s_touchRawY, (unsigned)s_touchZ);
    Serial.printf("        压力门限(灵敏度) %d：调大=要按更重，调小=更灵敏，0=不判\n",
                  CFG_TOUCH_PRESSURE_MIN);
    Serial.printf("        底部屏蔽 %d px（y >= %d 不响应）, 滑动限制 %d\n",
                  CFG_TOUCH_BOTTOM_MASK_PX, SCR_H - CFG_TOUCH_BOTTOM_MASK_PX,
                  CFG_TOUCH_SCROLL_LIMIT);
    Serial.printf("        按下 %lu 次, 测试按钮 %lu 次 | SWAP=%d INVERT_X=%d INVERT_Y=%d\n",
                  (unsigned long)s_touchPressCount, (unsigned long)s_btnClickCount,
                  CFG_TOUCH_SWAP_XY, CFG_TOUCH_INVERT_X, CFG_TOUCH_INVERT_Y);
    Serial.println("        一直显示 0,0 或从不按下 -> 先查 5 根线,再用 d,3 采样原始值");
#else
    Serial.println("  触摸: 未启用（config.h 第 3.2 节的 CFG_ENABLE_TOUCH / PIN_TOUCH_*）");
#endif
    Serial.printf("  排错模式: CFG_DISPLAY_BRINGUP=%d, 开机彩条=%dms\n",
                  CFG_DISPLAY_BRINGUP, CFG_TFT_SPLASH_MS);
    Serial.println("  判读: 刷屏次数一直是 0 -> LVGL 没在刷; 在涨但屏黑 -> 屏/背光/接线");
    Serial.println("  命令 d,1 = 画一次 TFT_eSPI 彩条; d,2 = 立刻测一次 SPI 总线速度");
}

// =============================================================================
// 内容签名：只取页面上真的会显示的值，内容没变就不动控件
// =============================================================================
// 字符串也进签名（"内容没变就不动控件"对字符串同样有效）：
// 只取前 32 字节，够区分"失败原因变了""书堆顺序变了"这种事，也不至于每轮算太久。
static uint32_t hashText(uint32_t sig, const char *text) {
    for (uint8_t i = 0; text[i] != '\0' && i < 32; i++) {
        sig = (sig ^ (uint8_t)text[i]) * 16777619UL;
    }
    return sig;
}

static uint32_t computeSignature(const HuskyFrame &f, const SystemInfo &info,
                                 const WaveState &wave, int16_t bookNum,
                                 uint32_t bookVersion, uint8_t page,
                                 const DebugState &dbg, bool clearArmed) {
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

    // 触摸状态也进签名：按下/移动/计数变化时触摸页要跟着刷。
    // 只在“按下”时用实时坐标参与签名——松手后坐标不再变，屏幕就不用一直重画了。
    sig = (sig ^ (uint32_t)(s_touchPressed ? 1 : 0)) * 16777619UL;
    if (s_touchPressed) {
        sig = (sig ^ (uint32_t)s_touchX) * 16777619UL;
        sig = (sig ^ (uint32_t)s_touchY) * 16777619UL;
        sig = (sig ^ (uint32_t)s_touchRawX) * 16777619UL;
        sig = (sig ^ (uint32_t)s_touchRawY) * 16777619UL;
        sig = (sig ^ (uint32_t)s_touchZ) * 16777619UL;
    }
    sig = (sig ^ s_btnClickCount) * 16777619UL;
    sig = (sig ^ s_touchPressCount) * 16777619UL;

    sig = (sig ^ info.ramUsedKb) * 16777619UL;
    sig = (sig ^ info.psramUsedKb) * 16777619UL;
    sig = (sig ^ info.flashUsedKb) * 16777619UL;
    sig = (sig ^ (info.uptimeSeconds / 10U)) * 16777619UL;

    // 书单：条数 + 版本号进签名（版本一变，书列表页的行控件就要重建）
    sig = (sig ^ (uint32_t)(bookNum & 0xFF)) * 16777619UL;
    sig = (sig ^ bookVersion) * 16777619UL;

    // 调试页：只把"屏幕上真的会变的东西"进签名（步数/在走/位置/测距/书单/点动步长…）。
    // 这样 stepperTask 一改快照，下一轮就重画；没变就不动控件，不会每 5ms 白刷一次。
    sig = (sig ^ (uint32_t)dbg.liftSteps) * 16777619UL;
    sig = (sig ^ (uint32_t)dbg.liftLeft) * 16777619UL;
    sig = (sig ^ (uint32_t)dbg.liftRange) * 16777619UL;
    sig = (sig ^ (uint32_t)(dbg.liftMoving ? 1 : 0)) * 16777619UL;
    sig = (sig ^ (uint32_t)dbg.pushSteps) * 16777619UL;
    sig = (sig ^ (uint32_t)(dbg.pushMoving ? 1 : 0)) * 16777619UL;
    sig = (sig ^ (uint32_t)(dbg.positionValid ? 1 : 0)) * 16777619UL;
    sig = (sig ^ (uint32_t)(dbg.needHoming ? 1 : 0)) * 16777619UL;
    sig = (sig ^ (uint32_t)dbg.jogSteps) * 16777619UL;
    sig = (sig ^ (uint32_t)(dbg.lastActionOk ? 1 : 0)) * 16777619UL;
    sig = (sig ^ (uint32_t)(dbg.flashSaved ? 1 : 0)) * 16777619UL;
    sig = (sig ^ (uint32_t)dbg.bookNum) * 16777619UL;
    sig = (sig ^ dbg.waveMm) * 16777619UL;
    sig = (sig ^ (uint32_t)(dbg.waveValid ? 1 : 0)) * 16777619UL;
    sig = (sig ^ (uint32_t)dbg.waveError) * 16777619UL;
    sig = hashText(sig, dbg.lastError);
    sig = hashText(sig, dbg.lastSavedInfo);
    sig = hashText(sig, dbg.bookOrder);
    sig = hashText(sig, dbg.note);
    // "清空书籍"按钮文字（上膛与否）也要进签名
    sig = (sig ^ (uint32_t)(clearArmed ? 1 : 0)) * 16777619UL;

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

    // 清空书单的二次确认超时必须在这里处理：下面内容没变时会直接 return，
    // 放在后面的话按钮文字就永远停在"再点一次确认清空"了。
    serviceDebugConfirm();

    // ---- 1) 加锁取快照，马上放锁 ----
    HuskyFrame frame;
    SystemInfo info;
    WaveState wave;
    uint8_t page = PAGE_STATUS;
    bool messageActive = false;
    char message[sizeof(_message)];
    uint32_t messageUntil = 0;
    DebugState dbg;          // 调试页快照（用独立的锁，下面单独取）

    memset(&frame, 0, sizeof(frame));
    memset(&info, 0, sizeof(info));
    memset(&wave, 0, sizeof(wave));
    memset(message, 0, sizeof(message));

    if (xSemaphoreTake(_mutex, pdMS_TO_TICKS(50)) != pdTRUE) {
        return;
    }
    frame = _frame;
    info = _info;
    wave = _wave;
    page = _page;
    messageActive = _messageActive;
    snprintf(message, sizeof(message), "%s", _message);
    messageUntil = _messageUntil;
    xSemaphoreGive(_mutex);

    // 书单是动态数组，在别的任务里增删：拷一份快照出来画，画的过程不占它的锁
    _bookSnapNum = books().snapshot(_bookSnap, CFG_BOOKLIST_SNAP_MAX);
    uint32_t bookVersion = books().version();
    int16_t bookTotal = books().count();   // 真实条数（可能比快照多，快照受 SNAP_MAX 限制）

    // 调试页数据也是"别的任务写、显示任务读"：用独立的锁拷一份，
    // 不和业务数据挤同一把锁（这一页 5ms 刷一轮，不能占着业务锁不放）。
    memset(&dbg, 0, sizeof(dbg));
    if (s_debugMutex != nullptr &&
        xSemaphoreTake(s_debugMutex, pdMS_TO_TICKS(20)) == pdTRUE) {
        dbg = s_debugState;
        xSemaphoreGive(s_debugMutex);
    }

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
    uint32_t sig = computeSignature(frame, info, wave, bookTotal, bookVersion, page,
                                    dbg, s_clearArmed);
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

    // 触摸测试页才显示按钮面板，其它页藏起来
    // （用 objSetHidden：状态没变就不动它，避免每轮刷新都让面板区域白重画一次）
    objSetHidden(s_touchPanel, page != PAGE_TOUCH);

    // 调试页：显示自己的竖直滚动容器（正文和按钮都在里面，只能上下滑）
    objSetHidden(s_debugPanel, page != PAGE_DEBUG);

    // 书列表页：显示可滚动的书单面板，隐藏通用正文标签（面板要占满整屏高度）
    objSetHidden(s_bookPanel, page != PAGE_BOOKS);
    objSetHidden(s_labelBody, page == PAGE_BOOKS || page == PAGE_DEBUG);

    // "调试"入口只在状态页/书列表页出现：其它页有自己的面板和按钮，别来抢触摸
    objSetHidden(s_btnDebugEnter, !(page == PAGE_STATUS || page == PAGE_BOOKS));

    switch (page) {
        case PAGE_SYSINFO:
            showSysInfoPage(info);
            break;
        case PAGE_WAVE:
            showWavePage(wave);
            break;
        case PAGE_BOOKS:
            showBooksPage();
            break;
        case PAGE_TOUCH:
            showTouchPage();
            break;
        case PAGE_DEBUG:
            showDebugPage(dbg);
            break;
        case PAGE_STATUS:
        default:
            showStatusPage(frame, bookTotal);
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
             "\n已记录书数: %d\n"
             "\n调试页: 点右下角按钮进入",
             f.online ? "在线" : "离线", modeName, (int)f.wordNum,
             (int)f.learnedNum, (unsigned long)f.frameCount,
             (unsigned long)f.errorCount, recent, (int)bookNum);

    lv_label_set_text(s_labelBody, body);
    objSetHidden(s_barValue, true);
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
    objSetHidden(s_barValue, false);
}

// =============================================================================
// 书列表页：一个可上下滚动的容器，里面每一行 = "序号. 书名 xN" + 一个"取出"按钮
//
// 数据来自 _bookSnap（update() 里从动态数组书单拷出来的快照），
// 这里只负责按快照把控件摆出来，增删都发生在 BookList 里。
// =============================================================================
void DisplayLib::createBookUi() {
    // 容器高度主动避开屏幕最下方 CFG_TOUCH_BOTTOM_MASK_PX 像素（那段不响应触摸），
    // 这样"取出"按钮永远不会被屏蔽区挡住。
    s_bookPanel = lv_obj_create(lv_scr_act());
    // 高度再减掉 DEBUG_BTN_RESERVE：给右下角"调试"入口让位，
    // 不然入口会压在最后几行的"取出"按钮上面，那边就点不着了。
    lv_obj_set_size(s_bookPanel, SCR_W - 8,
                    SCR_H - TITLE_H - CFG_TOUCH_BOTTOM_MASK_PX - 8 -
                        DEBUG_BTN_RESERVE);
    lv_obj_align(s_bookPanel, LV_ALIGN_TOP_MID, 0, TITLE_H + 4);
    lv_obj_set_style_radius(s_bookPanel, 6, 0);
    lv_obj_set_style_border_width(s_bookPanel, 0, 0);
    lv_obj_set_style_pad_all(s_bookPanel, 4, 0);
    lv_obj_set_style_pad_row(s_bookPanel, 4, 0);
    lv_obj_set_style_bg_color(s_bookPanel, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_bg_opa(s_bookPanel, LV_OPA_COVER, 0);

    // 竖着排，能上下滚：行控件本身禁止滚动，手指拖动才会交给容器（这点很重要）
    lv_obj_set_flex_flow(s_bookPanel, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scroll_dir(s_bookPanel, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(s_bookPanel, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_add_flag(s_bookPanel, LV_OBJ_FLAG_SCROLL_MOMENTUM);  // 松手带惯性

    lv_obj_add_flag(s_bookPanel, LV_OBJ_FLAG_HIDDEN);  // 默认隐藏，翻到第 3 页才显示
}

// 按快照重建所有行（只在列表版本变了，或刚翻到这一页时做）
// ⚠️ 只能在 update() 里调：这里会删掉旧的行控件，不能在按钮自己的事件回调里删它自己
void DisplayLib::rebuildBookRows() {
    if (s_bookPanel == nullptr) {
        return;
    }

    lv_obj_clean(s_bookPanel);  // 先全删（连同旧按钮），再按快照重建

    // 第一行是标题栏：条数 + 提示
    lv_obj_t *header = lv_label_create(s_bookPanel);
    lv_obj_set_style_text_font(header, DISPLAY_FONT_CJK, 0);
    lv_obj_set_style_text_color(header, lv_color_hex(0x303840), 0);
    int16_t total = books().count();   // 真实条数：快照最多 CFG_BOOKLIST_SNAP_MAX 条
    if (total == 0) {
        lv_label_set_text(header, "书单是空的（扫到书会自动进来）");
    } else if (total > _bookSnapNum) {
        lv_label_set_text_fmt(header, "共 %d 本（屏上只显示前 %d 本）", (int)total,
                              (int)_bookSnapNum);
    } else {
        lv_label_set_text_fmt(header, "共 %d 本（上下滑动看全部，点“取出”移出）",
                              (int)total);
    }

    for (int16_t i = 0; i < _bookSnapNum; i++) {
        // ---- 一行 ----
        lv_obj_t *row = lv_obj_create(s_bookPanel);
        lv_obj_set_size(row, LV_PCT(100), CFG_BOOK_ROW_H);
        lv_obj_set_style_radius(row, 4, 0);
        lv_obj_set_style_border_width(row, 0, 0);
        lv_obj_set_style_pad_all(row, 3, 0);
        lv_obj_set_style_pad_column(row, 4, 0);
        lv_obj_set_style_bg_color(row, lv_color_hex(0xEEF3F8), 0);
        lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
        lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);  // 手指拖动交给父容器滚动
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN,
                              LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

        // ---- 左边：序号 + 书名 + 次数 ----
        lv_obj_t *label = lv_label_create(row);
        lv_obj_set_style_text_font(label, DISPLAY_FONT_CJK, 0);
        lv_obj_set_style_text_color(label, lv_color_hex(COLOR_TEXT), 0);
        lv_obj_set_width(label, SCR_W - 8 - 4 - 62 - 10);   // 给"取出"按钮留位置
        lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);   // 太长结尾显示 ...
        const char *t = (_bookSnap[i].text[0] != '\0') ? _bookSnap[i].text
                                                      : "（无文字）";
        lv_label_set_text_fmt(label, "%d. %s x%u", (int)(i + 1), t,
                              (unsigned)_bookSnap[i].count);

        // ---- 右边：取出按钮（点一下把这条移出书单）----
        lv_obj_t *btn = lv_btn_create(row);
        lv_obj_set_size(btn, 58, CFG_BOOK_ROW_H - 8);
        lv_obj_set_style_radius(btn, 4, 0);
        lv_obj_set_style_bg_color(btn, lv_color_hex(0xE05050), 0);

        lv_obj_t *btnLabel = lv_label_create(btn);
        lv_obj_set_style_text_font(btnLabel, DISPLAY_FONT_CJK, 0);
        lv_obj_set_style_text_color(btnLabel, lv_color_white(), 0);
        lv_label_set_text(btnLabel, "取出");
        lv_obj_center(btnLabel);

#if CFG_RUN_TOUCH
        // 回调只带"第几条"，不带 BookItem 指针：列表每次变化都会重建控件，
        // 所以下标在两次点击之间不会失效（越界还有 removeAt 兜着）。
        lv_obj_add_event_cb(btn, onBtnTakeOut, LV_EVENT_CLICKED, (void *)(intptr_t)i);
#endif
    }

    _bookSnapVersion = books().version();
    _booksDirty = false;
}

// 书列表页刷新：列表版本变了（增/删/清空）才重建行控件
void DisplayLib::showBooksPage() {
    objSetHidden(s_barValue, true);

    if (s_bookPanel == nullptr) {
        return;
    }
    if (_booksDirty || _bookSnapVersion != books().version()) {
        rebuildBookRows();
    }
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
    objSetHidden(s_barValue, true);
}

void DisplayLib::showMessagePage(const char *text) {
    lv_label_set_text(s_msgLabel, text);
    lv_obj_clear_flag(s_msgPanel, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(s_msgPanel);
}

// =============================================================================
// 触摸测试页正文：只显示“是否就绪 + 当前坐标/原始值”，按钮在下面的面板里
// （正文控制在 5 行以内，免得和 y=172 开始的按钮面板叠在一起）
// =============================================================================
void DisplayLib::showTouchPage() {
    char body[CFG_STATUS_TEXT_LEN];

#if !CFG_ENABLE_TOUCH
    snprintf(body, sizeof(body),
             "触摸被 config.h 关掉了\n\n把 CFG_ENABLE_TOUCH 改成 1，\n"
             "并按第 3.2 节接好四根线。");
#elif !CFG_RUN_TOUCH
    snprintf(body, sizeof(body),
             "触摸引脚没配全\nCS=%d CLK=%d DIN=%d DO=%d\n"
             "见 config.h 第 3.2 节",
             PIN_TOUCH_CS, PIN_TOUCH_CLK, PIN_TOUCH_DIN, PIN_TOUCH_DO);
#else
    if (s_touchPressed) {
        snprintf(body, sizeof(body),
                 "触摸: 按下中\n屏幕: %u, %u\n原始: %u, %u   压力 Z=%u\n\n"
                 "Z 要 >= %d 才算按下（嫌灵敏就调大）",
                 (unsigned)s_touchX, (unsigned)s_touchY,
                 (unsigned)s_touchRawX, (unsigned)s_touchRawY,
                 (unsigned)s_touchZ, CFG_TOUCH_PRESSURE_MIN);
    } else {
        snprintf(body, sizeof(body),
                 "触摸: 松开\n屏幕: %u, %u   原始: %u, %u\n"
                 "按下 %lu 次   按钮点击 %lu 次\n"
                 "压力门限 %d（d,6 看实时 Z）",
                 (unsigned)s_touchX, (unsigned)s_touchY,
                 (unsigned)s_touchRawX, (unsigned)s_touchRawY,
                 (unsigned long)s_touchPressCount,
                 (unsigned long)s_btnClickCount, CFG_TOUCH_PRESSURE_MIN);
    }
#endif

    lv_label_set_text(s_labelBody, body);
    objSetHidden(s_barValue, true);
}

// =============================================================================
// 触摸原始值采样（串口命令 d,3）
// 第一次调用开始记极值，第二次调用打印结果 + 轴方向建议。
// 用途：确认四角都能读到，轴方向对不对，不用反复改代码烧录试。
// =============================================================================
void DisplayLib::requestTouchSample() {
    _testRequest = 3;
}

void DisplayLib::requestTouchDump() {
    _testRequest = 6;
}

// 打印触摸原始值（不做窗口过滤）：判断“芯片到底有没有回数据”
void DisplayLib::runTouchDump() {
#if !CFG_RUN_TOUCH
    Serial.println("触摸原始值: 触摸没启用（config.h 第 3.2 节）");
#else
    int irq = s_touch.irqLevel();
    Serial.printf("--- 触摸原始值 4 次采样 (T_IRQ=%s) ---\n",
                  (irq < 0) ? "未接" : (irq == 0 ? "LOW(有触摸)" : "HIGH(空闲)"));
    Serial.println("  按住屏幕不放，连着敲几次 d,6：T_IRQ 应该变 LOW，Z 应该变大");
    Serial.printf("  判断“算不算按下”的两个条件：坐标 %d~%d，压力 Z >= %d\n",
                  CFG_TOUCH_RAW_MIN, CFG_TOUCH_RAW_MAX, CFG_TOUCH_PRESSURE_MIN);
    for (int i = 0; i < 4; i++) {
        uint16_t rx = 0;
        uint16_t ry = 0;
        uint16_t rz = 0;
        s_touch.readRaw(&rx, &ry, &rz);
        int irqNow = s_touch.irqLevel();
        Serial.printf("  %d: 原始 %u, %u   压力 Z=%u   T_IRQ=%s%s\n", i + 1,
                      (unsigned)rx, (unsigned)ry, (unsigned)rz,
                      (irqNow < 0) ? "未接" : (irqNow == 0 ? "LOW" : "HIGH"),
                      (rz >= CFG_TOUCH_PRESSURE_MIN) ? "  <- 够重，会算按下" : "");
        vTaskDelay(pdMS_TO_TICKS(120));
    }
    Serial.println("  调灵敏度：把 CFG_TOUCH_PRESSURE_MIN 设成“轻碰的最大 Z”和“正常按的 Z”之间");
    Serial.println("    嫌灵敏 -> 调大，嫌迟钝 -> 调小，设 0 = 不判压力");
    Serial.printf("  有效窗口 %d~%d，当前方向 SWAP=%d INVERT_X=%d INVERT_Y=%d\n",
                  CFG_TOUCH_RAW_MIN, CFG_TOUCH_RAW_MAX, CFG_TOUCH_SWAP_XY,
                  CFG_TOUCH_INVERT_X, CFG_TOUCH_INVERT_Y);
    Serial.println("  判读：");
    Serial.println("    全是 4095 / 接近 4095 -> T_DO 悬空被上拉，芯片没回数据（查 T_DO 与 T_CS）");
    Serial.println("    全是 0               -> 数据线被拉低 / 芯片没响应（查接线与模块供电）");
    Serial.println("    手指按住时数值在变   -> 芯片在回数据，按不到就放宽有效窗口再看 d");
    Serial.println("    T_IRQ 按住时变 LOW   -> 触摸模块供电和 T_IRQ 接线正常（最省事的确认）");
#endif
}

void DisplayLib::runTouchSample() {
#if !CFG_RUN_TOUCH
    Serial.println("触摸采样: 触摸没启用（看 config.h 第 3.2 节的 CFG_ENABLE_TOUCH 和引脚）");
#else
    if (!s_touchSampling) {
        s_touchSampling = true;
        s_rawMinX = 0xFFFF;
        s_rawMaxX = 0;
        s_rawMinY = 0xFFFF;
        s_rawMaxY = 0;
        Serial.println("触摸采样: 开始。请沿屏幕四个角各按一下（越靠边越好），再敲一次 d,3 看结果");
        return;
    }

    s_touchSampling = false;
    if (s_rawMaxX == 0 && s_rawMaxY == 0) {
        Serial.println("触摸采样: 一个触点都没采到 —— 先查 T_CS=38 / T_CLK=40 / T_DIN=41 / T_DO=42 接线");
        return;
    }

    Serial.printf("触摸采样结果: X %u~%u   Y %u~%u\n", (unsigned)s_rawMinX,
                  (unsigned)s_rawMaxX, (unsigned)s_rawMinY,
                  (unsigned)s_rawMaxY);
    Serial.printf("  当前方向: SWAP_XY=%d INVERT_X=%d INVERT_Y=%d\n",
                  CFG_TOUCH_SWAP_XY, CFG_TOUCH_INVERT_X, CFG_TOUCH_INVERT_Y);

    if ((uint32_t)(s_rawMaxX - s_rawMinX) < 500 ||
        (uint32_t)(s_rawMaxY - s_rawMinY) < 500) {
        Serial.println("  ⚠️ 有轴的范围太小：可能那一轴接错脚，或手指没压到边");
    }
    Serial.println("  判读: 手指横向移动时要是 Y 变化大而 X 几乎不变 -> CFG_TOUCH_SWAP_XY 改 1");
    Serial.println("        左右反了改 CFG_TOUCH_INVERT_X，上下反了改 CFG_TOUCH_INVERT_Y");
#endif
}

// =============================================================================
// 调试页（第 6 页 PAGE_DEBUG）：数据接口 + 建界面 + 刷新
//
// 三份代码的分工（单向，谁都不越界）：
//   · stepperTask 等业务任务 -> setDebugState() 写"要显示的数字"（加锁，不画屏）
//   · main.cpp -> setDebugCmdHandler() 注册"收到调试命令以后干什么"
//   · 显示任务 -> showDebugPage() 只读快照画屏；按钮回调只往队列里投意图
// 本文件全程不 include StepperLib，也不碰任何电机引脚。
// =============================================================================

// -----------------------------------------------------------------------------
// 上屏前的字符清理：字库里没有的字符会画成空白或占位符
//
// src/ui_font_16.c 是 fontgen/gen_font.py 生成的，它给 lv_font_conv 的 --symbols
// 过滤掉了 " ' $ & ; < > | \` 这些 ASCII，GB2312 一级字里也不含顿号、弯引号、
// 破折号、省略号。本页这几段文字（失败原因/书堆顺序/备注/保存说明）都是别的任务填的，
// 我们管不了它们写什么，所以在显示这一层统一换成字库里有的字符：
// 宁可难看一点，也不能在屏上出现空白或占位符。
// （等 Lead 重新生成字库把 '>' 补上之后，这里的 '>' -> '/' 就可以删掉）
// -----------------------------------------------------------------------------
static void sanitizeForFont(const char *src, char *dst, size_t dstSize) {
    if (dst == nullptr || dstSize == 0) {
        return;
    }
    dst[0] = '\0';
    if (src == nullptr) {
        return;
    }

    size_t out = 0;
    for (size_t i = 0; src[i] != '\0' && out + 1 < dstSize; i++) {
        // 先看前三个字节，后面按 UTF-8 逐个判断
        uint8_t b0 = (uint8_t)src[i];
        uint8_t b1 = (uint8_t)src[i + 1];
        uint8_t b2 = (uint8_t)src[i + 2];

        // ASCII 里字库缺的 4 个字符：显示成 '/' 也别显示成空白
        if (b0 == '<' || b0 == '>' || b0 == '|' || b0 == '`') {
            dst[out++] = '/';
            dst[out] = '\0';
            continue;
        }
        if (b0 < 0x20) {   // 控制字符（含换行/制表）换成空格，别把一行标签顶乱
            dst[out++] = ' ';
            dst[out] = '\0';
            continue;
        }

        // 三字节 UTF-8：只处理"中文标点里字库确实缺的那几个"
        if (b0 == 0xE3 && b1 == 0x80 && b2 == 0x81) {                     // 顿号
            dst[out++] = ' ';
            dst[out] = '\0';
            i += 2;
            continue;
        }
        if (b0 == 0xE2 && b1 == 0x80 &&
            (b2 == 0x9C || b2 == 0x9D || b2 == 0x98 || b2 == 0x99)) {     // 弯引号
            i += 2;
            continue;
        }
        if (b0 == 0xE2 && b1 == 0x80 && b2 == 0xA6) {                     // 省略号
            if (out + 4 <= dstSize) {
                memcpy(dst + out, "。", 3);
                out += 3;
                dst[out] = '\0';
            }
            i += 2;
            continue;
        }
        if (b0 == 0xE2 && b1 == 0x80 && b2 == 0x94) {                     // 破折号
            dst[out++] = '-';
            dst[out] = '\0';
            i += 2;
            continue;
        }
        if (b0 == 0xE2 && b1 == 0x86 && b2 == 0x92) {                     // 右箭头
            dst[out++] = '/';
            dst[out] = '\0';
            i += 2;
            continue;
        }

        dst[out++] = src[i];
        dst[out] = '\0';
    }
}

// 调试页的一行按钮容器：宽度撑满，自己禁止滚动（手指拖动交给外层竖直容器）
static lv_obj_t *makeDebugRow(lv_obj_t *parent, int32_t height) {
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_set_size(row, LV_PCT(100), height);
    lv_obj_set_style_radius(row, 0, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(row, 0, 0);
    lv_obj_set_style_pad_column(row, 4, 0);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);   // 行不许滚，让父容器滚
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    return row;
}

// 调试页的按钮：中文字体、禁止自己滚动、底色由调用者给
static lv_obj_t *makeDebugButton(lv_obj_t *parent, const char *text, int32_t w,
                                 uint32_t colorHex) {
    lv_obj_t *btn = lv_btn_create(parent);
    lv_obj_set_size(btn, w, DEBUG_ROW_H - 4);
    lv_obj_set_style_bg_color(btn, lv_color_hex(colorHex), 0);
    lv_obj_set_style_radius(btn, 6, 0);
    lv_obj_clear_flag(btn, LV_OBJ_FLAG_SCROLLABLE);   // 拖动落在按钮上时交给外层滚动

    lv_obj_t *label = lv_label_create(btn);
    lv_obj_set_style_text_font(label, DISPLAY_FONT_CJK, 0);
    lv_obj_set_style_text_color(label, lv_color_white(), 0);
    lv_label_set_text(label, text);
    lv_obj_center(label);
    lv_obj_clear_flag(label, LV_OBJ_FLAG_SCROLLABLE);
    return btn;
}

// -----------------------------------------------------------------------------
// 外部写数据（只写数据，不画屏）
// -----------------------------------------------------------------------------
void setDebugState(const DebugState &st) {
    if (s_debugMutex == nullptr) {
        Serial.println("调试: 快照锁没建好，状态更新被丢弃");
        return;
    }
    if (xSemaphoreTake(s_debugMutex, pdMS_TO_TICKS(20)) != pdTRUE) {
        Serial.println("调试: 快照锁超时，状态更新被丢弃");
        return;
    }

    // 点动步长归"调试页的按钮"管：setDebugJogSteps() 是唯一入口，
    // 别的任务每轮刷状态时不能把它冲掉（否则用户刚选的步长下一轮就变回默认值）。
    // 所以拷完快照后，把 jogSteps 强制同步成内部权威值。
    s_debugState = st;
    s_debugState.jogSteps = debugJogStepsU16();
    xSemaphoreGive(s_debugMutex);
}

uint16_t debugJogSteps() {
    // jogSteps 是 uint16_t（lead 已确认改类型），10/100/500 都能原样返回
    return debugJogStepsU16();
}

void setDebugJogSteps(uint16_t steps) {
    // 只接受 CFG_DEBUG_JOG_SMALL/MID/LARGE 三个值，别的值在 debugApplyJogSteps() 里被拒
    debugApplyJogSteps((int16_t)steps);
}

void setDebugCmdHandler(void (*handler)(uint8_t cmd, int16_t arg)) {
    s_debugCmdHandler = handler;
    Serial.printf("调试: 动作处理函数 %s\n",
                  (handler != nullptr) ? "已注册" : "被清空");
}

// ---- 成员版转发（见 DisplayLib.h 的说明）----
// ⚠️ 必须写 ::setDebugXxx()，不然会解析成自己，变成无限递归。
void DisplayLib::setDebugCmdHandler(void (*handler)(uint8_t cmd, int16_t arg)) {
    ::setDebugCmdHandler(handler);
}

void DisplayLib::setDebugJogSteps(uint16_t steps) {
    ::setDebugJogSteps(steps);
}

// =============================================================================
// 调试页界面：一个"只能上下滑"的竖直滚动容器，里面 = 正文 + 一排排按钮
//
// 为什么整页都塞进滚动容器：这一页要显示的字多（升降/推书/位置/测距/书单/动作/Flash），
// 240x320 放不下，必然要滚。容器只给 LV_DIR_VER，
// 手指左右划不会有一丁点反应 —— 页面切换只允许按钮和串口命令。
// =============================================================================
void DisplayLib::createDebugUi() {
    s_debugPanel = lv_obj_create(lv_scr_act());
    // 高度主动避开屏幕最下方那段不响应触摸的屏蔽区（和书列表页同一个思路）
    lv_obj_set_size(s_debugPanel, SCR_W - 8, SCR_H - TITLE_H - DEBUG_BTN_BOTTOM - 6);
    lv_obj_align(s_debugPanel, LV_ALIGN_TOP_MID, 0, TITLE_H + 4);
    lv_obj_set_style_radius(s_debugPanel, 6, 0);
    lv_obj_set_style_border_width(s_debugPanel, 1, 0);
    lv_obj_set_style_border_color(s_debugPanel, lv_color_hex(0x90A0B0), 0);
    lv_obj_set_style_pad_all(s_debugPanel, 4, 0);
    lv_obj_set_style_pad_row(s_debugPanel, 4, 0);
    lv_obj_set_style_bg_color(s_debugPanel, lv_color_hex(0xF7F8FA), 0);
    lv_obj_set_style_bg_opa(s_debugPanel, LV_OPA_COVER, 0);

    // 只允许竖直滚动（硬性要求：全工程禁止左右滑动）
    lv_obj_set_scroll_dir(s_debugPanel, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(s_debugPanel, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_add_flag(s_debugPanel, LV_OBJ_FLAG_SCROLL_MOMENTUM);
    lv_obj_set_flex_flow(s_debugPanel, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_debugPanel, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_START);
    lv_obj_add_flag(s_debugPanel, LV_OBJ_FLAG_HIDDEN);   // 默认隐藏，翻到第 6 页才显示

    // ---- 正文：中文多行，跟着容器一起滚 ----
    s_debugText = lv_label_create(s_debugPanel);
    lv_obj_set_style_text_font(s_debugText, DISPLAY_FONT_CJK, 0);
    lv_obj_set_style_text_color(s_debugText, lv_color_hex(COLOR_TEXT), 0);
    lv_label_set_long_mode(s_debugText, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(s_debugText, LV_PCT(100));
    lv_obj_clear_flag(s_debugText, LV_OBJ_FLAG_SCROLLABLE);
    lv_label_set_text(s_debugText, "调试页启动中...");

    // ---- 按钮：宽度按"一行放几个"算出来，全部禁止自己滚动 ----
    const int32_t wFull = SCR_W - 8 - 8;       // 容器内宽（左右各 4 的内边距）
    const int32_t wHalf = (wFull - 4) / 2;
    const int32_t wThird = (wFull - 8) / 3;

    // 升降 / 推书：两个方向各一行，蓝色
    lv_obj_t *rowLift = makeDebugRow(s_debugPanel, DEBUG_ROW_H);
    lv_obj_t *btnLiftUp = makeDebugButton(rowLift, "升降上移", wHalf, 0x0088FF);
    lv_obj_t *btnLiftDown = makeDebugButton(rowLift, "升降下移", wHalf, 0x0088FF);

    lv_obj_t *rowPush = makeDebugRow(s_debugPanel, DEBUG_ROW_H);
    lv_obj_t *btnPushFwd = makeDebugButton(rowPush, "推书前进", wHalf, 0x0088FF);
    lv_obj_t *btnPushBack = makeDebugButton(rowPush, "推书后退", wHalf, 0x0088FF);

    // 急停：单独一行占满宽度 + 红色，出事时不用在按钮堆里找
    lv_obj_t *rowStop = makeDebugRow(s_debugPanel, DEBUG_ROW_H);
    lv_obj_t *btnStop = makeDebugButton(rowStop, "停止", wFull, 0xD02020);

    // 点动步长三档：当前档的颜色在 showDebugPage() 里刷成橙色
    lv_obj_t *rowJog = makeDebugRow(s_debugPanel, DEBUG_ROW_H);
    s_jogBtn[0] = makeDebugButton(rowJog, "小10步", wThird, 0x607080);
    s_jogBtn[1] = makeDebugButton(rowJog, "中100步", wThird, 0x607080);
    s_jogBtn[2] = makeDebugButton(rowJog, "大500步", wThird, 0x607080);

    // 零点 / 保存
    lv_obj_t *rowZero = makeDebugRow(s_debugPanel, DEBUG_ROW_H);
    lv_obj_t *btnSetZero = makeDebugButton(rowZero, "设为零点", wHalf, 0x109050);
    lv_obj_t *btnSaveNow = makeDebugButton(rowZero, "保存位置", wHalf, 0x109050);

    // 清空书籍数据（破坏性操作，必须二次确认，见 onBtnDebugClearBooks）
    lv_obj_t *rowClear = makeDebugRow(s_debugPanel, DEBUG_ROW_H);
    s_btnClearBooks = makeDebugButton(rowClear, "清空书籍", wFull, 0xD02020);

    // 返回状态页
    lv_obj_t *rowBack = makeDebugRow(s_debugPanel, DEBUG_ROW_H);
    lv_obj_t *btnBack = makeDebugButton(rowBack, "返回", wFull, 0x0070C0);

    // 事件回调：全部只"投意图"（入队 / 改点动步长），一行 GPIO 都不碰
    lv_obj_add_event_cb(btnLiftUp, onBtnDebugLiftUp, LV_EVENT_CLICKED, nullptr);
    lv_obj_add_event_cb(btnLiftDown, onBtnDebugLiftDown, LV_EVENT_CLICKED, nullptr);
    lv_obj_add_event_cb(btnPushFwd, onBtnDebugPushForward, LV_EVENT_CLICKED, nullptr);
    lv_obj_add_event_cb(btnPushBack, onBtnDebugPushBack, LV_EVENT_CLICKED, nullptr);
    lv_obj_add_event_cb(btnStop, onBtnDebugStop, LV_EVENT_CLICKED, nullptr);
    lv_obj_add_event_cb(s_jogBtn[0], onBtnDebugJogSmall, LV_EVENT_CLICKED, nullptr);
    lv_obj_add_event_cb(s_jogBtn[1], onBtnDebugJogMid, LV_EVENT_CLICKED, nullptr);
    lv_obj_add_event_cb(s_jogBtn[2], onBtnDebugJogLarge, LV_EVENT_CLICKED, nullptr);
    lv_obj_add_event_cb(btnSetZero, onBtnDebugSetZero, LV_EVENT_CLICKED, nullptr);
    lv_obj_add_event_cb(btnSaveNow, onBtnDebugSaveNow, LV_EVENT_CLICKED, nullptr);
    lv_obj_add_event_cb(s_btnClearBooks, onBtnDebugClearBooks, LV_EVENT_CLICKED, nullptr);
    lv_obj_add_event_cb(btnBack, onBtnDebugBack, LV_EVENT_CLICKED, nullptr);

    // ---- 状态页/书列表页右下角的"调试"入口 ----
    // 位置固定：底边躲开最下方那段不响应触摸的屏蔽区；书列表容器也已经让出了这段高度，
    // 所以它不会压住每行右边的"取出"按钮。只在状态页/书列表页显示（update() 里控制）。
    s_btnDebugEnter = lv_btn_create(lv_scr_act());
    lv_obj_set_size(s_btnDebugEnter, DEBUG_BTN_W, DEBUG_BTN_H);
    lv_obj_align(s_btnDebugEnter, LV_ALIGN_BOTTOM_RIGHT, -6, -(DEBUG_BTN_BOTTOM));
    lv_obj_set_style_radius(s_btnDebugEnter, 6, 0);
    lv_obj_set_style_bg_color(s_btnDebugEnter, lv_color_hex(0x0070C0), 0);
    lv_obj_clear_flag(s_btnDebugEnter, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *enterLabel = lv_label_create(s_btnDebugEnter);
    lv_obj_set_style_text_font(enterLabel, DISPLAY_FONT_CJK, 0);
    lv_obj_set_style_text_color(enterLabel, lv_color_white(), 0);
    lv_label_set_text(enterLabel, "调试");
    lv_obj_center(enterLabel);
    lv_obj_clear_flag(enterLabel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(s_btnDebugEnter, onBtnEnterDebug, LV_EVENT_CLICKED, nullptr);

    // 两个入口都默认藏起来，update() 里按当前页决定显示谁
    objSetHidden(s_debugPanel, true);
    objSetHidden(s_btnDebugEnter, true);

    Serial.println("调试: 调试页(第 6 页)已建好, 入口在状态页/书列表页右下角");
}

// =============================================================================
// 调试页刷新：只吃 DebugState 快照，不读成员业务数据
// （和别的页面一样：update() 里加锁取快照，锁外画屏）
// =============================================================================
void DisplayLib::showDebugPage(const DebugState &st) {
    char body[CFG_STATUS_TEXT_LEN];
    char order[96];       // 和 DebugState::bookOrder 同宽
    char reason[64];      // 和 DebugState::lastError 同宽
    char savedInfo[32];   // 和 DebugState::lastSavedInfo 同宽
    char note[64];        // 和 DebugState::note 同宽

    // 这几段文字是别的任务填的，上屏前统一把字库里没有的字符换掉（原因见 sanitizeForFont）
    sanitizeForFont(st.bookOrder, order, sizeof(order));
    sanitizeForFont(st.lastError, reason, sizeof(reason));
    sanitizeForFont(st.lastSavedInfo, savedInfo, sizeof(savedInfo));
    sanitizeForFont(st.note, note, sizeof(note));
    if (order[0] == '\0') {
        snprintf(order, sizeof(order), "空");
    }
    if (reason[0] == '\0') {
        snprintf(reason, sizeof(reason), "无");
    }
    if (savedInfo[0] == '\0') {
        snprintf(savedInfo, sizeof(savedInfo), "无");
    }
    if (note[0] == '\0') {
        snprintf(note, sizeof(note), "无");
    }

    // 一行一个信息，顺序按"调试时最想先看什么"排：位置/归零排在最前面，出事第一眼能看到
    snprintf(body, sizeof(body),
             "调试页 第6页  点动 %d 步/次\n"
             "位置: %s%s\n"
             "升降: 累计 %ld 步\n"
             "  剩余 %ld 步  软限位 %ld\n"
             "  状态 %s\n"
             "推书: 本次 %ld 步  状态 %s\n"
             "测距: %lu mm  %s  错误码 %u\n"
             "书单: %u 条\n"
             "顺序: %s\n"
             "动作: %s\n"
             "原因: %s\n"
             "Flash: %s (%s)\n"
             "备注: %s",
             (unsigned)st.jogSteps,
             st.positionValid ? "有效" : "无效",
             st.needHoming ? "  *需要人工归零*" : "",
             (long)st.liftSteps,
             (long)st.liftLeft, (long)st.liftRange,
             st.liftMoving ? "在走" : "停",
             (long)st.pushSteps, st.pushMoving ? "在走" : "停",
             (unsigned long)st.waveMm, st.waveValid ? "有效" : "无效",
             (unsigned)st.waveError,
             (unsigned)st.bookNum,
             order,
             st.lastActionOk ? "上次正常结束" : "上次失败",
             reason,
             st.flashSaved ? "已保存" : "未保存", savedInfo,
             note);

    if (s_debugText != nullptr) {
        lv_label_set_text(s_debugText, body);
    }
    objSetHidden(s_barValue, true);

    // 步长按钮高亮：当前档橙色，其余灰，一眼就知道下一脚走多远
    for (uint8_t i = 0; i < 3; i++) {
        if (s_jogBtn[i] == nullptr) {
            continue;
        }
        bool active = ((i == 0) && (st.jogSteps == CFG_DEBUG_JOG_SMALL)) ||
                      ((i == 1) && (st.jogSteps == CFG_DEBUG_JOG_MID)) ||
                      ((i == 2) && (st.jogSteps == CFG_DEBUG_JOG_LARGE));
        lv_obj_set_style_bg_color(s_jogBtn[i],
                                  lv_color_hex(active ? 0xE08000 : 0x607080), 0);
    }

    // 清空书籍按钮的文字跟着"是否已上膛"走（serviceDebugConfirm() 超时后会改回来）
    if (s_btnClearBooks != nullptr) {
        lv_obj_t *label = lv_obj_get_child(s_btnClearBooks, 0);
        if (label != nullptr) {
            lv_label_set_text(label, s_clearArmed ? "再点一次确认清空" : "清空书籍");
        }
    }
}

// =============================================================================
// 清空书单的二次确认超时回收
//
// 不在事件回调里做计时（回调只投意图）：每轮刷新前查一眼，
// 超时就把按钮文字改回"清空书籍"，下次点又要点两下。
// =============================================================================
void DisplayLib::serviceDebugConfirm() {
    if (!s_clearArmed) {
        return;
    }
    if ((int32_t)(millis() - s_clearArmUntil) < 0) {
        return;
    }
    debugClearArm(false);
    Serial.println("调试: 清空书籍的确认超时，已自动取消");
}

