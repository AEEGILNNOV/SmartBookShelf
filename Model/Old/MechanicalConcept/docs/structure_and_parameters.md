# 速定位书格 — 机械结构概念模型

智能图书存取系统 · Blender 机械概念模型（无动画，静态工作姿态）

## 1. 坐标与单位

| 项 | 值 |
| --- | --- |
| 单位系统 | 米，Unit Scale = 0.001（1 Blender 单位 = 1 mm）|
| X 轴 | 宽度方向（-X 左 / +X 右），0 = 柜体左右中心 |
| Y 轴 | 深度方向（-Y 后 / +Y 前），0 = 柜体前后中心 |
| Z 轴 | 高度方向（+Z 上），0 = 整机底部中心 |
| 静止姿态 | 主平台对准**中层书格**，推板处于**推出**状态 |

## 2. 整机包围盒

| 方向 | 最小 (mm) | 最大 (mm) | 尺寸 (mm) |
| --- | ---: | ---: | ---: |
| X | -250.0 | 250.0 | 500.0 |
| Y | -281.0 | 380.0 | 661.0 |
| Z | -52.0 | 1266.0 | 1318.0 |

## 3. 参数表（单位 mm）

| 分组 | 参数 | 值 | 说明 |
| --- | --- | ---: | --- |
| 柜体 | `CAB_W` | 500.0 |  |
| 柜体 | `CAB_D` | 400.0 |  |
| 柜体 | `CAB_H` | 1200.0 |  |
| 柜体 | `FRAME_T` | 20.0 | 框架/板材厚度 |
| 三层书格 | `SHELF_N` | 3.0 |  |
| 三层书格 | `LEVEL_H` | 300.0 | 每层净高（书格） |
| 三层书格 | `SHELF_X0` | 10.0 |  |
| 三层书格 | `SHELF_X1` | 190.0 |  |
| 三层书格 | `SHELF_Y0` | -170.0 |  |
| 三层书格 | `SHELF_Y1` | 20.0 |  |
| 三层书格 | `SHELF_T` | 30.0 | 层板厚度 |
| 顶部进书口 | `ENTRY_W` | 250.0 |  |
| 顶部进书口 | `ENTRY_D` | 80.0 |  |
| 顶部进书口 | `ENTRY_X0` | -125.0 |  |
| 顶部进书口 | `ENTRY_X1` | 125.0 |  |
| 顶部进书口 | `ENTRY_Y0` | 0.0 |  |
| 顶部进书口 | `ENTRY_Y1` | 80.0 |  |
| 42 步进电机 | `M42_W` | 42.0 |  |
| 42 步进电机 | `M42_D` | 42.0 |  |
| 42 步进电机 | `M42_H` | 38.0 |  |
| 42 步进电机 | `M42_SHAFT` | 8.0 |  |
| Z 轴 丝杆滑台 | `SCREW_D` | 12.0 |  |
| Z 轴 丝杆滑台 | `SCREW_Z0` | 95.0 | 丝杆下端高度 |
| Z 轴 丝杆滑台 | `SCREW_Z1` | 855.0 | 丝杆上端高度（丝杆长 = Z1-Z0 = 760） |
| Z 轴 丝杆滑台 | `COUPLING_D` | 15.0 |  |
| Z 轴 丝杆滑台 | `COUPLING_L` | 25.0 |  |
| Z 轴 丝杆滑台 | `NUT_W` | 40.0 |  |
| Z 轴 丝杆滑台 | `NUT_H` | 34.0 |  |
| Z 轴 丝杆滑台 | `MZ_Z0` | 32.0 |  |
| Z 轴 滑块/平台 | `CARRIAGE_W` | 76.0 |  |
| Z 轴 滑块/平台 | `CARRIAGE_D` | 90.0 |  |
| Z 轴 滑块/平台 | `CARRIAGE_H` | 80.0 |  |
| Z 轴 滑块/平台 | `CARRIAGE_Z0` | 250.0 | 滑块底面高度（静态工作位） |
| Z 轴 滑块/平台 | `PLATFORM_Z0` | 468.0 | 主平台板底面高度（上表面 480 = 中层书格） |
| Z 轴 滑块/平台 | `PLATFORM_T` | 12.0 |  |
| Z 轴 滑块/平台 | `PLATFORM_X0` | -212.0 |  |
| Z 轴 滑块/平台 | `PLATFORM_X1` | 188.0 |  |
| Z 轴 滑块/平台 | `PLATFORM_Y0` | 20.0 |  |
| Z 轴 滑块/平台 | `PLATFORM_Y1` | 380.0 |  |
| 右侧垂直导轨 | `RAIL_X0` | 178.0 |  |
| 右侧垂直导轨 | `RAIL_X1` | 198.0 |  |
| 右侧垂直导轨 | `RAIL_Y0` | -150.0 |  |
| 右侧垂直导轨 | `RAIL_Y1` | -110.0 |  |
| 右侧垂直导轨 | `RAIL_Z0` | 20.0 | 导轨下端 |
| 右侧垂直导轨 | `RAIL_Z1` | 880.0 | 导轨上端（导轨长 = Z1-Z0 = 860） |
| 右侧垂直导轨 | `SLIDER_Z0` | 480.0 |  |
| 右侧垂直导轨 | `SLIDER_Z1` | 560.0 |  |
| Y 轴 同步带推书 | `BELT_T` | 5.0 |  |
| Y 轴 同步带推书 | `BELT_W` | 75.0 |  |
| Y 轴 同步带推书 | `PULLEY_D` | 25.0 |  |
| Y 轴 同步带推书 | `PULLEY_W` | 10.0 |  |
| Y 轴 同步带推书 | `BELT_ZC` | 318.0 | 同步带中心高度 |
| Y 轴 同步带推书 | `MY_Z0` | 380.0 | Y 轴电机底面高度 |
| Y 轴 同步带推书 | `PUSH_X0` | -90.0 |  |
| Y 轴 同步带推书 | `PUSH_X1` | 90.0 |  |
| Y 轴 同步带推书 | `PUSH_T` | 5.0 |  |
| Y 轴 同步带推书 | `PUSH_Y0` | 160.0 |  |
| Y 轴 同步带推书 | `PUSH_Z1` | 754.0 | 推板上沿 = 平台面 420（相对平台顶面 60） |
| Y 轴 同步带推书 | `PUSH_STROKE_STATIC` | 70.0 | 静态工作姿态的推出量（沿 +Y） |
| 斜块 / 限位 | `WEDGE_W` | 100.0 |  |
| 斜块 / 限位 | `WEDGE_H` | 60.0 | 斜块高度（楔形，适应书厚） |
| 斜块 / 限位 | `WEDGE_T` | 20.0 |  |
| 斜块 / 限位 | `WEDGE_X0` | -34.0 |  |
| 斜块 / 限位 | `WEDGE_Y0` | 165.0 |  |
| 斜块 / 限位 | `LIMIT_X` | 97.0 |  |
| 斜块 / 限位 | `LIMIT_Y0` | 168.0 |  |
| 传感 / 电控 | `CAM_W` | 50.0 |  |
| 传感 / 电控 | `CAM_D` | 50.0 |  |
| 传感 / 电控 | `CAM_H` | 20.0 |  |
| 传感 / 电控 | `US_W` | 45.0 |  |
| 传感 / 电控 | `US_D` | 20.0 |  |
| 传感 / 电控 | `US_H` | 15.0 |  |
| 传感 / 电控 | `PCB_W` | 120.0 |  |
| 传感 / 电控 | `PCB_D` | 85.0 |  |
| 传感 / 电控 | `PCB_Z` | 205.0 |  |
| 书籍 | `BOOK_T_MIN` | 20.0 |  |
| 书籍 | `BOOK_T_MAX` | 50.0 |  |
| 书籍 | `BOOK_H_MIN` | 180.0 |  |
| 书籍 | `BOOK_H_MAX` | 240.0 |  |
| 书籍 | `BOOK_D` | 160.0 |  |
| 书籍 | `BOOK_LABEL_H` | 22.0 |  |

