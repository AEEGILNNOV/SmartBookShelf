#ifndef CONFIG_H
#define CONFIG_H

#include <stdint.h>

// =============================================================================
// SmartBookShelf / Main —— 全工程唯一配置来源（config.h）
//
// 【怎么用】
//   1. 想改任何参数（引脚、任务节奏、队列长度、开关），只改本文件；
//   2. 也可以用 platformio.ini 的 build_flags 里的 -DXXX=yyy 临时覆盖，
//      因为下面**每一个宏都用 #ifndef 包住**（-D 的优先级更高）；
//   3. 库和 main.cpp 一律 #include "config.h"，不要各自写一份默认值。
//
// 【包含顺序（重要）】
//   第 3 节的 SCR_W/SCR_H 用到 TFT_WIDTH/TFT_HEIGHT，字体宏用到 lv_font_xxx，
//   所以**需要用到这两组宏的文件，必须先 include <TFT_eSPI.h> / <lvgl.h> 再 include
//   "config.h"**（DisplayLib.h 已经是这个顺序）。不碰屏幕的库（Husky/Button/…）
//   直接 include "config.h" 即可。
//
// 【本文件与 BoardConfig.h 的关系】
//   BoardConfig.h 现在只剩一行 #include "config.h"，只是兼容壳，
//   老代码里的 #include "BoardConfig.h" 继续有效。新代码请直接 include config.h。
//
// 【硬件红线：ESP32-S3 N16R8 绝对不能接外设的引脚】
//   GPIO 26-32  封装内 SPI Flash
//   GPIO 33-37  封装内 OPI PSRAM（8MB）
//   GPIO 22-25  该芯片上不存在
//   GPIO 19/20  原生 USB D-/D+（留给烧录与监视）
//   GPIO 43/44  UART0 控制台
//   实际可用：GPIO 0-18、38-42、45-48
//
// 【引脚占用总表（改线只改这里）】
//   ILI9341 屏   SCLK=12 MOSI=11 CS=10 DC=9 RST=8 BL=47
//   HUSKYLENS 2  SDA=1  SCL=2
//   按键         BTN1=45 BTN2=46 BTN3=0     （0 是 BOOT 脚，见第 9 节警告）
//   步进电机1    4,5,6,7                     （ULN2003 的 IN1~IN4）
//   步进电机2    15,16,17,18                 （ULN2003 的 IN1~IN4）
//   步进电机3    38,39,48,21                 （机械到位后再接）
//   HC-SR04      TRIG=4 ECHO=5               （与电机1冲突，二选一，见第 8 节）
//   空闲未用     3,13,14,20,39,48,21 …
// =============================================================================


// -----------------------------------------------------------------------------
// 1. 模块总开关
//
// 置 0 的模块不建任务、不初始化，省 RAM 也便于分步排查。
// 用法示例：-DCFG_ENABLE_STEPPER=0  临时关掉步进
// -----------------------------------------------------------------------------
#ifndef CFG_ENABLE_HUSKY
#define CFG_ENABLE_HUSKY 1        // 二哈 HUSKYLENS 2 识别（recogTask）
#endif
#ifndef CFG_ENABLE_DISPLAY
#define CFG_ENABLE_DISPLAY 1      // 屏幕 + LVGL 界面（displayTask，在 DisplayLib 内）
#endif
#ifndef CFG_ENABLE_SYSINFO
#define CFG_ENABLE_SYSINFO 1      // 系统信息采集（monitorTask）
#endif
#ifndef CFG_ENABLE_BUTTON
#define CFG_ENABLE_BUTTON 1       // 按键（buttonTask，在 ButtonLib 内）
#endif
#ifndef CFG_ENABLE_SERIALCMD
#define CFG_ENABLE_SERIALCMD 1    // 串口命令行（serialCmdTask，在 SerialCmdLib 内）
#endif
#ifndef CFG_ENABLE_STEPPER
#define CFG_ENABLE_STEPPER 1      // 步进电机（stepperTask）
#endif
#ifndef CFG_ENABLE_WAVE
#define CFG_ENABLE_WAVE 1         // 超声波测距（waveTask）
#endif

