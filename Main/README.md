# Main —— 主控整合工程（库化组织）

ESP32-S3 **N16R8** 主控固件。**功能代码全部在 `lib/` 下的独立库里，`src/main.cpp` 只负责建对象、注册命令、建任务。**

编译烧录：

```powershell
cd D:\Project\SmartBookShelf\Main
pio run -e esp32-s3-devkitc-1 -t upload
pio device monitor -e esp32-s3-devkitc-1     # 115200
```

`pio` 不在 PATH 时用绝对路径：`C:\Users\GX\.platformio\penv\Scripts\pio.exe`

---

## 1. 目录结构

```
Main/
├─ include/
│  ├─ config.h              ← ★全工程唯一配置来源（引脚/节奏/开关/阈值，每个宏都可 -D 覆盖）
│  ├─ BoardConfig.h         ← 兼容壳：只有一行 #include "config.h"，老代码继续有效
│  └─ lv_conf.h             ← LVGL 8.4 配置（LV_FONT_SIMSUN_16_CJK=1，屏上中文靠它）
├─ lib/
│  ├─ HuskyLib/              ← 二哈 I2C + 内置 CNN 识别（含 library.json）
│  ├─ DisplayLib/            ← ILI9341 + LVGL 8.4 中文界面（状态页/系统信息页/书列表页/消息页）
│  ├─ SysInfoLib/            ← RAM/PSRAM/Flash/运行时间采集与格式化
│  ├─ ButtonLib/             ← 按键消抖 + 事件队列
│  ├─ SerialCmdLib/          ← 串口命令行解析 + 注册式分派
│  ├─ StepperLib/            ← 28BYJ-48 + ULN2003 步进驱动（宏可切 42 步进）
│  ├─ UltraLib/              ← HC-SR04 超声波测距（非阻塞状态机）
│  ├─ TouchLib/              ← XPT2046 电阻触摸（独立 5 线位操作驱动）
│  └─ DFRobot_HuskylensV2/   ← 二哈底层驱动（vendor，离线可编译）
├─ src/main.cpp              ← 只做拼装：全局对象 / 命令注册 / 任务
├─ src/ui_font_16.c          ← ★生成的中文字库（GB2312 一级 3755 字 + 界面用字）
├─ fontgen/                  ← 字库生成脚本 + 说明（改字库看这里的 README）
├─ platformio.ini            ← N16R8 + TFT_eSPI(USER_SETUP) + LVGL 依赖（业务配置在 config.h）
└─ README.md                 ← 本文件（库 API 文档）
```

**库之间不允许互相画屏或抢总线**：只有 `DisplayLib` 碰屏幕、只有 `HuskyLib` 碰二哈 I2C，
`main.cpp` 里的 `recogTask` 是唯一调用 `HuskyLib` 的任务。

---

## 2. 库 API

### 2.1 HuskyLib —— 二哈 HUSKYLENS 2（内置 CNN）

```cpp
#include "HuskyLib.h"

HuskyLib husky;
husky.begin(PIN_HUSKY_SDA, PIN_HUSKY_SCL);   // 只初始化 I2C，不碰二哈
husky.connect();                             // 探测 + 初始化 + 切算法（会等模型加载）
husky.online();                              // 是否已连接

HuskyFrame frame;
int16_t n = husky.readFrame(frame);          // >=0 读到；-1 通信超时
frame.wordNum, frame.words[0].content        // 本帧识别结果

husky.setModeByAlpha('4');                   // 切算法（'1'~'8'，见模式表）
husky.setModeByIndex(3);
husky.modeName();                            // "OCR 文字识别"
uint8_t id = husky.learn();                  // 学习画面中心目标
husky.forget();
husky.saveKnowledge(0); husky.loadKnowledge(0);
husky.printState(); husky.dumpAlgoParam();
```

| 数据结构 | 关键字段 |
| --- | --- |
| `HuskyWord` | `id` `x` `y` `w` `h` `name[40]` `content[40]` |
| `HuskyFrame` | `online` `modeIndex` `wordNum` `resultNum` `learnedNum` `maxId` `frameCount` `errorCount` `recoverCount` `words[8]` |

**模式表**（`HUSKY_MODE_TABLE`，串口按键 → 算法）：

```
1 物体识别   2 物体分类   3 自学习分类   4 OCR 文字识别(默认)
5 视觉标签   6 条形码     7 二维码       8 人脸识别
```

⚠️ **本库不是线程安全的**（直接操作 I2C），只允许一个任务调用。
⚠️ 设备不在线时底层库会阻塞 25 秒以上，本库所有操作都会先 `probe()`（约 1ms）。
⚠️ 取 ID 只能用 `res->ID`：`res->classID` 与 `res->xCenter` 是同一个 union 槽位。

### 2.2 DisplayLib —— 显示（TFT_eSPI + LVGL 8.4 中文界面）

```cpp
#include "DisplayLib.h"

DisplayLib display;
display.begin();                    // 初始化屏 + LVGL + 建界面 + displayTask(核心0)
display.setFrame(frame);            // 写识别结果（内部加锁，不画屏）
display.setSystemInfo(info);        // 写系统信息
display.countBook(frame.words[0]);  // 累计一本书（进动态数组书单，按 ID/文字去重）
display.bookCount(); display.clearBooks();
display.addBook("测试书 1");         // 直接加一条（串口测试用，不去重）
display.removeBook(0);              // 取出第 1 条（书列表页的“取出”按钮走这里）
display.printBooks();               // 串口打印整张书单
display.showMessage("开机自检完成", 2000);  // 整屏消息，2 秒后自动回正常页（中文直接写）
display.setPage(PAGE_STATUS); display.nextPage();
```

