# 显示屏 + 摄像头 上板自检（第一轮）

本轮只验证两件事：**ILI9341 240x320 SPI 屏**和 **OV2640 摄像头**。
SD 卡、超声波、C3 通信、云端识别尚未接入（见文末「下一步」）。

---

## 1. 接线表（ESP32-S3 N16R8）

⚠️ 本模块可用 GPIO 只有 **0-21 和 38-48**。
**GPIO 26-32 是封装内 flash、GPIO 33-37 是封装内 OPI PSRAM，绝对不能接任何东西。**

### 1.1 显示屏 ILI9341（SPI）

| 屏引脚 | ESP32-S3 | 说明 |
| --- | --- | --- |
| VCC | 3V3 | 不要接 5V |
| GND | GND | |
| SCK / CLK | GPIO12 | |
| SDA / MOSI | GPIO11 | |
| MISO | 不接 | 本方案不读屏，读 ID 会返回 0 属正常 |
| CS | GPIO10 | |
| DC / RS | GPIO9 | |
| RESET | GPIO8 | |
| LED / BL | GPIO47 | 由代码控制，**排错时不要直接接 VCC**（见 §5） |

驱动参数：`ILI9341_DRIVER`，240x320，`TFT_ROTATION=2`（竖屏），SPI 27 MHz。
全部定义在 `include/User_Setup.h`，可用 `platformio.ini` 的 `build_flags` 覆盖。

### 1.2 摄像头 OV2640（DVP 并口，SCCB = I2C）

| 摄像头引脚 | ESP32-S3 | 说明 |
| --- | --- | --- |
| VCC | 3V3 | |
| GND | GND | |
| XCLK | GPIO15 | 20 MHz 主时钟，由 ESP32 输出 |
| SIOD / SDA | GPIO1 | SCCB 数据（模块自带 4.7k 上拉，若没有需外接） |
| SIOC / SCL | GPIO48 | SCCB 时钟 |
| VSYNC | GPIO17 | |
| HREF | GPIO18 | |
| PCLK | GPIO16 | |
| D0 | GPIO3 | |
| D1 | GPIO21 | |
| D2 | GPIO39 | |
| D3 | GPIO14 | |
| D4 | GPIO38 | |
| D5 | GPIO40 | |
| D6 | GPIO41 | |
| D7 | GPIO42 | |
| PWDN / RESET | 接 3V3 | 代码里配置为 `-1`（不使用），所以必须硬件拉高 |

### 1.3 ⚠️ 按键与摄像头抢引脚

原计划的三个按键是 **GPIO42 / 41 / 40**，而摄像头的 **D5/D6/D7 正好也是 40/41/42**。
本轮的自检固件上电时会打印明确警告。

两种解法（任选）：

| 方案 | 做法 | 结果 |
| --- | --- | --- |
| A（推荐先做） | **按钮先不接** | 用串口命令切换页面，屏和摄像头都能验完 |
| B | 把按钮挪到空脚 | `pio run -e diag -t upload --project-option "build_flags=${env.build_flags} -DDIAG_TARGET_ALL -DBUTTON_PIN1=45 -DBUTTON_PIN2=46 -DBUTTON_PIN3=0"`，或直接改 `include/board_pins.h` |

> 当前空脚只剩 GPIO45 / 46 / 0（都是启动绑带脚，作为「按下即低/高电平」的输入可用，但别在启动瞬间按住不放）。

---

## 2. 三个自检环境

| 环境 | 包含内容 | 用途 |
| --- | --- | --- |
| `diag_display` | 屏 + LVGL，**不含摄像头驱动** | 摄像头接线有故障也不会导致黑屏；先证明屏是好的 |
| `diag_camera` | 只有摄像头，**不含屏和 LVGL** | 串口直接看抓帧结果，排除显示干扰 |
| `diag` | 屏 + 摄像头 + 按键（默认） | 完整自检，三页界面 + 摄像头实时预览 |

命令（在 `D:\Project\SmartBookShelf\Master` 下执行）：

```bash
pio run -e diag_display -t upload
pio run -e diag_camera  -t upload
pio run -e diag         -t upload
pio device monitor            # 115200
```

如果 `pio` 不在 PATH 里：

```bash
"C:\Users\GX\.platformio\penv\Scripts\pio.exe" run -e diag -t upload
```

---

## 3. `diag_display`：只验屏

上电串口输出（关键行）：