// 以后才做的功能，先留开关占位，默认全关，避免误连不存在的硬件
#ifndef CFG_ENABLE_CAMERA
#define CFG_ENABLE_CAMERA 0       // 本地 OV2640 取图（与二哈 SDA=GPIO1 冲突，见第 9 节）
#endif
#ifndef CFG_ENABLE_WIFI
#define CFG_ENABLE_WIFI 0         // WiFi 联网
#endif
#ifndef CFG_ENABLE_LLM
#define CFG_ENABLE_LLM 0          // DeepSeek 归类
#endif
#ifndef CFG_ENABLE_VOICE
#define CFG_ENABLE_VOICE 0        // 语音（Wave_Test 目前是空模板）
#endif
#ifndef CFG_ENABLE_LORA
#define CFG_ENABLE_LORA 0         // LoRa / ESP-NOW 组网
#endif

// config.h 只给宏，这里再给一份“编译期常量”，方便 main.cpp 写成
//   if (CFG_RUN_DISPLAY) { ... }   而不是每个调用点都套一组 #if。
// 判断结果和上面的开关完全一致，开关一改，这里自动跟着变。
// 命名用 CFG_RUN_xxx（RUN = 这次固件真的会跑这个模块）。
#define CFG_RUN_HUSKY     (CFG_ENABLE_HUSKY != 0)
#define CFG_RUN_DISPLAY   (CFG_ENABLE_DISPLAY != 0)
#define CFG_RUN_SYSINFO   (CFG_ENABLE_SYSINFO != 0)
#define CFG_RUN_BUTTON    (CFG_ENABLE_BUTTON != 0)
#define CFG_RUN_SERIALCMD (CFG_ENABLE_SERIALCMD != 0)
#define CFG_RUN_STEPPER   (CFG_ENABLE_STEPPER != 0)
#define CFG_RUN_WAVE      (CFG_ENABLE_WAVE != 0)


// -----------------------------------------------------------------------------
// 2. 串口
// -----------------------------------------------------------------------------
// 控制台波特率。改这里要同步改 platformio.ini 的 monitor_speed。
#ifndef CFG_SERIAL_BAUD
#define CFG_SERIAL_BAUD 115200
#endif

// 开机等串口稳定的时间(ms)
#ifndef CFG_SERIAL_BOOT_DELAY_MS
#define CFG_SERIAL_BOOT_DELAY_MS 200
#endif

// 串口打印等级：0=都不打 1=只打错误 2=正常信息(默认) 3=详细调试
// 业务代码统一用 CFG_LOG(level, fmt, ...) 宏，别再用裸 Serial.printf 打调试信息。
#ifndef CFG_LOG_LEVEL
#define CFG_LOG_LEVEL 2
#endif

// 是否打印中文标题（有些串口终端显示中文会乱码，可临时关掉）
#ifndef CFG_LOG_CHINESE
#define CFG_LOG_CHINESE 1
#endif


// -----------------------------------------------------------------------------
// 3. 显示：2.8" SPI ILI9341 240x320，竖屏
//
// 注意：TFT_eSPI 真正吃的是 platformio.ini 里的 -DTFT_MOSI=11 这类宏，
//       下面的 PIN_TFT_xxx 是给业务代码看的（避免业务里到处写死数字）。
//       两处必须保持一致，改引脚记得两边都改。
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

// 分辨率：SCR_W/SCR_H 不在本文件定义，它们在 DisplayLib.h 里从 TFT_WIDTH/TFT_HEIGHT
// 推导（因为本文件会被不碰屏幕的库 include，那时 TFT_WIDTH 还不存在）。
// 下面这段只做“包含顺序”体检：如果已经 include 了 <TFT_eSPI.h>，却拿不到 TFT_WIDTH，
// 说明 TFT_eSPI 的 USER_SETUP 没生效，会在这里直接报错，
// 而不是在别处报一句看不懂的 “TFT_WIDTH was not declared”。
#if defined(_TFT_eSPIH_) && !defined(TFT_WIDTH)
#error "config.h: TFT_eSPI 的 USER_SETUP 宏没生效，请检查 platformio.ini 的 build_flags"
#endif

// 背光亮度（LEDC 占空比 0~255）。排错时不要直接把 BL 接 VCC。
#ifndef CFG_TFT_BL_LEVEL
#define CFG_TFT_BL_LEVEL 255
#endif

