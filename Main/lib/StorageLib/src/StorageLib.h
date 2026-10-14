#ifndef STORAGE_LIB_H
#define STORAGE_LIB_H

#include <Arduino.h>
#include <Preferences.h>
#include <stddef.h>

#include "config.h"
#include "BookList.h"

// =============================================================================
// StorageLib —— Flash(NVS/Preferences) 持久化：书单 + 升降位置 + 动作完成标记
//
// 【本库解决什么问题】
//   1. 掉电后书单还在（识别出来的书不用重新扫一遍）；
//   2. 掉电后知道"升降平台大概在哪"（累计步数 + 位置是否有效 + 是否需要人工归零）；
//   3. 最关键的：知道"上一次动作有没有正常走完"。
//      动电机之前先把 actionPending 置 1 落盘，动作正常结束后置 0 落盘；
//      下次上电若读到 1，说明断电时机构可能停在一半 -> 位置不可信 -> 要求人工归零。
//      宁可让用户点一次"设为零点"，也绝不能让代码拿一个假位置去撞机构。
//
// 【不做什么】
//   · 不碰 GPIO、不碰电机、不建任务：本库只读写 Flash，谁来调、什么时候调由业务决定；
//   · 不偷偷改数据：版本不匹配 / 数据损坏时只报警并把 repairNeeded 置位，
//     绝不清空、绝不"尽力猜一下"后覆盖用户数据。
//
// 【写入时机（很省 Flash）】只在这几处写：
//   新书入库后、取书成功后、升降移动正常完成后、人工归零后、用户显式保存时、
//   动作开始/结束时（只有 1 字节的标记）。
//   ⚠️ 绝对不要每走一步电机就写一次：NVS 擦写次数有限，几步一写很快就把分区写坏。
//
// 【线程安全】
//   本库会被 main 任务（保存书单）和 stepperTask（动作标记）同时调用，
//   所有 public 接口内部都用 _mutex 串行化（和 BookList 一样的做法）；
//   名字带 Locked 后缀的私有函数都要求"调用前已经持锁"，它们之间可以互相调用
//   而不会自锁（所以这里用普通互斥锁，不用递归锁）。
// =============================================================================


// -----------------------------------------------------------------------------
// 一、NVS 命名空间与键名（集中在这里，改数据格式只改这几行）
//
// 为什么集中：#define 写在一处，Flash 里到底存了哪些键一眼能看完，
//            以后换键名或加字段也不会漏掉某个 get/put 对不上。
// ⚠️ NVS 的键名最长 15 个字符，改的时候注意别超。
// -----------------------------------------------------------------------------

// 命名空间（相当于 Flash 里的一块"文件夹"，和工程里别的模块互不干扰）
#ifndef CFG_STORAGE_NVS_NAMESPACE
#define CFG_STORAGE_NVS_NAMESPACE "sbs"   // TODO(Lead): 建议挪到 include/config.h，本文件留兜底
#endif

// 数据格式版本号。改动下面任何一个键的含义/长度时都要 +1。
// 版本对不上时本库拒绝加载并置 repairNeeded，把决定权交给用户。
#ifndef CFG_STORAGE_DATA_VERSION
#define CFG_STORAGE_DATA_VERSION 1        // config.h 第 10 节已定义，这里只兜底
#endif

// 升降步数的可信范围 = 升降软限位 × 这个系数（config.h 第 10 节已定义，这里只兜底）。
// 为什么要系数：软限位本身可能被改小（机械行程收紧），留一倍余量避免把
// "上一次合法的大行程"误判成坏数据；超出这个范围的值一定不可信，按"需要归零"处理。
#ifndef CFG_STORAGE_STEPS_TRUST_FACTOR
#define CFG_STORAGE_STEPS_TRUST_FACTOR 2
#endif

