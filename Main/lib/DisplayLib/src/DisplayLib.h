#ifndef DISPLAY_LIB_H
#define DISPLAY_LIB_H

#include <Arduino.h>
#include <TFT_eSPI.h>
#include <lvgl.h>

#include "BoardConfig.h"
#include "HuskyLib.h"
#include "SysInfoLib.h"
#include "UltraLib.h"

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
#define PAGE_COUNT   4

// 屏幕分辨率：从 TFT_eSPI 的宏推导（本头文件已在上面 include 过 <TFT_eSPI.h>）。
// 业务代码算坐标一律用 SCR_W/SCR_H，不要写死 240/320。
// MAX_BOOK_RECORDS、字体宏、队列与任务参数都在 include/config.h 里，改配置请改那个文件。
#ifndef SCR_W
#define SCR_W TFT_WIDTH
#endif
#ifndef SCR_H
#define SCR_H TFT_HEIGHT
#endif

// 已识别书的累计记录（按 ID + 内容去重，用于“扫多本书”）
struct BookRecord {
    int16_t id;
    uint16_t count;                        // 出现次数
    char text[HUSKY_TEXT_LEN];             // 书名/文字内容，可能为空
};

class DisplayLib {
public:
    DisplayLib();

    // 初始化屏幕 + LVGL + 界面，并建好 displayTask。返回 false 表示初始化失败。
    bool begin();

    // ---- 外部任务写数据（内部加锁，只写数据不画屏）----
    void setFrame(const HuskyFrame &frame);
    void setSystemInfo(const SystemInfo &info);
    void setWave(const WaveState &wave);      // 超声波测距快照
    void countBook(const HuskyWord &word);   // 累计一本识别到的书
    void clearBooks();
    int16_t bookCount() const;

    // 弹一条消息（覆盖整屏），ms 毫秒后自动回到正常页面。中文直接写，UTF-8。
    void showMessage(const char *text, uint32_t ms = 3000);

    // 翻页 / 指定页
    void setPage(uint8_t page);
    void nextPage();
    uint8_t page() const;

    // 串口帮助里打印页说明
    static void printHelp();

private:
    void taskLoop();
    static void displayTaskEntry(void *pvParameters);

    // 内部刷新：加锁取快照，锁外更新 LVGL 控件
    void update();

    // 建界面（只在 begin() 里调一次）
    void createUi();

    // 三个页面 + 消息页：都只吃快照数据，不读成员变量
    void showStatusPage(const HuskyFrame &f, int16_t bookNum);
    void showSysInfoPage(const SystemInfo &info);
    void showWavePage(const WaveState &wave);
    void showBooksPage(const BookRecord *books, int16_t bookNum);
    void showMessagePage(const char *text);

    TFT_eSPI _tft;
    SemaphoreHandle_t _mutex;

    HuskyFrame _frame;
    SystemInfo _info;
    WaveState _wave;
    BookRecord _books[MAX_BOOK_RECORDS];
    int16_t _bookNum;

    uint8_t _page;
    bool _dirty;
    char _message[CFG_MESSAGE_LEN];  // UTF-8 中文一个字 3 字节，留够一条整句
    uint32_t _messageUntil;
    bool _messageActive;
    uint32_t _lastSig;
    bool _sigValid;
};

const char *displayLibVersion();

#endif
