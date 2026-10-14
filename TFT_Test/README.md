# TFT_Test — ILI9341 240x320 单屏自检

ESP32-S3 N16R8 + 2.8 寸 SPI ILI9341 的**独立屏幕自检程序**，不依赖 LVGL、不依赖摄像头。
接线和 TFT_eSPI 配置沿用 `LVGLin240320TFTLCD` 和 `Master/include/board_pins.h`（同一套已验证过的参数）。

---

## 1. 接线（ESP32-S3 N16R8）

⚠️ 本模块可用 GPIO 只有 **0-21 和 38-48**。
**GPIO 26-32 是封装内 flash、GPIO 33-37 是封装内 OPI PSRAM，绝对不能接任何东西。**

| 屏引脚 | ESP32-S3 | 说明 |
| --- | --- | --- |
| VCC | 3V3 | 不要接 5V |
| GND | GND | |
| SCK / CLK | GPIO12 | |
| SDA / MOSI | GPIO11 | |
| MISO | 不接 | 本方案不读屏，读 ID 返回 0 属正常 |
| CS | GPIO10 | |
| DC / RS | GPIO9 | |
| RESET | GPIO8 | |
| LED / BL | GPIO47 | 由代码控制，排错时不要直接接 VCC |

驱动参数：`ILI9341_DRIVER`，240x320，`TFT_ROTATION=2`（竖屏），SPI 27 MHz，`USE_FSPI_PORT`。
全部定义在 `platformio.ini` 的 `build_flags` 里。

按键是**可选**的，不接也能用串口切页：

| 按键 | ESP32-S3 | 作用 |
| --- | --- | --- |
| BTN1 | GPIO42 | 上一个测试项 |
| BTN2 | GPIO41 | 下一个测试项 |
| BTN3 | GPIO40 | 重跑当前测试项 |

按键为**按下高电平**（`INPUT_PULLDOWN`），不接时引脚被拉低，不会误触发。

---

## 2. 编译 / 烧录

```bash
cd D:\Project\SmartBookShelf\TFT_Test
pio run -t upload
pio device monitor        # 115200
```

`pio` 不在 PATH 里时：

```bash
"C:\Users\GX\.platformio\penv\Scripts\pio.exe" run -t upload
```

---

## 3. 上电会看到什么

串口（关键行）：

```
=====================================
 SmartBookShelf / TFT_Test
=====================================
chip       : ESP32-S3  cores=2  freq=240 MHz
flash      : 16777216 bytes (16 MB)
psram found: YES  size=8386215 bytes
tft pins   : MOSI=11 SCLK=12 CS=10 DC=9 RST=8 BL=47
buttons    : BTN1=42 BTN2=41 BTN3=40 (按下为高电平)
[1] backlight blink
[2] tft.init()
[3] tft.setRotation(TFT_ROTATION)
TFT_eSPI: width=240 height=320 rotation=2
[4] readcommand8
readcommand8(RDMADCTL)=0 (0 when MISO is not wired)
[5] fillScreen timing
fillScreen(240x320) took 12000 us
[6] sprite create
sprite: 240x320 16bpp in PSRAM
[7] 系统信息...
ready: n/p/r or 1-9 over serial, or the three buttons
```

屏上：**背光先闪 2 下**（能看到闪就说明 MCU 真的在跑）→ 进入第 1 页 8 条彩带 + 颜色名。

---

## 4. 9 个测试项

| 序号 | 测试项 | 看什么 |
| --- | --- | --- |
| 1 | COLOR BAR | 8 条彩带颜色是否正确（红绿蓝黄青品红白黑） |
| 2 | GRADIENT | 黑白灰阶有没有断层；RGB 渐变有没有色偏 |
| 3 | GRAPHIC | 矩形/圆角矩形/圆/三角/斜线有没有缺线、错位 |
| 4 | TEXT | font 1/2/4/6 字号和 4 种对齐基准点 |
| 5 | GRID | 20 px 网格、四角绿块、白色边框（判断整体偏移/被裁） |
| 6 | SPRITE | 16 位色 Sprite 画布（PSRAM）绘制后整屏推屏 |
| 7 | SPEED | `fillScreen` 平均耗时、Sprite 推屏帧率 |
| 8 | SYSINFO | RAM/PSRAM/Flash 占用 + 进度条 + 运行时间，每秒刷新 |
| 9 | CLEAR | 清屏 |

串口切页：

```
n / p : 下一个 / 上一个测试项
r     : 重跑当前测试项
1-9   : 直接选测试项
h / ? : 再打印一遍帮助
```

> 屏上文字只能是英文：TFT_eSPI 内置字体不含中文，中文只走串口打印。
> 想在屏上显示中文要另外加载中文字库（LVGL 的 `simsun_16_cjk` 那条路）。

---

## 5. 判读标准（出问题时看这里）