四页：`PAGE_STATUS`(0) / `PAGE_SYSINFO`(1) / `PAGE_BOOKS`(2) / `PAGE_WAVE`(3) / `PAGE_TOUCH`(4)。
刷新策略：**脏标志 + 内容签名**，内容没变就不动控件（不闪屏、省 CPU）。
所有 `lv_*` 调用都只在库内 `displayTask` 里做，最后由刷屏回调把画好的区域推给 TFT_eSPI；
**LVGL 不是线程安全的**，外部只能写数据。

中文：`include/lv_conf.h` 里 `LV_FONT_SIMSUN_16_CJK=1` → 标题/正文/消息用 `simsun_16_cjk`，
页码和页脚提示用 `montserrat_12`。（TFT_eSPI 内置字体只有 ASCII，屏上中文必须走 LVGL。）
draw buffer：2 × `240×40×2B = 19.2KB`，优先 `ps_malloc` 放 PSRAM，失败退回内部 RAM。

### 2.3 SysInfoLib —— 系统信息

```cpp
#include "SysInfoLib.h"

SystemInfo info;
sysInfoCollect(info);                       // 采集
sysInfoPrint(info);                         // 串口打印
char line[24];
sysInfoFormatRam(info, line, sizeof(line));   // "12/320KB (4%)"
sysInfoFormatPsram(info, line, sizeof(line)); // "0.1/8.0MB (1%)" 或 "N/A"
sysInfoFormatFlash(info, line, sizeof(line));
sysInfoFormatUptime(info, line, sizeof(line));// "01:23:45"
sysInfoDiff(before, after, out);              // 求变化量，查内存泄漏
```

不用 `String`，全部写进调用方的 `char` 缓冲；百分比用 `uint64_t` 防溢出。

### 2.4 ButtonLib —— 按键

```cpp
#include "ButtonLib.h"

ButtonLib buttons;                  // 默认用 config.h 里的三个引脚
buttons.begin();                    // 配引脚 + 建事件队列
buttons.startTask();                // 建 buttonTask(核心0, 5ms 一轮)

ButtonEvent event;
if (xQueueReceive(buttons.queue(), &event, pdMS_TO_TICKS(10)) == pdPASS) {
    if (event.pressed) { ... }      // event.flag / event.index / event.ms
}
```

消抖用移位历史：`history <<= 1; history |= 读到有效电平;`，低 4 位全 1 判按下、全 0 判松开。
参数 `-DBUTTON_TRUE_VALUE=1`（按下高电平，用内部下拉）。引脚传 `-1` 表示不接这个键。

### 2.5 SerialCmdLib —— 串口命令行

```cpp
#include "SerialCmdLib.h"

SerialCmdLib cli;
cli.begin();
cli.registerCommand('i', "状态", cmdInfo);   // 静态函数指针
cli.startTask();                              // 建 serialCmdTask(核心0)
cli.printHelp();

static void cmdInfo(const char *arg) { ... }  // arg 是逗号后的参数，无参时是 ""

cli.sendCommand("algo,4");                    // 其它任务也可投递命令
```

行格式：`命令[,参数]`，只按**第一个逗号**切分（`1,2,3` → 命令 `1`，参数 `"2,3"`）。
**只认换行结尾的输入**，串口上的随机噪声不会误触发。

---

## 3. 任务与队列

| 任务 | 谁建的 | 核心 | 职责 |
| --- | --- | --- | --- |
| `recogTask` | main.cpp | 1 | **唯一**调用 HuskyLib：重连、读帧、累计书记录 |
| `monitorTask` | main.cpp | 0 | 每秒采集系统信息 → 共享结构体 + DisplayLib + 串口 |
| `displayTask` | DisplayLib | 0 | 每 5ms 一轮：取快照 → 内容变了才更新控件 → `lv_timer_handler()` |
| `buttonTask` | ButtonLib | 0 | 5ms 扫描消抖，事件进队列 |
| `serialCmdTask` | SerialCmdLib | 0 | 轮询串口 + 执行命令队列 |
| `stepperTask` | main.cpp | 1 | **唯一**碰电机引脚：消费 MotorQueue，推进三台电机的走步状态机 |
| `waveTask` | main.cpp | 0 | **唯一**碰 TRIG/ECHO：发起测量、推进状态机、滤波、写 WaveState |

```
recogTask   --HuskyFrame-->  DisplayLib.setFrame()      (库内加锁)
monitorTask --SystemInfo-->  DisplayLib.setSystemInfo() + infoMutex 共享
buttonTask  --按钮事件队列--> loop() 翻译成命令 --> SerialCmdLib
serialCmdTask --命令队列-->  各命令回调
cmdMotor/m  --MotorQueue-->  stepperTask --service()-->  motor1/2/3
waveTask    --WaveState(waveMutex 保护)-->  DisplayLib.setWave() + 串口
```

`loop()` 里只有按键事件转发 + 20ms 延时，不写业务。
共享的系统信息用 `infoMutex` 保护 + 快照拷贝。

---

## 4. 串口命令