```
=====================================================
SmartBookShelf / diag_display
=====================================================
chip       : ESP32-S3  cores=2  freq=240 MHz
flash      : 16777216 bytes (16 MB)
psram found: YES  size=8386215 bytes (7 MB)
buttons: BTN1=42 BTN2=41 BTN3=40 (active high)
[1] backlight blink
[2] tft.init()
[3] tft.setRotation(TFT_ROTATION)
[4] address-window probe
TFT_eSPI: width=240 height=320 rotation=2
readcommand8(RDMADCTL)=0 (0 when MISO is not wired)
fillScreen(240x320) took 12 ms
[5] colour bars
[6] LVGL init
LVGL: buffers 18 KB x2 in PSRAM
ready: n/b/g/c over serial, or the three buttons
alive uptime=5s lvgl_ticks/s=200 heap=... psram_free=...
```

肉眼应看到：

1. 背光先闪 2 下（**这一步能证明 MCU 真的在跑**）；
2. 8 条色带 + 中间 "240x320"；
3. 之后进入 LVGL 页面：浅灰底、中文标题「显示屏自检 / DISPLAY TEST」、进度条 66%、一个蓝色方块在右下角。

串口命令：`n` 换图案（LVGL 页 → 色带 → 灰阶渐变 → 网格标尺）、`b` 色带、`g` 网格、`c` 清屏、`d` 回 LVGL 页。

### 判读标准

| 现象 | 结论 |
| --- | --- |
| 背光闪但一直黑 | SPI 数据/时钟或 DC/CS 接错；先用 `g` 网格页确认 |
| 颜色反相 | 在 `include/User_Setup.h` 里取消注释 `TFT_INVERSION_ON` |
| 红蓝互换 | 加 `-DTFT_RGB_ORDER=TFT_BGR` |
| 画面整体偏移/顶部有杂线 | 调 `CGRAM offset`：加 `-DTFT_OFFSET_X=..`（本方案用 rotation 2，一般不需要） |
| 只有左上角 1/4 有画面 | 屏幕实际是 128x160 之类的小屏，检查型号 |
| 背光不亮、串口也没有日志 | flash 配置错配（本工程已按 N16R8 配置好，`bootloader.bin` 头部 `freq_size` 应为 `0x4F`） |

---

## 4. `diag_camera`：只验摄像头

上电串口输出：

```
=====================================================
SmartBookShelf / diag_camera (OV2640)
=====================================================
...
--- OV2640 DVP pin map ------------------------------------
  XCLK=15  PCLK=16  VSYNC=17  HREF=18
  SIOD=1   SIOC=48   (SCCB)
  D0=3  D1=21  D2=39  D3=14
  D4=38  D5=40  D6=41  D7=42
  RESET=-1 PWDN=-1 (unused when -1)
-----------------------------------------------------------
requesting: 800x600 JPEG q=12 fb_count=2 fb_location=1
esp_camera_init -> 0 (ESP_OK)
sensor pid=0x26 ver=0x42 midh=0x7F midl=0xA2
sensor set: brightness=0 contrast=0 saturation=0
initial JPEG capture:
JPEG capture ok, 41230 us
  fb: 800x600 format=3 len=24113 bytes data=FFD8FFE0...
  frame2jpg re-encode: 24113 bytes in 96 ms
ready: c=1 JPEG, y=1 YUV422, s=10 JPEG, r=driver cycle
alive uptime=5s heap=... psram_free=... ok=1 fail=0
```

**判读标准**

| 现象 | 结论 |
| --- | --- |
| `esp_camera_init -> 0 (ESP_OK)` 且 `fb: 800x600 ... FFD8FFE0` | 摄像头完全正常（`FFD8FF` 是 JPEG 文件头） |
| `esp_camera_init -> 259 (ESP_FAIL (sensor probe / SCCB read failed))` | SCCB 通信失败：SIOD/SIOC 接反或没上拉、XCLK 没输出、供电不足 |
| `ESP_ERR_NO_MEM` | PSRAM 没配好（检查 `board_build.arduino.memory_type = qio_opi`） |
| `ESP_ERR_NOT_DETECTED` | 数据线或 PCLK/VSYNC/HREF 接错 |
| `pid=0x00` 或传感器型号怪异 | 排线接触不良 |
| 画面全黑但抓帧成功 | 镜头盖没摘、或曝光需要几帧稳定（多按几次 `s`） |

串口命令：`c` 抓 1 张 JPEG、`y` 换成 YUV422 抓一张（预览用）、`s` 连抓 10 张、`r` 重启摄像头驱动。

---

## 5. `diag`：屏 + 摄像头联动

三页界面，按键或串口命令切换：

| 页 | 内容 | 串口命令 |
| --- | --- | --- |
| 1 显示屏自检 | 与 `diag_display` 相同（LVGL 页 + 4 种图案） | `1`、`n` 换图案 |
| 2 摄像头自检 | 深色页：初始化状态、抓帧成功/失败数、堆/PSRAM、像素变化百分比、亮度 | `2`、`c` 抓帧 + 变化检测 |
| 3 实时预览 | 摄像头 YUV422 缩放后**直接推到屏上**，大画面 | `3` |

