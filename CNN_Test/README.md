# CNN_Test —— HUSKYLENS 2 内置 CNN 识别测试

“速定位书格”识别环节的单板验证固件。二哈（HUSKYLENS 2）自己带 NPU，**CNN 推理在二哈内部跑**，
ESP32-S3 这边只做三件事：I2C 通信、结果搬运、串口打印。

## 1. 硬件与接线

| 项目 | 值 |
| --- | --- |
| 主控 | ESP32-S3-DevKitC-1 N16R8（16MB QIO Flash + 8MB OPI PSRAM） |
| 识别模块 | HUSKYLENS 2（GC2093），协议类型必须设为 **I2C** |
| SDA | 绿线 -> **GPIO1** |
| SCL | 蓝线 -> **GPIO2** |
| 供电 | VCC -> 5V（或 3V3），GND 必须共地 |

二哈的操作：屏幕 `系统设置 -> 协议类型 -> I2C`，改完要重启二哈。

> GPIO33~37 被 OPI PSRAM 占用，绝对不要接外设。

## 2. 编译烧录

```powershell
pio run -e esp32-s3-devkitc-1 -t upload
pio device monitor -e esp32-s3-devkitc-1
```

串口 115200。`DFRobot_HuskylensV2` 驱动已经 vendor 到 `lib/` 目录，离线也能编译。

## 3. 串口命令（要按回车，固件只认换行结尾的输入）

| 命令 | 作用 |
| --- | --- |
| `?` | 显示帮助 |
| `s` | 重扫 I2C 总线，看二哈在不在 |
| `r` | 断开重连二哈 |
| `i` | 打印二哈状态（算法 / 识别数 / 已学习数 / 通信统计） |
| `1`~`8` | 直接切到算法表里对应算法（见下） |
| `a` | 切到下一个算法 |
| `l` | 学习当前画面中心目标（CNN 提特征入库） |
| `f` | 清空当前算法的学习内容 |
| `w` / `o` | 保存 / 读取知识库槽位 0（断电不丢） |
| `p` | 读算法参数（排查 OCR 读出空内容用） |
| `t` | I2C 通信压测（看成功率） |
| `y` | 系统信息（RAM / PSRAM / Flash / 运行时间） |

可选算法表（`include/HuskyCam.h` 里可加）：

```
1 = 物体识别            2 = 物体分类
3 = 自学习分类          4 = OCR 文字识别   <- 开机默认
5 = 视觉标签识别        6 = 条形码识别
7 = 二维码识别          8 = 人脸识别
```

开机默认是 **OCR 文字识别**，因为本项目要认图书标题。

## 4. 代码结构

```
CNN_Test/
├─ include/
│  ├─ HuskyConfig.h    引脚、I2C 超时/重试、任务节奏、缓冲区大小
│  └─ HuskyCam.h       二哈封装的类声明 + 算法表
├─ src/
│  ├─ HuskyCam.cpp     二哈封装实现（probe/connect/readWords/learn/forget）
│  └─ main.cpp         全局对象、队列、互斥锁、任务、串口命令
├─ lib/DFRobot_HuskylensV2/   驱动（从 BookDispenser 拷来，版本 1.0.9）
└─ platformio.ini      N16R8 配置 + 全部可调参数
```

### 任务与队列（单一职责）

| 任务 | 核心 | 职责 |
| --- | --- | --- |
| `huskyTask` | 1 | **唯一**接触 I2C 和二哈库的任务：连接、切算法、读结果、执行命令 |
| `printTask` | 0 | 只负责串口打印，数据全部从队列/互斥锁快照拿 |
| `monitorTask` | 0 | 采集 RAM/PSRAM/Flash/运行时间，写共享结构体 |

```
huskyTask   --ResultQueue(邮箱, 只留最新一帧)-->  printTask
loop(串口)  --CmdQueue-->                        huskyTask
monitorTask --systemInfo (infoMutex 保护)-->     printTask
```

共享数据一律用 `infoMutex` 保护 + 快照拷贝；打印任务从不直接碰 I2C。

## 5. 踩过的坑（改代码前先看这里）

1. **必须先 `probe()` 再调库接口**。二哈不在线时库的 `knock()`/`wait()` 会阻塞 25 秒以上并刷屏 I2C 错误。
   本项目所有库调用前都先探 `0x50`（约 1ms）。
2. **`res->classID` 和 `res->xCenter` 是同一个内存槽位**（union 别名），要拿目标 ID 必须用 `res->ID`。
3. **`Wire.setTimeOut()` 必须调大**。ESP32 默认事务超时只有 50ms，二哈处理图像会拉伸时钟，
   默认值下协议往返实测三到五成失败，这里设成 1000ms。
4. **库的 `protocolWrite()` 每次都会把超时硬写回 5000ms**，所以 `timeOutDuration` 交给库之前的值
   对发送阶段无效；设备不在时想少等，只能调小 `husky.retry`（本工程 3）。
5. **切算法很慢**，二哈要重新加载 CNN 模型，经常失败，所以 `connect()` 里重试 5 次，成功后还要等 4 秒再读。
6. **OCR 只识别十字准心（画面中央）那个文字块**，未学习过的文字 ID=0，测试时要把书名对准准心。
7. **学习结果断电会丢**，要 `w` 保存到知识库槽位；不要用 `setNameByID` 设中文名，
   会出现“物物理”这类重复字，而且会抑制 `content`。
8. 通信不稳时先把 `-DHUSKY_I2C_HZ` 从 100000 降到 50000 试，再查上拉电阻和线材长度。

## 6. 典型排查流程

1. 上电看串口有没有 `BOOT_DONE`，`y` 命令确认 PSRAM 已找到。
2. `s` 扫总线，看不到 `0x50` -> 查供电、共地、绿蓝线是否接反、二哈协议类型是否 I2C。
3. 看到 `0x50` 但一直“等待二哈” -> 二哈可能停在菜单里，退出菜单再试。
4. 连上了但一直“画面无识别目标” -> 把目标对准镜头准心，换算法试试。
5. 有识别但读不出文字 -> `p` 看 OCR 参数（语言/字典），确认二哈端语言设置。
6. `t` 压测成功率低于 90% -> 总线电气问题，先降速再查上拉。