| 命令 | 作用 |
| --- | --- |
| `?` / `h` | 帮助（命令表 + 屏幕页说明） |
| `s` | 重扫 I2C 总线，确认二哈在不在 |
| `i` | 状态：二哈连接/算法/累计统计 + 系统信息 |
| `r` | 重新连接二哈 |
| `n` | 屏幕下一页 |
| `a` / `a,4` | 切换算法（不带参数切下一个；带参数切到对应编号） |
| `l` | 学习当前画面中心目标 |
| `f` | 清空当前算法的学习内容 |
| `w` / `w,1` | 保存知识库（默认槽位 0，可给 0~3） |
| `o` / `o,1` | 读取知识库 |
| `p` | 读算法参数（排查 OCR 读出空内容） |
| `b` / `b,a` / `b,p` | 书列表页：`b` 跳到书列表页，`b,a[,N]` 加 N 条测试书（没二哈也能试），`b,p` 打印整张书单 |
| `c` | 清空书列表记录 |
| `m` / `m,1,300` | 步进电机：`m` 看用法，`m,?` 看状态，`m,1,300` 电机1走300步，`m,s1500` 改速度 |
| `z` / `z,2` | 步进急停：`z` 全停，`z,2` 只停电机2 |
| `u` / `u,s` / `u,e` / `u,c` | 超声波：`u` 看状态，`u,s` 记测厚起点，`u,e` 算厚度，`u,c` 清统计 |
| `t` | 跳到**触摸测试页**（第 5 页），页上有一个大按钮做点击计数 |
| `d` / `d,1` / `d,2` / `d,3` | 显示诊断：`d` 看状态（含触摸坐标/原始值），`d,1` 画彩条，`d,2` SPI 测速，`d,3` 触摸原始值采样 |

> ⚠️ **超声波的命令字母从 `w` 改成了 `u`**：原来 `w` 被注册了两次（保存知识库 + 超声波测距），
> 而命令表是“第一个注册的生效”，所以**超声波那几条命令以前根本进不来**，
> A/B 两个按键按下实际执行的是“保存知识库”。现在 `SerialCmdLib` 会拒绝重复注册并在串口报警。

按键（可选）：按键1 = 下一页，按键2 = 重新连接，按键3 = 学习当前目标；A 键 = 记测厚起点（u,s），B 键 = 算书本厚度（u,e）。
（说明书里的 A/B 是取书/还书；机械没接之前先借 A/B 做测厚，接上机械后把 sendCommand 换成取还书流程即可。）
A/B 的引脚在 `config.h` 的 `BUTTON_PIN4`/`BUTTON_PIN5`，默认 -1（不接）。

---

## 5. 引脚（唯一来源：`include/config.h` 第 3~9 节）

| 外设 | 引脚 | 备注 |
| --- | --- | --- |
| ILI9341 | SCLK=12 MOSI=11 CS=10 DC=9 RST=8 BL=47 | TFT_eSPI 参数在 `platformio.ini` |
| 二哈 HUSKYLENS 2 | SDA=1 SCL=2 | I2C 地址 0x50，超时 1000ms |
| 按键 1/2/3 | 45 / 46 / 0 | 按下高电平，可选（**已从 40/41/42 挪走**） |
| 按键 A/B（说明书取书/还书） | -1（未接线） | `BUTTON_PIN4`/`BUTTON_PIN5`，接上后可用作取书/还书 |
| 步进电机1（推出小车） | 4, 5, 6, 7 | ULN2003 的 IN1~IN4；与 HC-SR04 冲突 |
| 步进电机2（中间夹层） | 15, 16, 17, 18 | ULN2003 的 IN1~IN4 |
| 步进电机3（升降平台） | 38, 39, 48, 21 | 机械到位后再接（默认关） |
| HC-SR04 | TRIG=4 ECHO=5 | ECHO 是 5V，必须分压 |

**不能接外设**：GPIO26-32（封装内 Flash）、GPIO33-37（OPI PSRAM）、GPIO22-25（不存在）、
GPIO19/20（USB）、GPIO43/44（UART0）。

⚠️ **为什么按键从 40/41/42 挪到 45/46/0**：40/41/42 同时是 OV2640 的 D5/D6/D7。
改完以后 40/41/42 空出来给摄像头，步进电机1 用的 4/5/6/7 也不和按键打架。

⚠️ **按键 3 = GPIO0 是 BOOT 脚**：芯片复位时若被按住会进下载模式（正常上电后再按没问题）。
DevKitC 上 GPIO0 有外部上拉，如果觉得别扭，建议改成按下低电平：
`-DBUTTON_PIN3=45 -DBUTTON_TRUE_VALUE=0`，或直接换到空闲脚。

⚠️ **HC-SR04 与步进电机1 抢 GPIO4/5**：同一时间只能接一个。
先做测距就把 `CFG_ENABLE_STEPPER` 置 0；先做电机就把 ECHO 挪到空闲脚
（如 `-DPIN_WAVE_TRIG=39 -DPIN_WAVE_ECHO=48`）。

⚠️ **冲突提醒**：`PIN_HUSKY_SDA=1` 与 `Master/include/board_pins.h` 里 OV2640 的
SCCB SDA（也是 GPIO1）冲突。要“本地摄像头 + 二哈”同时用，必须把其中一个挪脚。

---

## 6. 本次改动点 / 注意事项

