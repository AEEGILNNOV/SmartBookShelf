#include "StorageLib.h"

// =============================================================================
// StorageLib 实现 —— 书单 + 升降位置 + 动作完成标记的 Flash(NVS) 持久化
//
// 【为什么用 Preferences(NVS) 而不是自己擦写 Flash 扇区】
//   NVS 已经替我们做了磨损均衡、掉电保护和键值校验；自己擦扇区要管擦除粒度、
//   还要保证"写到一半断电"不把整块写坏，代价远大于收益。
//
// 【掉电恢复的完整闭环】（本库存在的第一理由）
//   动电机之前   markActionPending()   -> "pend" = 1 落盘
//   动作正常走完 clearActionPending()  -> "pend" = 0 落盘
//   下次上电     读到 "pend" = 1 就说明上次没走完 -> 位置一定不准
//                -> positionValid = false, needHoming = true
//   ⚠️ 顺序不能反：先落盘再动电机，断电才一定看得见这个标记。
//
// 【写入时机】只在"书单变了 / 用户显式保存 / 位置状态变了 / 动作开始与结束"这几处写，
//   绝不在电机每走一步时写 Flash（NVS 擦写次数有限，几步一写会很快写坏）。
//
// 【加锁约定】public 接口负责 _takeLock/_give；带 Locked 后缀的私有函数一律要求
//   调用前已经持锁，所以它们之间可以随便互相调用，不会自锁。
// =============================================================================

// 编译期体检：Flash 记录格式必须和 BookItem 同布局，才能让 snapshot() 直接写进 blob。
// （字段类型和顺序都一样，按标准布局必然一致；万一以后有人改了 BookItem，这里会直接拦下来。）
static_assert(sizeof(StoredBook) == sizeof(BookItem),
              "StoredBook 与 BookItem 布局不一致：改 BookList 后必须同步改 StorageLib 的 Flash 格式");

// 编译期体检：NVS 里单条 blob 超过 4000 字节就写不进去，
// 与其烧进去以后才发现，不如现在编译就报错（sizeof 只能在 static_assert 里算）。
static_assert(STORAGE_BOOK_BLOB_MAX <= 4000,
              "StorageLib: 书单 blob 超过 NVS 单条 blob 的 4000 字节上限，请调小 CFG_STORAGE_MAX_BOOKS");

// 学科 ID 校验：唯一来源是 BookDomainLib。
// 它落地了就调它的 bookIdIsValid()（"1~6 才算有效"这条规则全工程只留一份）；
// 万一它还没落地（STORAGE_HAS_BOOKDOMAIN=0），就用它的 ID 范围兜底，
// 保证本库单独也能编译过，而且行为和它完全一致。
static bool storageIdIsValid(int16_t id) {
#if STORAGE_HAS_BOOKDOMAIN
    return bookIdIsValid(id);
#else
    return (id >= BOOK_ID_MIN && id <= BOOK_ID_MAX);
#endif
}

// 学科中文名同样只认 BookDomainLib 的映射表，本库绝不自己再抄一份
// （抄两份迟早会不一致）。它还没落地时日志里只打数字 ID，不编一个名字出来。
static const char *storageBookName(int16_t id) {
#if STORAGE_HAS_BOOKDOMAIN
    return getBookNameById(id);
#else
    (void)id;
    return "BookDomainLib 未接入";
#endif
}

// 布尔值的"是/否"，只为了串口日志好读
static const char *storageYesNo(bool v) {
    return v ? "是" : "否";
}


// =============================================================================
// 构造 / 初始化
// =============================================================================

StorageLib::StorageLib() {
    _mutex = nullptr;
    _blob = nullptr;
    _blobCapacity = 0;

    _ready = false;
    _savedOnce = false;
    _loadOk = false;
    _versionMatch = false;
    _repairNeeded = false;

    // ⚠️ 上电默认取"最保守"的一组值：位置无效 + 需要人工归零。
    //    原因：机械上一次断电时停在哪没人知道，宁可让用户点一次"设为零点"，
    //    也绝不能让代码拿一个猜出来的位置去动电机（那是直接撞机构）。
    //    真正可信的值只在 loadBooks() 读到合法数据后才被覆盖。
    _positionValid = false;
    _needHoming = true;
    _actionPending = false;

    _liftSteps = 0;
    _bookCount = 0;
    _saveCount = 0;
    _loadCount = 0;
    _dataVersion = 0;
    _lastError[0] = '\0';
}

