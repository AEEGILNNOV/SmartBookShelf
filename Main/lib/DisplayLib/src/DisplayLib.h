#ifndef DISPLAY_LIB_H
#define DISPLAY_LIB_H

#include <Arduino.h>
#include <TFT_eSPI.h>
#include <lvgl.h>

#include "BoardConfig.h"
#include "HuskyLib.h"
#include "SysInfoLib.h"
#include "UltraLib.h"
#include "BookList.h"

// =============================================================================
// DisplayLib —— 显示库（2.8" SPI ILI9341 240x320 竖屏 + LVGL 8.4 中文界面）
//
// 职责：
//   1) 屏幕初始化（TFT_eSPI）+ LVGL 初始化（draw buffer 放 PSRAM）；
//   2) 三种页面：状态页 / 系统信息页 / 已识别书列表页；消息页覆盖整屏；
//   3) 自己建一个 displayTask，按“脏标志 + 内容签名”刷新，不阻塞、不刷屏。
//
// 中文显示：LVGL 用 include/lv_conf.h 里的 LV_FONT_SIMSUN_16_CJK=1 显示中文，
//           英文数字用 montserrat_12。（TFT_eSPI 内置字体只有 ASCII，
//           所以屏上中文必须走 LVGL 这条路。）
//
// 线程安全（重要）：
//   - LVGL 不是线程安全的，所有 lv_* 调用都只在 displayTask 里做；
//   - 其它任务只能通过 setFrame()/setSystemInfo()/countBook()/clearBooks()/
//     showMessage()/setPage() 写内部快照（互斥锁 + 脏标志），
//     绝对不能直接碰 LVGL 控件。
// =============================================================================

// 页面
#define PAGE_STATUS  0x00   // 状态页：连接/算法/识别块/已记书数
#define PAGE_SYSINFO 0x01   // 系统信息页：RAM/PSRAM/Flash/运行时间
#define PAGE_BOOKS   0x02   // 已识别书列表页
#define PAGE_WAVE    0x03   // 超声波测距页（HC-SR04）
#define PAGE_TOUCH   0x04   // 触摸测试页（按钮 + 坐标/原始值）
#define PAGE_DEBUG   0x05   // 电机/机械调试页（点动/急停/归零/保存/清空书单）
#define PAGE_COUNT   6      // 页面总数：翻页取模、setPage() 上界、标题栏 n/N 都看它

// 屏幕分辨率：从 TFT_eSPI 的宏推导（本头文件已在上面 include 过 <TFT_eSPI.h>）。
// 业务代码算坐标一律用 SCR_W/SCR_H，不要写死 240/320。
// MAX_BOOK_RECORDS、字体宏、队列与任务参数都在 include/config.h 里，改配置请改那个文件。
#ifndef SCR_W
#define SCR_W TFT_WIDTH
#endif
#ifndef SCR_H
#define SCR_H TFT_HEIGHT
#endif

// =============================================================================
// 调试页（第 6 页 PAGE_DEBUG）：电机 / 机械调试
//
// 为什么要有这一页：手动调机械时最怕两件事——"点一下按钮把机构撞坏"和"界面把业务卡住"。
// 所以这里把两条规矩写死：
//   1) 屏幕上的按钮**只收集意图**：LVGL 事件回调只往一个内部小队列投一条 {cmd,arg}
//      就立刻返回（几微秒），绝不在这里碰 GPIO、绝不 include StepperLib、绝不做阻塞动作。
//      真正干活的只有 stepperTask（全工程唯一碰电机引脚的任务），
//      它用 setDebugCmdHandler() 注册 handler，收到命令后往 MotorQueue 投一条消息。
//   2) 数据方向相反：stepperTask 把"要显示的数字"用 setDebugState() 写进 DebugState 快照，
//      显示任务只读这份快照画屏（LVGL 只有显示任务能碰，所以这里必须是纯数据）。
// =============================================================================

// 调试页发出来的请求：DisplayLib 只负责"收集意图"，真正执行在 stepperTask（唯一碰电机引脚的任务）
enum DisplayDebugCmd : uint8_t {
    DEBUG_CMD_NONE = 0,
    DEBUG_CMD_LIFT_UP,        // 升降上移点动
    DEBUG_CMD_LIFT_DOWN,      // 升降下移点动
    DEBUG_CMD_PUSH_FORWARD,   // 推书前进点动
    DEBUG_CMD_PUSH_BACK,      // 推书后退点动
    DEBUG_CMD_STOP_ALL,       // 急停
    DEBUG_CMD_SET_ZERO,       // 把当前位置设为零点（人工归零）
    DEBUG_CMD_SAVE_NOW,       // 保存当前位置到 Flash
    DEBUG_CMD_CLEAR_BOOKS     // 清空书籍数据（界面已做二次确认）
};

// 内部队列里的元素：一条"调试意图"。cmd 取上面的枚举，arg 是参数（点动时 = 本次走多少步）
struct DisplayDebugMsg {
    uint8_t cmd;
    int16_t arg;
};

