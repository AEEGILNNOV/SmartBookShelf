#ifndef CONFIG_H
#define CONFIG_H

#include <stdint.h>

// =============================================================================
// SmartBookShelf / Main —— 全工程唯一配置来源（config.h）
//
// 【怎么用】
//   1. 想改任何参数（引脚，任务节奏，队列长度，开关），只改本文件，
//   2. 也可以用 platformio.ini 的 build_flags 里的 -DXXX=yyy 临时覆盖，
//      因为下面**每一个宏都用 #ifndef 包住**（-D 的优先级更高），
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
//   实际可用：GPIO 0-18，38-42，45-48
//
// 【引脚占用总表（改线只改这里）】
//   ILI9341 屏   SCLK=12 MOSI=11 CS=10 DC=9 RST=8 BL=47（MISO 不接）
//   HUSKYLENS 2  SDA=1  SCL=2
//   按键         BTN1=45 BTN2=46 BTN3=0     （0 是 BOOT 脚，见第 9 节警告）
//   触摸 XPT2046 T_CS=38 T_IRQ=39 T_CLK=40 T_DIN=41 T_DO=42（独立 5 线，见第 3.2 节）
//   步进电机1    4,5,6,7                     （ULN2003 的 IN1~IN4）
//   步进电机2    15,16,17,18                 （ULN2003 的 IN1~IN4）
//   步进电机3    38,39,48,21                 （机械到位后再接，38/39 与触摸冲突，二选一）
//   HC-SR04      TRIG=4 ECHO=5               （与电机1冲突，二选一，见第 8 节）
//   空闲未用     3,13,14,20,48,21 …
// =============================================================================


// -----------------------------------------------------------------------------
// 1. 模块总开关
//
// 置 0 的模块不建任务，不初始化，省 RAM 也便于分步排查。
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
// =============================================================================
// ⚠️⚠️ 超声波总开关（**当前默认 0 = 关，代码全部保留，随时可以开回来**）
//
// 【为什么现在是 0】
//   实机实测 err=2 一直不变：ECHO 拉高以后**再也不拉低**（一直高电平）。
//   这不是软件能修的 —— 说明 ECHO 那一路的硬件有问题，先放一边，
//   让主流程（识别 -> 入库 -> 机械）先跑通，不要被传感器卡住。
//
// 【0 = 关的后果（要说清楚，避免下次又误判成"传感器坏"）】
//   · 不建 waveTask         -> 没有任何测量动作
//   · 不调 waveSensor.begin() -> TRIG/ECHO 连 pinMode 都不配
//   · 串口一条测距日志都不会有；敲 u 会直接告诉你"没启用"
//
// 【想开回来】
//   1) 把下面这个 0 改成 1；
//   2) 确认测距引脚不和升降电机抢（用第 8 节的 CFG_WAVE_PIN_LAYOUT：2 = 38/39 推荐）；
//   3) 把 ECHO 那一路的硬件问题解决掉（见第 8 节 err=2 的排查清单）。
//
// ⚠️ 选 CFG_WAVE_PIN_LAYOUT=1（TRIG=5/ECHO=6）又开着 CFG_STEPPER_LIFT_ENABLE 的话，
//    第 14.3 节会在**编译期**直接报中文错误，不会烧进去才发现。
// =============================================================================
#define CFG_ENABLE_WAVE 0         // 超声波测距（waveTask）1=开 0=关（当前：关，等硬件排查）
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

// 屏的 SDO/MISO：本方案不接（不读屏 ID，读回来是 0 属正常）。
// ⚠️ 触摸不靠这个脚！XPT2046 走独立的 5 根线，见下面第 3.2 节。
#define PIN_TFT_MISO (-1)

// 分辨率：SCR_W/SCR_H 不在本文件定义，它们在 DisplayLib.h 里从 TFT_WIDTH/TFT_HEIGHT
// 推导（因为本文件会被不碰屏幕的库 include，那时 TFT_WIDTH 还不存在）。
// 下面这段只做“包含顺序”体检：如果已经 include 了 <TFT_eSPI.h>，却拿不到 TFT_WIDTH，
// 说明 TFT_eSPI 的 USER_SETUP 没生效，会在这里直接报错，
// 而不是在别处报一句看不懂的 “TFT_WIDTH was not declared”。
#if defined(_TFT_eSPIH_) && !defined(TFT_WIDTH)
#error "config.h: TFT_eSPI 的 USER_SETUP 宏没生效，请检查 platformio.ini 的 build_flags"
#endif

// 背光亮度（LEDC 占空比 0~255，只在 CFG_TFT_BL_USE_LEDC=1 时生效）。
// 排错时不要直接把 BL 接 VCC。
#ifndef CFG_TFT_BL_LEVEL
#define CFG_TFT_BL_LEVEL 255
#endif

// 背光驱动方式：
//   0 = digitalWrite（默认，和已经验证过的 TFT_Test 完全一样，最稳）
//   1 = LEDC(PWM)，这时 CFG_TFT_BL_LEVEL 才是“亮度”
// 屏全黑先别动这里：digitalWrite 是已知能亮的写法，先排除背光被 PWM 关掉的可能。
#ifndef CFG_TFT_BL_USE_LEDC
#define CFG_TFT_BL_USE_LEDC 0
#endif
// LEDC 通道（0~15）与频率(Hz)，只在上面为 1 时用
#ifndef CFG_TFT_BL_LEDC_CHANNEL
#define CFG_TFT_BL_LEDC_CHANNEL 7
#endif
#ifndef CFG_TFT_BL_LEDC_FREQ
#define CFG_TFT_BL_LEDC_FREQ 5000
#endif

// 背光电平：
//   0 = 高电平点亮（默认，和 TFT_eSPI 的 TFT_BACKLIGHT_ON=1 一致）
//   1 = 低电平点亮（少数模块的 LED 是低有效，或者 BL 线接到了反相驱动）
// 怎么判断：串口敲 d,4 让背光闪 3 次——如果“关”的时候亮，“开”的时候灭，
//           就把这里改成 1，也可以直接敲 d,5 现场对调电平看哪个亮。
#ifndef CFG_TFT_BL_ACTIVE_LOW
#define CFG_TFT_BL_ACTIVE_LOW 0
#endif

// LVGL 刷屏缓冲：2 块 240x40 的 16 位缓冲 ≈ 19.2KB
// 这一项就是 DisplayLib.cpp 里刷屏缓冲的行数（原来叫 DISPLAY_DRAW_LINES，已合并过来）。
// 屏幕出现横向撕裂/闪烁就往下调到 20，内部 RAM 更紧就往上调到 80。
#ifndef CFG_LV_BUF_LINES
#define CFG_LV_BUF_LINES 40
#endif

// 刷屏缓冲放哪：1 = PSRAM(ps_malloc，原来的写法)   0 = 内部 RAM
// S3 的 TFT_eSPI 走 CPU 搬运像素，两者都能用，屏上出现随机噪点/花屏时，
// 可以改成 0 用内部 RAM 试（内部 RAM 更快，两块约 38KB，够用）。
#ifndef CFG_LV_BUF_IN_PSRAM
#define CFG_LV_BUF_IN_PSRAM 1
#endif

// LVGL 刷屏回调里 pushColors 是否交换字节：
//   1 = 交换（默认，配 lv_conf.h 的 LV_COLOR_16_SWAP=0）
//   0 = 不交换（配 LV_COLOR_16_SWAP=1）
// 屏上出现“颜色反了 / 偏蓝绿”就是这里和 LV_COLOR_16_SWAP 没对上，改其中一个即可。
// 注意：字节序不对只会让颜色错，不会让整屏没内容。
#ifndef CFG_LV_PUSH_SWAP
#define CFG_LV_PUSH_SWAP 1
#endif

// -----------------------------------------------------------------------------
// 3.1 屏幕排错（分层点亮：一次只验证一层，别一上来就怀疑字库）
//
// 为什么要有这一段：LVGL 界面的“整屏没内容”和“字库缺字”是两回事——
// 缺字只会让某几个汉字变空白，标题栏底色，页面背景这些不用字体的东西照样会画。
// 所以“整屏全黑/全白”一定不在字库这一层，按下面的顺序往下查：
//
//   CFG_DISPLAY_BRINGUP
//     0 = 正常：直接建 LVGL 中文界面
//     1 = 只用 TFT_eSPI 画彩条，**完全不初始化 LVGL**
//         -> 验证 接线/供电/背光/SPI/屏驱动型号 这条链
//     2 = 彩条 + LVGL 纯色块（一个字体都不碰）
//         -> 验证 LVGL 刷屏链路（tick / flush_cb / pushColors）
//     3 = 彩条 + LVGL 纯色块 + 完整中文界面（等于 0 再加一次开机彩条）
//
// 判读表：
//   彩条能出来，界面不出来 -> 硬件这条链是好的，问题在 LVGL/字体层
//   彩条也没有            -> 和 LVGL，字库都无关，查背光/接线/驱动型号
//
// 对应的一键环境（platformio.ini 里已经配好）：
//   pio run -e screen-raw  -t upload    只用彩条，不进 LVGL
//   pio run -e screen-lvgl -t upload    彩条 + LVGL 纯色块 + 刷屏日志
//   pio run -e screen-log  -t upload    正常界面 + 刷屏日志 + LVGL 日志
// -----------------------------------------------------------------------------
#ifndef CFG_DISPLAY_BRINGUP
#define CFG_DISPLAY_BRINGUP 0
#endif