// LVGL 刷屏缓冲：2 块 240x40 的 16 位缓冲 ≈ 19.2KB，优先放 PSRAM
// 这一项就是 DisplayLib.cpp 里刷屏缓冲的行数（原来叫 DISPLAY_DRAW_LINES，已合并过来）。
// 屏幕出现横向撕裂/闪烁就往下调到 20；内部 RAM 更紧就往上调到 80。
#ifndef CFG_LV_BUF_LINES
#define CFG_LV_BUF_LINES 40
#endif

// 一页最多记录几本书（按 ID + 书名去重）
#ifndef MAX_BOOK_RECORDS
#define MAX_BOOK_RECORDS 12
#endif

// 屏上字体：中文用自己生成的全字库 ui_font_16，英文数字用 montserrat_12。
// ⚠️ 不要再用 LVGL 自带的 lv_font_simsun_16_cjk：它只有 1097 个汉字，
//    界面里的“识/别/书/测/距”等字不在里面，会显示成空白。
// ui_font_16 由 Main/font/ui_font_16.c 提供（GB2312 一级汉字 3755 + 界面用字），
// 重新生成见 Main/fontgen/README.md；开关在 include/lv_conf.h。
#ifndef DISPLAY_FONT_CJK
#define DISPLAY_FONT_CJK (&ui_font_16)
#endif
#ifndef DISPLAY_FONT_ASCII
#define DISPLAY_FONT_ASCII (&lv_font_montserrat_12)
#endif

// 消息页自动消失时间(ms)
#ifndef CFG_MESSAGE_HOLD_MS
#define CFG_MESSAGE_HOLD_MS 3000
#endif

// 消息文字缓冲（UTF-8 一个汉字 3 字节，96 字节能放一条整句）
#ifndef CFG_MESSAGE_LEN
#define CFG_MESSAGE_LEN 96
#endif

// 状态页正文缓冲（多行快照文本，别太大，屏上放不下也白搭）
#ifndef CFG_STATUS_TEXT_LEN
#define CFG_STATUS_TEXT_LEN 512
#endif

// 显示任务一轮的节拍(ms)：实际是否重画由“脏标志 + 内容签名”决定，改大不闪屏
#ifndef CFG_DISPLAY_INTERVAL_MS
#define CFG_DISPLAY_INTERVAL_MS 5
#endif

// 内容签名里“帧数”按每 N 帧参与一次比较，避免每帧都重画
// 4 = 每 4 帧才可能刷新一次状态页上的帧计数
#ifndef CFG_SIGNATURE_FRAME_STEP
#define CFG_SIGNATURE_FRAME_STEP 4
#endif

// 页脚操作提示。改按键定义时同步改这一行（LVGL 只认 ASCII 才会用 montserrat_12）
#ifndef CFG_FOOTER_HINT
#define CFG_FOOTER_HINT "k1 next k2 recon k3 learn kA mark kB thick"
#endif

// 最多显示（或参与签名比较）几条识别块
#ifndef CFG_STATUS_WORD_SHOWN
#define CFG_STATUS_WORD_SHOWN 4
#endif


// -----------------------------------------------------------------------------
// 4. HUSKYLENS 2（二哈二代）I2C 内置 CNN / OCR
//   绿线 SDA -> GPIO1   蓝线 SCL -> GPIO2   VCC=5V/3V3   GND 必须共地
//   二哈屏幕：系统设置 -> 协议类型 -> I2C
// -----------------------------------------------------------------------------
#ifndef PIN_HUSKY_SDA
#define PIN_HUSKY_SDA 1
#endif
#ifndef PIN_HUSKY_SCL
#define PIN_HUSKY_SCL 2
#endif

// I2C 地址（库内部写死，这里给扫描/打印用）
#ifndef HUSKY_I2C_ADDR
#define HUSKY_I2C_ADDR 0x50
#endif

// I2C 时钟(Hz)。库每次收发都会重新设时钟；通信不稳先降到 50000。
#ifndef HUSKY_I2C_HZ
#define HUSKY_I2C_HZ 100000
#endif

// I2C 事务超时(ms)。ESP32 Wire 默认只有 50ms，二哈处理图像会拉伸时钟，
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

