# 速定位书格 · SmartBookShelf

面向公共书屋、社区书架等小型阅读场景的**智能图书存取系统**。
以“傻瓜式操作”约束随意归还行为，解决图书乱架、查找困难的问题，降低人工维护成本。

> 本文件是整个仓库的**总汇总说明**：硬件平台、工程划分、统一代码规范、各子工程入口、当前进度与已知问题。
> 每个子工程还有各自的详细 README，链接见第 4 节。

---

## 1. 项目速览

| 项 | 内容 |
| --- | --- |
| 项目名 | 速定位书格（SmartBookShelf） |
| 主控 | ESP32-S3-DevKitC-1 **N16R8**（16MB QIO Flash + 8MB OPI PSRAM） |
| 运行框架 | Arduino + FreeRTOS（PlatformIO 工程） |
| 目标场景 | 公共书屋 / 社区书架 / 自助书柜 |
| 核心功能 | 摄像头识别图书、电动机构存取书、屏上引导式交互、联网与语音（目标） |
| 代码风格 | C++ 但偏 C：过程式、模块化、无 STL/模板/lambda/异常 |
| 仓库状态 | 多子工程并行推进，识别与显示已验证，机械与联网仍在落地 |

---

## 2. 目录结构

```
SmartBookShelf/
├─ README.md                ← 本文件（总汇总）
├─ Master/                  ← 主控工程：屏幕 + 摄像头 + LLM 联网（开发主战场）
│  ├─ src/main.cpp          ← 当前只有 WiFi + HC-SR04 + LLM 联网测试
│  ├─ src/WiFiconfig.h      ← WiFi 账号（明文，见第 8 节风险 1）
│  ├─ src/LLMconfig.h       ← DeepSeek API 地址/密钥/模型（明文）
│  ├─ src/PINSETconfig.h    ← HC-SR04 引脚
│  ├─ include/board_pins.h  ← 整板引脚分配表（唯一权威）
│  ├─ include/camera_config.h      ← OV2640 采集/变化检测参数
│  ├─ include/lv_conf.h     ← LVGL 配置
│  ├─ master_diag/          ← 显示/摄像头分模块自检源码（当前 ini 无 diag 环境，暂不编译，见 8 节）
│  └─ docs/                 ← 外设 bring-up 与 LLM 调试记录
├─ CNN_Test/                ← 识别环节单板验证：HUSKYLENS 2 内置 CNN
│  ├─ include/HuskyConfig.h ← 引脚 / I2C 超时 / 任务节奏
│  ├─ include/HuskyCam.h    ← 二哈封装类声明 + 算法表
│  ├─ src/HuskyCam.cpp      ← 封装实现
│  ├─ src/main.cpp          ← 任务 + 队列 + 互斥锁 + 串口命令
│  └─ lib/DFRobot_HuskylensV2/  ← 驱动 1.0.9（vendor，离线可编译）
├─ TFT_Test/                ← 显示环节单板验证：ILI9341 240x320，9 项自检
├─ Main/                    ← 主控整合工程（库化）：识别 + 显示 + 系统信息 + 按键 + 串口命令
│  ├─ include/BoardConfig.h ← 全工程唯一引脚来源
│  ├─ lib/HuskyLib/         ← 二哈 I2C + 内置 CNN 识别库
│  ├─ lib/DisplayLib/       ← ILI9341 状态页/系统信息页/书列表页
│  ├─ lib/SysInfoLib/       ← RAM/PSRAM/Flash/运行时间采集
│  ├─ lib/ButtonLib/        ← 按键消抖 + 事件队列
│  ├─ lib/SerialCmdLib/     ← 串口命令行解析 + 注册式分派
│  ├─ lib/DFRobot_HuskylensV2/  ← 驱动 1.0.9（vendor）
│  └─ src/main.cpp          ← 只做拼装：全局对象 / 命令注册 / 任务
├─ Wave_Test/               ← 预留：语音/波形测试（当前仍是空模板）
└─ Model/                   ← 机械结构三维工程（Blender Python）
   ├─ build/                ← 参数化建模 / 动画 / 校验脚本
   ├─ scripts/              ← 第二版拆分脚本
   ├─ models/               ← .blend 源文件 + .glb 导出
   ├─ renders/              ← 9 张展示视图 + 演示动画 + 工序帧
   ├─ docs / _spec/         ← 结构参数文档 + 项目说明书
   └─ Old/                  ← 两版已废弃方案（保留作对比）
```

每个子工程都是**独立的 PlatformIO 工程**，各自有 `platformio.ini`，可单独编译烧录。
多个子工程共用同一块 ESP32-S3 N16R8。