bool StorageLib::begin() {
    if (!_takeLock("NVS 初始化")) {
        return false;
    }

    bool ok = _openIfNeeded();
    if (ok) {
        // 版本号和累计保存次数是"状态页要看"的，这里先读出来，省得非等 loadBooks
        _dataVersion = _prefs.getUShort(STORAGE_KEY_VERSION, 0);
        _versionMatch = (_dataVersion == (uint16_t)CFG_STORAGE_DATA_VERSION);
        _saveCount = _prefs.getUInt(STORAGE_KEY_SAVECNT, 0);

        // 顺手把"上次动作没完成"的标记读进内存：
        // 这样即使调用方在 loadBooks() 之前先查 actionPending()，
        // 也不会看到一个假的 false（位置是否可用由 loadBooks() 决定，这里只同步标记）。
        _actionPending = _prefs.getBool(STORAGE_KEY_PENDING, false);
        Serial.printf("存储: 初始化完成（命名空间 %s, Flash 版本 %u, 本机版本 %u, 累计保存 %lu 次）\n",
                      CFG_STORAGE_NVS_NAMESPACE, (unsigned)_dataVersion,
                      (unsigned)CFG_STORAGE_DATA_VERSION, (unsigned long)_saveCount);
    }

    xSemaphoreGive(_mutex);
    return ok;
}

// 打开命名空间（幂等）。只允许在持锁状态下调用
bool StorageLib::_openIfNeeded() {
    if (_ready) {
        return true;
    }

    // readOnly = false：本库既要读也要写。命名空间不存在时 NVS 会自动创建。
    // ⚠️ 不要传 partition_label，默认就是分区表里的 "nvs" 分区。
    if (!_prefs.begin(CFG_STORAGE_NVS_NAMESPACE, false)) {
        _setError("NVS 打开失败");
        Serial.printf("存储: ⚠️ NVS 命名空间 '%s' 打开失败（分区表里没有 nvs 分区？）\n",
                      CFG_STORAGE_NVS_NAMESPACE);
        return false;
    }

    _ready = true;
    Serial.printf("存储: NVS 已就绪（命名空间 %s）\n", CFG_STORAGE_NVS_NAMESPACE);
    return true;
}


// =============================================================================
// 锁与错误文本
// =============================================================================

// 取锁。public 接口都从这里进；返回 false 时调用方必须直接返回，绝不能去 Give
bool StorageLib::_takeLock(const char *what) {
    if (_mutex == nullptr) {
        // begin() 之前就被调用的兜底：宁可在这里建锁，也不要拿空指针去 Take
        _mutex = xSemaphoreCreateMutex();
    }
    if (_mutex == nullptr) {
        Serial.printf("存储: ⚠️ %s 失败：互斥锁创建不了\n", what);
        return false;
    }
    if (xSemaphoreTake(_mutex, pdMS_TO_TICKS(CFG_STORAGE_LOCK_TIMEOUT_MS)) != pdTRUE) {
        Serial.printf("存储: ⚠️ %s 取锁超时（%d ms），本次操作取消\n", what, CFG_STORAGE_LOCK_TIMEOUT_MS);
        return false;
    }
    return true;
}

void StorageLib::_setError(const char *text) {
    if (text == nullptr) {
        text = "";
    }
    snprintf(_lastError, sizeof(_lastError), "%s", text);
}

// 书单 blob 缓冲：一次申请后就一直复用，不再反复 malloc。
// 为什么不放成员数组：放成员就要常驻 RAM，而没用存储的固件不该白占这块内存。
bool StorageLib::_ensureBlobLocked() {
    if (_blob != nullptr) {
        return true;
    }
    _blob = (uint8_t *)malloc(STORAGE_BOOK_BLOB_MAX);
    if (_blob == nullptr) {
        _setError("书单缓冲申请失败");
        Serial.printf("存储: ⚠️ 书单缓冲申请失败（要 %u 字节）\n", (unsigned)STORAGE_BOOK_BLOB_MAX);
        return false;
    }
    _blobCapacity = STORAGE_BOOK_BLOB_MAX;
    return true;
}


// =============================================================================
// 写 Flash（全部要求已持锁）
// =============================================================================

// 落盘版本号。书单和版本必须成对写：只写书单不写版本的话，
// 下次加载会看到"版本键不存在"而被判成没数据。
bool StorageLib::_writeVersionLocked() {
    if (!_openIfNeeded()) {
        return false;
    }
    size_t wrote = _prefs.putUShort(STORAGE_KEY_VERSION, (uint16_t)CFG_STORAGE_DATA_VERSION);
    if (wrote != sizeof(uint16_t)) {
        _setError("版本号写入失败");
        Serial.println("存储: ⚠️ 版本号写入失败");
        return false;
    }
    _dataVersion = (uint16_t)CFG_STORAGE_DATA_VERSION;
    _versionMatch = true;
    return true;
}

