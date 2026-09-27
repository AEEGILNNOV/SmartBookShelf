# 速定位书格 — 智能图书存取系统 · Blender 机械结构概念模型

依据手绘设计草图（正面/中层、俯视图、左视图、三层书架、同步带/丝杆/滑块/平台/斜块/限位/摄像头/超声波）
建立的可参数化机械概念模型。**不含任何动画**，整机置于「中层取书瞬间」的静态工作姿态。

## 1. 快速开始

```powershell
# 生成模型 + 渲染 1920x1080 预览图 + 导出 GLB（约 2 分钟）
& "D:\Application\3D\Blender\blender.exe" --background --factory-startup `
  --python "MechanicalConcept\scripts\build_smart_bookshelf.py"

# 快速迭代（960x540 / 32 采样，再自动补一张正式图）
... --python build_smart_bookshelf.py -- fast

# 只生成模型与文档，不渲染
... --python build_smart_bookshelf.py -- norender noglb
```

也可在 Blender 图形界面中打开 `scripts/build_smart_bookshelf.py` 直接运行脚本。

## 2. 输出物

| 文件 | 说明 |
| --- | --- |
| `models/smart_bookshelf_v01.blend` | 可编辑工程源文件（含相机 / 三点布光 / 摄影棚） |
| `models/smart_bookshelf_v01.glb` | 仅含机械本体的便携模型（已排除地面/背板/相机/灯光） |
| `renders/smart_bookshelf_preview_1920x1080.png` | 最终静态预览图（EEVEE，128 采样） |
| `docs/structure_and_parameters.md` | 结构树 + 完整参数表 + 机构说明（Markdown） |
| `docs/structure_tree_and_parameters.txt` | 结构树 + 参数表（纯文本，脚本运行时同步输出到控制台） |
| `scripts/` | 模块化构建脚本 |

## 3. 坐标与单位

- 单位系统：**米，Unit Scale = 0.001**，即 1 个 Blender 单位 = 1 mm
- **X** = 宽度方向（-X 左 / +X 右），0 = 柜体左右中心
- **Y** = 深度方向（-Y 后 / +Y 前），0 = 柜体前后中心
- **Z** = 高度方向（+Z 上），0 = **整机底部中心**

## 4. 整机结构

```
SmartBookshelf_Root
└ EMPTY_Cabinet                    柜体（固定）
  ├ Cabinet_* / Shelf_Level1~3 / Top_Entry_* / Control_* / Book_*
  └ PIVOT_Lift_Z                   垂直升降轴（Z 轴）
    └ PIVOT_Platform               主平台（上表面 480 mm = 中层书格）
      └ PIVOT_Push_Y               水平推书轴（Y 轴）