// 升降机构的软限位行程。config.h 现在的名字是 CFG_STEPPER_LIFT_RANGE_STEPS，
// 万一遇到只有老名字（CFG_STEPPER_M3_RANGE_STEPS）的配置，这里自己兜一下。
#ifndef CFG_STEPPER_LIFT_RANGE_STEPS
    #ifdef CFG_STEPPER_M3_RANGE_STEPS
        #define CFG_STEPPER_LIFT_RANGE_STEPS CFG_STEPPER_M3_RANGE_STEPS
    #else
        #define CFG_STEPPER_LIFT_RANGE_STEPS 4096
    #endif
#endif

// 最多往 Flash 存几条书。书单 blob = 2 + N × sizeof(StoredBook) 字节
// （NVS 单条 blob 上限 4000 字节，超了在 .cpp 里的 static_assert 会直接拦下来）。
// 注意这和 BookList 的 CFG_BOOKLIST_MAX 是两码事：本库只保证"够存"，
// 实际条数取两者里更小的（见下面的 STORAGE_BOOK_LIMIT）。
#ifndef CFG_STORAGE_MAX_BOOKS
#define CFG_STORAGE_MAX_BOOKS 64          // config.h 第 10 节已定义（16），这里只兜底
#endif

// 恢复"识别次数"时最多累加多少次。
// 为什么要有上限：BookList 目前没有"直接设置 count"的接口，只能靠 addOrCount()
// 一条一条累加；万一 Flash 里的 count 被写坏成 65535，这里最多累加到上限就停，
// 既不让状态异常膨胀，也不会卡住太长时间。
#ifndef CFG_STORAGE_RESTORE_COUNT_MAX
#define CFG_STORAGE_RESTORE_COUNT_MAX 1000   // TODO(Lead): 建议挪到 include/config.h
#endif

// 每次读写 NVS 时等锁的最长时间(ms)。存储操作很短，等不到就说明有别的任务卡住了。
#ifndef CFG_STORAGE_LOCK_TIMEOUT_MS
#define CFG_STORAGE_LOCK_TIMEOUT_MS 200    // TODO(Lead): 建议挪到 include/config.h
#endif

// 库版本（config.h 第 13 节统一放各库版本，这里留兜底）
#ifndef STORAGELIB_VERSION
#define STORAGELIB_VERSION "1.0.0"         // config.h 第 13 节已定义，这里只兜底
#endif

// ---- 键名（不是配置，是数据格式的一部分，所以不加 CFG_ 前缀）----
#define STORAGE_KEY_VERSION   "ver"     // uint16：数据格式版本号
#define STORAGE_KEY_BOOKS     "books"   // blob  ：书单（见下面 StoredBook 的排列）
#define STORAGE_KEY_LIFT      "lift"    // int32 ：升降电机累计步数
#define STORAGE_KEY_POSVALID  "posok"   // bool  ：位置是否有效
#define STORAGE_KEY_HOMING    "homing"  // bool  ：是否需要人工归零
#define STORAGE_KEY_PENDING   "pend"    // bool  ：上次动作是否没走完（掉电恢复核心）
#define STORAGE_KEY_SAVECNT   "scnt"    // uint32：累计保存次数（给状态页看，不是业务数据）


// -----------------------------------------------------------------------------
// 二、学科 ID 来源（BookDomainLib）
//
// BookDomainLib（学科 ID -> 中文名）是学科的"唯一来源"，它和本库是并行开发的，
// 可能还没落地，所以这里做一次编译期探测：
//   · 已经有 BookDomain.h  -> 直接 include，BOOK_ID_MIN/MAX 用它的（唯一来源，不重复维护）；
//   · 还没有               -> 按约定把它的接口**声明**出来（见下面注释），
//                            本库暂时不调用这些函数（调了会在链接期缺符号），
//                            ID 范围先用兜底常量，保证本库单独就能编译过。
// 这样处理的好处：BookDomainLib 一落地，上面的 include 自动生效，本库一行都不用改。
// -----------------------------------------------------------------------------
#if __has_include("BookDomain.h")
    #include "BookDomain.h"
    #define STORAGE_HAS_BOOKDOMAIN 1