// 调试页要显示的东西：别的任务算好加锁写进来，显示任务只读快照（LVGL 只有显示任务能碰）
struct DebugState {
    int32_t  liftSteps;         // 升降累计步数
    int32_t  liftLeft;          // 升降剩余步数
    bool     liftMoving;        // 升降在走
    int32_t  liftRange;         // 升降软限位(步)
    int32_t  pushSteps;         // 推书最近一次动作步数
    bool     pushMoving;        // 推书在走
    bool     positionValid;     // 位置是否有效
    bool     needHoming;        // 是否需要人工归零
    uint16_t jogSteps;          // 当前点动步长（10/100/500：500 装不进 uint8_t，所以是 uint16_t）
    bool     lastActionOk;      // 上一次动作是否正常完成
    char     lastError[64];     // 上一次失败原因（中文）
    bool     flashSaved;        // Flash 是否已保存
    char     lastSavedInfo[32]; // 上次保存说明
    uint16_t bookNum;           // 书单条数
    char     bookOrder[96];     // 逻辑书堆顺序，形如 "[语文] > 数学"
    uint32_t waveMm;            // 最近测距 mm
    bool     waveValid;
    uint8_t  waveError;
    char     note[64];          // 额外提示（例如"该学科已存在，未入库"）
};

// 其它任务写调试页快照（内部加锁，只写数据不画屏）
void setDebugState(const DebugState &st);
// 读当前点动步长 / 设置点动步长（只接受 CFG_DEBUG_JOG_SMALL/MID/LARGE，别的值一律拒绝）
// ⚠️ 用 uint16_t 而不是 uint8_t：CFG_DEBUG_JOG_LARGE = 500 装不进 uint8_t（会截断成 244）
uint16_t debugJogSteps();
void setDebugJogSteps(uint16_t steps);
// 注册"调试命令"处理函数：main.cpp 注册，handler 里只做"往 MotorQueue 投一条消息"
void setDebugCmdHandler(void (*handler)(uint8_t cmd, int16_t arg));

// 已识别书的累计记录：数据放在 BookListLib 的动态数组里（见 lib/BookListLib），
// 这里不再用固定数组。显示时靠 BookList::snapshot() 拷一份快照出来画。

class DisplayLib {
public:
    DisplayLib();

    // 初始化屏幕 + LVGL + 界面，并建好 displayTask。返回 false 表示初始化失败。
    bool begin();

    // ---- 外部任务写数据（内部加锁，只写数据不画屏）----
    void setFrame(const HuskyFrame &frame);
    void setSystemInfo(const SystemInfo &info);
    void setWave(const WaveState &wave);      // 超声波测距快照
    // 累计一本识别到的书（按 ID/文字去重，进动态数组书单）。别的任务可调。
    void countBook(const HuskyWord &word);

    // ---- 书单（动态数组）----
    //
    // ⚠️ 书单的**唯一所有者是 main.cpp**（它建 static BookList，并在 begin() 之前挂进来）。
    //    这样"显示列表 / Flash 内容 / 机械位置计算"用的是同一份数据，不会出现
    //    "显示一份、业务一份"的不一致。本库不再自己拥有数组，只借用别人的指针；
    //    没人挂载时（串口测试、没接 main.cpp）本库自己惰性建一个兜底数组。
    void attachBookList(BookList *books);
    // 取当前书单（main.cpp 做业务判断时也用它）
    BookList &books();

    // 直接加一条（串口测试用，不去重），返回下标
    int16_t addBook(const char *text, int16_t id = 0);
    // 取出第 index 条（书列表页的"取出"按钮调它）：删掉并从列表移除
    int16_t removeBook(int16_t index);
    // 清空书单
    void clearBooks();
    int16_t bookCount();
    // 串口打印整张书单
    void printBooks();

    // 弹一条消息（覆盖整屏），ms 毫秒后自动回到正常页面。中文直接写，UTF-8。
    void showMessage(const char *text, uint32_t ms = 3000);

    // 翻页 / 指定页
    void setPage(uint8_t page);
    void nextPage();
    void prevPage();
    uint8_t page() const;

    // ---- 屏幕排错（串口命令 d）----
    // 打印诊断：刷屏次数 / LVGL 时钟 / 显示任务栈余量 / 缓冲位置 / 各排错开关
    void printDiag();
    // 请求一次“纯 TFT_eSPI 彩条”自检：由 displayTask 执行（LVGL 只能一个任务碰），
    // 停留 holdMs 毫秒后自动让 LVGL 整屏重画，画面自己回来。
    void requestRawTest(uint32_t holdMs = CFG_SELFTEST_BAR_HOLD_MS);
    // 请求一次 SPI 总线测速（由 displayTask 执行）：量“MCU 到底有没有在往屏上吐像素”
    void requestBusTest();
    // 请求一次触摸原始值采样（d,3）：在屏四角各按一下，再敲一次 d,3 会打印极值与轴方向建议
    void requestTouchSample();
    // 请求打印触摸原始值（d,6）：不做窗口过滤，直接看芯片回的是 0 / 4095 / 真实值
    void requestTouchDump();