// 落盘书单。outCount 允许传 nullptr。返回 false 时 Flash 里的旧书单保持不动
bool StorageLib::_writeBlobLocked(const BookList &books, int16_t *outCount) {
    if (!_openIfNeeded() || !_ensureBlobLocked()) {
        return false;
    }

    // BookList 的 count()/snapshot() 是既有库里的非 const 接口（本轮不改别人的库），
    // 这里只读不写，所以去掉 const 是安全的；对外签名仍然坚持 const 引用，
    // 让调用方一眼看出"保存书单不会改你的书单"。
    BookList &roBooks = const_cast<BookList &>(books);

    int16_t total = roBooks.count();
    if (total < 0) {
        total = 0;
    }
    if (total > STORAGE_BOOK_LIMIT) {
        Serial.printf("存储: ⚠️ 书单 %d 条超过保存上限 %d 条，只保存前面的\n",
                      (int)total, (int)STORAGE_BOOK_LIMIT);
        total = STORAGE_BOOK_LIMIT;
    }

    // 直接拿 blob 的正文段当快照缓冲（StoredBook 与 BookItem 布局一致，顶上 static_assert 钉住了），
    // 省掉一份 1KB 左右的栈上临时数组 —— 本库可能被只有 3KB 栈的任务调用。
    BookItem *dstBook = (BookItem *)(_blob + sizeof(int16_t));
    int16_t n = roBooks.snapshot(dstBook, total);

    // ⚠️ 快照不完整（书单锁没拿到）时绝对不能写：否则会用"空书单"覆盖 Flash 里已有的书单。
    if (n != total) {
        _setError("书单快照不完整");
        Serial.printf("存储: ⚠️ 书单快照不完整（要 %d 条，拿到 %d 条），本次不保存，避免写丢数据\n",
                      (int)total, (int)n);
        return false;
    }

    int16_t *countCell = (int16_t *)_blob;
    *countCell = n;
    const size_t blobLen = sizeof(int16_t) + (size_t)n * sizeof(StoredBook);

    if (!_writeVersionLocked()) {
        return false;
    }

    size_t wrote = _prefs.putBytes(STORAGE_KEY_BOOKS, _blob, blobLen);
    if (wrote != blobLen) {
        _setError("书单写入失败");
        Serial.printf("存储: ⚠️ 书单写入失败（要 %u 字节，实际写了 %u）\n",
                      (unsigned)blobLen, (unsigned)wrote);
        return false;
    }

    _bookCount = n;
    if (outCount != nullptr) {
        *outCount = n;
    }
    Serial.printf("存储: 已保存 %d 本书\n", (int)n);
    return true;
}

bool StorageLib::_writeLiftLocked(int32_t steps) {
    if (!_openIfNeeded()) {
        return false;
    }
    if (_prefs.putInt(STORAGE_KEY_LIFT, steps) != sizeof(int32_t)) {
        _setError("升降步数写入失败");
        Serial.printf("存储: ⚠️ 升降步数写入失败（%ld）\n", (long)steps);
        return false;
    }
    _liftSteps = steps;
    Serial.printf("存储: 已保存升降步数 %ld\n", (long)steps);
    return true;
}

bool StorageLib::_writePositionLocked(bool positionValid, bool needHoming) {
    if (!_openIfNeeded()) {
        return false;
    }
    bool ok = true;
    // ⚠️ 两个键要一起成功：只写"有效"不写"需归零"会留下互相矛盾的组合
    if (_prefs.putBool(STORAGE_KEY_POSVALID, positionValid) == 0) {
        _setError("位置有效性写入失败");
        Serial.println("存储: ⚠️ 位置有效性写入失败");
        ok = false;
    }
    if (_prefs.putBool(STORAGE_KEY_HOMING, needHoming) == 0) {
        _setError("归零标记写入失败");
        Serial.println("存储: ⚠️ 归零标记写入失败");
        ok = false;
    }
    if (!ok) {
        return false;
    }
    _positionValid = positionValid;
    _needHoming = needHoming;
    Serial.printf("存储: 已保存位置状态（有效=%s 需归零=%s）\n",
                  storageYesNo(positionValid), storageYesNo(needHoming));
    return true;
}

bool StorageLib::_writePendingLocked(bool pending) {
    if (!_openIfNeeded()) {
        return false;
    }
    if (_prefs.putBool(STORAGE_KEY_PENDING, pending) == 0) {
        _setError("未完成动作标记写入失败");
        Serial.printf("存储: ⚠️ 未完成动作标记写入失败（值=%d）\n", (int)pending);
        return false;
    }
    _actionPending = pending;
    return true;
}

bool StorageLib::_bumpSaveCountLocked() {
    _saveCount++;
    if (_prefs.putUInt(STORAGE_KEY_SAVECNT, _saveCount) != sizeof(uint32_t)) {
        // 这个计数只是给状态页看的，写不进去不影响业务数据，所以只提示不报错
        Serial.println("存储: ⚠️ 累计保存次数写入失败（不影响已保存的数据）");
        return false;
    }
    return true;
}


// =============================================================================
// 加载（掉电恢复 + 逐条校验）
// =============================================================================