// 开机先画一次 TFT_eSPI 彩条并停留多少 ms（0 = 不画，正常固件保持 0）
#ifndef CFG_TFT_SPLASH_MS
#define CFG_TFT_SPLASH_MS 0
#endif

// 串口命令 d,1 的彩条自检停留时间(ms)：画完自动让 LVGL 整屏重画，回到界面
#ifndef CFG_SELFTEST_BAR_HOLD_MS
#define CFG_SELFTEST_BAR_HOLD_MS 1500
#endif

// 显示任务心跳：每 N ms 打一行“刷屏次数 / 最近一次耗时 / LVGL 时钟 / 栈余量”
//   0 = 关（正常固件保持 0）
// 打开后能直接区分两种完全不同的故障：
//   刷屏次数一直是 0        -> LVGL 根本没在刷（tick / draw buffer / 驱动注册的问题）
//   刷屏次数在涨但屏是黑的  -> 数据发出去了屏没反应（背光/接线/屏型号的问题）
#ifndef CFG_DISPLAY_FLUSH_LOG_MS
#define CFG_DISPLAY_FLUSH_LOG_MS 0
#endif

// 把 LVGL 自己的日志（warning/info）打到串口。默认关（和原来一致）。
// 例：字库是 RLE 压缩的却没开 LV_USE_FONT_COMPRESSED 时，LVGL 会在这里出声。
#ifndef CFG_LV_LOG_TO_SERIAL
#define CFG_LV_LOG_TO_SERIAL 0
#endif


// -----------------------------------------------------------------------------
// 3.2 触摸（XPT2046，独立 5 根线）
//
// 【为什么以前完全没反应】不是接线坏，是代码里压根没有这一路：
//   · 本工程的触摸**不走屏的 SPI**，用的是触摸模块自己的 T_CLK/T_DIN/T_DO 三根线，
//     再配 T_CS/T_IRQ，由 lib/TouchLib 的 TouchXPT2046 位操作驱动（和
//     LVGLin240320TFTLCD 参考工程同一套，已在硬件上验证过），
//   · 所以 platformio.ini 里的 -DTFT_MISO=-1 / -DTOUCH_CS=-1 都不用动，
//   · 以前缺的是第 3 步：DisplayLib 从来没有注册 LVGL 输入设备(lv_indev)，
//     就算读到了坐标，按钮也收不到点击。
//
// 【接线】
//     T_CS  -> GPIO48      T_IRQ -> GPIO18（本工程用轮询，可不接）
//     T_CLK -> GPIO13      T_DIN -> GPIO14      T_DO -> GPIO38
//
// ⚠️ **本次改造把这 5 根脚整个挪过一次**。原因：旧的 38/39/40/41/42 里，
//    40/41/42 同时是 OV2640 摄像头的 D5/D6/D7，而 38/39 是旧工程"步进电机3(升降平台)"
//    的 IN1/IN2。本次改造**只用两台电机**（升降 + 推书），旧电机3 整个不要了，
//    所以 38/39 空出来可以给触摸；再避开摄像头那三根，就是现在这一组：
//      T_CS=48  T_IRQ=18  T_CLK=13  T_DIN=14  T_DO=38
//    这几个脚和第 9 节的升降(4,5,6,7)/推书(15,16,17,21) 都不冲突（14.6 节有体检）。
//
// ⚠️ 上面这段是"引脚为什么不冲突"的推导，**触摸模块实际接没接、接对没接对，
//    本工程无法从代码确认**，必须用串口命令 d,3 / d,6 实测（见 README 第 10 节）。
//
// ⚠️⚠️ 【触摸不动了先看这里】
//    本次改造把这 5 根脚**整体挪过位置**。如果你的触摸模块还是按旧线序接在
//    38/39/40/41/42 上，那改完固件以后触摸就会**完全没反应**——代码是好的，是线不对。
//    两种解决办法二选一：
//
//    办法一（推荐，不用动线）：下面 CFG_TOUCH_PIN_LAYOUT 的默认值已经是 2（新脚位），
//                              把触摸模块的 5 根线按"【接线】"那段重新接到 48/18/13/14/38。
//
//    办法二（不想动线）：把下面的 CFG_TOUCH_PIN_LAYOUT 改成 1，一键切回旧脚位 38/39/40/41/42。
//      ⚠️ 但旧脚位的 40/41/42 和【实体按键】是同一批 GPIO（见第 5 节：按键1/2/3 = 45/46/0，
//         但 A/B 或者你想加的按键很可能落在 40/41/42）。**触摸和这三个键只能二选一**。
//         所以办法二只适合"我不要那三个键，我就要触摸"。
//
//    无论用哪种办法，都可以用串口 d,6 看芯片到底回没回数据（会打印当前生效的引脚）。
// -----------------------------------------------------------------------------
#ifndef CFG_ENABLE_TOUCH
#define CFG_ENABLE_TOUCH 1        // 1 = 注册 LVGL 输入设备 + 显示触摸测试页
#endif

// ==== 触摸引脚布局选择（一键切换，不用改下面的 PIN_TOUCH_*）====
//   1 = 旧工程脚位：T_CS=38  T_IRQ=39  T_CLK=40  T_DIN=41  T_DO=42
//       ⚠️ 40/41/42 与实体按键/摄像头 D5/D6/D7 冲突，选这个就别想在那三个脚接按键。
//   2 = 本次改造脚位（默认）：T_CS=48  T_IRQ=18  T_CLK=13  T_DIN=14  T_DO=38
//       把触摸从"按键要用的 40/41/42"和"旧电机3 的 38/39"上让开，留出脚位给实体按键。
#ifndef CFG_TOUCH_PIN_LAYOUT
#define CFG_TOUCH_PIN_LAYOUT 1
#endif

// 触摸 5 根线（-1 = 这个脚不接）
// ⚠️ 实际值由上面的 CFG_TOUCH_PIN_LAYOUT 决定，这里只是两套默认值，不用手改。
#ifndef PIN_TOUCH_CS
#if CFG_TOUCH_PIN_LAYOUT == 1
#define PIN_TOUCH_CS 38           // T_CS  片选（旧脚位）
#else
#define PIN_TOUCH_CS 48           // T_CS  片选（新脚位，默认）
#endif
#endif
#ifndef PIN_TOUCH_IRQ
#if CFG_TOUCH_PIN_LAYOUT == 1
#define PIN_TOUCH_IRQ 39          // T_IRQ 中断（本工程用轮询，可不接）
#else
#define PIN_TOUCH_IRQ 18          // T_IRQ 中断（本工程用轮询，可不接）
#endif
#endif
#ifndef PIN_TOUCH_CLK
#if CFG_TOUCH_PIN_LAYOUT == 1
#define PIN_TOUCH_CLK 40          // T_CLK 时钟（旧脚位）
#else
#define PIN_TOUCH_CLK 13          // T_CLK 时钟（新脚位，默认）
#endif
#endif
#ifndef PIN_TOUCH_DIN
#if CFG_TOUCH_PIN_LAYOUT == 1
#define PIN_TOUCH_DIN 41          // T_DIN 主机输出（旧脚位）
#else
#define PIN_TOUCH_DIN 14          // T_DIN 主机输出（新脚位，默认）
#endif
#endif
#ifndef PIN_TOUCH_DO
#if CFG_TOUCH_PIN_LAYOUT == 1
#define PIN_TOUCH_DO 42           // T_DO  触摸数据回来（旧脚位）
#else
#define PIN_TOUCH_DO 38           // T_DO  触摸数据回来（新脚位，默认）
#endif
#endif

// 原始值有效窗口：落在窗口外当成“没按”。按面板边缘没反应就把窗口放宽。
// ⚠️ 这个窗口只管“坐标像不像有效触摸”，**管不了灵敏度**，灵敏度看下面的压力门限。
#ifndef CFG_TOUCH_RAW_MIN
#define CFG_TOUCH_RAW_MIN 50
#endif
#ifndef CFG_TOUCH_RAW_MAX
#define CFG_TOUCH_RAW_MAX 4045
#endif

// ==== 触发门限（= 灵敏度）====
// 判据是 XPT2046 的压力读数 Z（Z1/Z2 差分）：按得越重，Z 越大。
//   Z >= CFG_TOUCH_PRESSURE_MIN 才算“真的按下了”。
//
// 调法：
//   · 嫌太灵敏（轻轻碰一下就触发 / 自己乱跳）-> **调大**，比如 600 -> 900 -> 1200
//   · 嫌太迟钝（要使劲按才有反应）        -> **调小**，比如 600 -> 400 -> 250
//   · 设 0 = 不判压力，只看坐标窗口（回到之前“一碰就触发”的行为）
//
// 怎么定一个合适的数：按住屏幕敲 d,6 或 d，看“Z=”那一列的实测值：
//   · 轻轻碰一下的最大 Z   —— 门限要比它明显大
//   · 正常按一下的稳定 Z   —— 门限要比它明显小
//   两个数之间取中间值即可，默认 600 是 TFT_eSPI 的经验值（XPT2046 常见范围 200~2000）。
#ifndef CFG_TOUCH_PRESSURE_MIN
#define CFG_TOUCH_PRESSURE_MIN 600
#endif

