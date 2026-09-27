# 结构树与参数表

> 由 `scripts/build.py` 运行时自动同步生成，
> 同步文本见 `docs/structure_tree_and_parameters.txt`。

## 1. 结构树

```
SmartBookshelf_ElevatorClamp_Root        (原点 = 底部中心, 1 单位 = 1 mm)
├─ COL-01_Cabinet                    [cabinet]  31 objects
│   ├─ Frame_Post_{LB,LF,RB,RF}                20x20 框架立柱 ×4
│   ├─ Frame_Rail_{Bottom,Top}_{Back,Front,Left,Right}   框架横梁 ×8
│   ├─ Shell_Side_Left / Shell_Side_Right       半透明亚克力侧板
│   ├─ Shell_Base / Shell_Back                  亚克力底板 / 背板
│   ├─ Front_Level_Bottom / Front_Level_Top     正面三层板（上、下固定层）
│   ├─ Front_Port_Jamb_Left/Right, Front_Port_Sill   推书口两侧与下沿
│   ├─ Top_Plate_{Rear,Front,Left,Right}        顶板（中间留出放书孔）
│   ├─ Top_Hole_Bezel_{Left,Right,Rear,Front}   放书孔 250x80 铝制围边
│   └─ Control_Panel / Control_Screen           顶部操作面板
├─ COL-02_DriveColumns               [drive]    16 objects
│   ├─ Drive_Screw_Left / Drive_Screw_Right     丝杆 900 mm（Ø12）
│   ├─ Drive_Screw_Base_Left / _Right           丝杆下端轴承座
│   ├─ Drive_Rail_Left / Drive_Rail_Right       从动线轨 900 mm（20x20）
│   ├─ Drive_Rail_Foot_Left / _Right            线轨地脚
│   ├─ Drive_Motor42_{Left,Right}_Body          底部 42 步进电机（垂直）
│   ├─ Drive_Motor42_{Left,Right}_Cap / _Shaft  电机端盖 / 输出轴
│   └─ Drive_Coupling_Left / _Right             刚性联轴器
├─ COL-03_MovingPlatform             [platform] 12 objects
│   ├─ Platform_Plate_Main / _RearLeft / _RearRight   L 型托板（后侧中部留推书槽）
│   ├─ Platform_Lip_RearLeft / _RearRight / _Left     10 mm 金属挡边
│   ├─ Platform_Bracket_Left / _Right            联结左右滑块的支架
│   └─ Platform_Carriage_{Left,Right}(+_Nut)     80x80 滑块（左主动 / 右从动）
├─ COL-04_LimitPlate                 [limit]     8 objects
│   ├─ Limit_Pad_Black                          3 mm 黑色橡胶垫
│   ├─ Limit_Plate_Body                         限位板本体
│   ├─ Limit_Bracket_Left / _Right               联结左右滑块的支架
│   └─ Limit_Carriage_{Left,Right}(+_Nut)        80x80 滑块（右主动 / 左从动）
├─ COL-05_RearPushUnit               [push]      8 objects
│   ├─ Push_Motor_Mount                         电机安装法兰（后壁内表面）
│   ├─ Push_Motor42_Body / _Cap                  42 步进电机（水平，Z = 600）
│   ├─ Push_Coupling                            联轴器
│   ├─ Push_Rod / Push_Rod_LeadScrew             推杆（露出 80 mm）
│   ├─ Push_Plate_Hub                           推书板座
│   └─ Push_Plate                               推书板 150 x 80 x 5
├─ COL-06_Book                       [book]      4 objects
│   ├─ Book_Cover_Bottom / Book_Cover_Top        封面
│   ├─ Book_Spine                               书脊（朝后，贴推书板）
│   └─ Book_Pages                               书页（朝前）
├─ COL-07_TakeOffChute               [chute]     3 objects
│   ├─ Chute_Ramp                               30° 取书滑道 280 x 200
│   └─ Chute_Wall_Left / Chute_Wall_Right        滑道两侧挡边
└─ COL-90_RenderStage                [stage]     摄影棚（仅渲染，不导出 GLB）
    ├─ Stage_Floor / Stage_Backdrop
    ├─ LGT_Key / LGT_Fill / LGT_Rim               三点光
    ├─ LGT_TopSoftbox / LGT_Bounce_Front
    ├─ LGT_Interior_Fill / LGT_Interior_Low       柜内补光
    └─ CAM_Preview_3Q                             58 mm 相机
```

合计 **82 个网格对象**。

## 2. 关键参数表（节选）

