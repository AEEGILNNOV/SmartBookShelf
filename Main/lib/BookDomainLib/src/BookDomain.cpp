#include "BookDomain.h"

// =============================================================================
// BookDomainLib 实现
//
// 整个库没有动态内存、没有任务、不加锁：它只操作调用方传进来的 BookList，
// 而 BookList 自己内部有互斥锁。所以这个库可以在任何任务里安全调用。
// =============================================================================

// -----------------------------------------------------------------------------
// 学科映射表：分类 ID -> 学科名
//
// ⚠️ 这是**全工程唯一**的学科名来源。界面、串口日志、Flash 里的 text 字段、
//    书堆顺序文本全部走它。要加学科/改名，只改这张表（顺带改 BOOK_ID_MAX）。
//
// 表里下标 0 位置的 "未知" 是给 id=0 兜底的：二哈在"没学习过"时会给出 ID=0，
// 那种结果本来就是无效的，bookIdIsValid() 会先把它拦掉。
// -----------------------------------------------------------------------------
static const char *BOOK_NAME_TABLE[] = {
    "未知",     // 0：无效/未学习
    "语文",     // 1
    "数学",     // 2
    "英语",     // 3
    "物理",     // 4
    "化学",     // 5
    "生物",     // 6
};

// 表里有几个元素（用 sizeof 算，以后加学科不用手工改数字）
#define BOOK_NAME_TABLE_NUM (sizeof(BOOK_NAME_TABLE) / sizeof(BOOK_NAME_TABLE[0]))

// -----------------------------------------------------------------------------
// ID <-> 名字
// -----------------------------------------------------------------------------
bool bookIdIsValid(int16_t id) {
    return (id >= BOOK_ID_MIN) && (id <= BOOK_ID_MAX);
}

const char *getBookNameById(int16_t id) {
    // 先做范围检查再查表：越界访问数组是嵌入式里最难查的一类问题，
    // 而且这里的 id 直接来自二哈的识别结果（外部输入），必须校验。
    if (id < 0 || (size_t)id >= BOOK_NAME_TABLE_NUM) {
        return "未知";
    }
    return BOOK_NAME_TABLE[id];
}

const char *bookResultText(BookResult result) {
    switch (result) {
        case BOOK_OK:
            return "成功";
        case BOOK_ERR_ID_INVALID:
            return "分类 ID 无效(必须是 1~6)";
        case BOOK_ERR_DUPLICATE:
            return "该学科的书籍已存在";
        case BOOK_ERR_NO_SPACE:
            return "书单已满或内存不足";
        case BOOK_ERR_NOT_FOUND:
            return "书单里没有这本书";
        case BOOK_ERR_NOT_INITED:
            return "书单还没初始化";
        default:
            return "未知结果";
    }
}

// -----------------------------------------------------------------------------
// 查 / 查重
// -----------------------------------------------------------------------------
int16_t bookFindIndexById(const BookList &books, int16_t id) {
    // BookList::findById 不是 const 成员（它内部要拿锁），所以这里转一下类型。
    // 这样做的代价是"接口上看起来不像只读"，但换来的是不用给 BookList 再加一套
    // const 重载（对作者来说那反而是更难看懂的东西）。
    BookList &list = const_cast<BookList &>(books);
    return list.findById(id);
}

bool bookExists(const BookList &books, int16_t id) {
    return bookFindIndexById(books, id) >= 0;
}

