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
#define PAGE_COUNT   5

// 屏幕分辨率：从 TFT_eSPI 的宏推导（本头文件已在上面 include 过 <TFT_eSPI.h>）。
// 业务代码算坐标一律用 SCR_W/SCR_H，不要写死 240/320。
// MAX_BOOK_RECORDS、字体宏、队列与任务参数都在 include/config.h 里，改配置请改那个文件。
#ifndef SCR_W
#define SCR_W TFT_WIDTH
#endif
#ifndef SCR_H
#define SCR_H TFT_HEIGHT
#endif

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
    BookList _books;
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