// 识别节拍：一帧 CNN 结果几十毫秒，300ms 一帧够调试用
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

// 单条识别文字的最大字节数（UTF-8 一个汉字 3 字节）
#ifndef HUSKY_TEXT_LEN
#define HUSKY_TEXT_LEN 40
#endif

// 一帧最多缓存几个识别块（二哈库在 ESP32 上 MAX_RESULT_NUM=10，这里给 8 省 RAM）
#ifndef HUSKY_WORD_MAX
#define HUSKY_WORD_MAX 8
#endif

// 累计读超时多少次就判一次掉线、走重连流程
// ⚠️ 目前 recogTask 是按“累计错误数的整数倍”判的（errorCount % HUSKY_ERROR_LIMIT），
//    不是严格的“连续 N 次”；等接线实测稳定后再按这个宏改成连续计数。
#ifndef HUSKY_ERROR_LIMIT
#define HUSKY_ERROR_LIMIT 3
#endif

// 开机是否自动连接二哈（0 = 只初始化 I2C，等按键/串口命令 r 再连）
#ifndef CFG_HUSKY_AUTO_CONNECT
#define CFG_HUSKY_AUTO_CONNECT 1
#endif


// -----------------------------------------------------------------------------
// 5. 按键
//
// ⚠️ 原配置是 40/41/42，但 40/41/42 同时是 OV2640 的 D5/D6/D7，
//    且和步进电机2（15/16/17/18）不冲突，本工程已把按键挪到 45/46/0。
// ⚠️ 按键 3 = GPIO0 是 BOOT 脚：芯片复位时若被按下会进下载模式。
//    正常上电后再按没问题；如果你觉得别扭，改成空闲脚即可：
//    -DBUTTON_PIN3=48  （或 21/38/39，注意别和电机3冲突）
// GPIO0 在 DevKitC 上有外部上拉，强烈建议按下为“低电平”：
//    -DBUTTON_TRUE_VALUE=0
// -----------------------------------------------------------------------------
#ifndef BUTTON_PIN1
#define BUTTON_PIN1 45
#endif
#ifndef BUTTON_PIN2
#define BUTTON_PIN2 46
#endif
#ifndef BUTTON_PIN3
#define BUTTON_PIN3 0
#endif

// 按键 4 / 5 = 说明书里的 A/B 取书还书键（补齐主控要的 5 个键）。
// 暂时没接就置 -1，ButtonLib 会跳过这个键。
// ⚠️ 去掉按键库“只认 3 个键”的限制，是因为产品实际交互是 A/B 两个键 + 3 个功能键。
#ifndef BUTTON_PIN4
#define BUTTON_PIN4 -1
#endif
#ifndef BUTTON_PIN5
#define BUTTON_PIN5 -1
#endif

// 按下时的电平：1 = 按下高电平（内部下拉）  0 = 按下低电平（内部上拉）
// 传 -1 给某个按键引脚表示不接这个键。
#ifndef BUTTON_TRUE_VALUE
#define BUTTON_TRUE_VALUE 1
#endif

// 消抖移位历史位数：低 N 位全 1 判按下、全 0 判松开（2~8，越大越迟钝）
#ifndef BUTTON_DEBOUNCE_BITS
#define BUTTON_DEBOUNCE_BITS 4
#endif

// 按键扫描周期(ms)
#ifndef BUTTON_SCAN_INTERVAL_MS
#define BUTTON_SCAN_INTERVAL_MS 5
#endif

// 按键事件队列长度
#ifndef BUTTON_QUEUE_SIZE
#define BUTTON_QUEUE_SIZE 10
#endif

// 长按判定时间(ms)，给以后做“长按进菜单”留的（当前行为：短按/长按都发按下事件）
#ifndef BUTTON_LONG_PRESS_MS
#define BUTTON_LONG_PRESS_MS 1000
#endif


// -----------------------------------------------------------------------------
// 6. 串口命令行
// -----------------------------------------------------------------------------
// 一行命令的最大长度（含结尾），格式：命令[,参数]
#ifndef CMD_LINE_LEN
#define CMD_LINE_LEN 48
#endif

// 已注册命令的最大条数
#ifndef CMD_MAX_ITEMS
#define CMD_MAX_ITEMS 32
#endif