// XPT2046 原始值 -> 屏幕坐标的轴方向（照抄参考工程里已经验证过的一组）
// 现象对照：左右反了改 TOUCH_INVERT_X，上下反了改 TOUCH_INVERT_Y，
//           手指上下动，屏幕光标左右跑，就把 TOUCH_SWAP_XY 改成 1。
#ifndef CFG_TOUCH_SWAP_XY
#define CFG_TOUCH_SWAP_XY 0
#endif
#ifndef CFG_TOUCH_INVERT_X
#define CFG_TOUCH_INVERT_X 1
#endif
#ifndef CFG_TOUCH_INVERT_Y
#define CFG_TOUCH_INVERT_Y 0
#endif

// 一次 read() 里每个轴采样几次再取平均（参考工程用 3，太小会抖）
#ifndef CFG_TOUCH_SAMPLES
#define CFG_TOUCH_SAMPLES 3
#endif

// LVGL 轮询触摸的周期(ms)：20~30 手感合适，太小费 CPU
#ifndef CFG_TOUCH_READ_MS
#define CFG_TOUCH_READ_MS 20
#endif

// 触摸测试页计数上限（防溢出，够测就行）
#ifndef CFG_TOUCH_TEST_MAX
#define CFG_TOUCH_TEST_MAX 9999
#endif

// ==== 屏幕最下方屏蔽的高度(px)：这一段不响应触摸，防止误触 ====
// 滑到最后一行时手指容易蹭到屏幕边缘，这里直接把底部一段判成"没按"。
// 0 = 不屏蔽。注意书列表页的容器高度会主动避开这一段，所以不会挡住按钮。
#ifndef CFG_TOUCH_BOTTOM_MASK_PX
#define CFG_TOUCH_BOTTOM_MASK_PX 20
#endif

// ==== 滑动限制 ====
// LVGL 的 scroll_limit = "手指要先移动多少像素才算滑动（否则算点击）"。
// 0 = 解除限制，手指一动就滑动（列表手感最跟手，但按钮容错变小）。
// 如果发现按钮变得不好点（手一抖就变成滚动而不是点击），把它设成 10~20。
#ifndef CFG_TOUCH_SCROLL_LIMIT
#define CFG_TOUCH_SCROLL_LIMIT 0
#endif

// ---- 编译期体检 ----
// ⚠️ 本次改造已经把旧的"步进电机3"整个去掉（只用升降 + 推书两台），
//    触摸引脚也从 38/39/40/41/42 挪到 48/18/13/14/38，
//    所以这里不再需要"触摸 vs 电机3"的检查。
//    现在所有引脚冲突检查都集中在**第 14 节**（14.1~14.6），改完引脚请去那里看结论。

// 运行时是否真的启用触摸（编译期常量，条件不满足时整段代码被优化掉）
#define CFG_RUN_TOUCH (CFG_ENABLE_TOUCH && (PIN_TOUCH_CS >= 0) && (PIN_TOUCH_CLK >= 0) && \
                       (PIN_TOUCH_DIN >= 0) && (PIN_TOUCH_DO >= 0))

// 一页最多记录几本书（按 ID + 书名去重）
// ⚠️ 已被"动态数组书单"取代（见第 3.3 节 CFG_BOOKLIST_*），这里只留给老代码兼容
#ifndef MAX_BOOK_RECORDS
#define MAX_BOOK_RECORDS 12
#endif


// -----------------------------------------------------------------------------
// 3.3 书单（动态数组 BookList）
//
// 识别到的书会进 lib/BookListLib 的动态数组：容量不够就 realloc 翻倍，
// 书列表页按顺序显示每一条，每条右边有一个"取出"按钮把它移出列表。
// 内存是运行期按需申请的，所以下面这几个数只影响"起点"和"上限"，不影响功能。
// -----------------------------------------------------------------------------
// 初始容量（条）。第一次 begin() 就申请这么多，装不下自动翻倍
#ifndef CFG_BOOKLIST_INIT_CAP
#define CFG_BOOKLIST_INIT_CAP 4
#endif

// 容量上限（条）。防止识别出乱码时无限扩容把堆吃光，设 0 = 不限制
// 一条 BookItem 约 44 字节，64 条约 2.8KB，放内部 RAM 毫无压力
#ifndef CFG_BOOKLIST_MAX
#define CFG_BOOKLIST_MAX 64
#endif

// 显示任务一次能拷多少条快照去画（书列表页最多显示这么多行，其余不显示）
// 24 行 x 44 字节 ≈ 1KB，作为 DisplayLib 的成员常驻，不占任务栈
#ifndef CFG_BOOKLIST_SNAP_MAX
#define CFG_BOOKLIST_SNAP_MAX 24
#endif

// 书列表页每一行的高度(px)：行里放"序号 + 书名"和一个"取出"按钮
#ifndef CFG_BOOK_ROW_H
#define CFG_BOOK_ROW_H 34
#endif

// 中文字库开关：1 = 用生成的 ui_font_16，0 = 退回 LVGL 自带 simsun（只有 1097 字，会缺字）。
// 排错用：怀疑字库导致屏不亮时先置 0，屏幕能亮就说明问题在字库。
// 注意：置 0 时 lv_conf.h 里的 UI_FONT_16 要同时置 0，两边必须一致。
#ifndef CFG_FONT_CUSTOM_CJK
#define CFG_FONT_CUSTOM_CJK 1
#endif

// 开机中文字库自检：1 = 打开（把缺字打到串口）。默认关，
// 因为它在 setup 里同步跑，万一字库有问题会拖住整个启动，要用再打开。
#ifndef CFG_FONT_SELFCHECK
#define CFG_FONT_SELFCHECK 0
#endif

// 屏上字体：中文用自己生成的全字库 ui_font_16，英文数字用 montserrat_12。
// ⚠️ 不要再用 LVGL 自带的 lv_font_simsun_16_cjk：它只有 1097 个汉字，
//    界面里的“识/别/书/测/距”等字不在里面，会显示成空白。
// ui_font_16 由 Main/font/ui_font_16.c 提供（GB2312 一级汉字 3755 + 界面用字），
// 重新生成见 Main/fontgen/README.md，开关在 include/lv_conf.h。
#ifndef DISPLAY_FONT_CJK
#if CFG_FONT_CUSTOM_CJK
#define DISPLAY_FONT_CJK (&ui_font_16)
#else
#define DISPLAY_FONT_CJK (&lv_font_simsun_16_cjk)
#endif
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
#define CFG_FOOTER_HINT "k1:next/hold=DEBUG k2:recon k3:learn/hold=DEBUG"
#endif

// 最多显示（或参与签名比较）几条识别块
#ifndef CFG_STATUS_WORD_SHOWN
#define CFG_STATUS_WORD_SHOWN 4
#endif




// -----------------------------------------------------------------------------
#endif

// 4. HUSKYLENS 2（二哈二代）I2C 内置 CNN 分类 / OCR
//   绿线 SDA -> GPIO1   蓝线 SCL -> GPIO2   VCC=5V/3V3   GND 必须共地
//   二哈屏幕：系统设置 -> 协议类型 -> I2C
//
// 【本次改造要点】需求里"直接使用已经训练好的分类 ID 1~6"，不开发 OCR 关键字匹配。
//   所以二哈要切到**分类类**算法：
//     '2' = 物体分类（OBJECT_CLASSIFICATION）
//     '3' = 自学习分类（SELF_LEARNING_CLASSIFICATION）
//   这两个算法的结果块里，"分类标签"就是 Result::ID。
//
// ⚠️ 千万不要读 Result::classID！Result.h 里 classID 和 xCenter 是**同一个 union 槽位**，
//    读 classID 等价于读坐标。HuskyLib 已经把它正确暴露成 HuskyWord.id（见 HuskyLib.cpp）；
//    但要记住：HuskyWord.x 和 classID 是同一块内存，别拿 x 当标签用。
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

// I2C 时钟(Hz)。库每次收发都会重新设时钟，通信不稳先降到 50000。
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

// ---- ⚠️ 识别节拍：正常识别周期 = 1 秒（原来是 5 秒，实机上太慢了）----
// 原来 300ms 一帧；上一版改成 5 秒，实机用下来"要盯着看好几秒才反应"，太慢，
//   现在改成 1 秒。之所以不是更快，是因为：
//     每帧都要判重/更新界面，太快没有意义，还会把 CPU 和 I2C 时间片占掉。
// 注意区分两个东西：
//   HUSKY_READ_INTERVAL_MS      = 正常识别周期（1 秒）
//   HUSKY_RECONNECT_INTERVAL_MS = 掉线后多久重试一次（2 秒，这个要快，**没动它**）
#ifndef HUSKY_READ_INTERVAL_MS
#define HUSKY_READ_INTERVAL_MS 1000
#endif

// 二哈掉线后的重连间隔(ms)。掉线时不能等 5 秒才试一次，否则用户觉得"卡住了"。
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

// 累计读超时多少次就判一次掉线，走重连流程
// ⚠️ 目前 recogTask 是按"累计错误数的整数倍"判的（errorCount % HUSKY_ERROR_LIMIT），
//    不是严格的"连续 N 次"，等接线实测稳定后再按这个宏改成连续计数。
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
// 本次改造**没有动按键引脚**（需求明确说不要擅自分配新的实体按键 GPIO）。
// 按键 1 = 下一页、按键 2 = 重连二哈、按键 3 = 学习当前目标，
// A/B 两个键本次改成"存书确认 / 取书请求"，见第 9.3 节说明。
//
// ⚠️ 保留的已知取舍（原工程就存在，本次不动）：
//    40/41/42 同时是 OV2640 摄像头的 D5/D6/D7。本次任务范围里没有摄像头，
//    所以按键继续用 40/41/42；如果以后要同时用"本地摄像头 + 这三个按键"，
//    必须把其中一个挪脚。
// ⚠️ 按键 3 = GPIO0 是 BOOT 脚：芯片复位时若被按住会进下载模式（正常上电后再按没问题）。
//    DevKitC 上 GPIO0 有外部上拉，如果觉得别扭，建议改成按下低电平：
//    -DBUTTON_PIN3=45 -DBUTTON_TRUE_VALUE=0，或直接换到空闲脚。
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

