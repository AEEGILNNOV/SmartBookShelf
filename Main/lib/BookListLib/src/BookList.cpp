#include "BookList.h"

// 容量，上限，每本书文字长度都在 config.h（第 3.3 节）
#include "config.h"

// =============================================================================
// BookList 实现（动态数组）
//
// 扩容策略：容量不够就 ×2（4 -> 8 -> 16 ...）。
// 为什么翻倍而不是 +1：每次只加 1 的话，加 N 次要 malloc N 次，
// 翻倍可以让"一共加 N 条"的拷贝总代价保持在 O(N)。
// =============================================================================

BookList::BookList() {
    _items = nullptr;
    _count = 0;
    _capacity = 0;
    _version = 0;
    _mutex = nullptr;
}

bool BookList::begin(int16_t initCap) {
    if (_mutex == nullptr) {
        _mutex = xSemaphoreCreateMutex();
    }
    if (_mutex == nullptr) {
        Serial.println("书单: 互斥锁创建失败");
        return false;
    }

    if (initCap < 1) {
        initCap = 1;
    }
    if (_items == nullptr) {
        _items = (BookItem *)malloc(sizeof(BookItem) * (size_t)initCap);
        if (_items == nullptr) {
            Serial.println("书单: 首次申请内存失败");
            return false;
        }
        _capacity = initCap;
    }
    return true;
}

// 扩容：需要 needCap 个位置，不够就翻倍（一直翻到够）
bool BookList::grow(int16_t needCap) {
    if (needCap <= _capacity) {
        return true;
    }

    int16_t newCap = (_capacity > 0) ? _capacity : CFG_BOOKLIST_INIT_CAP;
    while (newCap < needCap) {
#if CFG_BOOKLIST_MAX > 0
        if (newCap >= CFG_BOOKLIST_MAX) {
            break;  // 到了上限就不再涨
        }
#endif
        newCap = (int16_t)(newCap * 2);
    }
#if CFG_BOOKLIST_MAX > 0
    if (newCap > CFG_BOOKLIST_MAX) {
        newCap = CFG_BOOKLIST_MAX;
    }
#endif
    if (newCap < needCap) {
        return false;  // 上限挡着，装不下了
    }

    // realloc：空间原地扩不了会自动搬到新地址，旧数据自己保留
    BookItem *p = (BookItem *)realloc(_items, sizeof(BookItem) * (size_t)newCap);
    if (p == nullptr) {
        Serial.printf("书单: 扩容失败（要 %d 条, 现有 %d 条）\n", newCap, _capacity);
        return false;
    }

    Serial.printf("书单: 扩容 %d -> %d 条\n", _capacity, newCap);
    _items = p;
    _capacity = newCap;
    return true;
}

// 按 ID（id != 0 时）或文字找，找不到返回 -1。调用前必须已持锁
int16_t BookList::findByIdOrText(int16_t id, const char *text) {
    for (int16_t i = 0; i < _count; i++) {
        if (id != 0 && _items[i].id == id) {
            return i;
        }
        if (text != nullptr && text[0] != '\0' &&
            strcmp(_items[i].text, text) == 0) {
            return i;
        }
    }
    return -1;
}

int16_t BookList::addOrCount(int16_t id, const char *text) {
    if (_mutex == nullptr || _items == nullptr) {
        return -1;
    }
    if (text == nullptr) {
        text = "";
    }

    int16_t index = -1;
    if (xSemaphoreTake(_mutex, pdMS_TO_TICKS(50)) != pdTRUE) {
        return -1;
    }

    int16_t found = findByIdOrText(id, text);

    if (found >= 0) {
        if (_items[found].count < 0xFFFF) {
            _items[found].count++;
        }
        index = found;
    } else if (grow((int16_t)(_count + 1))) {
        index = _count;
        _items[index].id = id;
        _items[index].count = 1;
        snprintf(_items[index].text, sizeof(_items[index].text), "%s", text);
        _count++;
    }

    if (index >= 0) {
        _version++;
    }
    xSemaphoreGive(_mutex);
    return index;
}