// 命令队列长度（其它任务投递进来的命令）
#ifndef CMD_QUEUE_SIZE
#define CMD_QUEUE_SIZE 8
#endif

// 命令任务轮询周期(ms)
#ifndef CMD_POLL_INTERVAL_MS
#define CMD_POLL_INTERVAL_MS 5
#endif


// -----------------------------------------------------------------------------
// 7. 任务节奏 / 优先级 / 栈 / 核心分配
//
// ⚠️ 总线独占规则（不要破坏）：
//   - 只有 DisplayLib 的 displayTask 碰屏幕和 LVGL；
//   - 只有 HuskyLib 的调用者（recogTask）碰二哈 I2C；
//   - 只有 StepperLib 的调用者（stepperTask）碰电机引脚。
// 想改核心分配，从下面的 CFG_xxx_CORE 宏改，不要散落在 xTaskCreatePinnedToCore。
// -----------------------------------------------------------------------------
#ifndef DISPLAY_INTERVAL_MS
#define DISPLAY_INTERVAL_MS 500       // 显示任务的“重建界面”节拍（重启/重连后兜底重画用）
#endif
#ifndef SYSINFO_INTERVAL_MS
#define SYSINFO_INTERVAL_MS 1000      // 系统信息采集周期
#endif
#ifndef SYSINFO_PRINT_EVERY
#define SYSINFO_PRINT_EVERY 10        // 每采集 N 次打印一行系统信息到串口
#endif
#ifndef STEPPER_SERVICE_INTERVAL_MS
#define STEPPER_SERVICE_INTERVAL_MS 2 // 步进任务服务周期（越小越快、越吃 CPU）
#endif
#ifndef WAVE_MEASURE_INTERVAL_MS
#define WAVE_MEASURE_INTERVAL_MS 100  // HC-SR04 发起测量的间隔（手册建议 >60ms）
#endif
#ifndef WAVE_UPDATE_INTERVAL_MS
#define WAVE_UPDATE_INTERVAL_MS 1     // 测距状态机推进周期
#endif
#ifndef WAVE_PRINT_EVERY
#define WAVE_PRINT_EVERY 0            // 0 = 不周期打印，只在串口命令里看
#endif

// 栈大小（字节）
#ifndef CFG_STACK_RECOG
#define CFG_STACK_RECOG 8192
#endif
#ifndef CFG_STACK_MONITOR
#define CFG_STACK_MONITOR 4096
#endif
#ifndef CFG_STACK_STEPPER
#define CFG_STACK_STEPPER 4096
#endif
#ifndef CFG_STACK_WAVE
#define CFG_STACK_WAVE 3072
#endif
#ifndef CFG_STACK_DISPLAY
#define CFG_STACK_DISPLAY 8192
#endif
#ifndef CFG_STACK_BUTTON
#define CFG_STACK_BUTTON 3072
#endif
#ifndef CFG_STACK_CMD
#define CFG_STACK_CMD 4096
#endif

// 优先级（大 = 高）。recog 要保证 I2C 超时不被饿死，所以给 3。
#ifndef CFG_PRIO_RECOG
#define CFG_PRIO_RECOG 3
#endif
#ifndef CFG_PRIO_MONITOR
#define CFG_PRIO_MONITOR 1
#endif
#ifndef CFG_PRIO_STEPPER
#define CFG_PRIO_STEPPER 2
#endif
#ifndef CFG_PRIO_WAVE
#define CFG_PRIO_WAVE 2
#endif
#ifndef CFG_PRIO_DISPLAY
#define CFG_PRIO_DISPLAY 1
#endif
#ifndef CFG_PRIO_BUTTON
#define CFG_PRIO_BUTTON 1
#endif
#ifndef CFG_PRIO_CMD
#define CFG_PRIO_CMD 1
#endif