// 按键 4 / 5 = 说明书里的 A/B 取书还书键。
// 本次改造把它们接到"存书确认(A) / 取书请求(B)"上，但**默认仍然是 -1（没接）**：
// 因为需求要求"不要擅自分配新的实体按键 GPIO"，而且机械还没标定，
// 自动流程现在也不该跑。要接线就把这两个宏改成实际引脚。
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

// 消抖移位历史位数：低 N 位全 1 判按下，全 0 判松开（2~8，越大越迟钝）
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

// 长按判定时间(ms)，给以后做"长按进菜单"留的（当前行为：短按/长按都发按下事件）
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

// 已注册命令的最大条数。⚠️ 本次新增了 g(调试页)/f(Flash)/v(取书存书) 等命令，
//    原来 32 条够用；如果以后再加命令发现"注册失败"，回头调大这个值。
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
// ⚠️ 总线独占规则（**本次改造必须继续守住**）：
//   - 只有 DisplayLib 的 displayTask 碰屏幕和 LVGL；
//   - 只有 recogTask 碰二哈 I2C（本工程里 HuskyLib 的唯一调用者）；
//   - 只有 stepperTask 碰两个电机的 GPIO 和 service()；
//   - 只有 waveTask 碰 TRIG/ECHO；
//   - 别的任务只能通过队列/受锁保护的快照传递请求。
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
#define STEPPER_SERVICE_INTERVAL_MS 2 // 步进任务服务周期（越小越快，越吃 CPU）
#endif

// ⚠️ 本次需求：HC-SR04 的**正常测量周期是 5 秒**（原来 100ms）。
//    state 推进周期仍然是 1ms（状态机要密一点才不丢回波），但"发起测量"放慢到 5 秒。
#ifndef WAVE_MEASURE_INTERVAL_MS
#define WAVE_MEASURE_INTERVAL_MS 5000
#endif
#ifndef WAVE_UPDATE_INTERVAL_MS
#define WAVE_UPDATE_INTERVAL_MS 1     // 测距状态机推进周期
#endif
#ifndef WAVE_PRINT_EVERY
#define WAVE_PRINT_EVERY 0            // 0 = 不周期打印，只在串口命令里看
#endif

// 书籍入库任务（bookTask）的节拍(ms)：它只做"请求 -> 等完成 -> 落盘"，不忙等。
#ifndef CFG_BOOK_TASK_INTERVAL_MS
#define CFG_BOOK_TASK_INTERVAL_MS 20
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
#ifndef CFG_STACK_BOOK
#define CFG_STACK_BOOK 6144           // 存书/取书状态机 + 拼中文日志
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
#ifndef CFG_PRIO_BOOK
#define CFG_PRIO_BOOK 2            // 比 stepper 低：只发请求和等结果，不抢电机
#endif

// 核心分配：0 = 协议核（WiFi/蓝牙/LVGL 心跳），1 = 应用核
// 识别任务独占 I2C，放核心 1，避免被 WiFi 协议栈打断导致 I2C 超时。
// stepperTask 独占电机引脚，也放核心 1（和 recogTask 时间上互不重叠）。
// bookTask 只是状态机 + 队列收发，放核心 0，别和电机抢 1 核的时间片。
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
#ifndef CFG_CORE_BOOK
#define CFG_CORE_BOOK 0
#endif

// 主循环 loop() 的延时(ms)：loop 里只做事件转发，不写业务
#ifndef CFG_LOOP_DELAY_MS
#define CFG_LOOP_DELAY_MS 20
#endif


// -----------------------------------------------------------------------------
// 8. 超声波测距 HC-SR04（沿用 Wave_Test 移植进来的非阻塞状态机）
//
// ⚠️⚠️ 【"一直检测不到"的排查顺序，按这个来】
//
//   第 0 步（最容易漏）：确认 CFG_ENABLE_WAVE = 1。
//       它是 0 的时候：不建 waveTask、不调 waveSensor.begin()，
//       TRIG/ECHO 连 pinMode 都没配，串口一条测距日志都没有。
//       —— 这就是本次"一直测不到"的直接原因。
//
//   第 1 步：确认 ECHO 上有**分压**。HC-SR04 的 ECHO 输出 5V，
//       直连 GPIO 会伤芯片（可能已经伤了）。推荐 1k 串联 + 2k 下拉 -> 约 3.3V。
//       **软件把引脚配成 INPUT 并不能解决 5V 电平问题**。
//
//   第 2 步：确认 TRIG/ECHO 的 GPIO 和接线一致。
//       串口敲 u 或 d，会打印"当前生效的 TRIG/ECHO 引脚"；
//       焊接前先敲一次，照着打印出来的脚接，就不会接错。
//
//   第 3 步：看错误码。u 会打印 err：
//       1 = ECHO 一直没拉高 -> TRIG 没到模块 / 模块没供 5V / 线接错 / 模块坏
//       2 = ECHO 一直没拉低 -> 实测**就是这个**（ECHO 拉高后再也不拉低）
//
//   【err=2 的硬件排查清单（按怀疑度排序）】
//     a) ECHO 分压那一路上，下拉电阻是不是**没焊/虚焊**？
//        没有下拉时 ECHO 悬空、被上拉 -> 永远读高 -> 正中 err=2。
//     b) ECHO 这根线是不是接到了一个**已经被别的外设驱动**的 GPIO？
//        （那根脚被别的模块推着高电平，也会一直读高）
//     c) GPIO38/39 这两个脚本身是不是坏了？换两个空闲脚试（例如 40/41/42）。
//        ⚠️ GPIO39 在 ESP32-S3 上默认是 JTAG MTCK，本工程没开 JTAG 所以能用，
//           但如果你手上有 JTAG 或调试器接着，先拔掉再试。
//     d) 模块是不是**供电不足**：HC-SR04 要 5V（VCC），且必须和主控共地；
//        3.3V 供电的模块可能完全不出回波。
//     e) 换一个 HC-SR04 模块对比 —— 排除模块本身损坏。
//     说明：这 5 条都是**硬件**侧，软件已经排除了；软件侧现在默认就是这个开关关着。
//
// 【引脚为什么要选】（本次把测距从 5/6 挪走）
//   旧的 TRIG=5 / ECHO=6 落在**升降电机的 IN1~IN4(4,5,6,7)** 里面，
//   两者同时接会互相干扰（电机走步会在 5/6 上乱翻转，测距必然不准）。
//   所以本工程提供 CFG_WAVE_PIN_LAYOUT 二选一：
//     1 = TRIG=5  ECHO=6    （老脚位；**必须**同时把升降电机关掉才不冲突）
//     2 = TRIG=38 ECHO=39   （默认；38/39 现在空着，和两台电机都不冲突）
//   ⚠️ 选 1 又开着 CFG_STEPPER_LIFT_ENABLE 的话，第 14.3 节会在编译期直接报错。
//
// ⚠️ GPIO4 是 ESP32-S3 的 strapping 脚（上电时决定 VDD_SPI 电压），
//    TRIG 在复位瞬间被拉高有风险，所以两套脚位都**不再使用 GPIO4**。
//
// ⚠️ HC-SR04 是"辅助观察书堆位置/机械状态"用的。它**不是**能分辨每次 6mm 位移的
//    高精度传感器，所以：
//      · 正常测量周期 = 5 秒（见第 7 节 WAVE_MEASURE_INTERVAL_MS）；
//      · 测量结果只做显示和辅助校验，**不直接触发机械动作**；
//      · 距离估算值和升降累计步数**分开保存**，不把估算距离当绝对位置。
// -----------------------------------------------------------------------------
// ==== 测距引脚布局（一键切换，不用手改下面的 PIN_WAVE_*）====
//   1 = 老脚位：TRIG=5  ECHO=6   （与升降电机 IN1~IN4 冲突，只能二选一）
//   2 = 默认  ：TRIG=38 ECHO=39  （和两台电机都不冲突，推荐）
#ifndef CFG_WAVE_PIN_LAYOUT
#define CFG_WAVE_PIN_LAYOUT 2
#endif

#ifndef PIN_WAVE_TRIG
#if CFG_WAVE_PIN_LAYOUT == 1
#define PIN_WAVE_TRIG 5
#else
#define PIN_WAVE_TRIG 38
#endif
#endif
#ifndef PIN_WAVE_ECHO
#if CFG_WAVE_PIN_LAYOUT == 1
#define PIN_WAVE_ECHO 6
#else
#define PIN_WAVE_ECHO 39
#endif
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

// 连续几次无效读数就认为"无目标/没接模块"
#ifndef CFG_WAVE_INVALID_LIMIT
#define CFG_WAVE_INVALID_LIMIT 3
#endif

// 是否把距离显示到屏幕上（0 = 只走串口）
#ifndef CFG_WAVE_SHOW_ON_SCREEN
#define CFG_WAVE_SHOW_ON_SCREEN 1
#endif