bool StorageLib::_loadLocked(BookList &books, int32_t &outLiftSteps,
                             bool &outPositionValid, bool &outNeedHoming) {
    _loadCount++;

    // 失败时给调用方的永远是"最保守"的值：不知道位置就不要假装知道
    outLiftSteps = 0;
    outPositionValid = false;
    outNeedHoming = true;

    if (!_openIfNeeded()) {
        _loadOk = false;
        return false;
    }

    // ---- 1) 版本号：对不上就拒绝加载，绝不"尽力解析"后覆盖用户数据 ----
    if (!_prefs.isKey(STORAGE_KEY_VERSION)) {
        _loadOk = false;
        _versionMatch = false;
        _setError("无历史数据");
        Serial.println("存储: 首次上电（Flash 里没有历史数据），按默认值启动");
        return false;
    }

    uint16_t flashVersion = _prefs.getUShort(STORAGE_KEY_VERSION, 0);
    _dataVersion = flashVersion;
    _versionMatch = (flashVersion == (uint16_t)CFG_STORAGE_DATA_VERSION);
    if (!_versionMatch) {
        // =====================================================================
        // ⚠️ 【为什么这里改成"尽力加载"而不是直接拒绝】
        //
        //   原来的逻辑是"版本号对不上就什么都不加载"，本意是保护数据（怕格式变了读成乱码）。
        //   但实机上出现过一个更难受的情况：浏览器版本键读出来是 0（键缺失/写失败），
        //   于是**每次开机都不载入，书单永远是空的**，用户看到的就是"存进去了但开机没有"。
        //
        //   现在的做法：版本对不上时**先尝试加载**，但走"全量校验"这条路 ——
        //     ① 书单 blob 的长度必须和头部记录的条数**完全吻合**（不吻合就绝不解析）；
        //     ② 每条书都要过 id 1~6 + 去重校验。
        //   校验通过 -> 数据是可信的，载入并**立刻用当前版本号重写一遍**（自我修复）；
        //   校验不过 -> 退回原来的行为：不加载、置 repairNeeded、**绝不清数据**。
        //
        //   这样即使键缺失/版本漂移，用户也不会丢书；而真的格式不兼容时仍然安全。
        // =====================================================================
        Serial.printf("存储: ⚠️ Flash 数据版本 %u 与本机 %u 不匹配，先尝试按当前格式校验加载\n",
                      (unsigned)flashVersion, (unsigned)CFG_STORAGE_DATA_VERSION);
        _repairNeeded = true;   // 先置上：下面校验通过才清掉
    }

    // ---- 2) 升降步数 / 位置有效性 / 归零需求 / 未完成动作标记 ----
    int32_t liftSteps = _prefs.getInt(STORAGE_KEY_LIFT, 0);
    bool positionValid = _prefs.getBool(STORAGE_KEY_POSVALID, false);
    bool needHoming = _prefs.getBool(STORAGE_KEY_HOMING, true);
    bool pending = _prefs.getBool(STORAGE_KEY_PENDING, false);

    // 超出 "软限位 × 安全系数" 的步数一定不可信（Flash 写坏 / 换了机构 / 有人手改过）。
    // 这种值直接拿去走电机可能撞机构，所以清零并按"需要归零"处理。
    const int32_t trustMax = (int32_t)CFG_STEPPER_LIFT_RANGE_STEPS * (int32_t)CFG_STORAGE_STEPS_TRUST_FACTOR;
    if (liftSteps > trustMax || liftSteps < -trustMax) {
        Serial.printf("存储: ⚠️ 保存的升降步数 %ld 超出可信范围 ±%ld，判为不可信\n",
                      (long)liftSteps, (long)trustMax);
        liftSteps = 0;
        positionValid = false;
        needHoming = true;
    }

    // 掉电恢复的核心判断：上次动作没走完 -> 位置一定不准
    if (pending) {
        Serial.println("存储: ⚠️ 上次动作没走完就断电了（pend=1），位置不可信 -> 要求人工归零");
        positionValid = false;
        needHoming = true;
    }

    outLiftSteps = liftSteps;
    outPositionValid = positionValid;
    outNeedHoming = needHoming;
    _liftSteps = liftSteps;
    _positionValid = positionValid;
    _needHoming = needHoming;
    _actionPending = pending;

    // 把 pending 翻译成"需要归零"之后，必须把结论落盘，否则下次上电还得重来一遍：
    //   · 先写位置状态，写成功了才清 pending —— 万一这次写失败，下一次上电
    //     还能靠 pending 再提醒一次（不能提前把唯一的证据清掉）；
    //   · 用户如果上电后点了"设为零点"，resetPosition() 自己也会把 pending 清掉。
    if (pending) {
        if (_writePositionLocked(false, true)) {
            _writePendingLocked(false);
        } else {
            Serial.println("存储: ⚠️ 位置状态落盘失败，保留 pend 标记，下次上电仍会提示归零");
        }
    }

    // ---- 3) 书单 ----
    int16_t kept = 0;
    bool booksOk = true;

    if (!_prefs.isKey(STORAGE_KEY_BOOKS)) {
        _bookCount = 0;
        Serial.println("存储: Flash 里没有书单，按空书单处理");
    } else if (!_ensureBlobLocked()) {
        // 位置状态已经拿到了，只是书单这次读不出来：如实返回 false，
        // 调用方至少还能拿到保守的位置状态，而不是被一个假成功骗过去。
        _loadOk = false;
        _setError("书单缓冲申请失败");
        return false;
    } else {
        size_t blobLen = _prefs.getBytesLength(STORAGE_KEY_BOOKS);
        if (blobLen < sizeof(int16_t) || blobLen > _blobCapacity) {
            _loadOk = false;
            _repairNeeded = true;
            _setError("书单数据长度异常");
            Serial.printf("存储: ⚠️ 书单 blob 长度 %u 异常（上限 %u），判为损坏\n",
                          (unsigned)blobLen, (unsigned)_blobCapacity);
            return false;
        }

        size_t got = _prefs.getBytes(STORAGE_KEY_BOOKS, _blob, blobLen);
        if (got != blobLen) {
            _loadOk = false;
            _repairNeeded = true;
            _setError("书单读取失败");
            Serial.printf("存储: ⚠️ 书单读取不完整（要 %u 字节，实际 %u 字节）\n",
                          (unsigned)blobLen, (unsigned)got);
            return false;
        }

        int16_t flashCount = 0;
        memcpy(&flashCount, _blob, sizeof(flashCount));
        const size_t expectLen = sizeof(int16_t) + (size_t)flashCount * sizeof(StoredBook);
        if (flashCount < 0 || flashCount > STORAGE_BOOK_LIMIT || expectLen != blobLen) {
            _loadOk = false;
            _repairNeeded = true;
            _setError("书单条数异常");
            Serial.printf("存储: ⚠️ 书单条数 %d 异常（限 0~%d，blob 应 %u 字节，实际 %u 字节），判为损坏\n",
                          (int)flashCount, (int)STORAGE_BOOK_LIMIT,
                          (unsigned)expectLen, (unsigned)blobLen);
            return false;
        }

        // ---- 逐条校验 + 就地压缩 ----
        // 为什么要就地压缩：合法条目往前挪，kept 就是"有效条数"，
        // 这样校验阶段一条都不用碰 books，也就做到了"校验不过绝不覆盖"。
        StoredBook *recs = (StoredBook *)(_blob + sizeof(int16_t));
        for (int16_t i = 0; i < flashCount; i++) {
            // ⚠️ Flash 里的内容一律不信：先强制补 '\0'，
            //    否则后面 snprintf/strcmp 会顺着越界内存读下去。
            recs[i].text[HUSKY_TEXT_LEN - 1] = '\0';

            // ⚠️ id==0 是"无学科/测试条目"（串口 b,a,N 造出来的测试书就是 id=0），
            //    它**必须**能通过校验：写入端 _writeBlobLocked() 不做 id 过滤、
            //    照样把 id=0 写进 Flash，读取端要是把它当"非法条目"丢掉，
            //    现象就是"保存成功、重启后书单 0 条" —— 用户想在没有二哈的情况下
            //    验证书单持久化，这条路会被彻底堵死。
            //    ⚠️ 注意**不要**去放宽 bookIdIsValid()：那是业务唯一来源，
            //    id=0 不属于任何学科，业务入库（bookAddAtTop）必须继续拒绝它。
            //    两处规则分开写在这里，谁也不许动谁。
            if (recs[i].id != 0 && !storageIdIsValid(recs[i].id)) {
                Serial.printf("存储: 丢弃非法条目（id=%d 既不是 0(测试) 也不在 %d~%d，text='%s'）\n",
                              (int)recs[i].id, BOOK_ID_MIN, BOOK_ID_MAX, recs[i].text);
                continue;
            }
            // 去重**只对 id!=0 的学科条目**做：方案A 是"一个学科最多一本"，
            // 而多条 id=0 的测试书是合法的，不能被当成"重复 id"丢掉
            //（否则 b,a,3 造 3 条，重启只会恢复 1 条）。
            bool dup = false;
            if (recs[i].id != 0) {
                for (int16_t k = 0; k < kept; k++) {
                    if (recs[k].id == recs[i].id) {
                        dup = true;
                        break;
                    }
                }
            }
            if (dup) {
                // 一个学科最多一本书，重复的只保留第一条
                Serial.printf("存储: 丢弃重复条目 id=%d(%s)（'%s'），只保留第一条\n",
                              (int)recs[i].id, storageBookName(recs[i].id), recs[i].text);
                continue;
            }
            if (kept != i) {
                recs[kept] = recs[i];
            }
            kept++;
        }
        Serial.printf("存储: 书单校验 %d 条 -> 有效 %d 条\n", (int)flashCount, (int)kept);

        // ---- 到这里才开始动 books：前面任何一步失败都保证 books 原样不动 ----
        books.clear();
        for (int16_t i = 0; i < kept; i++) {
            if (books.add(recs[i].text, recs[i].id) < 0) {
                Serial.printf("存储: ⚠️ 恢复第 %d 条失败（书单没 begin 或内存不足），已恢复 %d 条\n",
                              (int)(i + 1), (int)i);
                _setError("恢复书单失败");
                booksOk = false;
                break;
            }

            // 恢复"识别次数"：BookList 目前没有"直接设置 count"的接口，
            // 只能靠 addOrCount() 累加，所以这里 bounded 循环（见 .h 里的上限宏）。
            // TODO(Lead): 建议 BookListLib 加一个 setCount(index, count)，
            //             这里就能 O(1) 恢复，也不怕 count 被写坏成大数。
            uint16_t cnt = recs[i].count;
            if (cnt == 0) {
                cnt = 1;   // BookList 的 add() 语义就是"至少算 1 次"
            }
            if (cnt > CFG_STORAGE_RESTORE_COUNT_MAX) {
                Serial.printf("存储: ⚠️ 条目 id=%d 的识别次数 %u 异常偏大，按上限 %d 恢复\n",
                              (int)recs[i].id, (unsigned)cnt, CFG_STORAGE_RESTORE_COUNT_MAX);
                cnt = (uint16_t)CFG_STORAGE_RESTORE_COUNT_MAX;
            }
            for (uint16_t k = 1; k < cnt; k++) {
                if (books.addOrCount(recs[i].id, recs[i].text) < 0) {
                    break;   // 累加失败（锁超时等）就保留当前次数，不影响书本身
                }
            }
        }
        if (booksOk) {
            _bookCount = kept;
        }
    }

    _loadOk = booksOk;
    if (booksOk) {
        _setError("");
        Serial.printf("存储: 加载完成（书单 %d 条，升降 %ld 步，位置有效=%s 需归零=%s）\n",
                      (int)_bookCount, (long)_liftSteps,
                      storageYesNo(_positionValid), storageYesNo(_needHoming));

        // ---- 自我修复：校验通过说明数据是可信的，把版本号补成当前版本 ----
        // 为什么必须做：版本键缺失/对不上时，如果不补写，下次上电又要走一遍
        // "尽力加载"，而且 repairNeeded 会一直挂着、界面一直报警。
        if (!_versionMatch) {
            if (_writeVersionLocked()) {
                _repairNeeded = false;   // 修好了，不用再报警
                Serial.println("存储: 已把 Flash 版本号补写为当前版本（自我修复）");
            } else {
                Serial.println("存储: ⚠️ 版本号补写失败，下次上电仍会走一次兼容加载");
            }
        } else {
            _repairNeeded = false;
        }
    }
    return booksOk;
}