// 核心分配：0 = 协议核（WiFi/蓝牙/LVGL 心跳），1 = 应用核
// 识别任务独占 I2C，放核心 1，避免被 WiFi 协议栈打断导致 I2C 超时。
#ifndef CFG_CORE_RECOG
#define CFG_CORE_RECOG 1
#endif
#ifndef CFG_CORE_MONITOR
#define CFG_CORE_MONITOR 0
#endif
#ifndef CFG_CORE_STEPPER
#define CFG_CORE_STEPPER 1
#endif
#ifndef CFG_CORE_WAVE
#define CFG_CORE_WAVE 0
#endif
#ifndef CFG_CORE_DISPLAY
#define CFG_CORE_DISPLAY 0
#endif
#ifndef CFG_CORE_BUTTON
#define CFG_CORE_BUTTON 0
#endif
#ifndef CFG_CORE_CMD
#define CFG_CORE_CMD 0
#endif

// 主循环 loop() 的延时(ms)：loop 里只做事件转发，不写业务
#ifndef CFG_LOOP_DELAY_MS
#define CFG_LOOP_DELAY_MS 20
#endif


// -----------------------------------------------------------------------------
// 8. 超声波测距 HC-SR04（Wave_Test 移植进来的非阻塞版本）
//
// ⚠️ 引脚冲突：ECHO 规划在 GPIO4，而电机1 的 IN1~IN4 = 4/5/6/7 也用到 4。
//    同一时间只能接一个：
//      · 先做测距：保持 TRIG=4 ECHO=5，并把 CFG_ENABLE_STEPPER 置 0；
//      · 先做电机：把 ECHO 挪到空闲脚（如 -DPIN_WAVE_TRIG=39 -DPIN_WAVE_ECHO=48）。
// ⚠️ HC-SR04 的 ECHO 输出是 5V，直接接 GPIO 会伤芯片，中间必须分压（1k+2k）。
// -----------------------------------------------------------------------------
#ifndef PIN_WAVE_TRIG
#define PIN_WAVE_TRIG 4
#endif
#ifndef PIN_WAVE_ECHO
#define PIN_WAVE_ECHO 5
#endif

// 一次测距的超时(us)。量程 4m 时回波约 23ms，给 38ms 宽裕。
#ifndef CFG_WAVE_TIMEOUT_US
#define CFG_WAVE_TIMEOUT_US 38000UL
#endif

// 有效量程(mm)，超出范围判为无效读数（HC-SR04 手册：20~4000mm）
#ifndef CFG_WAVE_MIN_MM
#define CFG_WAVE_MIN_MM 20UL
#endif
#ifndef CFG_WAVE_MAX_MM
#define CFG_WAVE_MAX_MM 4000UL
#endif

// 滑动平均窗口（0 或 1 = 不做滤波直接用原始值）
#ifndef CFG_WAVE_FILTER_N
#define CFG_WAVE_FILTER_N 5
#endif

// 连续几次无效读数就认为“无目标/没接模块”
#ifndef CFG_WAVE_INVALID_LIMIT
#define CFG_WAVE_INVALID_LIMIT 3
#endif

// 是否把距离显示到屏幕上（0 = 只走串口）
#ifndef CFG_WAVE_SHOW_ON_SCREEN
#define CFG_WAVE_SHOW_ON_SCREEN 1
#endif


// -----------------------------------------------------------------------------
// 9. 步进电机（28BYJ-48 + ULN2003，宏可切 42 步进 + 丝杆）
//
// 机械分工（来自 Model/README.md，不可更改）：
//   电机1  水平丝杆 -> 推出小车 -> 推书板，沿 Y 前后（推出/退回）
//   电机2  垂直丝杆 -> 可调中间夹层，改变推出孔高度（对准不同厚度的书）
//   电机3  垂直丝杆 -> 升降平台，带整摞书沿 Z 升降
//
// 【驱动模式】
//   CFG_STEPPER_DRIVER = 0  -> 28BYJ-48 四相五线，每台 4 个 GPIO 接 ULN2003 的 IN1~IN4
//   CFG_STEPPER_DRIVER = 1  -> 42 步进 + 驱动器，每台用 STEP/DIR/EN 三个 GPIO（pins[3] 不用）
// 两种模式共用同一套“步数 + 非阻塞状态机”接口，切模式只改这个宏和引脚表。
//
// ⚠️ 电机2(15/16/17/18) 与按键原引脚(40/41/42)不冲突，但电机1(4/5/6/7)
//    与 HC-SR04 的 TRIG=4/ECHO=5 冲突，见第 8 节。
// ⚠️ 电机3(38/39/48/21)：38/39/48/21 当前空闲，可接；机械到位前可以先不接线，
//    只在 CFG_ENABLE_STEPPER 里关掉，或把对应 CFG_STEPPER_M3_ENABLE 置 0。
// -----------------------------------------------------------------------------
#ifndef CFG_STEPPER_DRIVER
#define CFG_STEPPER_DRIVER 0
#endif

