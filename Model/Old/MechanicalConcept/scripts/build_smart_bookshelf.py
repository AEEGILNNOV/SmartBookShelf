# -*- coding: utf-8 -*-
"""
============================================================================
 速定位书格 — 智能图书存取系统 · Blender 机械结构概念模型
 SmartBookshelf — mechanical concept model builder (Blender 4.3+ / 5.x)

 运行方式（无界面，推荐）:
     blender --background --factory-startup --python build_smart_bookshelf.py

 运行方式（图形界面，可交互查看）:
     在 Blender 中打开脚本编辑器 → 打开本文件 → 运行脚本

 输出:
     <workspace>/MechanicalConcept/models/smart_bookshelf_v01.blend
     <workspace>/MechanicalConcept/models/smart_bookshelf_v01.glb
     <workspace>/MechanicalConcept/renders/smart_bookshelf_preview_1920x1080.png
     <workspace>/MechanicalConcept/docs/structure_and_parameters.md

 结构（模块化）:
     [S1] 场景设置      setup_units_scene
     [S2] 参数表        P / PARAM_TABLE
     [S3] 材质          _lib01.build_materials
     [S4] 柜体 + 进书口  _lib02.build_cabinet
     [S5] 三层书架      _lib02.build_shelves
     [S6] Z轴升降       _lib02.build_lift   (42电机 + 联轴器 + 丝杆 + 滑块 + 平台 + 导轨)
     [S7] Y轴推书       _lib02.build_push   (42电机 + 同步带 + 推板 + 斜块 + 限位)
     [S8] 电控/传感     _lib02.build_electronics
     [S9] 书籍          _lib02.build_books
     [S10] 相机/灯光/渲染 _lib03
     [S11] 存档/导出     _lib03

 说明：不含任何关键帧、时间轴或动画导出。
============================================================================
"""

import bpy
import os
import sys
import importlib

# ---------------------------------------------------------------------------
# 路径与模块加载（兼容直接运行与 _runner 调用）
# ---------------------------------------------------------------------------
HERE = os.path.dirname(os.path.abspath(__file__ if '__file__' in dir() else '.'))
if HERE not in sys.path:
    sys.path.insert(0, HERE)

_lib01 = importlib.import_module('01_materials')
_lib02 = importlib.import_module('_lib02')
_lib03 = importlib.import_module('_lib03')
importlib.reload(_lib01)
importlib.reload(_lib02)
importlib.reload(_lib03)

P = _lib02.P
M = _lib01


# ---------------------------------------------------------------------------
# 工作区根目录（MechanicalConcept 的上一级）
# ---------------------------------------------------------------------------
def workspace_root():
    root = os.path.abspath(os.path.join(HERE, '..', '..'))
    return root


OUT = {
    'blend': os.path.join(HERE, '..', 'models', 'smart_bookshelf_v01.blend'),
    'glb': os.path.join(HERE, '..', 'models', 'smart_bookshelf_v01.glb'),
    'png': os.path.join(HERE, '..', 'renders',
                        'smart_bookshelf_preview_1920x1080.png'),
    'png_test': os.path.join(HERE, '..', 'renders', 'test_preview.png'),
    'doc': os.path.join(HERE, '..', 'docs', 'structure_and_parameters.md'),
    'tree': os.path.join(HERE, '..', 'docs', 'structure_tree_and_parameters.txt'),
}
OUT = {k: os.path.abspath(v) for k, v in OUT.items()}