// 相邻两次"有效读数"差多少 mm 以上就认为跳变太大、不可信。
// 超声波偶尔会读到多路径反射（比如书架侧板），这种读数不能直接拿去改变机械位置。
// 0 = 不做跳变检查。
// ⚠️ 待实机标定：先把书堆固定不动，看正常波动（一般 ±5mm 以内），
//    再把门限设在波动的 2~3 倍。
#ifndef CFG_WAVE_JUMP_MM
#define CFG_WAVE_JUMP_MM 80
#endif


// -----------------------------------------------------------------------------
// 9. 机械：两台 28BYJ-48 + ULN2003（本次改造的核心参数区）
//
// 【本次机械分工 —— 和旧工程的三台电机设计完全不同，不要再按旧设计写代码】
//   升降电机(lift)：齿轮齿条带动**整叠书**上下移动，使目标书对准侧壁出书口。
//   推书电机(push)：齿轮齿条带动水平推书机构，把已经对准出书口的书推出。
//
//   ⚠️ 升降电机**不能单独选择书堆内部的某一本书**！
//      要取第 N 本，必须先把整叠书移到"那一本对准出书口"的高度，再执行水平推出。
//
// 【旧工程的三台电机（已废弃，仅作历史记录，避免以后再被翻出来）】
//   旧电机1 水平丝杆 -> 推出小车   |  旧电机2 垂直丝杆 -> 中间夹层
//   旧电机3 垂直丝杆 -> 升降平台
//   旧引脚：M1=4,5,6,7   M2=15,16,17,18   M3=38,39,48,21
//   ⚠️ 旧方案的电机1 引脚(4,5,6,7) 和 HC-SR04 的 TRIG/ECHO 本来就冲突，
//      而且第三台电机本次根本用不到。所以本次**只保留两个电机轴**：
//      M1 = 升降，M2 = 推书。旧宏名在第 9 节末尾做兼容映射。
//
// 【28BYJ-48 的步数常识（算位置之前必须先想清楚）】
//   输出轴一圈 = 4096 步（四相八拍，见 StepperLib 注释）。
//   开环控制：没有编码器、没有限位开关。堵转、失步、齿轮间隙都会让
//   "软件以为的位置"和"机构的真实位置"越差越多。所以：
//     · 每本书的步数必须是**实机标定**出来的，不是纸面算出来的；
//     · 位置有效性靠"人工归零"重新建立（见第 9.2 节）；
//     · 软限位一定要设，而且宁可保守。
// -----------------------------------------------------------------------------
#ifndef CFG_STEPPER_DRIVER
#define CFG_STEPPER_DRIVER 0
#endif

// 升降电机：齿轮齿条带动整叠书上下
// 4,5,6,7 沿用旧工程里已经验证过能转的那一组，不改线，减少一次接线风险。
#ifndef CFG_STEPPER_LIFT_PINS
#define CFG_STEPPER_LIFT_PINS { 4, 5, 6, 7 }
#endif

// 推书电机：齿轮齿条把书水平推出
// 旧 M2 的 15,16,17,18 里，18 这次让给触摸中断 T_IRQ，最后一个补到 21。
// ⚠️ 21 在 ESP32-S3 上同时是默认 I2C 的 SDA，但本工程二哈用的是 GPIO1/2，
//    所以 21 是空闲的，可以当普通 GPIO 用。
#ifndef CFG_STEPPER_PUSH_PINS
#define CFG_STEPPER_PUSH_PINS { 15, 16, 17, 21 }
#endif

// 两个轴的独立开关。没接线的轴置 0，StepperLib 会跳过它、不碰任何 GPIO。
#ifndef CFG_STEPPER_LIFT_ENABLE
#define CFG_STEPPER_LIFT_ENABLE 1
#endif
#ifndef CFG_STEPPER_PUSH_ENABLE
#define CFG_STEPPER_PUSH_ENABLE 1
#endif

// ---- 旧宏名兼容（老代码/README 里还在用 M1/M2/M3 的说法）----
// ⚠️ 旧 M2 是"中间夹层"、旧 M3 是"升降平台"，语义和现在**不一样**了。
//    所以这里只把 M1 映射成升降、M2 映射成推书；
//    M3 直接映射成"没接线"，防止残留的旧代码真的去驱动一台已经不存在的电机。
#ifndef CFG_STEPPER_M1_PINS
#define CFG_STEPPER_M1_PINS CFG_STEPPER_LIFT_PINS
#endif
#ifndef CFG_STEPPER_M2_PINS
#define CFG_STEPPER_M2_PINS CFG_STEPPER_PUSH_PINS
#endif
#ifndef CFG_STEPPER_M1_ENABLE
#define CFG_STEPPER_M1_ENABLE CFG_STEPPER_LIFT_ENABLE
#endif
#ifndef CFG_STEPPER_M2_ENABLE
#define CFG_STEPPER_M2_ENABLE CFG_STEPPER_PUSH_ENABLE
#endif
#ifndef CFG_STEPPER_M3_ENABLE
#define CFG_STEPPER_M3_ENABLE 0    // 旧电机3（升降平台）已废弃：硬关，不让旧代码驱动它
#endif

// 步进模式：0 = 八拍（半拍，力矩大，更平滑）  1 = 四拍（单四拍，简单）
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

// 软限位行程（步）。_posSteps 是"相对人工零点的步数"，允许落在 [-range, +range]。
//
// ⚠️⚠️ 下面是**待实机标定的保守初值**，不是已经验证过的机械行程！
//   标定步骤：
//     ① 先用调试页的小步长(10 步)点动，确认方向对不对；
//     ② 用中等步长(100 步)慢慢走，量出机构从最底到最顶一共多少步；
//     ③ 把量到的步数打个八折填到这里（留安全余量），再考虑调大点动步长。
//   在标定完成之前**不要**执行自动取书/存书，只用调试页点动。
#ifndef CFG_STEPPER_LIFT_RANGE_STEPS
#define CFG_STEPPER_LIFT_RANGE_STEPS 4096    // 待标定：升降机构总行程(步)
#endif
#ifndef CFG_STEPPER_PUSH_RANGE_STEPS
#define CFG_STEPPER_PUSH_RANGE_STEPS 2048    // 待标定：推书机构总行程(步)
#endif

// 旧宏名兼容（旧 M1=推出 M2=夹层 M3=升降，语义已变，只给老代码兜底）
#ifndef CFG_STEPPER_M1_RANGE_STEPS
#define CFG_STEPPER_M1_RANGE_STEPS CFG_STEPPER_LIFT_RANGE_STEPS
#endif
#ifndef CFG_STEPPER_M2_RANGE_STEPS
#define CFG_STEPPER_M2_RANGE_STEPS CFG_STEPPER_PUSH_RANGE_STEPS
#endif
#ifndef CFG_STEPPER_M3_RANGE_STEPS
#define CFG_STEPPER_M3_RANGE_STEPS 2048
#endif

// 上下电后要不要回零（当前没有限位开关，只能把上电位置当零点）。
// ⚠️ **保持 0**：绝对不要在开机时盲目驱动电机去找一个假定的零点。
//    软件零点靠调试页的"将当前位置设为零点"由人工建立。
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

// 42 步进驱动器的 EN（使能）脚是否低有效（A4988/DRV8825/TMC 系都是低有效）
#ifndef CFG_STEPPER_EN_ACTIVE_LOW
#define CFG_STEPPER_EN_ACTIVE_LOW 1
#endif

// 步进命令队列长度 / 单条命令最长字符数
#ifndef CFG_STEPPER_QUEUE_SIZE
#define CFG_STEPPER_QUEUE_SIZE 8
#endif
#ifndef CFG_STEPPER_CMD_LEN
#define CFG_STEPPER_CMD_LEN 32
#endif

// 阻塞式 run() 的默认超时(ms)，0 = 不超时（一直转到走完）
#ifndef CFG_STEPPER_RUN_TIMEOUT_MS
#define CFG_STEPPER_RUN_TIMEOUT_MS 20000
#endif

// -----------------------------------------------------------------------------
// 9.1 书籍厚度与升降位置的换算（**全部是要实机标定的参数**）
//
// 机械是"齿轮齿条"，真正决定"一本书对应多少步"的是齿条的齿距对应多少步，
// 不是 28BYJ 转一圈多少步。理论换算是：
//   齿条模数 m、齿数 z -> 齿轮一圈走 π·m·z 毫米；
//   电机输出轴一圈 4096 步 -> 每毫米步数 = 4096 / (π·m·z)。
// 这些机械参数本次没有确认过，所以代码里给两条路：
//   · CFG_LIFT_STEPS_PER_BOOK：每本书对应的升降步数（**主用，直接标定**）；
//   · CFG_LIFT_STEPS_PER_MM  ：每毫米对应多少步（理论换算/标定后填）。
//   两个都填了就以 CFG_LIFT_STEPS_PER_BOOK 为准（更直接、误差更小）。
// -----------------------------------------------------------------------------

// 用超声波测厚时（串口命令 u,e）的判定容差(mm)：
// 算出来的厚度和理论厚度的差超过这个值就提示"厚度异常"。
// ⚠️ 待实机标定：默认 ±2mm 是沿用旧工程的取值，没在真机上验证过。
#ifndef CFG_THICK_TOLERANCE_MM
#define CFG_THICK_TOLERANCE_MM 2
#endif

// 每本书的理论厚度(mm)，初始估值 6mm。
// ⚠️ 只用来做粗算和界面显示，不是精确测量值。想更准就用超声波测厚（串口命令 u,e）。
#ifndef CFG_BOOK_THICK_MM
#define CFG_BOOK_THICK_MM 6.0f
#endif

