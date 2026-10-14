#ifndef HUSKY_CAM_H
#define HUSKY_CAM_H

#include <Arduino.h>
#include <DFRobot_HuskylensV2.h>
#include <Wire.h>

#include "HuskyConfig.h"

// =============================================================================
// HUSKYLENS 2（二哈二代）封裝 —— 内置 CNN 识别
// 二哈自己带 NPU，CNN 推理全在它内部跑，ESP32-S3 只负责：
//   1) I2C 探测/初始化/切算法
//   2) 把识别结果读回来，转成固定大小的结构体，供任务之间传递
// 所以这个类里没有神经网络代码，只有通信和结果搬运。
//
// 注意（血泪经验，都来自 BookDispenser/husky_test）：
//   1) 设备不在线时库接口会阻塞 25 秒以上并刷屏 I2C 错误，
//      所以任何库调用之前都必须先 probe()（约 1ms）。
//   2) res->classID 和 res->xCenter 是同一个内存槽位（union），
//      要拿目标 ID 必须用 res->ID，不能用 res->classID。
//   3) 该类不是线程安全的，只允许 huskyTask 一个任务调用。
// =============================================================================

// 一个识别结果（文字块/目标块）
struct HuskyWord {
    int16_t id;                                 // 目标 ID（0 = 未学习）
    int16_t level;                              // 附加字段（一般是 0）
    int16_t x;                                  // 中心 X
    int16_t y;                                  // 中心 Y
    int16_t w;                                  // 宽
    int16_t h;                                  // 高
    char name[WORD_TEXT_LEN];                   // 二哈里的名称
    char content[WORD_TEXT_LEN];                // 识别出来的文字内容
};

class HuskyCam {
public:
    HuskyCam();

    // 初始化 I2C 并设置参数。只做总线初始化，不碰二哈。
    void begin(int sda, int scl);

    // 快速探测：二哈 I2C 地址 0x50 是否应答。约 1ms，可随便调。
    bool probe();

    // 探测 + begin + 切到目标算法。成功返回 true。
    bool connect();

    bool connected() const;
    void markOffline();

    // 当前算法
    eAlgorithm_t algorithm() const;
    void setAlgorithm(eAlgorithm_t algo);

    // 读取当前画面结果。
    // 返回 60 表示读回来 wordNum 个结果；-1 表示通信超时（真故障）。
    // 注意二哈的 getResult() 是阻塞的，一次最长可能等好几秒。
    int16_t readWords(HuskyWord *words, uint8_t maxNum, uint8_t &wordNum);

    // 统计信息（读一次结果后有效）
    int16_t cachedResultNum();
    int16_t cachedLearnedNum();
    int16_t cachedMaxId();

    // 学习当前画面中心目标（CNN 特征入库），返回新 ID（0 = 失败）
    uint8_t learn();

    // 清空当前算法的全部学习内容
    bool forget();

    // 知识库槽位 0~3，学习完保存后断电不丢
    bool saveKnowledge(uint8_t slot);
    bool loadKnowledge(uint8_t slot);

    // 读取一个算法参数字符串（调试用，例如 "lang"）
    String getAlgoParam(const char *key);

    // 修改库内部重试次数（影响阻塞时间）
    void setRetry(uint8_t retry);

private:
    HuskylensV2 _husky;
    eAlgorithm_t _algo;
    bool _ok;
};

// -----------------------------------------------------------------------------
// 算法表：二哈 2 代支持 20 多种内置 CNN 算法，这里挑和“图书识别”相关的几种
// 做测试用。alpha 是串口里切换算法用的按键。
// -----------------------------------------------------------------------------
struct HuskyAlgoInfo {
    char alpha;                 // 串口按键
    eAlgorithm_t algo;          // 算法枚举
    const char *name;           // 中文名（打印用）
};

extern const HuskyAlgoInfo HUSKY_ALGO_TABLE[];
extern const uint8_t HUSKY_ALGO_COUNT;

// 默认算法在 .cpp 里设置（见 kDefaultAlgo），本项目用 OCR 文字识别。

#endif
