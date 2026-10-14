#ifndef BOOK_DOMAIN_LIB_H
#define BOOK_DOMAIN_LIB_H

#include <Arduino.h>

#include "BookList.h"
#include "config.h"

// =============================================================================
// BookDomainLib —— 书籍业务规则（学科映射 / 重名检查 / 书堆顺序）
//
// 为什么单独做一个库：
//   本次改造把"一个学科最多一本书"定为业务规则，这条规则会被三个地方用到：
//     · recogTask（识别到分类 ID 要不要入库）
//     · bookTask（存书 / 取书状态机）
//     · StorageLib（从 Flash 加载时校验，重复的 ID 只留第一条）
//   如果每个地方各写一份判断，早晚会不一致。所以集中到这里，只留一份。
//
// 【分类 ID 到底读哪个字段 —— 这是本项目最容易搞错的地方】
//   HUSKYLENS 2 的分类标签在底层 Result::ID 上，HuskyLib 已经把它暴露成
//   HuskyWord.id（见 lib/HuskyLib/src/HuskyLib.cpp 里 "out->id = (int16_t)res->ID;"）。
//
//   ⚠️ 不要读 Result::classID！Result.h 里 classID 和 xCenter 是**同一个 union 槽位**，
//      读 classID 等价于读"目标中心 X 坐标"，两个字节完全一样。
//      HuskyWord.x 就是那个槽位，所以也**不能**拿 x 当分类标签用。
//
//   本项目里 OCR 的文本结果（words[].content）和分类 ID 是两码事：
//   分类走 id，文字只用来显示，不做关键字匹配（需求明确不开发 OCR 关键字/AI 分类）。
// =============================================================================

// 学科分类 ID 的范围（本次需求固定 1~6）
#define BOOK_ID_MIN 1
#define BOOK_ID_MAX 6

// 入库/取书的结果码。统一用负数表示失败，0 表示成功，方便串口和界面分别处理。
enum BookResult : int8_t {
    BOOK_OK              = 0,     // 成功
    BOOK_ERR_ID_INVALID  = -1,    // 分类 ID 不在 1~6（无效识别结果，不入库、不动机械）
    BOOK_ERR_DUPLICATE   = -2,    // 该学科已经有一本书了（不新增、不累加数量）
    BOOK_ERR_NO_SPACE    = -3,    // 书单满了 / 内存不够
    BOOK_ERR_NOT_FOUND   = -4,    // 找不到这条书（取书时）
    BOOK_ERR_NOT_INITED  = -5,    // BookList 还没 begin()
};

// 把分类 ID 翻译成学科名。
// 【唯一的中文名映射表就在这里】要改学科名 / 加学科，只改 BookDomain.cpp 里的表。
// 非法 ID 返回 "未知"（绝不允许这里读出野指针）。
const char *getBookNameById(int16_t id);

// 分类 ID 是不是本次支持的 1~6
bool bookIdIsValid(int16_t id);

// 在书单里按分类 ID 找：找到返回下标（>=0），没有返回 -1。
int16_t bookFindIndexById(const BookList &books, int16_t id);

// 该学科是否已经在书单里（方案 A：一个学科最多一条记录）
bool bookExists(const BookList &books, int16_t id);

// 新书入库：插到**书堆最顶部**（数组下标 0）。
//   · 先校验 id 1~6；
//   · 再查重，已存在就返回 BOOK_ERR_DUPLICATE（**不累加 count、不改顺序**）；
//   · 通过后把记录插到下标 0，返回 BOOK_OK。
// 返回 BOOK_OK 时 *outIndex 回填它插进去后的下标（正常就是 0）。
BookResult bookAddAtTop(BookList &books, int16_t id, int16_t *outIndex);

// 取书：把第 index 条从书单里删掉，并把被删的记录拷到 *outRemoved（可为 nullptr）。
// ⚠️ 调用方必须**先确认机械动作已经正常完成**才允许调它（见 main.cpp 的取书状态机）。
BookResult bookRemoveAt(BookList &books, int16_t index, BookItem *outRemoved);

// 按分类 ID 取书（界面/串口用 ID 更自然）
BookResult bookRemoveById(BookList &books, int16_t id, BookItem *outRemoved);

// 逻辑书堆顺序的纯文本，形如 "语文 > 数学 > 英语"（下标 0 在最前面 = 书堆最顶部）。
// 用途：调试页显示、串口打印、日志。out 为空时写出"（空）"。
void bookOrderText(const BookList &books, char *out, size_t outSize);

// 结果码 -> 中文说明（串口/界面提示用）
const char *bookResultText(BookResult result);

// 版本字符串
const char *bookDomainLibVersion();

#endif