---

## 3. 项目背景与目标

### 3.1 要解决的问题

- 读者归还图书时随意摆放，不放回原位；
- 小型书屋数量多、位置偏，难以配备管理员，无法及时整理；
- 乱架直接影响后续读者的查找与借阅体验。

### 3.2 预设目标（来自《项目说明书》）

1. 使用摄像头智能识别归类图书；
2. 设计机械结构完成取书与放书；
3. 设计联网系统便于借阅与归还；
4. 使用语音识别实现快速借书。

### 3.3 实际落地范围

| 目标 | 实际状态 |
| --- | --- |
| 摄像头识别 | ✅ 已选定 HUSKYLENS 2（GC2093）内置 CNN，走 I2C 读结果；OCR 认书名已在驱动层打通 |
| OV2640 本地取图 | 🔶 引脚与采集参数已定义、bring-up 文档已写，主程序尚未接入 |
| 机械结构 | 🔶 三维模型与参数化校验已完成（3 台 42 步进 + T8 丝杆），实物控制程序未开始 |
| 屏幕交互 | ✅ ILI9341 自检 9 项通过；LVGL 菜单在参考工程已验证，待整合 |
| 联网 / LLM 归类 | ✅ DeepSeek API 对话链路已跑通（含 JSON 解析） |
| 语音识别 | ⬜ 未开始（`Wave_Test` 仅占位） |
| Lora / ESP-NOW 组网 | ⬜ 未开始 |

> ⚠️ 方案演进说明：说明书里原文是“Arduino UNO R3 控制电机 + 掌控板识别 + 串口/ESP-NOW 通信”，
> 实物方案已升级为 **单块 ESP32-S3 N16R8 主控 + ESP32-C3 Super Mini 步进控制**，电机也从
> 28BYJ-48 换成 **3 台 42 步进电机 + T8 丝杆（导程 4mm）**。以 `board_pins.h` 与 `Model/README.md` 为准。

---

## 4. 子项目索引

| 子工程 | 一句话 | 详细文档 |
| --- | --- | --- |
| `Master` | 主控：屏幕 + 摄像头 + 联网 + LLM | [bringup_display_camera.md](Master/docs/bringup_display_camera.md)、[llm_chat.md](Master/docs/llm_chat.md) |
| `CNN_Test` | 二哈 HUSKYLENS 2 内置 CNN 识别自检 | [CNN_Test/README.md](CNN_Test/README.md) |
| `TFT_Test` | ILI9341 屏幕 9 项自检 | [TFT_Test/README.md](TFT_Test/README.md) |
| `Model` | 机械结构三维工程（参数化 + 自动校验） | [Model/README.md](Model/README.md) |
| `Main` | 主控整合工程（库化）：识别 + 显示 + 系统信息 + 按键 + 串口命令 | [Main/README.md](Main/README.md) |
| `Wave_Test` | 预留的语音/波形测试 | 空模板，暂无 |

---

## 5. 硬件平台

### 5.1 主控板引脚总表（来源：`Master/include/board_pins.h`）

可用 GPIO 只有 **0-21 和 38-48**。以下引脚**绝对不能接外设**：

| 引脚 | 用途 |
| --- | --- |
| GPIO 26-32 | 封装内 SPI Flash |
| GPIO 33-37 | 封装内 OPI PSRAM（N16R8 的 8MB） |
| GPIO 22-25 | 该芯片上不存在 |
| GPIO 19/20 | 原生 USB D-/D+（留给烧录与监视） |
| GPIO 43/44 | UART0 控制台 |

已分配外设（无引脚复用冲突，除特别标注）：

| 外设 | 引脚 | 备注 |
| --- | --- | --- |
| ILI9341 屏 | SCLK=12 MOSI=11 CS=10 DC=9 RST=8 BL=47 | 240x320 竖屏，SPI 27MHz |
| microSD | CS=13（共用 SCLK/MOSI） | |
| HC-SR04 | TRIG=4 ECHO=5 | Echo 为 5V，**必须分压**后接 GPIO |
| ESP32-C3 串口链路 | TX=6 RX=7 | 下行步进控制板 |
| OV2640 摄像头 | XCLK=15 PCLK=16 VSYNC=17 HREF=18 SIOD=1 SIOC=48 D0=3 D1=21 D2=39 D3=14 D4=38 D5=40 D6=41 D7=42 | 8 位并口 DVP |
| 按键 | 42 / 41 / 40 | 按下为高电平（`BUTTON_TRUE_VALUE=1`） |
| HUSKYLENS 2 | SDA=1 SCL=2 | 见 `CNN_Test/include/HuskyConfig.h` |