### 派生量

| 参数 | 值 (mm) | 说明 |
| --- | ---: | --- |
| `PLATFORM_TOP_Z` | 480.0 | 主平台上表面高度（= 中层书格 480）|
| `PUSH_EXT` | 70.0 | 推板静态推出量（沿 -Y）|
| `CARRIAGE_Z1` | 330.0 | 滑块顶面 |
| `SCREW_LEN` | 760.0 | 丝杆长度 |
| `RAIL_LEN` | 860.0 | 右侧导轨长度 |

## 4. 机构说明

| 轴 | 机构 | 组成 |
| --- | --- | --- |
| Z（垂直升降）| 42 步进电机 + 刚性联轴器 + 丝杆滑台 + 滑块 + 主平台 | `Motor_Z_42` `Coupling_Z` `Screw_Z` `Slide_Block_Z` `Main_Platform` |
| Y（水平推书）| 42 步进电机 + GT2 同步带 + 推板小车 + 推板 + 斜块 | `Motor_Y_42` `Belt_Mechanism` `Push_Plate` `Wedge_Block` |
| 导向/抗倾覆 | 右侧垂直导轨 + 预载滑块，与丝杆构成双点支撑 | `Guide_Rail_Right` `Guide_Slider_Right` |
| 书厚适应 | 楔形斜块 + 独立微调限位挡板（腰形槽 + 滚花螺钉）| `Wedge_Block` `Limit_Adjuster` |
| 识别/测厚 | 顶置摄像头 + 超声波模块 | `Camera_Top` `Ultrasonic_Top` |