// public：加锁 + 交给内部实现
bool StorageLib::loadBooks(BookList &books, int32_t &outLiftSteps,
                           bool &outPositionValid, bool &outNeedHoming) {
    if (!_takeLock("加载 Flash 数据")) {
        outLiftSteps = 0;
        outPositionValid = false;
        outNeedHoming = true;
        return false;
    }
    bool ok = _loadLocked(books, outLiftSteps, outPositionValid, outNeedHoming);
    xSemaphoreGive(_mutex);
    return ok;
}


// =============================================================================
// 保存（public 接口：加锁 -> 落盘 -> 计数 -> 放锁）
// =============================================================================

bool StorageLib::saveBooks(const BookList &books) {
    if (!_takeLock("保存书单")) {
        return false;
    }
    bool ok = _writeBlobLocked(books, nullptr);
    if (ok) {
        _savedOnce = true;
        _bumpSaveCountLocked();
    }
    xSemaphoreGive(_mutex);
    return ok;
}

bool StorageLib::saveLiftSteps(int32_t steps) {
    if (!_takeLock("保存升降步数")) {
        return false;
    }
    bool ok = _writeLiftLocked(steps);
    if (ok) {
        _savedOnce = true;
        _bumpSaveCountLocked();
    }
    xSemaphoreGive(_mutex);
    return ok;
}