// 电机 1：推出小车（沿 Y）
#ifndef CFG_STEPPER_M1_PINS
#define CFG_STEPPER_M1_PINS { 4, 5, 6, 7 }
#endif
// 电机 2：中间夹层（调推出孔高度）
#ifndef CFG_STEPPER_M2_PINS
#define CFG_STEPPER_M2_PINS { 15, 16, 17, 18 }
#endif
// 电机 3：升降平台（沿 Z）
#ifndef CFG_STEPPER_M3_PINS
#define CFG_STEPPER_M3_PINS { 38, 39, 48, 21 }
#endif

// 每台电机单独开关（没接线的一定置 0，否则会白扫引脚）
#ifndef CFG_STEPPER_M1_ENABLE
#define CFG_STEPPER_M1_ENABLE 1
#endif
#ifndef CFG_STEPPER_M2_ENABLE
#define CFG_STEPPER_M2_ENABLE 1
#endif
#ifndef CFG_STEPPER_M3_ENABLE
#define CFG_STEPPER_M3_ENABLE 0
#endif

// 步进模式：0 = 八拍（半拍，力矩大、更平滑）  1 = 四拍（单四拍，简单）
#ifndef CFG_STEPPER_WAVE_MODE
#define CFG_STEPPER_WAVE_MODE 0
#endif

// 默认速度：两步之间的延时(us)。28BYJ-48 建议 1000~3000us，太小会丢步。
#ifndef CFG_STEPPER_STEP_DELAY_US
#define CFG_STEPPER_STEP_DELAY_US 1500
#endif

// 速度下限/上限(us)，给以后的加减速用
#ifndef CFG_STEPPER_DELAY_MIN_US
#define CFG_STEPPER_DELAY_MIN_US 800
#endif
#ifndef CFG_STEPPER_DELAY_MAX_US
#define CFG_STEPPER_DELAY_MAX_US 4000
#endif

// 每次 run() 允许的最大步数，防止参数写错把机构撞坏
#ifndef CFG_STEPPER_MAX_STEPS
#define CFG_STEPPER_MAX_STEPS 8192
#endif

// 是否启用软限位（到位后禁止继续往同方向走）。首次调试机械时建议保持 1。
#ifndef CFG_STEPPER_SOFT_LIMIT
#define CFG_STEPPER_SOFT_LIMIT 1
#endif

// 软限位行程（步）。28BYJ-48 输出轴一圈 = 4096 步（1:64 减速 + 八拍 8×64×... 见 StepperLib 注释）。
// 这里按“机构实际行程”填，不知道就先给大一点，等实测再收紧。
#ifndef CFG_STEPPER_M1_RANGE_STEPS
#define CFG_STEPPER_M1_RANGE_STEPS 4096
#endif
#ifndef CFG_STEPPER_M2_RANGE_STEPS
#define CFG_STEPPER_M2_RANGE_STEPS 2048
#endif
#ifndef CFG_STEPPER_M3_RANGE_STEPS
#define CFG_STEPPER_M3_RANGE_STEPS 8192
#endif

// 上下电后要不要回零（当前没有限位开关，只能把上电位置当零点）
#ifndef CFG_STEPPER_HOMING_ON_BOOT
#define CFG_STEPPER_HOMING_ON_BOOT 0
#endif

// 42 步进 + T8 丝杆（导程 4mm）的换算参数，切 CFG_STEPPER_DRIVER=1 时才用
#ifndef CFG_STEPPER42_STEPS_PER_REV
#define CFG_STEPPER42_STEPS_PER_REV 200    // 1.8° 电机整步 200 步/圈
#endif
#ifndef CFG_STEPPER42_MICROSTEP
#define CFG_STEPPER42_MICROSTEP 16         // 驱动器细分
#endif
#ifndef CFG_STEPPER42_LEAD_MM
#define CFG_STEPPER42_LEAD_MM 4.0f         // T8 丝杆导程 4mm/圈
#endif

