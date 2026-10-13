#ifndef HUSKY_LIB_H
#define HUSKY_LIB_H

#include <Arduino.h>
#include <DFRobot_HuskylensV2.h>

#include "BoardConfig.h"

// =============================================================================
// HuskyLib —— HUSKYLENS 2（二哈二代）内置 CNN 识别库
//
// 二哈自己带 NPU，CNN 推理全在它内部完成（物体识别/分类/自学习分类/OCR…），
// 本库只负责：I2C 探测与初始化、切算法、把识别结果读成固定大小的结构体、
// 学习/清空/知识库读写。库内没有一行神经网络代码。
//
// 【重要】线程安全：本库直接操作 I2C，同一时刻只允许一个任务调用。
// 建议由同一个任务负责 connect() + readFrame()，其它任务只读快照。
//
// 【重要】设备不在线时，底层库会阻塞 25 秒以上。所以本库所有对外操作
// 之前都会先 probe()（约 1ms）；probe 不通过就直接返回失败，不碰底层库。
// =============================================================================

// 一个识别结果（文字块 / 目标块）
struct HuskyWord {
    int16_t id;                            // 目标 ID（0 = 未学习）
    int16_t level;                         // 附加字段，一般是 0
    int16_t x;                             // 中心 X
    int16_t y;                             // 中心 Y
    int16_t w;                             // 宽
    int16_t h;                             // 高
    char name[HUSKY_TEXT_LEN];             // 二哈里的名称
    char content[HUSKY_TEXT_LEN];          // 识别出来的文字内容
};

// 一帧识别结果
struct HuskyFrame {
    bool online;                           // 二哈是否在线
    int16_t modeIndex;                     // 当前算法在模式表里的下标
    int16_t wordNum;                       // 本帧识别到的块数
    int16_t resultNum;                     // 设备报的识别总数
    int16_t learnedNum;                    // 其中已学习的数量
    int16_t maxId;                         // 设备里已学习的最大 ID
    uint32_t frameCount;                   // 累计读帧数
    uint32_t errorCount;                   // 累计通信错误
    uint32_t recoverCount;                 // 累计重连次数
    uint32_t lastFrameMs;                  // 上一帧的时间戳(millis)
    HuskyWord words[HUSKY_WORD_MAX];       // 识别到的块
};

// 支持的算法（模式）表：alpha 是串口按键
struct HuskyModeInfo {
    char alpha;
    eAlgorithm_t algo;
    const char *name;                      // 中文名
};

extern const HuskyModeInfo HUSKY_MODE_TABLE[];
extern const uint8_t HUSKY_MODE_COUNT;

class HuskyLib {
public:
    HuskyLib();

    // 初始化 I2C 总线与库参数。只做总线初始化，不碰二哈，设备不在也不会卡。
    void begin(int sda, int scl);

    // 快速探测：二哈 I2C 地址是否应答，约 1ms。
    bool probe();

    // 探测 + 初始化 + 切到当前算法（切算法会重试并等待模型加载）。
    bool connect();

    // 主动标记掉线，下一次循环会重连
    void markOffline();
    bool online() const;

    // ---- 算法（模式）----
    uint8_t modeCount() const;
    int16_t modeIndex() const;                       // 当前下标
    const char *modeName() const;                    // 当前算法中文名
    bool setModeByIndex(int16_t index);              // 换了会重连，返回是否变化
    bool setModeByAlpha(char alpha);                 // 串口按键切算法
    bool setModeNext();                              // 切到下一个

    // ---- 读结果 ----
    // 读一帧并填进 frame。返回：>=0 读到（0 表示画面无目标）；-1 通信超时。
    int16_t readFrame(HuskyFrame &frame);

    // ---- 学习 ----
    uint8_t learn();                                 // 学习画面中心目标，返回新 ID（0=失败）
    bool forget();                                   // 清空当前算法的学习内容
    bool saveKnowledge(uint8_t slot);                // 存到槽位 0~3
    bool loadKnowledge(uint8_t slot);

    // ---- 调试 ----
    String getAlgoParam(const char *key);            // 读算法参数，例如 "lang"
    void dumpAlgoParam();                            // 打印一组常用参数
    void printState();                               // 打印连接/算法/统计

private:
    eAlgorithm_t currentAlgo() const;

    HuskylensV2 _husky;      // 底层驱动（vendor 在 lib/DFRobot_HuskylensV2）
    int16_t _modeIndex;      // 当前模式下标
    bool _online;            // 是否已连接
    uint32_t _lastReadMs;    // 上一次读帧时间
    uint32_t _frames;        // 累计帧数
    uint32_t _errors;        // 累计错误
    uint32_t _recovers;      // 累计重连
};

// 版本字符串
const char *huskyLibVersion();

#endif