1. **板子从 `esp32-s3-devkitm-1` 改成 `esp32-s3-devkitc-1` + `qio_opi` + `default_16MB.csv`**：
   原模板是 4MB 无 PSRAM 的 M-1，不改会在二级 bootloader 卡死、屏不亮。
2. **`lib_ldf_mode = deep`**：让 PlatformIO 正确解析 `lib/*/src` 下的库依赖图。
3. **二哈驱动每个工程各存一份**（`Main/lib/DFRobot_HuskylensV2` 与 `CNN_Test/lib/...`），
   升级驱动要改两处；想只存一份可以给 Main 加 `lib_extra_dirs`。
4. **`-DTOUCH_CS=-1`**：本工程不用触摸，显式定义可以消掉 TFT_eSPI 的告警。
5. **显示任务不占锁画屏**：`update()` 加锁只取快照，更新控件在锁外做，避免拖住采集任务。
6. **书列表去重**：`countBook()` 按 `id`（非 0 时）或文字内容去重，
   `recogTask` 里还额外判断“内容是否变化”，避免同一本书每帧都被计一次。
7. **显示层已改成 LVGL 8.4 + 中文界面**：`DisplayLib.h/.cpp` 重写为 LVGL 版，
   公开 API（`begin` / `setFrame` / `setSystemInfo` / `countBook` / `clearBooks` / `bookCount` /
   `showMessage` / `setPage` / `nextPage` / `page` / `printHelp`）与原来完全一致，
   所以 `main.cpp` 一行都没改。`platformio.ini` 增加 `lvgl/lvgl@8.4.0` 与 `-DLV_CONF_INCLUDE_SIMPLE`；
   `include/lv_conf.h` 由“备着没用”变成真正生效（`LV_FONT_SIMSUN_16_CJK=1`）。
8. **编译结果（接入 LVGL 后）**：`RAM 7.7% (25296/327680)`，`Flash 10.4% (679681/6553600)`；
   增量构建 7.93 s，`EXITCODE=0`，5 个库 + `main.cpp` 零告警。
   接入前（纯 TFT_eSPI）是 `Flash 5.2% (337537/6553600)`，LVGL + 中文字体约多占 334KB Flash。
9. 还没做的：机械控制（3 台 42 步进 + ESP32-C3）、语音、组网。
   本工程目前只有识别 + 显示 + 系统信息 + 按键 + 串口命令这五块。

---

## 7. 配置改动（config.h 集中化）

1. **新增 `include/config.h`**：13 节集中配置 —— 模块开关 / 串口 / 显示 / 二哈 /
   按键 / 串口命令 / 任务节奏与核心分配 / 超声波 / 步进与机械尺寸 / ESP32-C3 链路 /
   日志宏 / 库版本。每个宏都用 `#ifndef` 包住，所以 `platformio.ini` 里仍可用
   `-DCFG_xxx=yyy` 临时覆盖。
2. **`BoardConfig.h` 变成兼容壳**：只剩一行 `#include "config.h"`。
   宏名一个都没改，5 个库的 include 不用动；新代码请直接 `#include "config.h"`。
3. **`platformio.ini` 只留“必须在编译命令行里给”的宏**：`BOARD_HAS_PSRAM`、`-Iinclude`、
   以及 TFT_eSPI 的 USER_SETUP 宏（它必须在编译 TFT_eSPI 库本身时就存在）。
   原来与 config.h 重复的 `-DPIN_HUSKY_*` / `-DBUTTON_PIN*` / `-D*_QUEUE_SIZE` 已删除。
4. **模块开关是“真开关”**：`CFG_ENABLE_DISPLAY=0` 时显示库整体被编译掉，
   实测 Flash 从 10.4% 降到 4.9%、RAM 从 7.7% 降到 7.2%，链接无未定义符号。
5. **屏上缓冲行数统一**：`DisplayLib.cpp` 原来的 `DISPLAY_DRAW_LINES` 并入
   `config.h` 的 `CFG_LV_BUF_LINES`，一个参数管一处。
6. **`nextPage()` 加了锁**：原来它在锁外先读 `_page` 再调 `setPage()`，
   两个任务同时翻页会漏页；现在读改写都在同一把锁里。
7. **包含顺序保护**：`SCR_W/SCR_H` 移到 `DisplayLib.h`（紧跟 `<TFT_eSPI.h>` 之后），
   config.h 里加了一条 `_TFT_eSPIH_` 体检，TFT_eSPI 的 USER_SETUP 没生效时
   会在编译期直接报清楚的中文错误。

### ⚠️ 需要你确认的两处硬件取舍

1. **按键 3 = GPIO0 是 BOOT 脚**。这样接能用，但复位时按住会进下载模式。
   不想用 BOOT 脚就改成 `-DBUTTON_PIN3=48`（或 21/38/39，注意别和电机3 冲突）；
   GPIO0 在 DevKitC 上有外部上拉，也可以改成按下低电平 `-DBUTTON_TRUE_VALUE=0`。
2. **HC-SR04 的 TRIG=4/ECHO=5 与步进电机1 的 4/5/6/7 冲突**。
   两个外设不能同时接。代码里已经会检测到并打印警告；
   要用测距就把 `CFG_ENABLE_STEPPER` 置 0，要用电机就把 ECHO 挪到空闲脚
   （如 `-DPIN_WAVE_TRIG=39 -DPIN_WAVE_ECHO=48`）。

---