// 42 步进驱动器的 EN（使能）脚是否低有效。
// 常见模块（A4988/DRV8825/TMC 系）都是低有效，所以默认 1；
// 如果你的模块是高有效，改成 0。
#ifndef CFG_STEPPER_EN_ACTIVE_LOW
#define CFG_STEPPER_EN_ACTIVE_LOW 1
#endif

// 步进任务/命令队列
#ifndef CFG_STEPPER_QUEUE_SIZE
#define CFG_STEPPER_QUEUE_SIZE 8
#endif

// 单条步进命令最长字符数
#ifndef CFG_STEPPER_CMD_LEN
#define CFG_STEPPER_CMD_LEN 32
#endif

// 阻塞式 run() 的默认超时(ms)，0 = 不超时（一直转到走完）
#ifndef CFG_STEPPER_RUN_TIMEOUT_MS
#define CFG_STEPPER_RUN_TIMEOUT_MS 20000
#endif


// -----------------------------------------------------------------------------
// 10. 记忆/机械关键尺寸（来自 Model/README.md，业务判断阈值放这里）
// -----------------------------------------------------------------------------
// 推出孔下沿固定高度(mm)与有效高度范围(mm)
#ifndef CFG_SLOT_BOTTOM_MM
#define CFG_SLOT_BOTTOM_MM 380
#endif
#ifndef CFG_SLOT_MIN_MM
#define CFG_SLOT_MIN_MM 18
#endif
#ifndef CFG_SLOT_MAX_MM
#define CFG_SLOT_MAX_MM 53
#endif

// 推书板高度(mm)，必须 ≤ 最薄书厚度，才能实现单本分离
#ifndef CFG_PUSHER_HEIGHT_MM
#define CFG_PUSHER_HEIGHT_MM 14
#endif
#ifndef CFG_BOOK_MIN_THICK_MM
#define CFG_BOOK_MIN_THICK_MM 15
#endif
#ifndef CFG_BOOK_MAX_THICK_MM
#define CFG_BOOK_MAX_THICK_MM 50
#endif

// 用超声波测厚时的判定容差(mm)
#ifndef CFG_THICK_TOLERANCE_MM
#define CFG_THICK_TOLERANCE_MM 2
#endif


// -----------------------------------------------------------------------------
// 11. 与 ESP32-C3 步进控制板的串口链路（预留，暂未接线）
//     Master 工程规划是 TX=6 RX=7。这里先留宏，等真接线再启用。
// -----------------------------------------------------------------------------
#ifndef PIN_C3_UART_TX
#define PIN_C3_UART_TX 6
#endif
#ifndef PIN_C3_UART_RX
#define PIN_C3_UART_RX 7
#endif
#ifndef CFG_C3_UART_BAUD
#define CFG_C3_UART_BAUD 115200
#endif


// -----------------------------------------------------------------------------
// 12. 日志宏（统一入口，方便以后整体关掉）
//
//   CFG_LOG(2, "系统信息: ram=%uKB\n", ramKb);
//   等级 >= CFG_LOG_LEVEL 才真的打印；CFG_LOG_LEVEL=0 时连编译都不进。
// -----------------------------------------------------------------------------
#define CFG_LOG_NONE  0
#define CFG_LOG_ERROR 1
#define CFG_LOG_INFO  2
#define CFG_LOG_DEBUG 3

#if CFG_LOG_LEVEL >= 1
#define CFG_LOGE(fmt, ...) Serial.printf("[E] " fmt, ##__VA_ARGS__)
#else
#define CFG_LOGE(fmt, ...) do { } while (0)
#endif

#if CFG_LOG_LEVEL >= 2
#define CFG_LOGI(fmt, ...) Serial.printf("[I] " fmt, ##__VA_ARGS__)
#else
#define CFG_LOGI(fmt, ...) do { } while (0)
#endif

#if CFG_LOG_LEVEL >= 3
#define CFG_LOGD(fmt, ...) Serial.printf("[D] " fmt, ##__VA_ARGS__)
#else
#define CFG_LOGD(fmt, ...) do { } while (0)
#endif


// -----------------------------------------------------------------------------
// 13. 库版本（library.json 里也有一份，这里给开机打印用）
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