**⚠️ 两处需要使用前确认的引脚冲突：**

1. **GPIO 40/41/42 同时是按键和摄像头 D5/D6/D7**。硬件调试顺序：先只接屏 + 摄像头（不接按键），
   确认取图正常后再把按键挪到 45/46/0（用 `-DBUTTON_PIN1=..` 覆盖），或把 D5/D6/D7 让到空脚。
2. **GPIO 1 是 OV2640 的 SCCB SDA，同时是 HUSKYLENS 2 的 I2C SDA**（CNN_Test 用 GPIO1/2）。
   如果最终方案要“本地 OV2640 取图 + 二哈识别”同时挂，必须把其中一个挪脚，
   否则两条 I2C/SCCB 总线会打架。

### 5.2 显示

| 项 | 值 |
| --- | --- |
| 型号 | 2.8 寸 SPI ILI9341，240×320 |
| 方向 | 竖屏（`TFT_ROTATION=2`） |
| 驱动库 | TFT_eSPI **2.5.43**（版本必须锁死，`USE_FSPI_PORT` 是该版本的补丁） |
| 背光 | GPIO47，由 LEDC 控制（排错时不要直接接 VCC） |
| 中文字体 | 需额外加载（LVGL 走 `simsun_16_cjk`；TFT_eSPI 内置字体只有英文） |

### 5.3 识别

| 项 | 值 |
| --- | --- |
| 模块 | HUSKYLENS 2（GC2093 传感器，自带 NPU） |
| 通信 | I2C，地址 **0x50**，SDA=GPIO1 SCL=GPIO2 |
| 关键设置 | 二哈屏幕 `系统设置 -> 协议类型 -> I2C` |
| 关键参数 | I2C 100kHz、事务超时 **1000ms**（默认 50ms 会大面积失败）、库 retry=3 |
| 可用算法 | 物体识别 / 物体分类 / 自学习分类 / OCR 文字识别（默认）/ 标签 / 条码 / 二维码 / 人脸 |

### 5.4 机械与执行机构（来源：`Model/README.md`）

**核心原理（不可更改）**：整机内部放的是“一摞平躺的书”，移动的是**书 + 升降平台**，
推出机构位置固定，前面板的**中间夹层**上下移动来改变推出孔的有效高度。

| 项 | 值 |
| --- | --- |
| 整机外形 | 600 × 350 × 700 mm（宽×深×高） |
| 柜体内腔 | 544 × 334 × 670 mm |
| 电机 | **3 台 42 步进电机**，全部经 T8 丝杆（导程 4mm）传动 |
| 电机1（固定） | 水平丝杆 → 推出小车 → 推书板，沿 Y 前后 |
| 电机2（固定） | 垂直丝杆 → 可调中间夹层，改变推出孔高度 |
| 电机3（固定于底部） | 垂直丝杆 → 升降平台，带整摞书沿 Z 升降 |
| 推出孔 | 下沿固定 Z=380，有效高度 18–53 mm 可调 |
| 推书板 | 高 14 mm ≤ 最薄书 15 mm，实现单本分离 |
| 书籍 | 5 本示例，180×150×{15,25,30,40,50} mm，全部水平平躺 |

运动时序（15 秒演示动画）：平台带书上移 → 中间夹层校准推出孔 → 推书板推出目标书 → 推书板退回 → 取书完成。

---

## 6. 软件架构与统一代码规范

### 6.1 统一约定（所有子工程一致）

- **C++ 但偏 C**：不用 `std::vector/string/map/unique_ptr/shared_ptr`，不用模板、lambda、异常、复杂继承。
- **命名**：函数 `collectSystemInfo()`；任务 `xxxTask`；队列 `XxxQueue` / `ToXxxQueue`；
  结构体类 `SystemInfo`；私有成员 `_pins`；宏全大写 `GOSTEP`。
- **格式**：4 空格缩进，大括号换行，一行一个语句，中文注释，分节注释 `//名称`。
- **共享数据**：`结构体 + 互斥锁 + 快照拷贝`，不用复杂容器。
- **类型**：`uint8_t/uint16_t/uint32_t/int16_t/int32_t/float/bool`；百分比计算用 `uint64_t` 防溢出。
- **文件划分**：`.h` 放声明/宏，`.cpp` 放实现，`main.cpp` 放全局对象、任务、队列、UI、业务逻辑。

### 6.2 任务与队列模式（全项目通用骨架）