// -----------------------------------------------------------------------------
// 入库：插到书堆最顶部
//
// 为什么插到顶部而不是追加：机械上新书是从**顶部**放进去的，
// 所以书堆从上到下的顺序就是"最新的在最上面"。数组下标 0 表示最顶部，
// 这样"目标书的升降位置"就可以直接用下标乘每本书步数算出来，不用正负号绕来绕去。
// -----------------------------------------------------------------------------
BookResult bookAddAtTop(BookList &books, int16_t id, int16_t *outIndex) {
    if (outIndex != nullptr) {
        *outIndex = -1;
    }

    // ① 分类 ID 必须是 1~6。
    //    ⚠️ 这里是"识别到的分类 ID"，不是 OCR 文字、不是目标 ID。
    //    无效就**不入库**，也绝不允许调用方继续去触发机械动作。
    if (!bookIdIsValid(id)) {
        return BOOK_ERR_ID_INVALID;
    }

    // ② 方案 A：同一个学科最多一条记录。
    //    已存在就返回重复错误，**不累加 count、不改顺序**。
    //    ⚠️ 注意这只保证"书单里每个学科一条"，无法阻止用户把重复学科的实体书
    //       真的放进书堆——那个限制在界面和 README 里要如实说明。
    if (bookExists(books, id)) {
        return BOOK_ERR_DUPLICATE;
    }

    // ③ 插到下标 0（书堆最顶部）
    int16_t index = books.insertAt(0, id, getBookNameById(id));
    if (index < 0) {
        return BOOK_ERR_NO_SPACE;
    }

    if (outIndex != nullptr) {
        *outIndex = index;
    }
    return BOOK_OK;
}

// -----------------------------------------------------------------------------
// 取书：从书单里删掉一条
//
// ⚠️ 这个函数只负责**改数据**。调用方（bookTask）必须先确认机械动作已经正常完成，
//    才允许调它。取书动作一开始就删记录是绝对不行的：机械失败了书还在书堆里，
//    书单却已经空了，两边就对不上了。
// -----------------------------------------------------------------------------
BookResult bookRemoveAt(BookList &books, int16_t index, BookItem *outRemoved) {
    // 先把记录读出来（日志/后续处理要用），再删
    BookItem item;
    memset(&item, 0, sizeof(item));
    if (!books.at(index, &item)) {
        return BOOK_ERR_NOT_FOUND;
    }

    if (books.removeAt(index) < 0) {
        return BOOK_ERR_NOT_FOUND;
    }

    if (outRemoved != nullptr) {
        *outRemoved = item;
    }
    return BOOK_OK;
}

BookResult bookRemoveById(BookList &books, int16_t id, BookItem *outRemoved) {
    int16_t index = bookFindIndexById(books, id);
    if (index < 0) {
        return BOOK_ERR_NOT_FOUND;
    }
    return bookRemoveAt(books, index, outRemoved);
}

// -----------------------------------------------------------------------------
// 书堆顺序文本：下标 0 在最前面（= 书堆最顶部）
// -----------------------------------------------------------------------------
void bookOrderText(const BookList &books, char *out, size_t outSize) {
    if (out == nullptr || outSize == 0) {
        return;
    }
    out[0] = 0;

    // 快照一把（最多 CFG_BOOKLIST_SNAP_MAX 条），拼接过程不占 BookList 的锁
    BookItem snap[CFG_BOOKLIST_SNAP_MAX];
    memset(snap, 0, sizeof(snap));
    BookList &list = const_cast<BookList &>(books);
    int16_t num = list.snapshot(snap, CFG_BOOKLIST_SNAP_MAX);

    if (num <= 0) {
        snprintf(out, outSize, "（空）");
        return;
    }

    size_t used = 0;
    for (int16_t i = 0; i < num; i++) {
        // 顶层那本用 "[" 标出来：一眼看出"新书在顶部"这个约定有没有生效
        const char *name = getBookNameById(snap[i].id);
        const char *prefix = (i == 0) ? "[" : "";
        const char *suffix = (i == 0) ? "]" : "";

        // 分隔符只在第 2 条开始加，避免第一条前面多一个 " > "
        const char *sep = (i == 0) ? "" : " > ";

        int written = snprintf(out + used, outSize - used, "%s%s%s%s", sep,
                              prefix, name, suffix);
        if (written < 0) {
            break;
        }
        if ((size_t)written >= outSize - used) {
            // 缓冲区满了：snprintf 已经截断并且补了结束符，直接结束
            used = outSize - 1;
            break;
        }
        used += (size_t)written;
    }
}

const char *bookDomainLibVersion() {
    return BOOKDOMAINLIB_VERSION;
}