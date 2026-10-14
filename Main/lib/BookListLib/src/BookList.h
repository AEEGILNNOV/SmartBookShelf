#ifndef BOOK_LIST_LIB_H
#define BOOK_LIST_LIB_H

#include <Arduino.h>

#include "BoardConfig.h"

// =============================================================================
// BookListLib —— 书单（**动态数组**实现）
//
// 为什么不用固定数组：识别到的书数是未知的，固定数组要么开太大浪费 RAM，
// 要么装满就丢数据。这里用最朴素的做法：malloc 一块，装不下就 realloc 翻倍。
//
// 和 std::vector 的区别（本工程不用 STL）：
//   · 容量/数据都自己管，_items/_count/_capacity 三个变量一眼能看懂，
//   · 没有模板，没有迭代器，没有异常，取第 i 条用 at(i)。
//
// 线程安全：
//   · 识别任务会 addOrCount()，显示任务会 snapshot()/removeAt()，
//     所以每条对外接口内部都拿 _mutex（和主工程"共享数据 = 互斥锁"的约定一致），
//   · snapshot() 是给显示任务用的：加锁把数据拷到调用方缓冲，立刻放锁，
//     然后显示任务拿着快照慢慢画屏，不会长时间占着锁。
// =============================================================================

// 一条书籍记录
struct BookItem {
    int16_t id;                          // 二哈给的 ID（OCR 识别时一般是 0）
    uint16_t count;                      // 被识别到几次
    char text[HUSKY_TEXT_LEN];           // 书名 / 识别出的文字（UTF-8）
};

class BookList {
public:
    BookList();

    // 建互斥锁 + 申请初始容量。返回 false = 内存/锁失败
    bool begin(int16_t initCap = CFG_BOOKLIST_INIT_CAP);

    // 加一本：已经有同 ID（id!=0）或同名文字就计数 +1，否则追加到末尾。
    // 返回该条的下标，-1 = 失败（没 begin / 内存不够 / 超过 CFG_BOOKLIST_MAX）
    int16_t addOrCount(int16_t id, const char *text);

    // 直接追加一条（串口测试用，不去重）
    int16_t add(const char *text, int16_t id = 0);

    // 插到第 index 条的位置（后面的整体往后挪一格），返回它插进去后的下标。
    // 本次改造用它实现"新书插到书堆顶部"：insertAt(0, ...)。
    //   index <= 0      -> 插到最前面（新书从顶部放入，逻辑书堆里也在最顶）
    //   index >= _count -> 等同于 add()（追加到末尾）
    // 返回 -1 = 失败（没 begin / 内存不够 / 超过 CFG_BOOKLIST_MAX / index 为负）
    int16_t insertAt(int16_t index, int16_t id, const char *text);

    // 按下标找 id：找到返回下标，找不到返回 -1。
    // 每个学科最多一条记录，所以"找某个学科"就是"按 id 找"。
    int16_t findById(int16_t id);

    // 删掉第 index 条，后面的整体往前挪，并把容量留着不缩（避免反复 malloc）。
    // 返回删除后剩下的条数，-1 = 下标不合法
    int16_t removeAt(int16_t index);

    // 清空（保留已申请的容量）
    void clear();

    int16_t count();
    int16_t capacity();
    uint16_t countClamped();   // count() 但按 uint16 给（省得调用方来回转）

    // 每次增/删/清空都 +1：显示库靠它判断"列表变了，要重建控件"
    uint32_t version();

    // 加锁拷一份快照给显示任务用，返回实际拷了几条（可能少于 count，见 maxItems）
    int16_t snapshot(BookItem *out, int16_t maxItems);

    // 读第 index 条到 out（加锁）。返回 false = 下标不合法
    bool at(int16_t index, BookItem *out);

    // 打印整张列表（串口命令 b,p 用）
    void print();

private:
    // 以下两个内部函数都要求"调用前已经持锁"
    int16_t findByIdOrText(int16_t id, const char *text);
    bool grow(int16_t needCap);

    BookItem *_items;        // 动态数组本体
    int16_t _count;          // 现有几条
    int16_t _capacity;       // 已经申请了几条的位置
    uint32_t _version;
    SemaphoreHandle_t _mutex;
};

const char *bookListLibVersion();

#endif