## 8. 步进（StepperLib）与测距（UltraLib）

### 8.1 StepperLib —— 28BYJ-48 + ULN2003（宏可切 42 步进）

- 非阻塞：`run()` 只登记目标，真正的脉冲在 `service()` 里按时间线发，
  由 `stepperTask` 每 `STEPPER_SERVICE_INTERVAL_MS`（默认 2ms）调一次。
  写固件时**不要** `while (busy());`，那会把整核占满。
- 一次只动一台：`runMotor()` 会先把其它两台停掉，避免三台同时走步抢电流。
- 软限位：`_posSteps` 是相对上电位置的步数（没有限位开关，上电即零点）。
  撞到 `xxx_RANGE_STEPS` 会立刻停并丢弃剩余步数，不硬顶机构。
- 停机自动断电：走完或 `stop()` 都会 `release()` 把四相拉低——
  28BYJ 停机不断电会一直发热，这是必须的。
- 保护：`CFG_STEPPER_MAX_STEPS` 之外的单次步数直接拒绝（防止参数多写一个 0）。
- 换 42 步进：`CFG_STEPPER_DRIVER=1`，此时每台只用 3 个脚
  （`pins[0]=STEP` `pins[1]=DIR` `pins[2]=EN`），细分/导程见 `CFG_STEPPER42_xxx`。

> ⚠️ **供电**：28BYJ-48 每相约 200mA、ULN2003 同时两相通电，峰值可到 300mA 以上。
> 不要从 ESP32-S3 的 3V3 取电，也不要和 USB 口共用一根细线；
> 用独立 5V 并**与主控共地**，否则会出现“一走电机就复位/串口掉线”。

### 8.2 UltraLib —— HC-SR04（非阻塞状态机）

- 从 `Wave_Test` 的 `HCSR04` 移植过来，接口没变，只把参数改走 `config.h`，
  并修掉 `startMeasure()` 里 `_ready` 没清零的问题。
- 全程不用 `pulseIn()`：TRIG 拉高 10us，然后状态机等 ECHO 上升沿/下降沿，
  每个等待阶段都有超时，模块没接也不会把任务卡死。
- 错误码：`1` = ECHO 一直没拉高（查 TRIG / 模块 5V），
  `2` = ECHO 一直没拉低（查分压 / 是否超量程）。
- 量程过滤 `CFG_WAVE_MIN_MM`~`CFG_WAVE_MAX_MM` + 滑动平均 `CFG_WAVE_FILTER_N`。
- 测书厚：`w,s` 记起点、`w,e` 算厚度（起点距离 - 当前距离），
  也可以用 A/B 两个按键。厚度判定阈值在 `CFG_THICK_TOLERANCE_MM`。

> ⚠️ **ECHO 是 5V 输出，必须分压（1k + 2k）后再接 GPIO**，否则可能损坏 S3。
> ⚠️ 超时按最远 4000mm 留了余量：往返约 23.3ms，`CFG_WAVE_TIMEOUT_US=38000`。

### 8.3 中文字库（重要，踩过坑）

**问题**：LVGL 自带的 `lv_font_simsun_16_cjk` 不是全字库，是用 `--symbols` 指定的固定子集，
**只有 1097 个汉字**。本项目界面用到的 326 个字里 **135 个不在里面**，
其中 35 个是屏幕可见的（识 / 别 / 书 / 测 / 距 / 态 / 记 / 录 …），所以屏上大片中文空白。

**解决**：自己生成字库 `src/ui_font_16.c`
（SimSun 16px / 2bpp / RLE / GB2312 一级汉字 3755 + 界面用字），
开关与说明见 `fontgen/README.md`。改界面文字后跑：

```powershell
python tools\font_charset_check.py     # 查缺字，0 = 没有缺字
```

**两个必须同时满足的开关**（少一个中文字都是空白）：

| 位置 | 值 | 不设的后果 |
| --- | --- | --- |
| `include/lv_conf.h` | `#define UI_FONT_16 1` | 生成文件用 `#if UI_FONT_16` 包住数据，不定义则整份字库被预处理掉，链接报 undefined |
| `include/lv_conf.h` | `#define LV_USE_FONT_COMPRESSED 1` | 字库是 RLE 压缩的，不开解压所有**汉字**都画不出来（LVGL 只打一句 warning） |

> ⚠️ **别把“整屏没内容”归到字库头上**：字库只影响“文字”这一种元素。
> 标题栏的蓝色底色、页面白底、进度条、消息浮层这些都不经过字体，
> 缺字最多让某几个汉字变空白，不可能让整屏什么都没有。
> 整屏全黑/全白请按第 9 节的分层排错查，不要反复折腾字库。

另外 `platformio.ini` 必须有 `-DLV_LVGL_H_INCLUDE_SIMPLE`，
生成的 `.c` 只在定义它时才 `#include "lvgl.h"`。

开机 `display.begin()` 会跑 `fontSelfCheck()`：串口打印字库行高、是否压缩、
缺字清单。看到 `自检通过: N 组词全部有字形` 就说明中文字库可用。

### 8.4 本步验证方式

本机没有 x86 编译器，所以纯逻辑用等价的 Python 仿真验证（脚本可复现）：

```powershell
python tools\stepper_sim.py    # 33 项：拍序/方向/步数/位置/软限位/参数校验/命令解析
python tools\wave_sim.py       # 16 项：换算精度/量程边界/超时余量/状态机三种结局
```