bool StorageLib::savePositionState(bool positionValid, bool needHoming) {
    if (!_takeLock("保存位置状态")) {
        return false;
    }
    bool ok = _writePositionLocked(positionValid, needHoming);
    if (ok) {
        _savedOnce = true;
        _bumpSaveCountLocked();
    }
    xSemaphoreGive(_mutex);
    return ok;
}

// 一次全存：移动正常结束时调这个最省事，也避免"书单存了位置没存"的半截状态。
// 注意整个过程的锁一直拿着，中途不放手，也不反复 open/close 命名空间。
bool StorageLib::saveAll(const BookList &books, int32_t liftSteps,
                         bool positionValid, bool needHoming) {
    if (!_takeLock("整体保存")) {
        return false;
    }

    bool ok = _writeBlobLocked(books, nullptr);
    // 用 && 但把函数调用放左边：保证每一项都会被尝试，不会被短路掉
    ok = _writeLiftLocked(liftSteps) && ok;
    ok = _writePositionLocked(positionValid, needHoming) && ok;

    if (ok) {
        _savedOnce = true;
        _bumpSaveCountLocked();
    }
    xSemaphoreGive(_mutex);

    if (!ok) {
        Serial.println("存储: ⚠️ 整体保存没全部成功，请看上面具体是哪一项失败");
    }
    return ok;
}


// =============================================================================
// 未完成动作标记（掉电恢复核心）
// =============================================================================

// 动作开始前调：先落盘再动电机，断电才一定看得见这个标记
bool StorageLib::markActionPending() {
    if (!_takeLock("标记动作开始")) {
        return false;
    }
    bool ok = _writePendingLocked(true);
    xSemaphoreGive(_mutex);

    if (ok) {
        Serial.println("存储: 动作开始，未完成标记已落盘");
    } else {
        Serial.println("存储: ⚠️ 动作开始标记落盘失败，这次动作若断电将无法察觉");
    }
    return ok;
}

