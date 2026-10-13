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
│  └─ DFRobot_HuskylensV2/   ← 二哈底层驱动（vendor，离线可编译）
├─ src/main.cpp              ← 只做拼装：全局对象 / 命令注册 / 任务
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
display.countBook(frame.words[0]);  // 累计一本书（按 ID/文字去重）
display.bookCount(); display.clearBooks();
display.showMessage("开机自检完成", 2000);  // 整屏消息，2 秒后自动回正常页（中文直接写）
display.setPage(PAGE_STATUS); display.nextPage();
```

四页：`PAGE_STATUS`(0) / `PAGE_SYSINFO`(1) / `PAGE_BOOKS`(2) / `PAGE_WAVE`(3)。
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
| `b` | 跳到书列表页并打印数量 |
| `c` | 清空书列表记录 |
| `m` / `m,1,300` | 步进电机：`m` 看用法，`m,?` 看状态，`m,1,300` 电机1走300步，`m,s1500` 改速度 |
| `z` / `z,2` | 步进急停：`z` 全停，`z,2` 只停电机2 |
| `w` / `w,s` / `w,e` / `w,c` | 超声波：`w` 看状态，`w,s` 记测厚起点，`w,e` 算厚度，`w,c` 清统计 |

按键（可选）：按键1 = 下一页，按键2 = 重新连接，按键3 = 学习当前目标；A 键 = 记测厚起点（w,s），B 键 = 算书本厚度（w,e）。
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

### 8.3 本步验证方式

本机没有 x86 编译器，所以纯逻辑用等价的 Python 仿真验证（脚本可复现）：

```powershell
python tools\stepper_sim.py    # 33 项：拍序/方向/步数/位置/软限位/参数校验/命令解析
python tools\wave_sim.py       # 16 项：换算精度/量程边界/超时余量/状态机三种结局
```

另外 `Main/test/stepper_host_test/` 里有一份 C++ 版单元测试（带 Arduino 桩），
在装了 g++ 的机器上可以直接编译运行；本机已用 xtensa 的 g++ 做过
`-Wall -Wextra -fsyntax-only` 语法检查。