另外 `Main/test/stepper_host_test/` 里有一份 C++ 版单元测试（带 Arduino 桩），
在装了 g++ 的机器上可以直接编译运行；本机已用 xtensa 的 g++ 做过
`-Wall -Wextra -fsyntax-only` 语法检查。

---

## 9. 屏幕没内容怎么查（分层排错）

**核心原则：一次只验证一层，先把“硬件这条链”证明掉，再怀疑软件。**

显示这条链一共四层，任何一层断了都是“屏上什么都没有”，所以不能靠猜：

```
① 供电/背光/接线/SPI 引脚/屏驱动型号      <- TFT_eSPI 直接画彩条就能证明
② LVGL 刷屏链路(tick / draw buffer / flush_cb / 字节序)
③ 界面控件(页面/标题栏/底色)              <- 这些不经过字体
④ 字体(汉字字形/压缩解压)                 <- 只影响“文字”，不会让整屏空白
```

### 9.1 不用重新烧录：串口命令 `d`

```
d        打印显示诊断
d,1      立刻用 TFT_eSPI 画一次彩条（约 1.5 秒后自动回到界面）
d,2      立刻测一次 SPI 总线速度（看 MCU 到底有没有在往屏上吐像素）
```

> 📌 **真实踩过的坑（先看这条）**：有一次现象是“背光亮着、屏上全白没内容”，
> 代码没改，**重新烧录一遍就好了**。所以黑屏排查的顺序应该是：
> ① 先确认上次烧录是完整的（`pio run -t upload` 有没有报错、有没有自动复位），
> 必要时断电重新上电一次；② 再看串口日志是不是完整的启动日志；
> ③ 然后才用下面的分层手段定位。

`d` 会打印这些数（排错就看它们）：

```
显示库: 清屏(填满 240x320) 耗时 41230us = 0.53us/像素 (SPI 在发数据)
显示库: LVGL 时钟 1234 -> 1254 (正常)
刷屏回调: 1234 次, 最近一次 3120 us     <- LVGL 到底有没有在刷屏
SPI 总线(开机测): 41230us / 76800 像素 = 0.53us/像素
LVGL 时钟: 5678 ms (millis=5678)        <- LVGL 的心跳有没有走
显示任务: 已创建, 栈余量 3216 字节
刷屏缓冲: 40 行 x2, 位置=PSRAM
背光: GPIO47, 方式=digitalWrite
字节交换 CFG_LV_PUSH_SWAP=1 (配 lv_conf.h 的 LV_COLOR_16_SWAP=0)
中文字库: ui_font_16(自建全字库)
排错模式: CFG_DISPLAY_BRINGUP=0, 开机彩条=0ms
```

判读：

| 现象 | 结论 |
| --- | --- |
| `d,1` 能看到彩条 | ① 这层是好的：接线/背光/SPI/驱动型号都没问题 |
| `d,1` 也看不到彩条 | 和 LVGL、字库**无关**，去查背光/接线/供电/屏型号（先烧一遍 `TFT_Test` 对比） |
| 刷屏次数一直是 0 | ② 断了：LVGL 没在刷（看开机那行 `LVGL 时钟 … 正常/❌没走`） |
| 刷屏次数一直在涨但屏黑 | 数据已经发出去了：背光没亮、或屏/接线的问题 |
| `d,2` 显示 `< 0.1us/像素` | SPI 根本没在发数据（查 `platformio.ini` 的 USER_SETUP / `USE_FSPI_PORT` / 引脚宏） |
| `d,2` 显示 `0.5~3us/像素` | SPI 在正常吐数据，问题在屏那一侧（背光/接线/屏本体） |

### 9.2 需要重新烧录时的三个环境

```powershell
pio run -e screen-raw  -t upload   # ① 只画彩条，完全不初始化 LVGL
pio run -e screen-lvgl -t upload   # ② 彩条 + LVGL 纯色块（不碰任何字体）+ 刷屏日志
pio run -e screen-log  -t upload   # ③ 正常界面 + 刷屏日志 + LVGL 日志
```

`screen-raw` 有彩条、`screen-lvgl` 纯色块也有 → 问题在字体层，才轮到去查字库；
`screen-raw` 有彩条、纯色块没有 → 问题在 LVGL 刷屏层；
两个都没有 → 和软件无关，先解决硬件。

### 9.3 相关的 config.h 开关（第 3 / 3.1 节）

| 宏 | 默认 | 作用 |
| --- | --- | --- |
| `CFG_DISPLAY_BRINGUP` | 0 | 0 正常 / 1 只画彩条 / 2 彩条+纯色块 / 3 彩条+完整界面 |
| `CFG_TFT_SPLASH_MS` | 0 | 开机先画一次彩条并停留 N ms |
| `CFG_SELFTEST_BAR_HOLD_MS` | 1500 | `d,1` 彩条停留时间 |
| `CFG_DISPLAY_FLUSH_LOG_MS` | 0 | 每 N ms 打一行刷屏心跳（0 = 关） |
| `CFG_LV_LOG_TO_SERIAL` | 0 | 把 LVGL 自己的告警打到串口 |
| `CFG_TFT_BL_USE_LEDC` | 0 | 背光改 LEDC(PWM)，才能用 `CFG_TFT_BL_LEVEL` 调亮度 |
| `CFG_LV_BUF_IN_PSRAM` | 1 | 刷屏缓冲放 PSRAM；改 0 用内部 RAM（更快更稳） |
| `CFG_LV_PUSH_SWAP` | 1 | 刷屏字节序，必须和 `lv_conf.h` 的 `LV_COLOR_16_SWAP` 配对 |