按键映射（若按方案 B 挪过脚，以下按键号随之变化）：

| 按键 | 页 1 | 页 2 / 页 3 |
| --- | --- | --- |
| BTN1 (42) | 换图案 | 下一页 |
| BTN2 (41) | 换图案 | 抓帧 + 变化检测 |
| BTN3 (40) | 清屏 | 重启摄像头驱动 |

**这一页同时验证了你需求里最关键的两条链路**：
摄像头数据 → PSRAM → 缩放/色彩转换 → SPI 屏，以及「帧间像素变化率」这个唤醒触发量。
串口会持续打印：

```
DIFF changed=182/768 (23%) luma=104 trigger=YES
```

`changed` 就是「画面大幅变化」的判据（当前阈值：像素差 > 25 且变化面积 ≥ 12%，都在
`include/camera_config.h` 里可调）。

**怎么测试触发**：让摄像头对着书格，然后用手或书在镜头前挥一下 —— `trigger=YES` 就该跳出来。
没有触发时保持 `trigger=no`，说明摄像头静默待机是有效的。

---

## 6. 代码结构

```
Master/
├── include/
│   ├── board_pins.h      ← 全部引脚定义，改这里就够
│   ├── camera_config.h   ← 摄像头分辨率/质量、变化检测阈值、连拍参数
│   ├── User_Setup.h      ← TFT_eSPI 面板配置（ILI9341 240x320）
│   └── lv_conf.h         ← LVGL 8.4 配置（PSRAM 内存、CJK 字体开关）
├── master_diag/          ← 三个自检固件的实现（.inc 由 src/main.cpp 条件包含）
│   ├── diag_common.h     通用：PSRAM 检查、PWM 触发信号、串口命令
│   ├── diag_display.h    屏：TFT 实例、背光、LVGL 驱动、测试图案、总线互斥锁
│   ├── diag_camera.h     摄像头：配置结构体、错误名、状态统计、RGB565 转换
│   ├── display_main.cpp.inc
│   ├── camera_main.cpp.inc
│   └── all_main.cpp.inc
├── src/main.cpp          ← 按 -DDIAG_TARGET_xxx 选择入口；否则是应用入口（当前为占位）
└── platformio.ini        ← 4 个环境：app / diag / diag_display / diag_camera
```

已处理的几个关键工程问题：

1. **N16R8 flash 配置**：`board_build.arduino.memory_type = qio_opi` + `default_16MB.csv`
   （板级定义只认 N8，不改会在二级 bootloader 卡死，屏永远不亮）。
2. **TFT 与摄像头分环境**：`diag_display` 不链接摄像头驱动，避免摄像头故障掩盖屏的问题。
3. **显示总线互斥锁**：预览是第二个任务直接推屏，`diagLvglFlush` 与预览共用一把互斥锁，避免撕裂。
4. **LVGL 时基**：`LV_TICK_CUSTOM` 用 `millis()`，不依赖任何任务去喂 tick。
5. **中文字体**：`include/lv_conf.h` 里 `LV_FONT_SIMSUN_16_CJK 1`，屏上中文菜单能直接显示；
   若想省 flash，改成 0 会自动回落英文。

---

## 7. 下一步（等你这轮验完再做）

1. **SD 卡**：SPI 共享屏总线 + 独立 CS(GPIO13)；原始 JPEG + 240x320 缩略图分开存；单实例访问用互斥锁串行。
2. **触发 → 抓拍**：变化率过阈值 → 唤醒窗口内连拍 → 存盘 → 索引文件（JSON/CSV）。
3. **HC-SR04**：GPIO4/5，等你建模完给我安装方向，我再定「距离 → 书厚 → 步数」的公式。
4. **UART 到 C3**：GPIO6/7，帧协议 + 状态回传。
5. **云端识别**：可插拔 `CloudClient` 接口 + `include/secrets.h`（写进 `.gitignore`）。
6. **应用 UI**：图库列表（缩略图网格 2 列，竖屏）、A/B 键翻页、长按确认。

---

## 8. 已知限制（本轮）

- 按键与摄像头 D5/D6/D7 争用 GPIO40/41/42，见 §1.3。
- 屏自检页与预览页共用 TFT，预览期间 LVGL 不刷新（这是设计如此）。
- 摄像头预览是 YUV422 → RGB565 整数缩放，非等比例时只填充对应方向的边带，不做黑边。
- 未接 SD 卡时所有存图相关功能都不存在，`app` 环境目前只有占位 `setup()/loop()`。