```

| 轴 | 机构 | 关键零件 |
| --- | --- | --- |
| **Z 垂直升降** | 42 步进电机 → 刚性联轴器 → 丝杆滑台 → 滑块 → 主平台 | `Motor_Z_42` `Coupling_Z` `Screw_Z` `Slide_Block_Z` `Main_Platform` |
| **Y 水平推书** | 42 步进电机 → GT2 同步带 → 推板小车 → 推板（+ 楔形斜块） | `Motor_Y_42` `Belt_Mechanism` `Push_Plate` `Wedge_Block` |
| **导向 / 抗倾覆** | 右侧垂直导轨 + 预载滑块，与丝杆构成双点支撑，抑制悬臂变形 | `Guide_Rail_Right` `Guide_Slider_Right` |
| **书厚适应** | 楔形斜块 + 独立微调限位挡板（腰形槽 + 滚花螺钉） | `Wedge_Block` `Limit_Adjuster` |
| **识别 / 测厚** | 顶置摄像头（朝下对准进书口）+ 斜置超声波模块 | `Camera_Top_*` `Ultrasonic_Top_*` |

> **动力源统一为 42 步进电机（42 × 42 × 38 mm，轴径 8 mm）**。
> 未使用 28BYJ-48 / ULN2003，未使用齿轮齿条。

## 5. 集合组织

| 集合 | 内容 |
| --- | --- |
| `00_Fixed` | 柜体、三层书格、顶部进书口、控制面板、框架 |
| `01_Lifting` | Z 轴电机、联轴器、丝杆、滑块、主平台、右侧导轨 |
| `02_Push` | Y 轴电机、同步带与带轮、推板小车、推板、斜块、限位调节器 |
| `03_Electronics` | 顶置摄像头、超声波模块、主控板、驱动板、线缆束 |
| `04_Books` | 三层书格书籍 + 平台待存书 |

## 6. 场景 / 渲染

| 项 | 值 |
| --- | --- |
| 引擎 | BLENDER_EEVEE（光线追踪 + AO + 软阴影） |
| 分辨率 / 采样 | 1920 × 1080 / 128 |
| 色彩管理 | Standard，exposure 0.30 |
| 相机 | 58 mm，方位角 30°，仰角 24°，距离 4204 mm（3/4 俯视） |
| 光照 | 三点布光 Key(暖) + Fill(冷) + Rim + Top 柔光箱 + 内部补光 |
| 背景 | 渐变环境世界 + 摄影棚背板 + 地面（仅用于渲染，不进入 GLB） |
| 动画 | **无** — 无关键帧、无时间轴、无动画导出 |

## 7. 脚本模块

| 文件 | 职责 |
| --- | --- |
| `scripts/01_materials.py` | 材质库（38 个 Principled 材质，跨 4.x/5.x 插槽兼容） |
| `scripts/_lib02.py` | 参数表 P、场景设置、几何工具、柜体/书格/升降/推书/电控/书籍 |
| `scripts/_lib03.py` | 相机自动构图、三点布光、渲染设置、存档与导出 |
| `scripts/build_smart_bookshelf.py` | 主入口：按 [S1]–[S11] 顺序编排全流程 |
| `scripts/diag_views.py` | 诊断多视角渲染（结构核查） |
| `scripts/cam_candidates.py` | 相机角度候选对比 |
| `scripts/mat_tune.py` | 材质快速微调 + 渲染 |
| `scripts/validate_glb.py` | GLB 回读校验（几何 / 材质 / 包围盒 / 关键零件） |
| `scripts/export_clean_glb.py` | 导出仅含机械本体的干净 GLB |
| `scripts/fix_viewport_clip.py` | 修正视口 3D 裁剪距离（解决拉远后"黑屏"）并自动取景 |

## 8. 书籍摆放

书籍统一**平躺**放置（书脊朝柜体前方 +Y），每层自层板向上叠放一摞：

| 方向 | 含义 | 尺寸 |
| --- | --- | --- |
| X | 书高（书脊长度） | 180–240 mm |
| Y | 书宽（书脊 → 书口） | 160 mm |
| Z | 书厚（叠放方向） | 20–50 mm |

每层叠放高度上限由 `BOOK_STACK_H`（245 mm）控制，中层为平台工作位，
其书堆整体后移（`BOOK_Y0` → `BOOK_Y1`）以便让出推书机构与平台书的插入空间。

> 修改 `P['PUSH_STROKE_STATIC']` 可切换展示姿态：
> `0` = 推送起点（书在平台上）、`70` = 推送中段、`155` = 推入书格完成。

## 9. 视口使用提示（重要）

本模型 **1 Blender 单位 = 1 mm**，整机高约 1318 单位，而 Blender 视口默认
`clip_end = 1000`。若直接拉远视图，模型会被远裁剪面切掉而**看起来像黑屏 / 消失**。

`fix_viewport_clip.py` 已把视口裁剪范围调整为 `clip_start = 1`、`clip_end = 659000`，
且 `build_smart_bookshelf.py` 会在**保存 .blend 之前自动调用同一修正**，
因此重新生成模型后也不会再出现拉远即"黑屏"的情况。

若需单独修复某个已有 .blend，可执行：

```powershell
& "D:\Application\3D\Blender\blender.exe" --background --factory-startup `
  --python "MechanicalConcept\scripts\fix_viewport_clip.py" `
  -- "MechanicalConcept\models\smart_bookshelf_v01.blend"
```

手动方式：`视图 → 侧栏(N) → 视图 → 裁剪起点 / 裁剪终点`，把**裁剪终点**设大
（例如 100000 mm）即可。

## 10. 参数化调整

所有尺寸集中在 `_lib02.py` 的字典 `P` 中，直接修改即可重新生成：

```python
P['CAB_W'] = 500.0      # 柜体宽
P['SHELF_Z'] = [180.0, 480.0, 780.0]   # 三层书格上表面高度
P['SCREW_Z0'], P['SCREW_Z1'] = 95.0, 855.0   # 丝杆上下端
P['PUSH_STROKE_STATIC'] = 70.0               # 推书机构静态姿态（0 / 70 / 155）
P['BOOK_STACK_H'] = 245.0                    # 每层书堆高度上限
```

静态姿态约定：`PLATFORM_TOP_Z` 与 `SHELF_Z[1]` 均为 480 mm，使主平台精确对准中层书格；
`PIVOT_Push_Y` 沿 -Y 预置 70 mm，呈现推书机构的工作行程状态。

## 11. 校验结果

| 项 | 结果 | 
| --- | --- |
| 网格对象 | 299 个（含曲线转网格），面数 3405；应用倒角后 GLB 为 15864 面 |
| 材质 | 38 个创建，GLB 内 32 个 |
| 整机包围盒 | 500 × 661 × 1318 mm（含前方工作平台与顶部传感头） |
| GLB 回读 | 通过：包围盒一致，关键零件（双 42 电机、丝杆、同步带、推板、斜块、导轨、摄像头、超声波）全部在位 |
| 视口裁剪 | 已修正并持久化：`clip_start = 1`，`clip_end = 659000` |
| 视觉校验 | 已渲染 6 个诊断视角 + 4 个相机候选 + 最终 1920×1080 预览图并逐张目视核查 |