### 9.4 顺带加上的两处保护

1. **SPI 总线锁（`s_busMutex`）**：刷屏回调和“别的任务直接写屏”共用一个锁。
   现在只有显示任务写屏，锁是零成本保险；以后接摄像头预览/别的任务画屏时，
   没有这把锁就是花屏。
2. **`SerialCmdLib` 拒绝重复注册命令**：以前 `w` 被注册两次（保存知识库 + 超声波），
   命令表只认第一个，导致超声波命令和 A/B 按键实际走的是“保存知识库”。
   现在重复注册会在串口报警，超声波命令也改成了 `u`。

---

## 10. 触摸（XPT2046）

### 10.1 为什么以前“完全没反应”

不是接线坏，是**代码里压根没有这一路**：

| 缺的东西 | 说明 |
| --- | --- |
| 没有触摸驱动 | 本工程触摸**不走屏的 SPI**，用的是触摸自己的 T_CLK/T_DIN/T_DO 三根线；原来一个驱动都没接 |
| 没有引脚定义 | `Main` 里从来没有 T_CS/T_CLK/T_DIN/T_DO 的宏（`MacTryESP32S3N16R8` 那个工程是 ST7735 屏，也没有触摸） |
| **没有 LVGL 输入设备** | `DisplayLib` 里没有 `lv_indev_drv_register`，即使读到坐标，按钮也收不到点击事件 |

> 顺带说明：`platformio.ini` 里的 `-DTFT_MISO=-1`、`-DTOUCH_CS=-1` **和触摸能不能用没有关系**。
> 触摸这根总线是独立的（软件位操作），屏的 MISO 本来就不需要接。

### 10.2 接线（与 `LVGLin240320TFTLCD` / `esp32mouse` 参考工程完全一致）

| 触摸模块脚 | ESP32-S3 | 说明 |
| --- | --- | --- |
| T_CS | GPIO38 | 片选 |
| T_IRQ | GPIO39 | 中断，**可不接**（本工程用轮询） |
| T_CLK | GPIO40 | 时钟 |
| T_DIN | GPIO41 | 主机 → 触摸 |
| T_DO | GPIO42 | 触摸 → 主机（相当于 MISO） |

⚠️ **GPIO38/39 同时是步进电机3 的引脚**（`CFG_STEPPER_M3_PINS`）。电机3 默认关着，
两者只能二选一；同时打开会在**编译期直接报错**，不会烧进去才发现。

### 10.3 软件链路

```
TouchLib(TouchXPT2046, 位操作读原始值)
   -> DisplayLib 的 touchReadCb（LVGL 输入设备回调，20ms 一次）
        -> SWAP/INVERT + 满量程映射 -> 屏幕坐标
           -> LVGL 按钮 / 事件回调 onBtnTestClicked
```

- 触摸读的是自己的 5 根脚，**不占屏的 SPI**，所以和刷屏没有总线冲突，也不用加锁；
  两者都在 `displayTask` 里跑（输入设备回调由 `lv_timer_handler()` 调起）。
- 原始值窗口 `CFG_TOUCH_RAW_MIN..MAX`（默认 50~4045）之外的读数当成“没按”。

### 10.4 怎么测（有专门一页）

```powershell
# 烧正常固件，然后串口敲：
t        # 跳到触摸测试页（第 5 页），页上有一个大按钮
d        # 看触摸状态：是否按下、屏幕坐标、原始值、按下次数
d,3      # 触摸原始值采样：第一次开始，沿四角按一遍，再敲一次看结果
```

- 按钮点一下：屏上计数 +1，串口打印
  `触摸: 测试按钮第 N 次按下 (屏幕 x,y | 原始 rx,ry)`；另一个按钮清零。
- 页上还会实时显示“触摸: 按下中 / 松开 + 坐标 + 原始值”，**手指动的时候数字应该在变**。

### 10.5 灵敏度（触发阈值）与轴方向

**灵敏度 = 触发阈值 = `CFG_TOUCH_PRESSURE_MIN`**（config.h 第 3.2 节）。

XPT2046 除了 X/Y 坐标，还能读出一个压力值 **Z**（Z1/Z2 差分，按得越重越大）。
判断“算不算按下”要同时满足两条：

```
1) 坐标落在 CFG_TOUCH_RAW_MIN ~ CFG_TOUCH_RAW_MAX 之间   （像不像有效触摸）
2) 压力 Z >= CFG_TOUCH_PRESSURE_MIN                      （按得够不够重 ← 灵敏度）
```

| 想要的效果 | 怎么改 |
| --- | --- |
| 太灵敏（轻碰/自己乱跳就触发） | **调大**阈值：600 → 900 → 1200 |
| 太迟钝（要使劲按才有反应） | **调小**阈值：600 → 400 → 250 |
| 回到“一碰就触发”（不判压力） | 设 `0` |

**怎么定这个数**：按住屏幕敲 `d,6`（或 `d`），看串口里的 `Z=`：