// 升降"每走 1 毫米"对应的步数。
// ⚠️ 待实机标定。0 = 不用这个参数，改用 CFG_LIFT_STEPS_PER_BOOK。
//   标定方法：① 人工归零；② 调试页点动 1000 步；③ 卡尺量平台实际走了多少毫米；
//            ④ 1000 / 实际毫米数，填到这里。
#ifndef CFG_LIFT_STEPS_PER_MM
#define CFG_LIFT_STEPS_PER_MM 0.0f
#endif

// 升降"每本书"对应的步数（**主用参数，必须实机标定**）。
// ⚠️ 待标定。标定方法：
//     ① 人工归零；② 书堆只放 1 本书；③ 调试页点动，让这本书对准出书口；
//     ④ 记下走了多少步，就是"每本书步数"。
//   未标定前保持这个保守值，并且**不要**开自动取书/存书。
#ifndef CFG_LIFT_STEPS_PER_BOOK
#define CFG_LIFT_STEPS_PER_BOOK 200
#endif

// 数组下标 0（书堆最顶上一本，也就是零点位置那一本）相对零点的步数偏移。
// 如果"人工归零"时就是把最上面那本对准出书口，这里保持 0。
#ifndef CFG_LIFT_INDEX0_OFFSET_STEPS
#define CFG_LIFT_INDEX0_OFFSET_STEPS 0
#endif

// 单次自动动作允许的最大升降步数（比软限位更早拦住"参数多写一个 0"）
#ifndef CFG_LIFT_MAX_MOVE_STEPS
#define CFG_LIFT_MAX_MOVE_STEPS 6000
#endif

// ---- 推书机构 ----

// 推书行程：把书完全推出需要的步数（**待实机标定**）
#ifndef CFG_PUSH_OUT_STEPS
#define CFG_PUSH_OUT_STEPS 600
#endif

// 推书回退步数。
// ⚠️ 只有实际机械结构允许（推书板能退回来、且不会把书带回去）才启用。
//    默认**不回退**（CFG_PUSH_AUTO_RETRACT=0）：推出后由用户把书拿走，推书板停在前面。
#ifndef CFG_PUSH_AUTO_RETRACT
#define CFG_PUSH_AUTO_RETRACT 0
#endif
#ifndef CFG_PUSH_RETRACT_STEPS
#define CFG_PUSH_RETRACT_STEPS 600
#endif

// 单次自动动作允许的最大推书步数
#ifndef CFG_PUSH_MAX_MOVE_STEPS
#define CFG_PUSH_MAX_MOVE_STEPS 2000
#endif

// ---- 方向约定 ----
// 换相序、齿轮齿条装配方向都会影响"正转到底是往上还是往下"。
// 这里给两个符号宏，标定时用调试页点动确认后改符号即可，不用改逻辑：
//   1 = run() 传正数时机构朝"正方向"（升降=向上，推书=推出）
//  -1 = 反过来
// ⚠️ 默认值只是按现有接线**假定**的，必须实机点动确认！
#ifndef CFG_LIFT_DIR_SIGN
#define CFG_LIFT_DIR_SIGN 1
#endif
#ifndef CFG_PUSH_DIR_SIGN
#define CFG_PUSH_DIR_SIGN 1
#endif

// 单次动作的兜底超时(ms)。正常情况靠"走完步数"结束；
// 这里防的是"电机因为某种原因一直 busy"，超时后判动作失败并把位置标记为需要校准。
#ifndef CFG_MECH_ACTION_TIMEOUT_MS
#define CFG_MECH_ACTION_TIMEOUT_MS 30000
#endif

// 机械动作请求队列长度（bookTask / 串口 / 调试页都往这里投）
#ifndef CFG_MECH_QUEUE_SIZE
#define CFG_MECH_QUEUE_SIZE 8
#endif

// 取书结束后是否用超声波读数做一次"辅助校验"。
// ⚠️ 默认 0（关）：传感器安装几何关系和固定目标距离都还没确认，
//    盲目用距离判"书到位了"会引入假阳性。等实机标定完再打开。
#ifndef CFG_MECH_USE_WAVE_CHECK
#define CFG_MECH_USE_WAVE_CHECK 0
#endif


// -----------------------------------------------------------------------------
// 9.2 调试界面（第 6 页）的机械点动参数
//
// 所有点动都必须走 stepperTask（唯一碰电机引脚的任务），
// **不能**由 LVGL 事件回调直接操作 GPIO，也不能在显示任务里等电机走完。
// -----------------------------------------------------------------------------
// 三档点动步长。改动范围受 CFG_STEPPER_MAX_STEPS 和软限位双重限制。
#ifndef CFG_DEBUG_JOG_SMALL
#define CFG_DEBUG_JOG_SMALL 10
#endif
#ifndef CFG_DEBUG_JOG_MID
#define CFG_DEBUG_JOG_MID 100
#endif
#ifndef CFG_DEBUG_JOG_LARGE
#define CFG_DEBUG_JOG_LARGE 500
#endif


// -----------------------------------------------------------------------------
// 9.3 书籍识别与入库规则（方案 A：每个分类 ID 最多一条记录）
//
// ⚠️ 分类 ID 用的是 HuskyWord.id（= 底层 Result::ID），**不是 classID**！
//    Result.h 里 classID 和 xCenter 是同一个 union 槽位，读 classID 读到的是坐标。
//    学科映射表在 lib/BookDomainLib 里（getBookNameById()），不在这里写死。
//
// 方案 A 的完整规则：
//   · 分类 ID 必须是 1~6，否则视为无效：不入库，也**不触发任何机械动作**；
//   · 同一个分类 ID 已经存在时：不新增、不累加数量，只在界面/串口提示"已存在"；
//   · 新书入库时**插到数组最顶部**（新书从顶部放入，逻辑书堆里也是最新的一本在最顶）。
//
// ⚠️ 必须承认的现实限制：
//   软件只能保证"书单里一个学科只有一条记录"，**无法阻止用户把重复学科的实体书
//   并且在界面和 README 里把这条限制写清楚，不能假装软件已经拦住了实体书。
// -----------------------------------------------------------------------------
//
// 同一个分类 ID 要**连续几次**识别到才认为"稳定"，防止单帧误判触发入库。
// 1 = 不防抖（不推荐）；识别周期 5 秒时，3 次约等于连续 15 秒都识别到同一学科。
// ⚠️ 这个值决定"识别到入库"要等多久，调试时可以改成 2 加快节奏。
#ifndef CFG_BOOK_CONFIRM_FRAMES
#define CFG_BOOK_CONFIRM_FRAMES 3

// ⚠️ 上面这个值 × HUSKY_READ_INTERVAL_MS(5秒) = 从"看到书"到"真正请求入库"要等的时间。
//    默认 3 × 5 = 15 秒。**调试阶段如果觉得太慢，把它改成 1**（看到就立刻请求）。
//    流水线上正式用再改回 3，用连续多帧把单帧误判挡掉。
#endif

// 操作提示/错误文本缓冲（UTF-8 一个汉字 3 字节）
#ifndef CFG_BOOK_NOTE_LEN
#define CFG_BOOK_NOTE_LEN 96
#endif

// 存书/取书状态机的兜底超时(ms)：等机械动作最多等这么久，超时就判失败、保留原记录。
#ifndef CFG_BOOK_FLOW_TIMEOUT_MS
#define CFG_BOOK_FLOW_TIMEOUT_MS 40000
#endif

// 存书流程里"等用户把书放好"的最长时间(ms)。
//   本次机械**没有**"书已放入"的检测手段（没有对射开关、没有微动开关），
//   所以**不能虚构自动检测能力**。流程拆成：
//     识别确认 -> 提示用户放书 -> 用户按确认 -> 才把记录插入书单并落盘。
//   超时没确认就取消入库（机械位置不动，书单也不改）。
#ifndef CFG_BOOK_PLACE_TIMEOUT_MS
#define CFG_BOOK_PLACE_TIMEOUT_MS 60000
#endif


// -----------------------------------------------------------------------------
// 10. Flash 持久化（Preferences / NVS，用 ESP32-S3 内部 Flash）
//
// 为什么用 Preferences：Arduino 框架自带，不需要外部存储芯片，也不需要额外依赖。
// 保存的内容和校验规则见 lib/StorageLib；这里只放"存哪些、什么时候存"的开关和上限。
//
// 【掉电恢复的核心思路（三句话）】
//   1) 任何会改变机械位置的动作**开始之前**，先往 Flash 写一个"动作未完成"标记；
//   2) 动作正常结束后，写"动作已完成" + 新的步数；
//   3) 复位时发现标记还是"未完成" -> 说明上次是断电中断的，把位置判为无效，
//      **要求人工归零**，在归零之前不允许执行依赖绝对位置的自动取书。
//   ⚠️ 开机**绝对不要**驱动电机去找一个假定的零点。
// -----------------------------------------------------------------------------
// 数据版本号。**改 BookItem 结构或存储布局时必须 +1**，
// 这样老固件的数据会被识别成"版本不匹配"，而不是被当成有效数据读出乱码。
#ifndef CFG_STORAGE_DATA_VERSION
#define CFG_STORAGE_DATA_VERSION 1
#endif

// 一次最多保存多少本书（每个学科最多一本，6 本实际上就够了，给到 16 有余量）
#ifndef CFG_STORAGE_MAX_BOOKS
#define CFG_STORAGE_MAX_BOOKS 16
#endif