// 动作正常完成后调
bool StorageLib::clearActionPending() {
    if (!_takeLock("清除动作标记")) {
        return false;
    }
    bool ok = _writePendingLocked(false);
    xSemaphoreGive(_mutex);
    return ok;
}

// 读内存里的标志（loadBooks/标记接口都会同步它，不用每次去翻 Flash）
bool StorageLib::actionPending() {
    if (!_takeLock("读动作标记")) {
        return false;
    }
    bool pending = _actionPending;
    xSemaphoreGive(_mutex);
    return pending;
}


// =============================================================================
// 危险操作：只清该清的，绝不"顺手"多清一样
// =============================================================================

// 只清空书籍数据。位置相关的键一个都不许动：
// 顺手把位置也抹了，下次上电就会以为位置有效/失效混乱，很容易撞机构。
bool StorageLib::clearBooks() {
    if (!_takeLock("清空书籍数据")) {
        return false;
    }

    bool ok = false;
    if (_openIfNeeded()) {
        ok = true;
        if (_prefs.isKey(STORAGE_KEY_BOOKS) && !_prefs.remove(STORAGE_KEY_BOOKS)) {
            _setError("书单键删除失败");
            Serial.println("存储: ⚠️ 书单键删除失败");
            ok = false;
        }
        if (ok) {
            // 版本号留着：让 Flash 里始终有"一份有效数据"的框架，
            // 下次加载能正常走到"空书单"而不是"没有历史数据"。
            ok = _writeVersionLocked();
            _bookCount = 0;
            _savedOnce = true;
            _bumpSaveCountLocked();
            Serial.println("存储: 已清空书籍数据（位置数据未动）");
        }
    }

    xSemaphoreGive(_mutex);
    return ok;
}

// 只重置位置：步数归零 + 位置有效 + 不需要归零。绝不清书单
bool StorageLib::resetPosition() {
    if (!_takeLock("重置位置")) {
        return false;
    }

    bool ok = false;
    if (_openIfNeeded()) {
        ok = _writeLiftLocked(0);
        ok = _writePositionLocked(true, false) && ok;
        if (ok) {
            // 位置已经由人工重新建立为可信，"上次动作没走完"这个标记必须一起清掉：
            // 它存在的唯一目的就是让位置失效，位置都重建了还留着，
            // 只会让下一次复位又白要求一次归零。
            _writePendingLocked(false);
            _savedOnce = true;
            _bumpSaveCountLocked();
            Serial.println("存储: 已把当前位置设为零点（书单未动）");
        } else {
            Serial.println("存储: ⚠️ 重置位置没写全，位置状态可能仍不可信，请重试");
        }
    }

    xSemaphoreGive(_mutex);
    return ok;
}

// 恢复出厂：书单 + 位置全清。只有用户明确要"恢复出厂"时才调
bool StorageLib::eraseAll() {
    if (!_takeLock("恢复出厂（全部清除）")) {
        return false;
    }

    bool ok = false;
    if (_openIfNeeded()) {
        if (_prefs.clear()) {
            // 缓存也要一起回到默认值，否则状态页/调试页还在显示老数据
            _liftSteps = 0;
            _positionValid = false;
            _needHoming = true;
            _actionPending = false;
            _bookCount = 0;
            _dataVersion = 0;
            _versionMatch = false;
            _repairNeeded = false;
            _savedOnce = false;
            _saveCount = 0;
            _setError("");
            ok = true;
            Serial.println("存储: ⚠️ 已清空本命名空间的全部数据（书单 + 位置），下次上电按首次上电处理");
        } else {
            _setError("清空 NVS 失败");
            Serial.println("存储: ⚠️ 清空 NVS 命名空间失败");
        }
    }

    xSemaphoreGive(_mutex);
    return ok;
}


// =============================================================================
// 状态 / 串口打印
// =============================================================================

void StorageLib::status(StorageStatus &out) {
    memset(&out, 0, sizeof(out));

    if (!_takeLock("读存储状态")) {
        snprintf(out.lastError, sizeof(out.lastError), "取锁超时");
        return;
    }

    out.ready = _ready;
    out.savedOnce = _savedOnce;
    out.loadOk = _loadOk;
    out.versionMatch = _versionMatch;
    out.repairNeeded = _repairNeeded;
    out.positionValid = _positionValid;
    out.needHoming = _needHoming;
    out.actionPending = _actionPending;
    out.liftSteps = _liftSteps;
    out.bookCount = _bookCount;
    out.saveCount = _saveCount;
    out.loadCount = _loadCount;
    out.dataVersion = _dataVersion;
    snprintf(out.lastError, sizeof(out.lastError), "%s", _lastError);

    xSemaphoreGive(_mutex);
}