```
  d,6 输出示例（手指按住屏幕时）
  1: 原始 1782, 2440   压力 Z=1180   T_IRQ=LOW  <- 够重，会算按下
  2: 原始 1780, 2442   压力 Z=1210   T_IRQ=LOW  <- 够重，会算按下
```
- 记下「轻轻碰一下」的最大 Z 和「正常按一下」的稳定 Z，把阈值设在两者之间。
- 默认 600 是 TFT_eSPI 的经验值（XPT2046 常见 Z 范围大约 200~2000）。

轴方向不对时（都在 config.h 第 3.2 节，改完重烧即可）：

| 现象 | 改哪个宏 |
| --- | --- |
| 手指上下动、屏幕上光标左右跑（轴对调） | `CFG_TOUCH_SWAP_XY` 0 ↔ 1 |
| 左右反了 | `CFG_TOUCH_INVERT_X` 0 ↔ 1 |
| 上下反了 | `CFG_TOUCH_INVERT_Y` 0 ↔ 1 |
| 边缘按不到 / 中间就判“没按” | 放宽 `CFG_TOUCH_RAW_MIN` / `CFG_TOUCH_RAW_MAX` |
| 坐标抖 | 加大 `CFG_TOUCH_SAMPLES`（默认 3） |

默认值 `SWAP_XY=0 / INVERT_X=1 / INVERT_Y=0` 是从参考工程照抄的、已在那块屏上验证过的一组。

### 10.6 底部触摸屏蔽与滑动限制

| 宏 | 默认 | 作用 |
| --- | --- | --- |
| `CFG_TOUCH_BOTTOM_MASK_PX` | 20 | 屏幕最下方这么多像素**不响应触摸**（防止手指划到边缘误触）；0 = 不屏蔽 |
| `CFG_TOUCH_SCROLL_LIMIT` | 0 | LVGL 的“手指要先移动多少像素才算滑动”。0 = 解除限制，一动就滑 |

> ⚠️ `CFG_TOUCH_BOTTOM_MASK_PX` 会把 `y >= SCR_H - 20 = 300` 的触摸判成“没按”。
> 书列表页的滚动容器高度已经主动避开这一段（见 `createBookUi()`），所以按钮不会被挡住。
> `CFG_TOUCH_SCROLL_LIMIT = 0` 手感最跟手，但按钮容错变小；如果发现点按钮常常变成滚动，
> 把它设成 10 ~ 20。

---

## 11. 书单（动态数组）与书列表页

### 11.1 动态数组在哪儿

`lib/BookListLib`（`BookList.h/.cpp`）——**不用 STL**，就是 `malloc` + `realloc` 翻倍：

```cpp
struct BookItem { int16_t id; uint16_t count; char text[HUSKY_TEXT_LEN]; };

BookList books;
books.begin();                       // 建锁 + 申请 CFG_BOOKLIST_INIT_CAP 条
books.addOrCount(id, text);          // 有同 ID/同名就计数 +1，否则追加（不够就扩容）
books.removeAt(i);                   // 删第 i 条，后面的往前挪
books.snapshot(buf, maxNum);         // 加锁拷快照给显示任务，画屏时不占锁
books.version();                     // 每次增删清空 +1，显示库靠它决定要不要重建行控件
```

| 宏 | 默认 | 作用 |
| --- | --- | --- |
| `CFG_BOOKLIST_INIT_CAP` | 4 | 初始容量，装不下自动翻倍 |
| `CFG_BOOKLIST_MAX` | 64 | 容量上限（防乱码把堆吃光）；0 = 不限 |
| `CFG_BOOKLIST_SNAP_MAX` | 24 | 书列表页一次最多显示几行 |
| `CFG_BOOK_ROW_H` | 34 | 每行高度(px) |

线程安全：识别任务 `addOrCount()`、显示任务 `snapshot()`/`removeAt()`，
每条接口内部都拿 `_mutex`；显示任务拷完快照立刻放锁，再慢慢画。

### 11.2 书列表页（第 3 页）

```
┌──────────────────────────────────────┐
│ 共 8 本（上下滑动看全部，点“取出”移出）│  ← 标题行（跟着列表一起滚）
├──────────────────────────────────────┤
│ 1. 测试书 1                    x1 [取出] │
│ 2. 测试书 2                    x1 [取出] │
│ …                                      │  ← 竖排、可上下滑动（带惯性）
└──────────────────────────────────────┘
```

- 每行左边是“序号. 书名 x次数”，右边是**取出**按钮（红色）。
- 点“取出” → `DisplayLib::removeBook(i)` → 从动态数组里删掉 + 串口打日志 + 重建列表。
- 行控件自己**禁止滚动**（`lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE)`），
  这样手指拖动才会交给父容器滚动 —— 这点不设的话列表压不动。
- ⚠️ 事件回调里**只改数据、不删控件**：删掉正在处理事件的按钮会崩。
  行控件统一交给 `update()` 里的 `rebuildBookRows()` 重建。

### 11.3 机械接上以后往哪写

`DisplayLib::removeBook()` 里留了入口：

```cpp
// TODO(机械接上以后): 这里就是"取书流程"的入口——
//   把这条的书号/位置交给 stepperTask（M1 推出小车 / M2 中间夹层 / M3 升降平台）。
```

### 11.4 没有二哈也能测

二哈不在线时用串口造数据：

```
b,a,8     加 8 条测试书（够多，能试滑动）
b         跳到书列表页
b,p       串口打印整张书单
c         清空书单
```