// 保存的 liftSteps 绝对值超过"软限位 × 这个系数"就认为不可信，
// 复位时应判为"需要人工归零"，而不是直接相信它。
#ifndef CFG_STORAGE_STEPS_TRUST_FACTOR
#define CFG_STORAGE_STEPS_TRUST_FACTOR 2
#endif

// 是否在开机时自动加载 Flash 里的书单和位置（0 = 只打开 NVS，不加载）
#ifndef CFG_STORAGE_AUTO_LOAD
#define CFG_STORAGE_AUTO_LOAD 1
#endif


// -----------------------------------------------------------------------------
// 11. 与 ESP32-C3 步进控制板的串口链路（**本次不使用，预留**）
//
// 本次改造的机械控制**全部在 ESP32-S3 主控上直接做**（两台 28BYJ-48 + ULN2003），
// 不引入第二块控制板、不引入云服务/联网借阅/语音识别。
// 这一节只是把旧工程预留的宏留住，避免老代码找不到定义。
// ⚠️ 想启用必须确认 GPIO6/7 没有被测距占用（本工程 ECHO 默认就是 6，会冲突）。
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
//   CFG_LOGI("系统信息: ram=%uKB", ramKb);
//   等级 >= CFG_LOG_LEVEL 才真的打印，CFG_LOG_LEVEL=0 时连编译都不进。
//
// 【本次改造要求：错误状态不能只打串口】
//   用户可见的重要错误（分类 ID 无效、学科重复、机械失败、需要人工归零、
//   Flash 读写失败）必须**同时**反映到界面上（消息层/调试页），不能只 printf。
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
#define DISPLAYLIB_VERSION "1.2.0"    // 1.2.0: 加第6页电机调试页 + 全页面只允许上下滑动
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
#ifndef TOUCHLIB_VERSION
#define TOUCHLIB_VERSION "1.0.0"
#endif
#ifndef BOOKLISTLIB_VERSION
#define BOOKLISTLIB_VERSION "1.1.0"    // 1.1.0: 方案A —— 一个学科只留一条记录
#endif
#ifndef BOOKDOMAINLIB_VERSION
#define BOOKDOMAINLIB_VERSION "1.0.0"
#endif
#ifndef MECHANISMLIB_VERSION
#define MECHANISMLIB_VERSION "1.0.0"
#endif
#ifndef STORAGELIB_VERSION
#define STORAGELIB_VERSION "1.0.0"
#endif

