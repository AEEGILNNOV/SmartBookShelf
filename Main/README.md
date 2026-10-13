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
│  ├─ BoardConfig.h          ← 全工程唯一引脚/参数来源（每个宏都可 -D 覆盖）
│  └─ lv_conf.h              ← LVGL 8.4 配置（LV_FONT_SIMSUN_16_CJK=1，屏上中文靠它）
├─ lib/
│  ├─ HuskyLib/              ← 二哈 I2C + 内置 CNN 识别（含 library.json）
│  ├─ DisplayLib/            ← ILI9341 + LVGL 8.4 中文界面（状态页/系统信息页/书列表页/消息页）
│  ├─ SysInfoLib/            ← RAM/PSRAM/Flash/运行时间采集与格式化
│  ├─ ButtonLib/             ← 按键消抖 + 事件队列
│  ├─ SerialCmdLib/          ← 串口命令行解析 + 注册式分派
│  └─ DFRobot_HuskylensV2/   ← 二哈底层驱动（vendor，离线可编译）
├─ src/main.cpp              ← 只做拼装：全局对象 / 命令注册 / 任务
├─ platformio.ini            ← N16R8 + TFT_eSPI + LVGL + 二哈 + 按键参数
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

三页：`PAGE_STATUS`(0) / `PAGE_SYSINFO`(1) / `PAGE_BOOKS`(2)。
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

ButtonLib buttons;                  // 默认用 BoardConfig.h 里的三个引脚
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

```
recogTask   --HuskyFrame-->  DisplayLib.setFrame()      (库内加锁)
monitorTask --SystemInfo-->  DisplayLib.setSystemInfo() + infoMutex 共享
buttonTask  --按钮事件队列--> loop() 翻译成命令 --> SerialCmdLib
serialCmdTask --命令队列-->  各命令回调
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

按键（可选）：按键1 = 下一页，按键2 = 重新连接，按键3 = 学习当前目标。

---

## 5. 引脚（唯一来源：`include/BoardConfig.h`）

| 外设 | 引脚 | 备注 |
| --- | --- | --- |
| ILI9341 | SCLK=12 MOSI=11 CS=10 DC=9 RST=8 BL=47 | TFT_eSPI 参数在 `platformio.ini` |
| 二哈 HUSKYLENS 2 | SDA=1 SCL=2 | I2C 地址 0x50，超时 1000ms |
| 按键 | 42 / 41 / 40 | 按下高电平，可选 |

**不能接外设**：GPIO26-32（封装内 Flash）、GPIO33-37（OPI PSRAM）、GPIO22-25（不存在）、
GPIO19/20（USB）、GPIO43/44（UART0）。

⚠️ **冲突提醒一**：按键 GPIO40/41/42 同时是 OV2640 的 D5/D6/D7。本工程不接 OV2640；
若线上同时接着摄像头，三个按键不要接（或用 `-DBUTTON_PIN1=45` 之类挪脚）。

⚠️ **冲突提醒二**：`PIN_HUSKY_SDA=1` 与 `Master/include/board_pins.h` 里 OV2640 的
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