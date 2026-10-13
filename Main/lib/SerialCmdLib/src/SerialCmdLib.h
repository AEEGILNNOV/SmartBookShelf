#ifndef SERIAL_CMD_LIB_H
#define SERIAL_CMD_LIB_H

#include <Arduino.h>

#include "BoardConfig.h"

// =============================================================================
// SerialCmdLib —— 串口命令行库
//
// 规则（和主工程习惯一致）：只认“换行结尾”的输入，串口噪声不会误触发。
// 一行格式：  命令[,参数]
//   例：  i          -> 命令 'i'，无参数，arg 为空串
//        l,1        -> 命令 'l'，参数 "1"
//        1,2,3      -> 只按第一个逗号切，参数是 "2,3"
//
// 用法：
//   SerialCmdLib cli;
//   cli.begin();
//   cli.registerCommand('i', "状态", onInfo);        // 静态函数指针
//   cli.startTask();                                 // 或自己在 loop 里 cli.poll()
//   ...
//   static void onInfo(const char *arg) { ... }
//
// 其它任务想“模拟输入一条命令”，调 cli.sendCommand("algo,4") 即可（走同一条队列）。
// =============================================================================

// 最多注册多少条命令
#ifndef CMD_MAX_ITEMS
#define CMD_MAX_ITEMS 24
#endif

// 命令处理函数：arg 是逗号后面的参数（没有参数时是空串 ""，不会为 nullptr）
typedef void (*SerialCmdCallback)(const char *arg);

// 一条命令的注册项
struct SerialCmdItem {
    char cmd;                     // 命令字符（大小写都能匹配）
    const char *help;             // 帮助文本，printHelp 用
    SerialCmdCallback callback;
};

class SerialCmdLib {
public:
    SerialCmdLib();

    // 建命令队列（长度 CMD_QUEUE_SIZE）
    bool begin();

    // 注册一条命令。cmd 传大写字符，小写输入也能匹配。
    bool registerCommand(char cmd, const char *help, SerialCmdCallback callback);

    // 建串口任务（核心 0），任务里轮询串口并分派
    bool startTask();

    // 手动轮询一次（不建任务时在 loop 里调）
    void poll();

    // 从别的任务投递一行命令（不立即执行，走队列）
    bool sendCommand(const char *line);

    // 打印已注册命令的帮助表
    void printHelp();

private:
    void dispatch(const char *line);
    static void serialCmdTaskEntry(void *pvParameters);

    SerialCmdItem _items[CMD_MAX_ITEMS];
    uint8_t _itemNum;

    QueueHandle_t _queue;
    char _buf[CMD_LINE_LEN];
    uint8_t _len;
};

const char *serialCmdLibVersion();

#endif