// 串口命令 f 会调它。格式跟其它库保持一致：每行"存储: ..."
// =============================================================================
// 打印 NVS 里实际存在哪些键
//
// 为什么要有它：出现"明明保存成功、开机却说没有历史数据"时，
// 光看业务日志分不清是"键根本没写进去"还是"读的时候版本对不上"。
// 这个函数把每个键是否存在、值是多少直接打出来，一眼定位。
// =============================================================================
void StorageLib::printKeys() {
    if (!_takeLock("打印 NVS 键")) {
        return;
    }
    if (!_openIfNeeded()) {
        xSemaphoreGive(_mutex);
        return;
    }

    Serial.printf("存储: ---- NVS 键清单（命名空间 %s）----\n", CFG_STORAGE_NVS_NAMESPACE);
    Serial.printf("  %-8s 存在=%s  值=%u (本机版本 %u)\n", STORAGE_KEY_VERSION,
                  storageYesNo(_prefs.isKey(STORAGE_KEY_VERSION)),
                  (unsigned)_prefs.getUShort(STORAGE_KEY_VERSION, 0),
                  (unsigned)CFG_STORAGE_DATA_VERSION);
    Serial.printf("  %-8s 存在=%s  长度=%u 字节\n", STORAGE_KEY_BOOKS,
                  storageYesNo(_prefs.isKey(STORAGE_KEY_BOOKS)),
                  (unsigned)_prefs.getBytesLength(STORAGE_KEY_BOOKS));
    Serial.printf("  %-8s 存在=%s  值=%ld\n", STORAGE_KEY_LIFT,
                  storageYesNo(_prefs.isKey(STORAGE_KEY_LIFT)),
                  (long)_prefs.getInt(STORAGE_KEY_LIFT, 0));
    Serial.printf("  %-8s 存在=%s  值=%s\n", STORAGE_KEY_POSVALID,
                  storageYesNo(_prefs.isKey(STORAGE_KEY_POSVALID)),
                  storageYesNo(_prefs.getBool(STORAGE_KEY_POSVALID, false)));
    Serial.printf("  %-8s 存在=%s  值=%s\n", STORAGE_KEY_HOMING,
                  storageYesNo(_prefs.isKey(STORAGE_KEY_HOMING)),
                  storageYesNo(_prefs.getBool(STORAGE_KEY_HOMING, true)));
    Serial.printf("  %-8s 存在=%s  值=%s\n", STORAGE_KEY_PENDING,
                  storageYesNo(_prefs.isKey(STORAGE_KEY_PENDING)),
                  storageYesNo(_prefs.getBool(STORAGE_KEY_PENDING, false)));
    Serial.printf("  %-8s 存在=%s  值=%lu\n", STORAGE_KEY_SAVECNT,
                  storageYesNo(_prefs.isKey(STORAGE_KEY_SAVECNT)),
                  (unsigned long)_prefs.getUInt(STORAGE_KEY_SAVECNT, 0));
    Serial.println("  判读: 如果 ver 存在=否 -> 版本号从来没写进去；");
    Serial.println("        如果 ver 存在=是 但值=0 -> 写的是别的键/别的命名空间；");
    Serial.println("        如果 books 存在=是 长度为 0 -> 书单没写进去。");

    xSemaphoreGive(_mutex);
}

void StorageLib::print() {
    StorageStatus st;
    status(st);

    Serial.println("存储: ---- Flash(NVS) 状态 ----");
    Serial.printf("存储: 库版本 %s / 命名空间 %s\n", storageLibVersion(), CFG_STORAGE_NVS_NAMESPACE);

    if (!st.ready) {
        Serial.println("存储: ⚠️ NVS 没打开，本次上电保存/加载都不可用");
    }

    Serial.printf("存储: 数据版本 %u（本机 %u，匹配=%s）\n",
                  (unsigned)st.dataVersion, (unsigned)CFG_STORAGE_DATA_VERSION,
                  storageYesNo(st.versionMatch));
    Serial.printf("存储: 上次加载成功=%s 本次上电保存过=%s\n",
                  storageYesNo(st.loadOk), storageYesNo(st.savedOnce));
    Serial.printf("存储: 保存 %lu 次 / 加载 %lu 次\n",
                  (unsigned long)st.saveCount, (unsigned long)st.loadCount);
    Serial.printf("存储: 升降 %ld 步 位置有效=%s 需归零=%s 未完成动作=%s\n",
                  (long)st.liftSteps, storageYesNo(st.positionValid),
                  storageYesNo(st.needHoming), storageYesNo(st.actionPending));
    Serial.printf("存储: 书单 %d 条（上限 %d）\n", (int)st.bookCount, (int)STORAGE_BOOK_LIMIT);

    if (st.needHoming) {
        Serial.println("存储: ⚠️ 位置不可信，请先人工归零（调试页：将当前位置设为零点）");
    }
    if (st.repairNeeded) {
        Serial.println("存储: ⚠️ 数据版本不匹配或已损坏，本库不会自动覆盖，请确认后再决定清空/重建");
    }
    if (st.lastError[0] != '\0') {
        Serial.printf("存储: 最近一次错误: %s\n", st.lastError);
    }
}

const char *storageLibVersion() {
    return STORAGELIB_VERSION;
}
