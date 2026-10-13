#ifndef BOARD_CONFIG_H
#define BOARD_CONFIG_H

// =============================================================================
// SmartBookShelf / Main —— 全工程唯一引脚与板级参数来源
//
// 规则：所有引脚只在这里定义，库和 main.cpp 一律 include 本文件；
//       每个宏都用 #ifndef 包住，方便在 platformio.ini 里用 -DXXX=yyy 覆盖。
//
// ESP32-S3 N16R8 可用 GPIO 只有 0-21 和 38-48。绝对不能接外设：
//   GPIO 26-32  封装内 SPI Flash
//   GPIO 33-37  封装内 OPI PSRAM（8MB）
//   GPIO 22-25  该芯片上不存在
//   GPIO 19/20  原生 USB D-/D+（留给烧录与监视）
//   GPIO 43/44  UART0 控制台
// =============================================================================

// -----------------------------------------------------------------------------
// 显示：2.8" SPI ILI9341 240x320，竖屏
// 注意：TFT_eSPI 的实际引脚是在 platformio.ini 的 build_flags 里配置的
//       （-DTFT_MOSI=11 这类），这里只是给业务代码看的“文档性”定义。
// -----------------------------------------------------------------------------
#ifndef PIN_TFT_SCLK
#define PIN_TFT_SCLK 12
#endif
#ifndef PIN_TFT_MOSI
#define PIN_TFT_MOSI 11
#endif
#ifndef PIN_TFT_CS
#define PIN_TFT_CS 10
#endif
#ifndef PIN_TFT_DC
#define PIN_TFT_DC 9
#endif
#ifndef PIN_TFT_RST
#define PIN_TFT_RST 8
#endif
#ifndef PIN_TFT_BL
#define PIN_TFT_BL 47
#endif

// 屏幕分辨率与方向（业务代码里用这两个宏算坐标，不要写死数字）
#ifndef SCR_W
#define SCR_W TFT_WIDTH
#endif
#ifndef SCR_H
#define SCR_H TFT_HEIGHT
#endif

// -----------------------------------------------------------------------------
// HUSKYLENS 2（二哈二代）I2C 内置 CNN
//   绿线 SDA -> GPIO1   蓝线 SCL -> GPIO2   VCC=5V/3V3   GND 必须共地
//   二哈屏幕：系统设置 -> 协议类型 -> I2C
// I2C 地址固定 0x50（库内部写死）。
// -----------------------------------------------------------------------------
#ifndef PIN_HUSKY_SDA
#define PIN_HUSKY_SDA 1
#endif
#ifndef PIN_HUSKY_SCL
#define PIN_HUSKY_SCL 2
#endif

// 二哈 I2C 地址
#ifndef HUSKY_I2C_ADDR
#define HUSKY_I2C_ADDR 0x50
#endif

// I2C 时钟(Hz)。库每次收发都会重新设时钟，降速要改这个宏。
#ifndef HUSKY_I2C_HZ
#define HUSKY_I2C_HZ 100000
#endif

// I2C 事务超时(ms)。ESP32 Wire 默认只有 50ms，二哈图像处理会拉伸时钟，
// 默认值下协议往返实测三到五成失败，别改小。
#ifndef HUSKY_I2C_TIMEOUT_MS
#define HUSKY_I2C_TIMEOUT_MS 1000
#endif

// 库内部重试次数。库每次发送都会把超时硬写回 5000ms，
// 设备不在时的阻塞时间 ≈ retry × 超时，调小可减少等待。
#ifndef HUSKY_RETRY
#define HUSKY_RETRY 3
#endif

// 库等待回复的超时(ms)
#ifndef HUSKY_TIMEOUT_MS
#define HUSKY_TIMEOUT_MS 800
#endif

// 识别节拍：一帧 CNN 结果大约几十毫秒，300ms 一帧够调试用
#ifndef HUSKY_READ_INTERVAL_MS
#define HUSKY_READ_INTERVAL_MS 300
#endif

// 二哈掉线后的重连间隔(ms)
#ifndef HUSKY_RECONNECT_INTERVAL_MS
#define HUSKY_RECONNECT_INTERVAL_MS 2000
#endif

// 切换算法后等 CNN 模型加载的时间(ms)。官方示例是 5 秒，太早读会读不到。
#ifndef HUSKY_ALGO_SETTLE_MS
#define HUSKY_ALGO_SETTLE_MS 4000
#endif

// -----------------------------------------------------------------------------
// 按键（可选）：按下为高电平，不接时被内部下拉拉低，不会误触发
// 警告：GPIO40/41/42 同时是 OV2640 的 D5/D6/D7。
//       本工程不接 OV2640；若线上同时接着摄像头，三个按键不要接。
// -----------------------------------------------------------------------------
#ifndef BUTTON_PIN1
#define BUTTON_PIN1 42
#endif
#ifndef BUTTON_PIN2
#define BUTTON_PIN2 41
#endif
#ifndef BUTTON_PIN3
#define BUTTON_PIN3 40
#endif

// 按下时的电平（1 = 按下高电平）
#ifndef BUTTON_TRUE_VALUE
#define BUTTON_TRUE_VALUE 1
#endif

// -----------------------------------------------------------------------------
// 任务节奏 / 队列长度
// -----------------------------------------------------------------------------
#ifndef DISPLAY_INTERVAL_MS
#define DISPLAY_INTERVAL_MS 500
#endif
#ifndef SYSINFO_INTERVAL_MS
#define SYSINFO_INTERVAL_MS 1000
#endif
#ifndef BUTTON_QUEUE_SIZE
#define BUTTON_QUEUE_SIZE 10
#endif
#ifndef CMD_QUEUE_SIZE
#define CMD_QUEUE_SIZE 8
#endif

// -----------------------------------------------------------------------------
// 共享缓冲区大小
// -----------------------------------------------------------------------------
// 识别文字/名称最长字节数（UTF-8 一个汉字 3 字节）
#ifndef HUSKY_TEXT_LEN
#define HUSKY_TEXT_LEN 40
#endif

// 一帧最多缓存几个识别块（二哈库在 ESP32 上 MAX_RESULT_NUM=10）
#ifndef HUSKY_WORD_MAX
#define HUSKY_WORD_MAX 8
#endif

// 串口命令行缓冲
#ifndef CMD_LINE_LEN
#define CMD_LINE_LEN 48
#endif

// -----------------------------------------------------------------------------
// 库版本（library.json 里也有一份，这里给运行时打印用）
// -----------------------------------------------------------------------------
#ifndef HUSKYLIB_VERSION
#define HUSKYLIB_VERSION "1.0.0"
#endif
#ifndef DISPLAYLIB_VERSION
#define DISPLAYLIB_VERSION "1.0.0"
#endif
#ifndef SYSINFOLIB_VERSION
#define SYSINFOLIB_VERSION "1.0.0"
#endif
#ifndef BUTTONLIB_VERSION
#define BUTTONLIB_VERSION "1.0.0"
#endif
#ifndef SERIALCMDLIB_VERSION
#define SERIALCMDLIB_VERSION "1.0.0"
#endif

#endif