| 参数 | 值 (mm) | 说明 |
| --- | --- | --- |
| `CAB_W / CAB_D / CAB_H` | 600 / 450 / 1200 | 柜体外形 |
| `FRAME_T` | 20 | 框架 / 板厚 |
| `PORT_Z0 / PORT_Z1` | 540 / 660 | 正面中层全开口（推书口）高度区间 |
| `PORT_CZ` | 600 | 推书口中心高度（固定） |
| `TOP_HOLE_W / TOP_HOLE_D` | 250 / 80 | 顶部放书孔 |
| `COL_X` | 255 | 丝杆 / 线轨立柱中心偏置 |
| `SCREW_Y / RAIL_Y` | −175 / −105 | 丝杆与从动线轨的深度位置 |
| `SCREW_LEN / SCREW_D` | 900 / 12 | 丝杆长度与直径 |
| `RAIL_LEN / RAIL_SQ` | 900 / 20 | 线轨长度与方截面 |
| `MOTOR_L / MOTOR_W / MOTOR_H` | 42.4 / 42.4 / 38 | 42 步进电机外形 |
| `COUPLING_H` | 32 | 联轴器高度（电机 + 联轴器 = 70） |
| `SLIDER_H / SLIDER_T / SLIDER_D` | 80 / 24 / 56 | 80×80 滑块 |
| `PLATFORM_SLIDER_Z` | 480 | 左（平台）主动滑块高度 |
| `LIMIT_SLIDER_Z` | 640 | 右（限位板）主动滑块高度 → 错开 160 |
| `PLAT_TOP_Z` | 580 | 平台顶面（冻结姿态） |
| `PLAT_X0/X1, PLAT_Y0/Y1` | −212/212, −62/165 | 平台托板范围 |
| `PLAT_FLANGE_H / _T` | 10 / 10 | L 型挡边高度 / 厚度 |
| `PLAT_SLOT_X / _Y0 / _Y1` | 88 / −62 / −46 | 后侧推书槽（推书板通道） |
| `PLAT_MIN_Z` | 150 | 平台组件最低极限（距电机 60 余量） |
| `LIM_X0/X1, LIM_Y0/Y1` | −132/132, −70/180 | 限位板范围 |
| `LIM_PAD_T` | 3 | 黑色橡胶垫厚度 |
| `LIM_PAD_BOTTOM_Z` | 642 | 橡胶垫底面（提示词 640 + 2 mm 间隙修正） |
| `PUSH_PLATE_W/H/T` | 150 / 80 / 5 | 推书板 |
| `PUSH_CZ` | 600 | 推杆轴心高度 |
| `PUSH_STROKE` | 80 | 推杆露出长度 |
| `PUSH_ROD_D` | 12 | 推杆直径 |
| `BOOK_W/D/T` | 150 / 210 / 40 | 书本（书脊朝后） |
| `BOOK_SPINE_Y` | −50 | 书脊平面 |
| `CHUTE_W / CHUTE_LEN / CHUTE_ANGLE` | 280 / 200 / 30° | 取书滑道 |
| `MIN_CLEARANCE` | 2 | 全局最小安全间隙 |

## 3. 材质

| 材质 | 用途 | 关键参数 |
| --- | --- | --- |
| `MAT-Acrylic_Shell` | 柜体亚克力壳（侧 / 底 / 背 / 正面三层 / 顶板） | 透光率 40%，菲涅尔 Alpha（正视 0.025，掠射 0.34） |
| `MAT-Aluminium_Frame` | 柜体框架 | 金属 0.70，粗糙 0.42 |
| `MAT-Aluminium` | 平台托板、推书板座、联轴器、围边 | 金属 0.95，粗糙 0.30 |
| `MAT-Aluminium_Dark` | 滑块、限位板、支架 | 金属 0.88，粗糙 0.42 |
| `MAT-Steel_Screw` / `MAT-Rail_Steel` | 丝杆 / 线轨（铝合金银色） | 金属 1.0 |
| `MAT-Motor_Black` / `MAT-Motor_EndCap` / `MAT-Motor_Shaft` | 42 电机黑色主体 + 银色轴 | — |
| `MAT-Acrylic_White` | 推书板（提示词允许透明或白色） | Alpha 0.78 |
| `MAT-Rubber_Black` | 限位板橡胶垫 | 粗糙 0.92 |
| `MAT-Book_Cover_DarkRed` / `MAT-Book_Pages` | 书本封面 / 书页 | 暗红 + 米白 |

## 4. 渲染设置

（`renders/diag/` 下另有 4 张正交诊断图，仅用于结构核查，不计入交付预览。）

| 项 | 值 |
| --- | --- |
| 引擎 | Cycles（CPU），160 采样 + OpenImageDenoise |
| 分辨率 | 1920 × 1080 |
| 相机 | 58 mm，方位角 40°，仰角 30°，自动取景（占画幅 95%） |
| 光照 | 三点光（Key 暖 / Fill 冷 / Rim）+ 顶部柔光箱 + 柜内双层补光 |
| 色彩管理 | AgX（不可用时回退 Filmic / Standard），曝光 0 |
| 亚克力 | 面板不投影（`visible_shadow = False`），保证柜内机构可见 |
| 动画 | **无** |
| 视口裁剪 | `clip_start = 1` / `clip_end = 300000`（已在保存前写入，见 README 第 9 节） |