# ===========================================================================
# 参数表 / 结构树文本输出
# ===========================================================================
PARAM_GROUPS = [
    ('柜体', ['CAB_W', 'CAB_D', 'CAB_H', 'FRAME_T']),
    ('三层书格', ['SHELF_N', 'LEVEL_H', 'SHELF_X0', 'SHELF_X1', 'SHELF_Y0',
                  'SHELF_Y1', 'SHELF_T']),
    ('顶部进书口', ['ENTRY_W', 'ENTRY_D', 'ENTRY_X0', 'ENTRY_X1',
                    'ENTRY_Y0', 'ENTRY_Y1']),
    ('42 步进电机', ['M42_W', 'M42_D', 'M42_H', 'M42_SHAFT']),
    ('Z 轴 丝杆滑台', ['SCREW_D', 'SCREW_Z0', 'SCREW_Z1', 'COUPLING_D',
                       'COUPLING_L', 'NUT_W', 'NUT_H', 'MZ_Z0']),
    ('Z 轴 滑块/平台', ['CARRIAGE_W', 'CARRIAGE_D', 'CARRIAGE_H', 'CARRIAGE_Z0',
                        'PLATFORM_Z0', 'PLATFORM_T', 'PLATFORM_X0', 'PLATFORM_X1',
                        'PLATFORM_Y0', 'PLATFORM_Y1']),
    ('右侧垂直导轨', ['RAIL_X0', 'RAIL_X1', 'RAIL_Y0', 'RAIL_Y1', 'RAIL_Z0',
                      'RAIL_Z1', 'SLIDER_Z0', 'SLIDER_Z1']),
    ('Y 轴 同步带推书', ['BELT_T', 'BELT_W', 'PULLEY_D', 'PULLEY_W', 'PULLEY_YA',
                         'PULLEY_YB', 'BELT_ZC', 'MY_Z0', 'PUSH_X0', 'PUSH_X1',
                         'PUSH_T', 'PUSH_Y0', 'PUSH_Z1', 'PUSH_STROKE_STATIC']),
    ('斜块 / 限位', ['WEDGE_W', 'WEDGE_H', 'WEDGE_T', 'WEDGE_X0', 'WEDGE_Y0',
                     'LIMIT_X', 'LIMIT_Y0']),
    ('传感 / 电控', ['CAM_W', 'CAM_D', 'CAM_H', 'US_W', 'US_D', 'US_H',
                     'PCB_W', 'PCB_D', 'PCB_Z']),
    ('书籍', ['BOOK_T_MIN', 'BOOK_T_MAX', 'BOOK_H_MIN', 'BOOK_H_MAX',
              'BOOK_D', 'BOOK_LABEL_H']),
]

PARAM_NOTES = {
    'LEVEL_H': '每层净高（书格）',
    'FRAME_T': '框架/板材厚度',
    'SHELF_T': '层板厚度',
    'SCREW_Z0': '丝杆下端高度',
    'SCREW_Z1': '丝杆上端高度（丝杆长 = Z1-Z0 = 760）',
    'CARRIAGE_Z0': '滑块底面高度（静态工作位）',
    'PLATFORM_Z0': '主平台板底面高度（上表面 480 = 中层书格）',
    'RAIL_Z0': '导轨下端',
    'RAIL_Z1': '导轨上端（导轨长 = Z1-Z0 = 860）',
    'PULLEY_YA': '主动轮中心 Y',
    'PULLEY_YB': '从动轮中心 Y（中心距 200）',
    'BELT_ZC': '同步带中心高度',
    'MY_Z0': 'Y 轴电机底面高度',
    'PUSH_STROKE_STATIC': '静态工作姿态的推出量（沿 +Y）',
    'PUSH_Z1': '推板上沿 = 平台面 420（相对平台顶面 60）',
    'WEDGE_H': '斜块高度（楔形，适应书厚）',
    'CAM_Y': '顶置摄像头中心 Y（原文缺省）',
    'US_Y': '超声波模块中心 Y（原文缺省）',
}


def param_table_text(width=78):
    lines = []
    lines.append('=' * width)
    lines.append('  参数表 (单位: mm,  模型空间: 1 Blender unit = 1 mm)')
    lines.append('  坐标: X 宽 / Y 深 / Z 高,  原点 = 整机底部中心')
    lines.append('=' * width)
    for group, keys in PARAM_GROUPS:
        lines.append('')
        lines.append('-- %s %s' % (group, '-' * max(0, width - len(group) - 6)))
        for k in keys:
            if k not in P:
                continue
            note = PARAM_NOTES.get(k, '')
            extra = ('  # ' + note) if note else ''
            lines.append('   %-20s = %10.1f%s' % (k, float(P[k]), extra))
    lines.append('')
    lines.append('-- 派生量 %s' % ('-' * (width - 9)))
    lines.append('   %-20s = %10.1f' % ('PLATFORM_TOP_Z', _lib02.PLATFORM_TOP_Z))
    lines.append('   %-20s = %10.1f' % ('PUSH_EXT', _lib02.PUSH_EXT))
    lines.append('   %-20s = %10.1f' % ('PUSH_Y_HOME', _lib02.PUSH_Y_HOME))
    lines.append('   %-20s = %10.1f' % ('CARRIAGE_Z1', P['CARRIAGE_Z0'] + P['CARRIAGE_H']))
    lines.append('   %-20s = %10.1f' % ('SCREW_LEN', P['SCREW_Z1'] - P['SCREW_Z0']))
    lines.append('   %-20s = %10.1f' % ('RAIL_LEN', P['RAIL_Z1'] - P['RAIL_Z0']))
    lines.append('=' * width)
    return '\n'.join(lines)