// -----------------------------------------------------------------------------,// 14. 引脚冲突编译期体检（**新增：一次性把明确的抢脚拦在编译期**）,//,// 为什么要有这一段：引脚冲突在运行期的表现是"某个外设时好时坏"，非常难查。,// 这里用预处理期就把能确定的冲突拦下来，编译直接报中文错误，,// 而不是烧进去以后在台子上猜。,//,// ⚠️ 只检查"能确定冲突"的情况：同一个 GPIO 被两个**同时启用**的外设占用。,//    外设关掉时不算占用（没接线的脚不算冲突）。,// -----------------------------------------------------------------------------,,// ---- 14.1 每个电机的 4 个 IN 脚内部不能重复 ----,#if CFG_STEPPER_LIFT_ENABLE,#if (CFG_STEPPER_LIFT_PINS)[0] == (CFG_STEPPER_LIFT_PINS)[1] || \,    (CFG_STEPPER_LIFT_PINS)[0] == (CFG_STEPPER_LIFT_PINS)[2] || \,    (CFG_STEPPER_LIFT_PINS)[0] == (CFG_STEPPER_LIFT_PINS)[3],#error "config.h 14.1: 升降电机的 4 个 IN 脚里有重复的 GPIO，检查 CFG_STEPPER_LIFT_PINS",#endif,#endif,,#if CFG_STEPPER_PUSH_ENABLE,#if (CFG_STEPPER_PUSH_PINS)[0] == (CFG_STEPPER_PUSH_PINS)[1] || \,    (CFG_STEPPER_PUSH_PINS)[0] == (CFG_STEPPER_PUSH_PINS)[2] || \,    (CFG_STEPPER_PUSH_PINS)[0] == (CFG_STEPPER_PUSH_PINS)[3],#error "config.h 14.1: 推书电机的 4 个 IN 脚里有重复的 GPIO，检查 CFG_STEPPER_PUSH_PINS",#endif,#endif,,// ---- 14.2 两个电机之间不能抢脚 ----,#if CFG_STEPPER_LIFT_ENABLE && CFG_STEPPER_PUSH_ENABLE,#if (CFG_STEPPER_LIFT_PINS)[0] == (CFG_STEPPER_PUSH_PINS)[0] || \,    (CFG_STEPPER_LIFT_PINS)[0] == (CFG_STEPPER_PUSH_PINS)[1] || \,    (CFG_STEPPER_LIFT_PINS)[0] == (CFG_STEPPER_PUSH_PINS)[2] || \,    (CFG_STEPPER_LIFT_PINS)[0] == (CFG_STEPPER_PUSH_PINS)[3] || \,    (CFG_STEPPER_LIFT_PINS)[1] == (CFG_STEPPER_PUSH_PINS)[0] || \,    (CFG_STEPPER_LIFT_PINS)[1] == (CFG_STEPPER_PUSH_PINS)[1] || \,    (CFG_STEPPER_LIFT_PINS)[1] == (CFG_STEPPER_PUSH_PINS)[2] || \,    (CFG_STEPPER_LIFT_PINS)[1] == (CFG_STEPPER_PUSH_PINS)[3] || \,    (CFG_STEPPER_LIFT_PINS)[2] == (CFG_STEPPER_PUSH_PINS)[0] || \,    (CFG_STEPPER_LIFT_PINS)[2] == (CFG_STEPPER_PUSH_PINS)[1] || \,    (CFG_STEPPER_LIFT_PINS)[2] == (CFG_STEPPER_PUSH_PINS)[2] || \,    (CFG_STEPPER_LIFT_PINS)[2] == (CFG_STEPPER_PUSH_PINS)[3] || \,    (CFG_STEPPER_LIFT_PINS)[3] == (CFG_STEPPER_PUSH_PINS)[0] || \,    (CFG_STEPPER_LIFT_PINS)[3] == (CFG_STEPPER_PUSH_PINS)[1] || \,    (CFG_STEPPER_LIFT_PINS)[3] == (CFG_STEPPER_PUSH_PINS)[2] || \,    (CFG_STEPPER_LIFT_PINS)[3] == (CFG_STEPPER_PUSH_PINS)[3],#error "config.h 14.2: 升降电机和推书电机抢 GPIO，检查 CFG_STEPPER_LIFT_PINS / CFG_STEPPER_PUSH_PINS",#endif,#endif,,// ---- 14.3 HC-SR04 与升降电机抢脚（这是原工程就有的老问题，必须拦） ----,// 把 TRIG/ECHO 改成空闲脚（例如 38/39）之后，这个检查就会自然通过。,#if CFG_ENABLE_STEPPER && CFG_STEPPER_LIFT_ENABLE && (CFG_ENABLE_WAVE || PIN_WAVE_TRIG >= 0),#if (PIN_WAVE_TRIG >= 0) && ((PIN_WAVE_TRIG == (CFG_STEPPER_LIFT_PINS)[0]) || \,    (PIN_WAVE_TRIG == (CFG_STEPPER_LIFT_PINS)[1]) || \,    (PIN_WAVE_TRIG == (CFG_STEPPER_LIFT_PINS)[2]) || \,    (PIN_WAVE_TRIG == (CFG_STEPPER_LIFT_PINS)[3]) || \,    (PIN_WAVE_ECHO == (CFG_STEPPER_LIFT_PINS)[0]) || \,    (PIN_WAVE_ECHO == (CFG_STEPPER_LIFT_PINS)[1]) || \,    (PIN_WAVE_ECHO == (CFG_STEPPER_LIFT_PINS)[2]) || \,    (PIN_WAVE_ECHO == (CFG_STEPPER_LIFT_PINS)[3]),#error "config.h 14.3: HC-SR04 的 TRIG/ECHO 与升降电机的 IN 脚冲突。请把测距挪到空闲脚，例如在 platformio.ini 的 build_flags 里加 -DPIN_WAVE_TRIG=38 -DPIN_WAVE_ECHO=39",#endif,#endif,,// ---- 14.4 步进电机与 TFT 抢脚（TFT 的 6 根线是硬需求，冲突时只能改电机） ----,// 用"4 个 IN 脚之和"和"6 个 TFT 脚之和"做交叉检查不现实，,// 这里用逐项比较，只查最容易犯的错：LIFT/PUSH 的前两个脚撞上 TFT 的任一引脚。,#if CFG_STEPPER_LIFT_ENABLE,#if (CFG_STEPPER_LIFT_PINS)[0] == PIN_TFT_SCLK || (CFG_STEPPER_LIFT_PINS)[0] == PIN_TFT_MOSI || \,    (CFG_STEPPER_LIFT_PINS)[0] == PIN_TFT_CS   || (CFG_STEPPER_LIFT_PINS)[0] == PIN_TFT_DC   || \,    (CFG_STEPPER_LIFT_PINS)[0] == PIN_TFT_RST  || (CFG_STEPPER_LIFT_PINS)[0] == PIN_TFT_BL   || \,    (CFG_STEPPER_LIFT_PINS)[1] == PIN_TFT_SCLK || (CFG_STEPPER_LIFT_PINS)[1] == PIN_TFT_MOSI || \,    (CFG_STEPPER_LIFT_PINS)[1] == PIN_TFT_CS   || (CFG_STEPPER_LIFT_PINS)[1] == PIN_TFT_DC   || \,    (CFG_STEPPER_LIFT_PINS)[1] == PIN_TFT_RST  || (CFG_STEPPER_LIFT_PINS)[1] == PIN_TFT_BL   || \,    (CFG_STEPPER_LIFT_PINS)[2] == PIN_TFT_SCLK || (CFG_STEPPER_LIFT_PINS)[2] == PIN_TFT_MOSI || \,    (CFG_STEPPER_LIFT_PINS)[2] == PIN_TFT_CS   || (CFG_STEPPER_LIFT_PINS)[2] == PIN_TFT_DC   || \,    (CFG_STEPPER_LIFT_PINS)[2] == PIN_TFT_RST  || (CFG_STEPPER_LIFT_PINS)[2] == PIN_TFT_BL   || \,    (CFG_STEPPER_LIFT_PINS)[3] == PIN_TFT_SCLK || (CFG_STEPPER_LIFT_PINS)[3] == PIN_TFT_MOSI || \,    (CFG_STEPPER_LIFT_PINS)[3] == PIN_TFT_CS   || (CFG_STEPPER_LIFT_PINS)[3] == PIN_TFT_DC   || \,    (CFG_STEPPER_LIFT_PINS)[3] == PIN_TFT_RST  || (CFG_STEPPER_LIFT_PINS)[3] == PIN_TFT_BL,#error "config.h 14.4: 升降电机与 TFT 抢 GPIO，检查第 3 节和第 9 节的引脚表",#endif,#endif,,#if CFG_STEPPER_PUSH_ENABLE,#if (CFG_STEPPER_PUSH_PINS)[0] == PIN_TFT_SCLK || (CFG_STEPPER_PUSH_PINS)[0] == PIN_TFT_MOSI || \,    (CFG_STEPPER_PUSH_PINS)[0] == PIN_TFT_CS   || (CFG_STEPPER_PUSH_PINS)[0] == PIN_TFT_DC   || \,    (CFG_STEPPER_PUSH_PINS)[0] == PIN_TFT_RST  || (CFG_STEPPER_PUSH_PINS)[0] == PIN_TFT_BL   || \,    (CFG_STEPPER_PUSH_PINS)[1] == PIN_TFT_SCLK || (CFG_STEPPER_PUSH_PINS)[1] == PIN_TFT_MOSI || \,    (CFG_STEPPER_PUSH_PINS)[1] == PIN_TFT_CS   || (CFG_STEPPER_PUSH_PINS)[1] == PIN_TFT_DC   || \,    (CFG_STEPPER_PUSH_PINS)[1] == PIN_TFT_RST  || (CFG_STEPPER_PUSH_PINS)[1] == PIN_TFT_BL   || \,    (CFG_STEPPER_PUSH_PINS)[2] == PIN_TFT_SCLK || (CFG_STEPPER_PUSH_PINS)[2] == PIN_TFT_MOSI || \,    (CFG_STEPPER_PUSH_PINS)[2] == PIN_TFT_CS   || (CFG_STEPPER_PUSH_PINS)[2] == PIN_TFT_DC   || \,    (CFG_STEPPER_PUSH_PINS)[2] == PIN_TFT_RST  || (CFG_STEPPER_PUSH_PINS)[2] == PIN_TFT_BL   || \,    (CFG_STEPPER_PUSH_PINS)[3] == PIN_TFT_SCLK || (CFG_STEPPER_PUSH_PINS)[3] == PIN_TFT_MOSI || \,    (CFG_STEPPER_PUSH_PINS)[3] == PIN_TFT_CS   || (CFG_STEPPER_PUSH_PINS)[3] == PIN_TFT_DC   || \,    (CFG_STEPPER_PUSH_PINS)[3] == PIN_TFT_RST  || (CFG_STEPPER_PUSH_PINS)[3] == PIN_TFT_BL,#error "config.h 14.4: 推书电机与 TFT 抢 GPIO，检查第 3 节和第 9 节的引脚表",#endif,#endif,,// ---- 14.5 外设不能用被内部 Flash / OPI PSRAM / USB / UART0 占掉的 GPIO ----,// 这一段是"防手滑"：这些 GPIO 一旦被外设占用，表现是随机复位或直接起不来。,#if (PIN_HUSKY_SDA >= 26 && PIN_HUSKY_SDA <= 32) || (PIN_HUSKY_SCL >= 26 && PIN_HUSKY_SCL <= 32),#error "config.h 14.5: 二哈 SDA/SCL 不能落在 GPIO26~32（封装内 Flash）",#endif,#if (PIN_HUSKY_SDA >= 33 && PIN_HUSKY_SDA <= 37) || (PIN_HUSKY_SCL >= 33 && PIN_HUSKY_SCL <= 37),#error "config.h 14.5: 二哈 SDA/SCL 不能落在 GPIO33~37（OPI PSRAM）",#endif,#if (PIN_WAVE_TRIG >= 26 && PIN_WAVE_TRIG <= 32) || (PIN_WAVE_ECHO >= 26 && PIN_WAVE_ECHO <= 32),#error "config.h 14.5: HC-SR04 不能落在 GPIO26~32（封装内 Flash）",#endif,#if (PIN_WAVE_TRIG >= 33 && PIN_WAVE_TRIG <= 37) || (PIN_WAVE_ECHO >= 33 && PIN_WAVE_ECHO <= 37),#error "config.h 14.5: HC-SR04 不能落在 GPIO33~37（OPI PSRAM）",#endif,#if (PIN_WAVE_TRIG == 19) || (PIN_WAVE_TRIG == 20) || (PIN_WAVE_ECHO == 19) || (PIN_WAVE_ECHO == 20),#error "config.h 14.5: HC-SR04 不能落在 GPIO19/20（USB D-/D+）",#endif,#if (PIN_WAVE_TRIG == 43) || (PIN_WAVE_TRIG == 44) || (PIN_WAVE_ECHO == 43) || (PIN_WAVE_ECHO == 44),#error "config.h 14.5: HC-SR04 不能落在 GPIO43/44（UART0 调试串口）",#endif,,// ---- 14.6 超声波引脚不能留给触摸（两条总线都在同一批 GPIO 上很容易搞混） ----,#if CFG_RUN_TOUCH && (PIN_WAVE_TRIG >= 0),#if (PIN_WAVE_TRIG == PIN_TOUCH_CS) || (PIN_WAVE_TRIG == PIN_TOUCH_CLK) || \,    (PIN_WAVE_TRIG == PIN_TOUCH_DIN) || (PIN_WAVE_TRIG == PIN_TOUCH_DO) || \,    (PIN_WAVE_ECHO == PIN_TOUCH_CS) || (PIN_WAVE_ECHO == PIN_TOUCH_CLK) || \,    (PIN_WAVE_ECHO == PIN_TOUCH_DIN) || (PIN_WAVE_ECHO == PIN_TOUCH_DO),#error "config.h 14.6: HC-SR04 与触摸抢 GPIO，检查第 3.2 节和第 8 节",#endif,#endif,,,// =============================================================================,// 【本次改造的统一引脚分配表】—— 一眼看全部，改线只改第 3 / 3.2 / 4 / 5 / 8 / 9 节,//,//  外设                     GPIO                      备注,//  ----------------------   -----------------------   ------------------------------,//  ILI9341 SCLK             12                        TFT_eSPI USER_SETUP（platformio.ini）,//  ILI9341 MOSI             11                        同上,//  ILI9341 CS               10                        同上,//  ILI9341 DC               9                         同上,//  ILI9341 RST              8                         同上,//  ILI9341 BL               47                        同上,//  XPT2046 T_CS             48                        触摸自己的 5 根线，不占屏的 SPI,//  XPT2046 T_IRQ            18                        可不接（本工程轮询）,//  XPT2046 T_CLK            13                        本来是步进电机3 的旧引脚，本次释放,//  XPT2046 T_DIN            14                        同上,//  XPT2046 T_DO             38                        同上,//  HUSKYLENS 2 SDA          1                         I2C 地址 0x50；与摄像头 SCCB 冲突,//  HUSKYLENS 2 SCL          2                         同上,//  按键 1 / 2 / 3           45 / 46 / 0               45/46 空闲；GPIO0 是 BOOT 脚,//  按键 A / B（取书/存书）  -1 / -1                   默认不接（需求：不要擅自加按键脚）,//  升降电机 IN1~IN4         4, 5, 6, 7                28BYJ-48 + ULN2003,//  推书电机 IN1~IN4         15, 16, 17, 21            28BYJ-48 + ULN2003,//  HC-SR04 TRIG             5                         与升降电机 IN2 冲突 -> 默认关,//  HC-SR04 ECHO             6                         5V，必须 1k+2k 分压！,//,//  ⚠️ 绝对不能接外设：GPIO26~32（封装内 Flash）、GPIO33~37（OPI PSRAM）、,//     GPIO22~25（S3 上不存在）、GPIO19/20（USB）、GPIO43/44（UART0）。,//,//  ⚠️ 空出来的 GPIO（本次没用到，以后要用先查一遍）：,//     3, 7 已被升降电机占用；剩余空闲：37 之后的 39、40、41、42（但 40/41/42 = 摄像头 D5/D6/D7）,// =============================================================================