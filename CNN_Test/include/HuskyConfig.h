#ifndef HUSKY_CONFIG_H
#define HUSKY_CONFIG_H

// =============================================================================
// CNN_Test 全局配置（HUSKYLENS 2 内置 CNN 识别测试）
// 所有参数都可以在 platformio.ini 的 build_flags 里用 -DXXX=yyy 覆盖，
// 不要直接改源文件里的引脚，方便以后换板子/换接线。
// =============================================================================

// -----------------------------------------------------------------------------
// I2C 接线（二哈 HUSKYLENS 2）
//   VCC -> 5V(或3V3)   GND -> GND(必须共地)
//   绿线 SDA -> GPIO1  蓝线 SCL -> GPIO2
// 机器里：二哈屏幕 系统设置 -> 协议类型 -> 必须选 I2C
// 注意：GPIO1/GPIO2 是 ESP32-S3 的 ADC1 通道，默认没有外部上拉，
//       靠 ESP32 内部上拉也能跑；若通信丢包严重，先怀疑上拉和线材。
// -----------------------------------------------------------------------------
#ifndef PIN_HUSKY_SDA
#define PIN_HUSKY_SDA 1
#endif
#ifndef PIN_HUSKY_SCL
#define PIN_HUSKY_SCL 2
#endif

// I2C 时钟速率(Hz)。库内部每次收发都会 wire->setClock(HUSKY_I2C_HZ)，
// 所以要降速只能在这里用 build_flags 覆盖。默认 100kHz。
#ifndef HUSKY_I2C_HZ
#define HUSKY_I2C_HZ 100000
#endif

// I2C 事务超时(ms)。关键参数！ESP32 Wire 默认只有 50ms，二哈处理图像时会
// 长时间拉伸时钟，默认值下协议往返实测有三到五成失败；放到 1000ms 后好转。
#ifndef HUSKY_I2C_TIMEOUT_MS
#define HUSKY_I2C_TIMEOUT_MS 1000
#endif

// 库内部重试次数。库的 protocolWrite() 每次都会把发送超时硬写回 5000ms，
// 所以只能靠调小 retry 来减少“设备不在时”的阻塞时间。
#ifndef HUSKY_RETRY
#define HUSKY_RETRY 3
#endif

// 库等待回复的超时(ms)。
#ifndef HUSKY_TIMEOUT_MS
#define HUSKY_TIMEOUT_MS 800
#endif

// -----------------------------------------------------------------------------
// 任务节奏(ms)
// -----------------------------------------------------------------------------
// 一次 CNN 识别大概几十毫秒，300ms 一帧对调试足够，也留出裕量给打印任务。
#ifndef HUSKY_READ_INTERVAL_MS
#define HUSKY_READ_INTERVAL_MS 300
#endif

// 二哈掉线后的重连间隔
#ifndef HUSKY_RECONNECT_INTERVAL_MS
#define HUSKY_RECONNECT_INTERVAL_MS 2000
#endif

// 串口打印间隔
#ifndef PRINT_INTERVAL_MS
#define PRINT_INTERVAL_MS 500
#endif

// 系统信息采集间隔
#ifndef MONITOR_INTERVAL_MS
#define MONITOR_INTERVAL_MS 2000
#endif

// -----------------------------------------------------------------------------
// 缓冲区大小
// -----------------------------------------------------------------------------
// 单个结果里文字/名称的最大字节数(UTF-8 中文一个字 3 字节)
#ifndef WORD_TEXT_LEN
#define WORD_TEXT_LEN 40
#endif

// 一帧最多缓存多少个识别块。二哈库在 ESP32 上 MAX_RESULT_NUM=10，
// 这里给 8 就够，省点 RAM。
#ifndef WORD_MAX_NUM
#define WORD_MAX_NUM 8
#endif

// 串口命令行缓冲
#ifndef CMD_BUF_LEN
#define CMD_BUF_LEN 16
#endif

// -----------------------------------------------------------------------------
// 队列长度
// -----------------------------------------------------------------------------
// 结果快照是“邮箱队列”：只保留最新一帧，满了直接覆盖旧数据。
// 打印任务 500ms 取一次，没必要排队积压。
#ifndef SNAPSHOT_QUEUE_SIZE
#define SNAPSHOT_QUEUE_SIZE 1
#endif

// 串口命令队列，长度大一点，避免连按命令丢指令。
#ifndef CMD_QUEUE_SIZE
#define CMD_QUEUE_SIZE 8
#endif

#endif