> 动力源统一为 **42 步进电机 (42 × 42 × 38 mm, 轴径 8 mm)**；未使用 28BYJ-48 与 ULN2003，未使用齿轮齿条。

## 5. 书籍统计

| 位置 | 数量 |
| --- | ---: |
| level1 | 6 |
| level2 | 6 |
| level3 | 6 |
| platform | 1 |
| **合计** | **19** |

## 6. 场景与渲染

| 项 | 值 |
| --- | --- |
| engine | BLENDER_EEVEE（光线追踪 + AO + 软阴影） |
| resolution | 1920 x 1080 |
| samples | 128 |
| view_transform | Standard |
| camera_location | (1915, 3367, 2312) mm |
| camera_target | (0, 50, 607) mm |
| camera_lens | 58 mm |
| azimuth_deg | 30.0 |
| elevation_deg | 24.0 |
| camera_distance | 4193 mm |
| 灯光 | 三点布光：Key(暖) + Fill(冷) + Rim + Top 柔光箱 + 内部补光；EEVEE 光线追踪 / AO / 软阴影 |
| 动画 | **无**（无关键帧 / 无时间轴）|

## 7. 层级结构树

```
COL-00_Fixed  (直接成员 48)
    SmartBookshelf_Root  [empty]  {00_Fixed}
        EMPTY_Cabinet  [empty]  {00_Fixed}
            Book_L1_01  [mesh]  {04_Books}
            Book_L1_01_Label  [mesh]  {04_Books}
            Book_L1_01_Pages  [mesh]  {04_Books}
            Book_L1_02  [mesh]  {04_Books}
            Book_L1_02_Label  [mesh]  {04_Books}
            Book_L1_02_Pages  [mesh]  {04_Books}
            Book_L1_03  [mesh]  {04_Books}
            Book_L1_03_Label  [mesh]  {04_Books}
            Book_L1_03_Pages  [mesh]  {04_Books}
            Book_L1_04  [mesh]  {04_Books}
            Book_L1_04_Label  [mesh]  {04_Books}
            Book_L1_04_Pages  [mesh]  {04_Books}
            Book_L1_05  [mesh]  {04_Books}
            Book_L1_05_Label  [mesh]  {04_Books}
            Book_L1_05_Pages  [mesh]  {04_Books}
            Book_L1_06  [mesh]  {04_Books}
            Book_L1_06_Label  [mesh]  {04_Books}
            Book_L1_06_Pages  [mesh]  {04_Books}
            Book_L2_01  [mesh]  {04_Books}
            Book_L2_01_Label  [mesh]  {04_Books}
            Book_L2_01_Pages  [mesh]  {04_Books}
            Book_L2_02  [mesh]  {04_Books}
            Book_L2_02_Label  [mesh]  {04_Books}
            Book_L2_02_Pages  [mesh]  {04_Books}
            Book_L2_03  [mesh]  {04_Books}
            Book_L2_03_Label  [mesh]  {04_Books}
            Book_L2_03_Pages  [mesh]  {04_Books}
            Book_L2_04  [mesh]  {04_Books}
            Book_L2_04_Label  [mesh]  {04_Books}
            Book_L2_04_Pages  [mesh]  {04_Books}
            Book_L2_05  [mesh]  {04_Books}
            Book_L2_05_Label  [mesh]  {04_Books}
            Book_L2_05_Pages  [mesh]  {04_Books}
            Book_L2_06  [mesh]  {04_Books}
            Book_L2_06_Label  [mesh]  {04_Books}
            Book_L2_06_Pages  [mesh]  {04_Books}
            Book_L3_01  [mesh]  {04_Books}
            Book_L3_01_Label  [mesh]  {04_Books}
            Book_L3_01_Pages  [mesh]  {04_Books}
            Book_L3_02  [mesh]  {04_Books}
            Book_L3_02_Label  [mesh]  {04_Books}
            Book_L3_02_Pages  [mesh]  {04_Books}
            Book_L3_03  [mesh]  {04_Books}
            Book_L3_03_Label  [mesh]  {04_Books}
            Book_L3_03_Pages  [mesh]  {04_Books}
            Book_L3_04  [mesh]  {04_Books}
            Book_L3_04_Label  [mesh]  {04_Books}
            Book_L3_04_Pages  [mesh]  {04_Books}
            Book_L3_05  [mesh]  {04_Books}
            Book_L3_05_Label  [mesh]  {04_Books}
            Book_L3_05_Pages  [mesh]  {04_Books}
            Book_L3_06  [mesh]  {04_Books}
            Book_L3_06_Label  [mesh]  {04_Books}
            Book_L3_06_Pages  [mesh]  {04_Books}
            Book_Platform_01  [mesh]  {04_Books}
            Book_Platform_01_Label  [mesh]  {04_Books}
            Book_Platform_01_Pages  [mesh]  {04_Books}
            Cabinet_Back  [mesh]  {00_Fixed}
            Cabinet_BeamF  [mesh]  {00_Fixed}
            Cabinet_BeamF_Low  [mesh]  {00_Fixed}
            Cabinet_Bottom  [mesh]  {00_Fixed}
            Cabinet_Foot_01  [mesh]  {00_Fixed}
            Cabinet_Foot_02  [mesh]  {00_Fixed}
            Cabinet_Foot_03  [mesh]  {00_Fixed}
            Cabinet_Foot_04  [mesh]  {00_Fixed}
            Cabinet_Foot_05  [mesh]  {00_Fixed}
            Cabinet_Foot_06  [mesh]  {00_Fixed}
            Cabinet_PostFL  [mesh]  {00_Fixed}
            Cabinet_PostFR  [mesh]  {00_Fixed}
            Cabinet_SideLeft  [mesh]  {00_Fixed}
            Cabinet_SideRight  [mesh]  {00_Fixed}
            Cabinet_Top  [mesh]  {00_Fixed}
            Control_Btn_01  [mesh]  {00_Fixed}
            Control_Btn_02  [mesh]  {00_Fixed}
            Control_Btn_03  [mesh]  {00_Fixed}
            Control_Display  [mesh]  {00_Fixed}
            Control_Panel  [mesh]  {00_Fixed}
            Entry_LED_L  [mesh]  {00_Fixed}
            Entry_LED_R  [mesh]  {00_Fixed}
            Frame_Rail_L  [mesh]  {00_Fixed}
            Frame_Rail_R  [mesh]  {00_Fixed}
            PIVOT_Lift_Z  [empty]  {01_Lifting}
                PIVOT_Platform  [empty]  {01_Lifting}
                    Belt_Idler_Axle  [mesh]  {02_Push}
                    Belt_Idler_Bracket  [mesh]  {02_Push}
                    Belt_Mechanism_Lower  [mesh]  {02_Push}
                    Belt_Mechanism_Upper  [mesh]  {02_Push}
                    Belt_Pulley_Drive  [mesh]  {02_Push}
                    Belt_Pulley_Drive_Tooth_01  [mesh]  {02_Push}
                    Belt_Pulley_Drive_Tooth_02  [mesh]  {02_Push}
                    Belt_Pulley_Drive_Tooth_03  [mesh]  {02_Push}
                    Belt_Pulley_Drive_Tooth_04  [mesh]  {02_Push}
                    Belt_Pulley_Drive_Tooth_05  [mesh]  {02_Push}
                    Belt_Pulley_Drive_Tooth_06  [mesh]  {02_Push}
                    Belt_Pulley_Drive_Tooth_07  [mesh]  {02_Push}
                    Belt_Pulley_Drive_Tooth_08  [mesh]  {02_Push}
                    Belt_Pulley_Drive_Tooth_09  [mesh]  {02_Push}
                    Belt_Pulley_Drive_Tooth_10  [mesh]  {02_Push}
                    Belt_Pulley_Drive_Tooth_11  [mesh]  {02_Push}
                    Belt_Pulley_Drive_Tooth_12  [mesh]  {02_Push}
                    Belt_Pulley_Drive_Tooth_13  [mesh]  {02_Push}
                    Belt_Pulley_Drive_Tooth_14  [mesh]  {02_Push}
                    Belt_Pulley_Idler  [mesh]  {02_Push}
                    Belt_Tensioner  [mesh]  {02_Push}
                    Belt_Tensioner_Screw  [mesh]  {02_Push}
                    Guide_Slider_Right  [mesh]  {01_Lifting}
                    Limit_Switch_Body  [mesh]  {02_Push}
                    Limit_Switch_Bracket  [mesh]  {02_Push}
                    Main_Platform  [mesh]  {01_Lifting}
                    Motor_Y_Bracket  [mesh]  {02_Push}
                    Motor_Y_MountArm  [mesh]  {02_Push}
                    PIVOT_Push_Y  [empty]  {02_Push}
                        Limit_Adjuster  [mesh]  {02_Push}
                        Limit_Adjuster_Slot  [mesh]  {02_Push}
                        Limit_Flag  [mesh]  {02_Push}
                        Limit_L_Bracket  [mesh]  {02_Push}
                        Limit_ThumbScrew  [mesh]  {02_Push}
                        Motor_Y_42_Body  [mesh]  {02_Push}
                        Motor_Y_42_FacePlate  [mesh]  {02_Push}
                        Motor_Y_42_Shaft  [mesh]  {02_Push}
                        Push_Belt_Clamp  [mesh]  {02_Push}
                        Push_Carriage  [mesh]  {02_Push}
                        Push_Carriage_Arm  [mesh]  {02_Push}
                        Push_Carriage_Arm_L  [mesh]  {02_Push}
                        Push_Plate  [mesh]  {02_Push}
                        Push_Plate_Face  [mesh]  {02_Push}
                        Push_Plate_RibL  [mesh]  {02_Push}
                        Push_Plate_RibR  [mesh]  {02_Push}
                        Push_Rod_1  [mesh]  {02_Push}
                        Push_Rod_2  [mesh]  {02_Push}
                        Push_Upright  [mesh]  {02_Push}
                        Wedge_Block  [mesh]  {02_Push}
                        Wedge_Block_Rib  [mesh]  {02_Push}
                        Wedge_Block_Screw  [mesh]  {02_Push}
                    Platform_CarriageArm  [mesh]  {01_Lifting}
                    Platform_CarriageGusset_1  [mesh]  {01_Lifting}
                    Platform_CarriageGusset_2  [mesh]  {01_Lifting}
                    Platform_RailArm  [mesh]  {01_Lifting}
                    Platform_Rib_01  [mesh]  {01_Lifting}
                    Platform_Rib_02  [mesh]  {01_Lifting}
                    Platform_Rib_03  [mesh]  {01_Lifting}
                    Platform_Rib_04  [mesh]  {01_Lifting}
                    Platform_Rib_05  [mesh]  {01_Lifting}
                    Platform_SideGuardL  [mesh]  {01_Lifting}
                    Platform_SideGuardR  [mesh]  {01_Lifting}
                    Platform_Slider_Bridge  [mesh]  {01_Lifting}
                    Platform_StopFront  [mesh]  {01_Lifting}
                    Push_Stop_Block  [mesh]  {02_Push}
                    Slide_Block_Z  [mesh]  {01_Lifting}
                    Slide_Block_Z_Bolt_1  [mesh]  {01_Lifting}
                    Slide_Block_Z_Bolt_2  [mesh]  {01_Lifting}
                    Slide_Block_Z_Bolt_3  [mesh]  {01_Lifting}
                    Slide_Block_Z_Bolt_4  [mesh]  {01_Lifting}
                    Slide_Block_Z_Nut  [mesh]  {01_Lifting}
            Shelf_Level1_BeamB  [mesh]  {00_Fixed}
            Shelf_Level1_Board  [mesh]  {00_Fixed}
            Shelf_Level1_LipF  [mesh]  {00_Fixed}
            Shelf_Level1_SideL  [mesh]  {00_Fixed}
            Shelf_Level1_SideR  [mesh]  {00_Fixed}
            Shelf_Level2_BeamB  [mesh]  {00_Fixed}
            Shelf_Level2_Board  [mesh]  {00_Fixed}
            Shelf_Level2_LipF  [mesh]  {00_Fixed}
            Shelf_Level2_SideL  [mesh]  {00_Fixed}
            Shelf_Level2_SideR  [mesh]  {00_Fixed}
            Shelf_Level3_BeamB  [mesh]  {00_Fixed}
            Shelf_Level3_Board  [mesh]  {00_Fixed}
            Shelf_Level3_LipF  [mesh]  {00_Fixed}
            Shelf_Level3_SideL  [mesh]  {00_Fixed}
            Shelf_Level3_SideR  [mesh]  {00_Fixed}
            Top_Entry_ChuteB  [mesh]  {00_Fixed}
            Top_Entry_ChuteL  [mesh]  {00_Fixed}
            Top_Entry_ChuteR  [mesh]  {00_Fixed}
            Top_Entry_WallB  [mesh]  {00_Fixed}
            Top_Entry_WallF  [mesh]  {00_Fixed}
            Top_Entry_WallL  [mesh]  {00_Fixed}
            Top_Entry_WallR  [mesh]  {00_Fixed}
COL-01_Lifting  (直接成员 119)
    Coupling_Z  [mesh]  {01_Lifting}
    Coupling_Z_Clamp1  [mesh]  {01_Lifting}
    Coupling_Z_Clamp2  [mesh]  {01_Lifting}
    Guide_Rail_Bracket_1  [mesh]  {01_Lifting}
    Guide_Rail_Bracket_2  [mesh]  {01_Lifting}
    Guide_Rail_Right  [mesh]  {01_Lifting}
    Guide_Rail_Tick_01  [mesh]  {01_Lifting}
    Guide_Rail_Tick_02  [mesh]  {01_Lifting}
    Guide_Rail_Tick_03  [mesh]  {01_Lifting}
    Guide_Rail_Tick_04  [mesh]  {01_Lifting}
    Guide_Rail_Tick_05  [mesh]  {01_Lifting}
    Guide_Rail_Tick_06  [mesh]  {01_Lifting}
    Guide_Rail_Tick_07  [mesh]  {01_Lifting}
    Guide_Rail_Tick_08  [mesh]  {01_Lifting}
    Guide_Rail_Tick_09  [mesh]  {01_Lifting}
    Guide_Rail_Tick_10  [mesh]  {01_Lifting}
    Guide_Rail_Tick_11  [mesh]  {01_Lifting}
    Motor_Z_42_Body  [mesh]  {01_Lifting}
    Motor_Z_42_FacePlate  [mesh]  {01_Lifting}
    Motor_Z_42_Shaft  [mesh]  {01_Lifting}
    Motor_Z_Bracket  [mesh]  {01_Lifting}
    Motor_Z_Screw_1  [mesh]  {01_Lifting}
    Motor_Z_Screw_2  [mesh]  {01_Lifting}
    Motor_Z_Screw_3  [mesh]  {01_Lifting}
    Motor_Z_Screw_4  [mesh]  {01_Lifting}
    Screw_Z  [mesh]  {01_Lifting}
    Screw_Z_Thread_001  [mesh]  {01_Lifting}
    Screw_Z_Thread_002  [mesh]  {01_Lifting}
    Screw_Z_Thread_003  [mesh]  {01_Lifting}
    Screw_Z_Thread_004  [mesh]  {01_Lifting}
    Screw_Z_Thread_005  [mesh]  {01_Lifting}
    Screw_Z_Thread_006  [mesh]  {01_Lifting}
    Screw_Z_Thread_007  [mesh]  {01_Lifting}
    Screw_Z_Thread_008  [mesh]  {01_Lifting}
    Screw_Z_Thread_009  [mesh]  {01_Lifting}
    Screw_Z_Thread_010  [mesh]  {01_Lifting}
    Screw_Z_Thread_011  [mesh]  {01_Lifting}
    Screw_Z_Thread_012  [mesh]  {01_Lifting}
    Screw_Z_Thread_013  [mesh]  {01_Lifting}
    Screw_Z_Thread_014  [mesh]  {01_Lifting}
    Screw_Z_Thread_015  [mesh]  {01_Lifting}
    Screw_Z_Thread_016  [mesh]  {01_Lifting}
    Screw_Z_Thread_017  [mesh]  {01_Lifting}
    Screw_Z_Thread_018  [mesh]  {01_Lifting}
    Screw_Z_Thread_019  [mesh]  {01_Lifting}
    Screw_Z_Thread_020  [mesh]  {01_Lifting}
    Screw_Z_Thread_021  [mesh]  {01_Lifting}
    Screw_Z_Thread_022  [mesh]  {01_Lifting}
    Screw_Z_Thread_023  [mesh]  {01_Lifting}
    Screw_Z_Thread_024  [mesh]  {01_Lifting}
    Screw_Z_Thread_025  [mesh]  {01_Lifting}
    Screw_Z_Thread_026  [mesh]  {01_Lifting}
    Screw_Z_Thread_027  [mesh]  {01_Lifting}
    Screw_Z_Thread_028  [mesh]  {01_Lifting}
    Screw_Z_Thread_029  [mesh]  {01_Lifting}
    Screw_Z_Thread_030  [mesh]  {01_Lifting}
    Screw_Z_Thread_031  [mesh]  {01_Lifting}
    Screw_Z_Thread_032  [mesh]  {01_Lifting}
    Screw_Z_Thread_033  [mesh]  {01_Lifting}
    Screw_Z_Thread_034  [mesh]  {01_Lifting}
    Screw_Z_Thread_035  [mesh]  {01_Lifting}
    Screw_Z_Thread_036  [mesh]  {01_Lifting}
    Screw_Z_Thread_037  [mesh]  {01_Lifting}
    Screw_Z_Thread_038  [mesh]  {01_Lifting}
    Screw_Z_Thread_039  [mesh]  {01_Lifting}
    Screw_Z_Thread_040  [mesh]  {01_Lifting}
    Screw_Z_Thread_041  [mesh]  {01_Lifting}
    Screw_Z_Thread_042  [mesh]  {01_Lifting}
    Screw_Z_Thread_043  [mesh]  {01_Lifting}
    Screw_Z_Thread_044  [mesh]  {01_Lifting}
    Screw_Z_Thread_045  [mesh]  {01_Lifting}
    Screw_Z_Thread_046  [mesh]  {01_Lifting}
    Screw_Z_Thread_047  [mesh]  {01_Lifting}
    Screw_Z_Thread_048  [mesh]  {01_Lifting}
    Screw_Z_Thread_049  [mesh]  {01_Lifting}
    Screw_Z_Thread_050  [mesh]  {01_Lifting}
    Screw_Z_Thread_051  [mesh]  {01_Lifting}
    Screw_Z_Thread_052  [mesh]  {01_Lifting}
    Screw_Z_Thread_053  [mesh]  {01_Lifting}
    Screw_Z_Thread_054  [mesh]  {01_Lifting}
    Screw_Z_Thread_055  [mesh]  {01_Lifting}
    Screw_Z_Thread_056  [mesh]  {01_Lifting}
    Screw_Z_Thread_057  [mesh]  {01_Lifting}
    Screw_Z_Thread_058  [mesh]  {01_Lifting}
    Screw_Z_Thread_059  [mesh]  {01_Lifting}
    Screw_Z_Thread_060  [mesh]  {01_Lifting}
    Screw_Z_Thread_061  [mesh]  {01_Lifting}
    Screw_Z_Thread_062  [mesh]  {01_Lifting}
    Screw_Z_Thread_063  [mesh]  {01_Lifting}
    Screw_Z_Thread_064  [mesh]  {01_Lifting}
    Screw_Z_Thread_065  [mesh]  {01_Lifting}
    Screw_Z_Thread_066  [mesh]  {01_Lifting}
    Screw_Z_Thread_067  [mesh]  {01_Lifting}
    Screw_Z_Thread_068  [mesh]  {01_Lifting}
    Screw_Z_TopBearing  [mesh]  {01_Lifting}
    Screw_Z_TopBearingBore  [mesh]  {01_Lifting}
COL-02_Push  (直接成员 50)
COL-03_Electronics  (直接成员 28)
    Cable_Clip_1  [mesh]  {03_Electronics}
    Cable_Clip_2  [mesh]  {03_Electronics}
    Cable_Clip_3  [mesh]  {03_Electronics}
    Cable_Group  [curve]  {03_Electronics}
    Camera_Top_Body  [mesh]  {03_Electronics}
    Camera_Top_Cover  [mesh]  {03_Electronics}
    Camera_Top_Lens  [mesh]  {03_Electronics}
    Camera_Top_LensRing  [mesh]  {03_Electronics}
    Camera_Top_Mount1  [mesh]  {03_Electronics}
    Camera_Top_Mount2  [mesh]  {03_Electronics}
    Controller_Board  [mesh]  {03_Electronics}
    Controller_Board_MCU  [mesh]  {03_Electronics}
    Controller_Board_Stand_11  [mesh]  {03_Electronics}
    Controller_Board_Stand_12  [mesh]  {03_Electronics}
    Controller_Board_Stand_21  [mesh]  {03_Electronics}
    Controller_Board_Stand_22  [mesh]  {03_Electronics}
    Controller_Board_Term_1  [mesh]  {03_Electronics}
    Controller_Board_Term_2  [mesh]  {03_Electronics}
    Controller_Board_Term_3  [mesh]  {03_Electronics}
    Controller_Board_Term_4  [mesh]  {03_Electronics}
    Controller_Board_Term_5  [mesh]  {03_Electronics}
    Controller_Heatsink  [mesh]  {03_Electronics}
    Driver_Z_Board  [mesh]  {03_Electronics}
    Driver_Z_Chip  [mesh]  {03_Electronics}
    Ultrasonic_Top_Body  [mesh]  {03_Electronics}
    Ultrasonic_Top_Cup_1  [mesh]  {03_Electronics}
    Ultrasonic_Top_Cup_2  [mesh]  {03_Electronics}
    Ultrasonic_Top_Mount  [mesh]  {03_Electronics}
COL-04_Books  (直接成员 57)
```