    // ---- 背光排错（d,4 / d,5）----
    // 直接开关背光。背光不亮时先用它确认“引脚到底能不能点亮 LED”。
    void backlightSet(bool on);
    // 背光闪烁 times 次（看灯跟不跟着闪）
    void backlightBlink(uint8_t times, uint16_t onMs = 250, uint16_t offMs = 250);
    // 目前记录的状态：true = 背光逻辑上开着
    bool backlightIsOn() const { return _blOn; }

    // ---- 调试页接口的"成员版"转发 ----
    // 调试页的对外接口按设计是全局函数（见本文件顶部），但 main.cpp 里习惯写
    //   display.setDebugCmdHandler(...) / display.setDebugJogSteps(...)
    // 这里给两个薄转发，让"成员写法"和"全局写法"都能编译；签名/语义与全局版完全一致。
    void setDebugCmdHandler(void (*handler)(uint8_t cmd, int16_t arg));
    void setDebugJogSteps(uint16_t steps);

    // 串口帮助里打印页说明
    static void printHelp();

private:
    void taskLoop();
    static void displayTaskEntry(void *pvParameters);

    // 内部刷新：加锁取快照，锁外更新 LVGL 控件
    void update();

    // 建界面（只在 begin() 里调一次）
    void createUi();

    // 纯 TFT_eSPI 彩条（不经过 LVGL）：用来证明“接线/供电/背光/SPI/驱动型号”是好的
    void drawRawBars(const char *tag);

    // SPI 总线测速：往屏上写一块像素并计时，返回耗时(us)
    // fullScreen = true 写 240x320（fillScreen），false 只写 240x40 一条
    uint32_t busWriteTest(bool fullScreen);

    // 处理自检请求 + 执行（都在 displayTask 里跑）
    void handleSelfTest();
    void runRawTest(uint32_t holdMs);
    void restoreScreenAfterTest();
    void runTouchSample();
    void runTouchDump();

    // 触摸测试页：建按钮面板 + 刷新页面文字
    void createTouchUi();
    void showTouchPage();

    // 书列表页：建滚动容器 + 按快照重建每一行（行里带"取出"按钮）
    void createBookUi();
    void showBooksPage();
    void rebuildBookRows();

    // 调试页（第 6 页）：建竖直滚动容器 + 按钮面板，并按快照刷新正文
    void createDebugUi();
    void showDebugPage(const DebugState &st);
    // 清空书单"二次确认"的超时回收：每轮刷新都要看一眼，超时就把按钮文字改回去
    void serviceDebugConfirm();

#if CFG_DISPLAY_BRINGUP == 2
    // 排错模式 2 的纯色块页面（一个字体都不碰）
    void createColorTestUi();
#endif

    // 三个页面 + 消息页：都只吃快照数据，不读成员变量
    void showStatusPage(const HuskyFrame &f, int16_t bookNum);
    void showSysInfoPage(const SystemInfo &info);
    void showWavePage(const WaveState &wave);
    void showMessagePage(const char *text);

    TFT_eSPI _tft;
    SemaphoreHandle_t _mutex;      // 数据锁：保护下面这些快照
    TaskHandle_t _taskHandle;      // 显示任务句柄（排错打印栈余量用）

    HuskyFrame _frame;
    SystemInfo _info;
    WaveState _wave;

    // ---- 书单：动态数组（BookList）+ 给显示用的快照 ----
    // 动态数组在别的任务里增删，所以画屏前先 snapshot() 到下面这块常驻内存里，
    // 画的过程中不占 BookList 的锁（CFG_BOOKLIST_SNAP_MAX x 约 44 字节）。
    // _booksPtr 指向"真正在用的那一份"（正常固件里是 main.cpp 挂进来的），
    // _booksOwn 是没人挂载时的兜底数组；两个都没挂时 books() 会用 _booksOwn。
    BookList *_booksPtr;
    BookList _booksOwn;
    bool      _booksOwnReady;       // 兜底数组是否已经 begin() 过

    BookItem _bookSnap[CFG_BOOKLIST_SNAP_MAX];
    int16_t _bookSnapNum;
    uint32_t _bookSnapVersion;      // 上次重建行控件时的列表版本
    volatile bool _booksDirty;      // 列表变了 -> 下次刷新要重建行控件

    uint8_t _page;
    bool _dirty;
    char _message[CFG_MESSAGE_LEN];  // UTF-8 中文一个字 3 字节，留够一条整句
    uint32_t _messageUntil;
    bool _messageActive;
    uint32_t _lastSig;
    bool _sigValid;

    // 自检请求：别的任务只写，displayTask 读并清零（单向标志，不需要加锁）
    //   0 = 无请求   1 = 彩条自检(_rawTestHoldMs)   2 = SPI 总线测速   3 = 触摸原始值采样
    volatile uint8_t _testRequest;
    uint32_t _rawTestHoldMs;
    bool _blOn;   // 背光当前状态（逻辑上的开/关，实际电平可能被 ACTIVE_LOW 翻过）
};

const char *displayLibVersion();

#endif