#else
    #define STORAGE_HAS_BOOKDOMAIN 0
    // BookDomainLib/BookDomain.h 约定（等它落地后本段由上面的 include 取代）
    bool bookIdIsValid(int16_t id);                       // 只有 1~6 才算有效
    const char *getBookNameById(int16_t id);              // 唯一的中文名映射表，非法 id 返回 "未知"
    int16_t bookFindIndexById(const BookList &books, int16_t id);   // 找到返回下标，没有返回 -1
    void bookOrderText(const BookList &books, char *out, size_t outSize);  // 形如 "语文 > 数学 > 英语"
    const char *bookDomainLibVersion();
#endif

// ID 范围兜底：BookDomain.h 没给出这两个宏时也不能编译不过
#ifndef BOOK_ID_MIN
#define BOOK_ID_MIN 1
#endif
#ifndef BOOK_ID_MAX
#define BOOK_ID_MAX 6
#endif


// -----------------------------------------------------------------------------
// 三、Flash 里的书单记录格式
//
// 字段顺序和 BookItem 完全一致，所以在 .cpp 里可以让 snapshot() 直接写进 blob 的
// 正文段（省一份 1KB 的栈上临时数组），.cpp 顶上有 static_assert 把这一点钉住。
// 单独定义一份（而不是直接用 BookItem）是为了让"Flash 格式"有一个自己的名字：
// 以后 BookList 内部改了结构，只要这两者还一致就不影响老数据。
// -----------------------------------------------------------------------------
struct StoredBook {
    int16_t  id;                    // 学科 ID（1~6）
    uint16_t count;                 // 被识别到几次
    char     text[HUSKY_TEXT_LEN];  // 书名 / 识别出的文字（UTF-8），读回来必须自己补 '\0'
};

// 书单 blob 的最大长度：2 字节条数 + N 条记录
#define STORAGE_BOOK_BLOB_MAX ((size_t)(sizeof(int16_t) + (size_t)CFG_STORAGE_MAX_BOOKS * sizeof(StoredBook)))

// 实际能存几条 = CFG_STORAGE_MAX_BOOKS 和 BookList 上限里更小的那个
// （BookList 的 CFG_BOOKLIST_MAX 设 0 表示不限，这时只能听本库的上限）
#if (CFG_BOOKLIST_MAX > 0) && (CFG_BOOKLIST_MAX < CFG_STORAGE_MAX_BOOKS)
    #define STORAGE_BOOK_LIMIT CFG_BOOKLIST_MAX
#else
    #define STORAGE_BOOK_LIMIT CFG_STORAGE_MAX_BOOKS
#endif

// 注：blob 上限（4000 字节）的体检用 sizeof 计算，只能放在 .cpp 的 static_assert 里，
//     预处理期的 #if 不认 sizeof。见 StorageLib.cpp 顶部。


// -----------------------------------------------------------------------------
// 四、串口/调试页要看的存储状态（一次性快照，读它不会长时间占锁）
// -----------------------------------------------------------------------------
struct StorageStatus {
    bool     ready;            // Preferences 是否打开成功
    bool     savedOnce;        // 本次上电以来是否成功保存过
    bool     loadOk;           // 上次加载是否成功
    bool     versionMatch;     // 版本号是否匹配
    bool     repairNeeded;     // 数据异常需要人工处理（绝不自动覆盖）
    bool     positionValid;    // 位置是否有效
    bool     needHoming;       // 是否需要人工归零
    bool     actionPending;    // Flash 里记着"上次动作没完成"
    int32_t  liftSteps;        // 保存的升降步数
    int16_t  bookCount;        // 保存的书籍条数
    uint32_t saveCount;        // 累计保存次数
    uint32_t loadCount;        // 累计加载次数
    uint16_t dataVersion;      // Flash 里的版本号
    char     lastError[48];    // 最近一次失败原因（中文）
};