```cpp
// 任务：单一职责，循环里读队列 + 短延时
static void xxxTask(void *pvParameters) {
    uint8_t flag = 0x00;
    for (;;) {
        if (xQueueReceive(XxxQueue, &flag, pdMS_TO_TICKS(10)) == pdPASS) {
            // 业务
        }
        vTaskDelay(pdMS_TO_TICKS(5));
    }
}

// 创建：指定核心与栈大小
xTaskCreatePinnedToCore(xxxTask, "xxx", 8192, nullptr, 1, nullptr, 0);

// 共享数据：互斥锁 + 快照
if (xSemaphoreTake(infoMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
    snap = systemInfo;      // 拷贝出去用，不长时间持锁
    xSemaphoreGive(infoMutex);
}
```

### 6.3 各子工程的队列流向

**TFT_Test / CNN_Test（按键或串口 → 判定 → 执行）**

```
按键 GPIO42/41/40 ─> ButtonTask ─> ButtonValueQueue ─┐
                                                     ├─> FlagJudgeTask ─> MyQueueSend ─> 执行任务
串口命令 ─────────> serialCmdTask / loop ────────────┘
```

**CNN_Test（本次新增）**

```
huskyTask(核心1, 唯一碰 I2C) --ResultQueue(邮箱,只留最新帧)--> printTask(核心0)
loop(串口 pollSerial)        --CmdQueue-->                     huskyTask
monitorTask                  --systemInfo(infoMutex 保护)-->   printTask
```

**设计要点**：屏幕 / 摄像头 / I2C 这类总线**只允许一个任务访问**。
`TFT_Test` 因为屏只有一个消费者所以没有总线锁；一旦要加 LVGL 或摄像头预览，
必须像 `Master/master_diag` 那样加 **SPI 总线互斥锁**，否则花屏/卡死。

---

## 7. 快速上手

### 7.1 环境

- PlatformIO（VSCode 插件或 CLI）
- Espressif32 平台 + Arduino 框架（各子工程 `platformio.ini` 已配好）
- 若 `pio` 不在 PATH，用绝对路径：
  `C:\Users\GX\.platformio\penv\Scripts\pio.exe`

### 7.2 通用烧录流程

```powershell
cd D:\Project\SmartBookShelf\<子工程>
pio run -e esp32-s3-devkitc-1 -t upload
pio device monitor -e esp32-s3-devkitc-1     # 115200
```

### 7.3 各子工程命令

| 子工程 | 命令 | 说明 |
| --- | --- | --- |
| CNN_Test | `pio run -e esp32-s3-devkitc-1 -t upload` | 识别自检，串口命令 `?` 看帮助 |
| TFT_Test | `pio run -t upload` | 屏幕自检，9 个测试项，`1-9` 切换 |
| Master | `pio run -e esp32-s3-devkitc-1 -t upload` | 联网/LLM 测试 |
| Main | `pio run -e esp32-s3-devkitc-1 -t upload` | 主控整合（库化），串口命令 `?` 看帮助 |
| Model | `blender.exe --background --factory-startup --python build\run_all.py -- build anim explode stage validate save` | 重建三维模型并自动校验 |

### 7.4 推荐硬件调试顺序

```
1. TFT_Test        → 屏能不能亮、能不能刷（先解决显示）
2. CNN_Test        → 二哈在不在总线上、能不能读出识别结果（再解决识别）
3. Master/diag     → 摄像头取图 + 屏预览能不能同时跑（解决总线互斥）
4. Master          → WiFi + DeepSeek 归类链路
5. 机械 + ESP32-C3 → 3 台 42 步进 + 丝杆（尚未开始）
6. Main            → 已库化整合识别/显示/系统信息/按键/串口命令（机械控制待接）
```

---

## 8. 进度与待办

### ✅ 已完成

- **显示**：ILI9341 9 项自检（彩带/灰阶/图形/字体/网格/Sprite/速度/系统信息/清屏）通过，
  含 N16R8 的 `qio_opi` 正确配置（不改会在二级 bootloader 卡死）。
- **识别**：HUSKYLENS 2 驱动封装与 CNN_Test 自检固件，编译通过（RAM 6.7% / Flash 4.6%），
  支持 8 种算法切换、学习/清空、知识库保存、I2C 压测。
- **联网**：WiFi 连接 + HTTPS + DeepSeek Chat Completions + JSON 解析跑通。
- **主控整合（Main）**：5 个独立库（HuskyLib / DisplayLib / SysInfoLib / ButtonLib / SerialCmdLib）
  + 引脚表 `BoardConfig.h`，编译通过（RAM 7.3% / Flash 5.2%，清空构建缓存后全量重编译一致）。