int16_t BookList::add(const char *text, int16_t id) {
    if (_mutex == nullptr || _items == nullptr) {
        return -1;
    }
    if (text == nullptr) {
        text = "";
    }

    int16_t index = -1;
    if (xSemaphoreTake(_mutex, pdMS_TO_TICKS(50)) != pdTRUE) {
        return -1;
    }
    if (grow((int16_t)(_count + 1))) {
        index = _count;
        _items[index].id = id;
        _items[index].count = 1;
        snprintf(_items[index].text, sizeof(_items[index].text), "%s", text);
        _count++;
        _version++;
    }
    xSemaphoreGive(_mutex);
    return index;
}

int16_t BookList::removeAt(int16_t index) {
    if (_mutex == nullptr || _items == nullptr) {
        return -1;
    }
    if (xSemaphoreTake(_mutex, pdMS_TO_TICKS(50)) != pdTRUE) {
        return -1;
    }

    int16_t left = -1;
    if (index >= 0 && index < _count) {
        // 后面的整体往前挪一格（动态数组的删除就是这一步 memmove）
        for (int16_t i = index; i < _count - 1; i++) {
            _items[i] = _items[i + 1];
        }
        _count--;
        _version++;
        left = _count;
    }
    xSemaphoreGive(_mutex);
    return left;
}

void BookList::clear() {
    if (_mutex == nullptr) {
        return;
    }
    if (xSemaphoreTake(_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        _count = 0;      // 容量留着，下次加书不用重新 malloc
        _version++;
        xSemaphoreGive(_mutex);
    }
}

int16_t BookList::count() {
    if (_mutex == nullptr) {
        return 0;
    }
    int16_t n = 0;
    if (xSemaphoreTake(_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        n = _count;
        xSemaphoreGive(_mutex);
    }
    return n;
}

int16_t BookList::capacity() {
    if (_mutex == nullptr) {
        return 0;
    }
    int16_t n = 0;
    if (xSemaphoreTake(_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        n = _capacity;
        xSemaphoreGive(_mutex);
    }
    return n;
}

uint16_t BookList::countClamped() {
    int16_t n = count();
    return (n < 0) ? 0 : (uint16_t)n;
}

uint32_t BookList::version() {
    if (_mutex == nullptr) {
        return 0;
    }
    uint32_t v = 0;
    if (xSemaphoreTake(_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        v = _version;
        xSemaphoreGive(_mutex);
    }
    return v;
}

// 加锁拷一份快照出去：显示任务拿它画屏，画的过程中不占锁
int16_t BookList::snapshot(BookItem *out, int16_t maxItems) {
    if (_mutex == nullptr || out == nullptr || maxItems <= 0) {
        return 0;
    }

    int16_t n = 0;
    if (xSemaphoreTake(_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        n = (_count < maxItems) ? _count : maxItems;
        for (int16_t i = 0; i < n; i++) {
            out[i] = _items[i];
        }
        xSemaphoreGive(_mutex);
    }
    return n;
}

// 读第 index 条（加锁）：日志/“取出”按钮拿走数据用
bool BookList::at(int16_t index, BookItem *out) {
    if (_mutex == nullptr || out == nullptr) {
        return false;
    }
    bool ok = false;
    if (xSemaphoreTake(_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        if (index >= 0 && index < _count) {
            *out = _items[index];
            ok = true;
        }
        xSemaphoreGive(_mutex);
    }
    return ok;
}

void BookList::print() {
    int16_t total = count();
    Serial.printf("书单: 共 %d 条 (容量 %d, 版本 %lu)\n", total, capacity(),
                  (unsigned long)version());

    if (total == 0) {
        Serial.println("  （空）");
        return;
    }

    // 逐条打印，每次只持锁拷一条，不在持锁期间做串口输出
    BookItem item;
    for (int16_t i = 0; i < total; i++) {
        if (_mutex == nullptr ||
            xSemaphoreTake(_mutex, pdMS_TO_TICKS(50)) != pdTRUE) {
            break;
        }
        item = _items[i];
        xSemaphoreGive(_mutex);
        Serial.printf("  %2d. id=%d x%u  '%s'\n", (int)(i + 1), (int)item.id,
                      (unsigned)item.count, item.text);
    }
}

const char *bookListLibVersion() {
    return BOOKLISTLIB_VERSION;
}