def markdown_doc(info, counts, tree_lines, bounds):
    L = []
    L.append('# 速定位书格 — 机械结构概念模型')
    L.append('')
    L.append('智能图书存取系统 · Blender 机械概念模型（无动画，静态工作姿态）')
    L.append('')
    L.append('## 1. 坐标与单位')
    L.append('')
    L.append('| 项 | 值 |')
    L.append('| --- | --- |')
    L.append('| 单位系统 | 米，Unit Scale = 0.001（1 Blender 单位 = 1 mm）|')
    L.append('| X 轴 | 宽度方向（-X 左 / +X 右），0 = 柜体左右中心 |')
    L.append('| Y 轴 | 深度方向（-Y 后 / +Y 前），0 = 柜体前后中心 |')
    L.append('| Z 轴 | 高度方向（+Z 上），0 = 整机底部中心 |')
    L.append('| 静止姿态 | 主平台对准**中层书格**，推板处于**推出**状态 |')
    L.append('')
    L.append('## 2. 整机包围盒')
    L.append('')
    L.append('| 方向 | 最小 (mm) | 最大 (mm) | 尺寸 (mm) |')
    L.append('| --- | ---: | ---: | ---: |')
    for i, ax in enumerate('XYZ'):
        L.append('| %s | %.1f | %.1f | %.1f |'
                 % (ax, bounds[0][i], bounds[1][i], bounds[1][i] - bounds[0][i]))
    L.append('')
    L.append('## 3. 参数表（单位 mm）')
    L.append('')
    L.append('| 分组 | 参数 | 值 | 说明 |')
    L.append('| --- | --- | ---: | --- |')
    for group, keys in PARAM_GROUPS:
        for k in keys:
            if k not in P:
                continue
            L.append('| %s | `%s` | %.1f | %s |'
                     % (group, k, float(P[k]), PARAM_NOTES.get(k, '')))
    L.append('')
    L.append('### 派生量')
    L.append('')
    L.append('| 参数 | 值 (mm) | 说明 |')
    L.append('| --- | ---: | --- |')
    L.append('| `PLATFORM_TOP_Z` | %.1f | 主平台上表面高度（= 中层书格 480）|'
             % _lib02.PLATFORM_TOP_Z)
    L.append('| `PUSH_EXT` | %.1f | 推板静态推出量（沿 -Y）|' % _lib02.PUSH_EXT)
    L.append('| `CARRIAGE_Z1` | %.1f | 滑块顶面 |'
             % (P['CARRIAGE_Z0'] + P['CARRIAGE_H']))
    L.append('| `SCREW_LEN` | %.1f | 丝杆长度 |' % (P['SCREW_Z1'] - P['SCREW_Z0']))
    L.append('| `RAIL_LEN` | %.1f | 右侧导轨长度 |' % (P['RAIL_Z1'] - P['RAIL_Z0']))
    L.append('')
    L.append('## 4. 机构说明')
    L.append('')
    L.append('| 轴 | 机构 | 组成 |')
    L.append('| --- | --- | --- |')
    L.append('| Z（垂直升降）| 42 步进电机 + 刚性联轴器 + 丝杆滑台 + 滑块 + 主平台 |'
             ' `Motor_Z_42` `Coupling_Z` `Screw_Z` `Slide_Block_Z` `Main_Platform` |')
    L.append('| Y（水平推书）| 42 步进电机 + GT2 同步带 + 推板小车 + 推板 + 斜块 |'
             ' `Motor_Y_42` `Belt_Mechanism` `Push_Plate` `Wedge_Block` |')
    L.append('| 导向/抗倾覆 | 右侧垂直导轨 + 预载滑块，与丝杆构成双点支撑 |'
             ' `Guide_Rail_Right` `Guide_Slider_Right` |')
    L.append('| 书厚适应 | 楔形斜块 + 独立微调限位挡板（腰形槽 + 滚花螺钉）|'
             ' `Wedge_Block` `Limit_Adjuster` |')
    L.append('| 识别/测厚 | 顶置摄像头 + 超声波模块 |'
             ' `Camera_Top` `Ultrasonic_Top` |')
    L.append('')
    L.append('> 动力源统一为 **42 步进电机 (42 × 42 × 38 mm, 轴径 8 mm)**；'
             '未使用 28BYJ-48 与 ULN2003，未使用齿轮齿条。')
    L.append('')
    L.append('## 5. 书籍统计')
    L.append('')
    L.append('| 位置 | 数量 |')
    L.append('| --- | ---: |')
    for k in sorted(counts):
        L.append('| %s | %d |' % (k, counts[k]))
    L.append('| **合计** | **%d** |' % sum(counts.values()))
    L.append('')
    L.append('## 6. 场景与渲染')
    L.append('')
    L.append('| 项 | 值 |')
    L.append('| --- | --- |')
    for k in ('engine', 'resolution', 'samples', 'view_transform',
              'camera_location', 'camera_target', 'camera_lens',
              'azimuth_deg', 'elevation_deg', 'camera_distance'):
        if k in info:
            L.append('| %s | %s |' % (k, info[k]))
    L.append('| 灯光 | 三点布光：Key(暖) + Fill(冷) + Rim + Top 柔光箱 + 内部补光；EEVEE 光线追踪 / AO / 软阴影 |')
    L.append('| 动画 | **无**（无关键帧 / 无时间轴）|')
    L.append('')
    L.append('## 7. 层级结构树')
    L.append('')
    L.append('```')
    L.extend(tree_lines)
    L.append('```')
    L.append('')
    return '\n'.join(L)