// -----------------------------------------------------------------------------
// 五、对外接口
// -----------------------------------------------------------------------------
class StorageLib {
public:
    StorageLib();

    // 打开 NVS 命名空间。返回 false = NVS 打不开（数据保持默认，不要当机）
    bool begin();

    // ---- 加载 ----
    // 从 Flash 读书单/步数/位置状态。
    //   books：加载目标。逐条校验 id 是否 1~6、是否重复，非法或重复的条目丢弃并打日志。
    // 返回 false = 没数据 / 损坏 / 版本不匹配（此时 books 保持原样，**不覆盖**）
    bool loadBooks(BookList &books, int32_t &outLiftSteps,
                   bool &outPositionValid, bool &outNeedHoming);

    // ---- 保存 ----
    bool saveBooks(const BookList &books);
    bool saveLiftSteps(int32_t steps);
    bool savePositionState(bool positionValid, bool needHoming);
    bool saveAll(const BookList &books, int32_t liftSteps,
                 bool positionValid, bool needHoming);   // 推荐：一次全存

    // ---- 未完成动作标记（掉电恢复的核心）----
    bool markActionPending();    // 动作开始前调：置 true 并立刻落盘
    bool clearActionPending();   // 动作正常完成后调：置 false 并立刻落盘
    bool actionPending();        // 读当前内存里的标志

    // ---- 危险操作（必须显式调用，不许偷偷做）----
    bool clearBooks();           // 只清空书籍数据，**绝不许顺带清零位置**
    bool resetPosition();        // 只重置位置（步数 0 + 有效），**绝不许清书单**
    bool eraseAll();             // 两个都清（只有明确的"恢复出厂"才调）

    // ---- 状态 / 调试 ----
    void status(StorageStatus &out);
    void print();                // 串口打印状态（中文），串口命令 f 会调

private:
    bool _openIfNeeded();
    // ---- 以下私有函数都要求"调用前已持锁"（public 接口负责加锁）----
    bool _ensureBlobLocked();                                  // 懒申请书单 blob 缓冲
    bool _writeVersionLocked();                                // 落盘版本号
    bool _writeBlobLocked(const BookList &books, int16_t *outCount);  // 落盘书单
    bool _writeLiftLocked(int32_t steps);
    bool _writePositionLocked(bool positionValid, bool needHoming);
    bool _writePendingLocked(bool pending);
    bool _bumpSaveCountLocked();
    bool _loadLocked(BookList &books, int32_t &outLiftSteps,
                     bool &outPositionValid, bool &outNeedHoming);
    // ---- 加锁小工具（public 接口用，不要求已持锁）----
    bool _takeLock(const char *what);
    void _setError(const char *text);

    Preferences _prefs;              // NVS 句柄

    SemaphoreHandle_t _mutex;        // 本库所有 public 接口的串行化锁
    uint8_t  *_blob;                 // 书单 blob 的临时缓冲（懒申请，2 + N×sizeof(StoredBook) 字节）
    size_t    _blobCapacity;         // _blob 的字节数

    bool      _ready;                // NVS 打开成功
    bool      _savedOnce;            // 本次上电保存过
    bool      _loadOk;               // 上次加载成功
    bool      _versionMatch;         // 版本号匹配
    bool      _repairNeeded;         // 数据异常，等人工处理
    bool      _positionValid;        // 位置有效
    bool      _needHoming;           // 需要人工归零
    bool      _actionPending;        // 上次动作没走完
    int32_t   _liftSteps;            // 缓存：升降累计步数
    int16_t   _bookCount;            // 缓存：Flash 里的书籍条数
    uint32_t  _saveCount;            // 累计保存次数
    uint32_t  _loadCount;            // 累计加载次数
    uint16_t  _dataVersion;          // Flash 里的版本号
    char      _lastError[48];        // 最近一次失败原因（中文）
};

const char *storageLibVersion();
#endif