| 现象 | 结论 |
| --- | --- |
| 背光闪但一直黑 | SPI 数据/时钟或 DC/CS 接错；先按 `5` 看网格页 |
| 背光完全不亮、串口也没日志 | flash 配置错（本工程已按 N16R8 配好；`bootloader.bin` 头部 `freq_size` 应为 `0x4F`） |
| 颜色反相 | 在 `platformio.ini` 的 `build_flags` 加 `-DTFT_INVERSION_ON` |
| 红蓝互换 | 加 `-DTFT_RGB_ORDER=TFT_BGR` |
| 画面整体偏移、顶部有杂线 | 加 `-DTFT_OFFSET_X=..` / `-DTFT_OFFSET_Y=..`（rotation 2 一般不需要） |
| 只有左上角 1/4 有画面 | 屏实际是 128x160 之类的小屏，检查型号并改驱动宏 |
| 屏上文字有锯齿/缺笔画 | 正常：内置点阵字体的显示效果 |

---

## 6. 代码结构（单文件，按分节注释分块）

```
TFT_Test/
├── platformio.ini      ← 引脚 + TFT_eSPI 配置 + 库版本，改这里就够
├── src/main.cpp        ← 全局对象 / 测试图案 / 任务 / 队列
└── README.md
```

`main.cpp` 的分节顺序：状态宏 → TFT 对象 → SystemInfo 结构体 → 全局队列 → 通用小函数
→ 测试图案 → 队列收发 → 任务 → `setup/loop`。

### 队列流向

```
按键 GPIO42/41/40 ──> ButtonTask ──> ButtonValueQueue ──┐
                                                       ├─> FlagJudgeTask ──> MyQueueSend ──> ToTFTTestFlagQueue ──> tftTestTask（独占屏幕）
串口 n/p/r ──> serialCmdTask ──> ButtonValueQueue ─────┘
串口 1-9 ──> serialCmdTask ──> MyQueueSend ─────────────────────────────────────────────> ToTFTTestFlagQueue
```

- `ButtonValueQueue`：按键/翻页标志（`uint8_t`），长度 `BUTTON_QUEUE_SIZE`。
- `ToTFTTestFlagQueue`：测试项标志（`uint8_t`），长度 `TESTFLAG_QUEUE_SIZE`。
- 两个队列都在 `setup()` 里创建，`ToTFTTestFlagQueue` 只有 `tftTestTask` 一个消费者。

### 任务分配

| 任务 | 优先级 | 核心 | 职责 |
| --- | --- | --- | --- |
| `tftTestTask` | 2 | 1 | 独占 TFT，画测试页面；系统信息页每秒刷新 |
| `systemInfoTask` | 1 | 0 | 每秒采集堆/PSRAM/Flash/运行时间，写 `SystemInfo` + 串口打印 |
| `ButtonTask` | 1 | 0 | 移位历史消抖（连续 4 次），按下发标志 |
| `FlagJudgeTask` | 1 | 0 | 按键标志 → 具体测试项 |
| `serialCmdTask` | 1 | 0 | 读串口命令，复用上面两条路 |

`loop()` 里只有 `vTaskDelay(pdMS_TO_TICKS(1000))`，不写业务。

### 共享数据

`SystemInfo` 是唯一的共享结构体，用 `infoMutex` 保护：`systemInfoTask` 整体写入，
`tftTestTask` 刷新系统信息页时**快照拷贝**出来用，不长时间持锁。

---

## 7. 注意事项

1. **工程名和板子**：PlatformIO 向导生成时板子是 `esp32-s3-devkitm-1`（M-1 = 4 MB flash、无 PSRAM）。
   本工程按参考工程改成了 `esp32-s3-devkitc-1` + `board_build.arduino.memory_type = qio_opi`，
   这才是 N16R8 的正确配置，不改会在二级 bootloader 卡死、屏永远不亮。
   如果你的实物真是 DevKitM-1，请把板子名字改回去并告诉我。
2. **TFT_eSPI 版本固定 2.5.43**：`USE_FSPI_PORT` 是给这个版本打的补丁（ESP32-S3 核把 FSPI 映射成 0，
   库会当成寄存器地址）。
3. **屏幕只有一个消费者**：TFT 只由 `tftTestTask` 访问，所以没有总线互斥锁。以后要加
   LVGL 或摄像头预览时，必须像 `Master/master_diag` 那样加一把 SPI 总线互斥锁。
4. **按键和摄像头抢脚**：GPIO40/41/42 同时也是 OV2640 的 D5/D6/D7。本自检不含摄像头，
   但如果线上同时接着摄像头，三个按键不要接。
5. **Sprite 占 PSRAM**：整屏 16 位色画布 240x320x2 = 150 KB。申请失败会自动退回 240x60，
   Sprite 页仍可跑，串口会打印退回信息。
6. **换屏只改 `platformio.ini`**：例如换 ST7735 128x160，把 `-DILI9341_DRIVER` 换成
   `-DST7735_DRIVER`（并按面板加 `-DST7735_GREENTAB2` 之类），`TFT_WIDTH/TFT_HEIGHT` 改成 128/160，
   `TFT_ROTATION` 按需改。代码里所有坐标都用 `SCR_W/SCR_H`，不用改。
7. **测试页会互相覆盖**：切页就是整屏重画，这是测试程序，不做局部刷新。