- **机械**：参数化三维模型（13 个集合、33 个零件、360 帧动画），
  两道自动闸门校验（跨刚体干涉 0 处、26/26 设计断言通过）。

### 🔶 进行中

- 主控整合：把屏、摄像头、识别、LLM 收进一个固件。当前 `Master/src/main.cpp` 只有 WiFi + HC-SR04 + LLM 联网测试；
  摄像头/屏幕代码只存在于 `Master/master_diag/`（3 个 `.inc` 自检固件）和 bring-up 文档里，
  而 `Master/platformio.ini` **没有 diag 环境、也没有 build_src_filter**，所以这些自检文件现在编译不了。
  要跑它们，需参考 BookDispenser 的写法补一个 `[env]` 共用段 + `[env:diag]`（`build_src_filter = +<../master_diag/>`）。
- 总线互斥：摄像头预览 + LVGL 同时跑需要 SPI 互斥锁（`master_diag` 已有实现可参考）。

### ⬜ 待开始

- `Wave_Test/` 仍是 PlatformIO 空模板（`myFunction` 骨架），尚无业务代码。
- `Main/` 已完成库化骨架（5 个库，编译通过），但**机械控制、语音、组网还没接**。
- 3 台 42 步进电机 + ESP32-C3 的控制程序（含软限位、加减速、位置反馈）。
- 语音识别、LoRa / ESP-NOW 组网、Blynk 物联网接入。

### ⚠️ 风险与注意事项（重要）

1. **密钥明文入库**：`Master/src/WiFiconfig.h`（WiFi 名与密码）和 `Master/src/LLMconfig.h`
   （DeepSeek API Key）都是**明文写在源码里**的。推到公开仓库前必须改为：
   本地 `secrets.h`（加入 `.gitignore`）或运行期配置，并**轮换已泄露的 API Key**。
2. **引脚冲突**：按键 GPIO40/41/42 与摄像头 D5/D6/D7 冲突；GPIO1 与二哈 SDA 冲突（见 5.1）。
3. **`qio_opi` 不能漏**：所有子工程都必须保留
   `board_build.arduino.memory_type = qio_opi` + `board_build.partitions = default_16MB.csv`。
4. **TFT_eSPI 锁 2.5.43**：换版本会破坏 `USE_FSPI_PORT` 补丁。
5. **包管理**：`Master/lib` 与多数子工程 `lib/` 为空，依赖走 `lib_deps` 从网络拉取；
   只有 `CNN_Test/lib/DFRobot_HuskylensV2` 是本地 vendor 的，**完全离线可编译**。
6. **HC-SR04 的 ECHO 是 5V**，必须分压后接 GPIO，否则可能损坏芯片。
7. 二哈的库接口**非线程安全**且设备离线时会阻塞 25 秒以上，调用前必须先探 `0x50`；
   `res->classID` 与 `res->xCenter` 是同一内存槽位，取 ID 必须用 `res->ID`。

---

## 9. 相关文档

| 文档 | 内容 |
| --- | --- |
| [Master/docs/bringup_display_camera.md](Master/docs/bringup_display_camera.md) | 显示 + 摄像头 bring-up 全过程与判读标准 |
| [Master/docs/llm_chat.md](Master/docs/llm_chat.md) | DeepSeek LLM 对话链路调试记录 |
| [CNN_Test/README.md](CNN_Test/README.md) | 二哈识别自检：接线、命令、踩坑记录 |
| [TFT_Test/README.md](TFT_Test/README.md) | 屏幕自检：接线、9 个测试项、故障判读表 |
| [Model/README.md](Model/README.md) | 机械原理、参数表、动画、自动校验 |
| [Model/_spec/spec_text.txt](Model/_spec/spec_text.txt) | 《“速定位书格”智能图书存取系统项目说明书》正文 |
| `Model/Old/` | 两版废弃方案（多层书架 / 电梯夹取）及废弃原因 |

---

## 10. 版本管理现状

- 仓库已初始化 git（`git log`：`SmartShelf` → `WiFiConnectTask` → `httpstest` → `httpsJSONtest` → `JSON解析` → `API`）。
- 当前**已跟踪**的只有早期 `Master` 代码；`CNN_Test/`、`TFT_Test/`、`Main/`、`Wave_Test/` 尚未提交。
- 提交前建议先处理第 8 节风险 1 的密钥问题，并把 `.pio/`、`build_log.txt` 等构建产物排除在版本库外。