# ===========================================================================
# 主流程
# ===========================================================================
def main(do_render=True, samples=128, width=1920, height=1080, export_glb=True,
         fast=False):
    print('\n' + '=' * 78)
    print(' SmartBookshelf — mechanical concept model build')
    print('=' * 78)

    # [S1] 场景
    _lib02.clear_scene()
    _lib02.setup_units_scene()

    # [S3] 材质
    mats = M.build_materials()
    print('[materials] %d created' % len(mats))

    # 集合
    cols = _lib02.make_collections(['00_Fixed', '01_Lifting', '02_Push',
                                    '03_Electronics', '04_Books'])
    Y = {}

    # 根空物体：整机 / 柜体
    _lib02.set_holder(cols['00_Fixed'])
    root = _lib02.empty('SmartBookshelf_Root', 0.0, 0.0, 0.0, size=120.0)
    cabinet = _lib02.empty('EMPTY_Cabinet', 0.0, 0.0, 0.0, size=100.0)
    _lib02.parent(cabinet, root)
    Y['ROOT'] = root
    Y['CABINET'] = cabinet

    # [S4][S5] 固定件
    _lib02.build_cabinet(cols, mats)
    _lib02.build_shelves(cols, mats)
    for n in list(cols['00_Fixed'].objects):
        if n.parent is None and n is not cabinet:
            _lib02.parent(n, cabinet)

    # [S6] Z 轴升降
    _lib02.build_lift(cols, mats, Y)
    # [S7] Y 轴推书
    _lib02.build_push(cols, mats, Y)
    # [S8] 电控 / 传感
    _lib02.build_electronics(cols, mats, Y)
    # [S9] 书籍
    _, counts = _lib02.build_books(cols, mats, Y)

    bpy.context.view_layer.update()

    # ---- 结构树 & 参数表 ----
    tree = _lib02.structure_tree()
    print('\n' + param_table_text() + '\n')
    print('结构树 / Structure tree')
    print('-' * 78)
    for line in tree:
        print(line)
    print('-' * 78)

    mn, mx = _lib03.scene_bounds()
    print('[bounds] min=(%.1f, %.1f, %.1f) max=(%.1f, %.1f, %.1f)'
          % (mn[0], mn[1], mn[2], mx[0], mx[1], mx[2]))
    print('[bounds] size=(%.1f, %.1f, %.1f)'
          % (mx[0] - mn[0], mx[1] - mn[1], mx[2] - mn[2]))

    # [S10] 地面 / 背板 / 相机 / 灯光
    _lib03.build_floor(mats)
    _lib03.build_backdrop(mats, cabinet_w=P['CAB_W'], cabinet_h=P['CAB_H'])
    cam, cam_info = _lib03.build_camera(mats, azimuth=30.0, elevation=24.0,
                                        focal=58.0, margin=1.06)
    _lib03.build_lights()

    # [S11] 渲染 / 存档 / 导出
    out_png = OUT['png_test'] if fast else OUT['png']
    out_w, out_h, out_s = (960, 540, 32) if fast else (width, height, samples)
    _lib03.configure_render(out_w, out_h, out_s)

    # 文档（快速迭代模式不写盘，避免用测试出图参数覆盖正式文档）
    info = {
        'engine': 'BLENDER_EEVEE（光线追踪 + AO + 软阴影）',
        'resolution': '1920 x 1080',
        'samples': 128,
        'view_transform': bpy.context.scene.view_settings.view_transform,
        'camera_location': '(%.0f, %.0f, %.0f) mm' % tuple(cam_info['location']),
        'camera_target': '(%.0f, %.0f, %.0f) mm' % tuple(cam_info['target']),
        'camera_lens': '%.0f mm' % cam_info['focal'],
        'azimuth_deg': '%.1f' % cam_info['azimuth'],
        'elevation_deg': '%.1f' % cam_info['elevation'],
        'camera_distance': '%.0f mm' % cam_info['distance'],
    }
    if not fast:
        os.makedirs(os.path.dirname(OUT['doc']), exist_ok=True)
        with open(OUT['doc'], 'w', encoding='utf-8') as fh:
            fh.write(markdown_doc(info, counts, tree, (mn, mx)))
        print('[doc] ->', OUT['doc'])

        # 纯文本结构树 + 参数表（脚本运行时同步打印到控制台）
        with open(OUT['tree'], 'w', encoding='utf-8') as fh:
            fh.write('速定位书格 SmartBookshelf — 结构树与参数表\n')
            fh.write('生成方式: build_smart_bookshelf.py\n')
            fh.write('=' * 78 + '\n\n')
            fh.write('[A] 结构树 / Structure tree\n')
            fh.write('-' * 78 + '\n')
            fh.write('\n'.join(tree))
            fh.write('\n\n')
            fh.write(param_table_text())
            fh.write('\n')
        print('[tree] ->', OUT['tree'])

    if do_render:
        _lib03.render_preview(out_png, out_w, out_h, out_s)
        if fast:
            # 迭代模式：按最终分辨率与采样数再出正式预览图
            _lib03.render_preview(OUT['png'], 1920, 1080, 128)
    if not fast:
        # 修正视口 3D 裁剪距离：本模型 1 unit = 1 mm，默认 clip_end=1000 会使
        # 拉远观察时模型被裁剪掉（"黑屏"）。必须在保存 .blend 之前完成。
        _lib03.fix_viewport_clip()
        _lib03.save_blend(OUT['blend'])
    if export_glb:
        _lib03.export_glb(OUT['glb'], selection_only=True)

    n_obj = len([o for o in bpy.data.objects if o.type in ('MESH', 'CURVE')])
    n_tri = 0
    for o in bpy.data.objects:
        if o.type == 'MESH':
            n_tri += len(o.data.polygons)
    print('\n[done] objects=%d faces=%d collections=%d'
          % (n_obj, n_tri, len(bpy.data.collections)))
    print('[done] blend=%s' % OUT['blend'])
    print('[done] render=%s' % OUT['png'])
    return {'objects': n_obj, 'faces': n_tri, 'counts': counts}


if __name__ == '__main__':
    import json
    argv = sys.argv
    if '--' in argv:
        argv = argv[argv.index('--') + 1:]
    else:
        argv = []
    fast = 'fast' in argv
    no_render = 'norender' in argv
    noglb = 'noglb' in argv
    res = json.dumps(main(do_render=not no_render,
                          samples=32 if fast else 128,
                          width=960 if fast else 1920,
                          height=540 if fast else 1080,
                          export_glb=not noglb,
                          fast=fast), ensure_ascii=False)
    print('[RESULT] ' + res)
