# -*- coding: utf-8 -*-
"""
速定位书格 SmartBookshelf — 模块 02：参数表、场景设置与几何构建
Module 02: parameter table, scene setup, geometry builders.

坐标约定（单位：米，Unit Scale = 0.001，1 Blender 单位 = 1 mm）
    X = 宽度（-X 左 / +X 右），0 = 柜体左右中心
    Y = 深度（-Y 后 / +Y 前），0 = 柜体前后中心
    Z = 高度（+Z 上），0 = 整机底部中心（原点位于整机底面中心）

运动学链路（自下而上）：
    Cabinet(柜体/书格, 固定)
      └ PIVOT_Lift_Z        垂直升降轴（丝杆滑台）
          └ PIVOT_Platform  主平台（与中层书格齐平的工作位）
              └ PIVOT_Push_Y  推书机构（沿 +Y 取出 / -Y 存入）
"""

import bpy
import math
import random
import importlib
from mathutils import Euler, Vector

_01 = importlib.import_module('01_materials')
M = _01


# ============================================================================
# 1. 参数表  (单位 mm)
# ============================================================================
P = {
    # ---- 柜体 ----
    'CAB_W': 500.0, 'CAB_D': 400.0, 'CAB_H': 1200.0, 'FRAME_T': 20.0,
    'CAB_Y0': -200.0, 'CAB_Y1': 200.0,

    # ---- 三层书格（净深 190，净宽 180，净高 300）----
    'SHELF_N': 3,
    'LEVEL_H': 300.0,
    'SHELF_Z': [180.0, 480.0, 780.0],      # 各层【上表面】= 书本支撑面
    'SHELF_T': 30.0,                       # 层板厚
    'SHELF_X0': 10.0, 'SHELF_X1': 190.0,
    'SHELF_Y0': -170.0, 'SHELF_Y1': 20.0,

    # ---- 顶部进书口 250 x 80 ----
    'ENTRY_W': 250.0, 'ENTRY_D': 80.0,
    'ENTRY_X0': -125.0, 'ENTRY_X1': 125.0,
    'ENTRY_Y0': 0.0, 'ENTRY_Y1': 80.0,

    # ---- 42 步进电机 (统一动力源) ----
    'M42_W': 42.0, 'M42_D': 42.0, 'M42_H': 38.0, 'M42_SHAFT': 8.0,

    # ---- Z 轴：丝杆滑台升降 ----
    'SCREW_D': 12.0, 'SCREW_X': 0.0, 'SCREW_Y': -155.0,
    'SCREW_Z0': 95.0, 'SCREW_Z1': 855.0,        # 丝杆长 760
    'COUPLING_D': 15.0, 'COUPLING_L': 25.0,
    'NUT_W': 40.0, 'NUT_H': 34.0,
    'MZ_X': 0.0, 'MZ_Y': -155.0, 'MZ_Z0': 32.0,

    # ---- Z 轴滑块（丝杆螺母座）+ 主平台 ----
    'CARRIAGE_XC': 0.0, 'CARRIAGE_YC': -215.0,
    'CARRIAGE_W': 76.0, 'CARRIAGE_D': 90.0, 'CARRIAGE_H': 80.0,
    'CARRIAGE_Z0': 388.0, 'CARRIAGE_Z1': 468.0,
    'PLATFORM_Z0': 468.0, 'PLATFORM_T': 12.0,   # 上表面 480 = 中层书格
    'PLATFORM_X0': -212.0, 'PLATFORM_X1': 188.0,
    'PLATFORM_Y0': 20.0, 'PLATFORM_Y1': 380.0,
    'PLATFORM_FRONT_LIP': 12.0,

    # ---- 右侧垂直导轨（导向 + 抗倾覆）----
    'RAIL_X0': 178.0, 'RAIL_X1': 198.0,
    'RAIL_Y0': -150.0, 'RAIL_Y1': -110.0,
    'RAIL_Z0': 20.0, 'RAIL_Z1': 880.0,
    'SLIDER_X0': 178.0, 'SLIDER_X1': 200.0,
    'SLIDER_Y0': -152.0, 'SLIDER_Y1': -108.0,
    'SLIDER_Z0': 480.0, 'SLIDER_Z1': 560.0,

    # ---- Y 轴：同步带推书机构 ----
    'BELT_T': 5.0, 'BELT_W': 75.0,
    'PULLEY_D': 25.0, 'PULLEY_W': 10.0,
    'PULLEY_X': -152.5,
    'PULLEY_YD': -168.0, 'PULLEY_YI': -48.0,     # 中心距 120
    'BELT_ZC': 318.0,
    'MY_X': -58.0, 'MY_Y': -189.0, 'MY_Z0': 380.0,   # Y 轴 42 电机（竖装，轴朝下）
    'MY_BRACKET_Z0': 380.0, 'MY_BRACKET_Z1': 418.0,

    # 推出机构（小车 + 悬臂 + 推板）
    'CARRIAGE_X0': -90.0, 'CARRIAGE_X1': 90.0,
    'CARRIAGE_Y0': -48.0, 'CARRIAGE_Y1': 0.0,
    'CARRIAGE_Z0': 250.0, 'CARRIAGE_Z1': 315.0,
    'UPRIGHT_X0': -10.0, 'UPRIGHT_X1': 0.0,
    'PUSH_X0': -90.0, 'PUSH_X1': 90.0, 'PUSH_T': 5.0,
    'PUSH_Y0': 160.0, 'PUSH_Z0': 460.0, 'PUSH_Z1': 754.0,
    'PUSH_STROKE_STATIC': 70.0,                  # 静态姿态：推送中段（0 = 起点，155 = 推入完成）
                                                 # 修改此值即可查看不同工作姿态
    'WEDGE_W': 100.0, 'WEDGE_H': 60.0, 'WEDGE_T': 20.0,
    'WEDGE_X0': -34.0, 'WEDGE_Y0': 165.0,
    'LIMIT_X': 97.0, 'LIMIT_Y0': 168.0,

    # ---- 电控 / 传感 ----
    'CAM_W': 50.0, 'CAM_D': 50.0, 'CAM_H': 20.0, 'CAM_Y': -110.0,
    'US_W': 45.0, 'US_D': 20.0, 'US_H': 15.0, 'US_Y': 88.0, 'US_TILT': -42.0,
    'PCB_W': 120.0, 'PCB_D': 85.0, 'PCB_T': 3.0, 'PCB_Z': 205.0, 'PCB_Y0': -180.0,

    # ---- 书籍（平躺放置：书脊朝柜体前方 +Y，沿 Z 叠放）----
    'BOOK_T_MIN': 20.0, 'BOOK_T_MAX': 50.0,
    'BOOK_H_MIN': 180.0, 'BOOK_H_MAX': 240.0,
    'BOOK_D': 160.0, 'BOOK_LABEL_H': 22.0, 'BOOK_XC': 90.0,
    'BOOK_STACK_H': 245.0, 'BOOK_Y0': -160.0,
    'BOOK_PLATFORM_STROKE': 155.0,               # 平台书行程（沿 -Y 推入书格）
    'BOOK_PLATFORM_T': 32.0, 'BOOK_PLATFORM_H': 228.0,
}

PLATFORM_TOP_Z = P['PLATFORM_Z0'] + P['PLATFORM_T']     # 480
PUSH_EXT = P['PUSH_STROKE_STATIC']                      # 静态推出量 120
PUSH_Y_HOME = P['PUSH_Y0']                              # 推板复位位置 95


# ============================================================================
# 2. 场景设置
# ============================================================================
def clear_scene():
    for obj in list(bpy.data.objects):
        bpy.data.objects.remove(obj, do_unlink=True)
    for coll in list(bpy.data.collections):
        bpy.data.collections.remove(coll)
    for datablocks in (bpy.data.meshes, bpy.data.curves, bpy.data.materials,
                       bpy.data.lights, bpy.data.cameras):
        for block in list(datablocks):
            if block.users == 0:
                datablocks.remove(block)
    print('[scene] factory scene cleared')


def setup_units_scene():
    scene = bpy.context.scene
    scene.unit_settings.system = 'METRIC'
    scene.unit_settings.scale_length = 0.001
    try:
        scene.unit_settings.length_unit = 'MILLIMETERS'
    except TypeError:
        pass
    scene.render.engine = 'BLENDER_EEVEE'
    scene.render.film_transparent = False
    scene.render.image_settings.file_format = 'PNG'
    scene.frame_start = 1
    scene.frame_end = 1
    scene.frame_current = 1
    print('[scene] unit scale =', scene.unit_settings.scale_length,
          '| engine =', scene.render.engine)


# ============================================================================
# 3. 集合 / 层级 / 几何工具
# ============================================================================
HOLDER = {'coll': None}


def make_collections(names):
    cols = {}
    for n in names:
        cols[n] = bpy.data.collections.new(n)
        bpy.context.scene.collection.children.link(cols[n])
    return cols


def set_holder(coll):
    HOLDER['coll'] = coll


def _finish(obj, name, mat, coll):
    obj.name = name
    if obj.data is not None:
        obj.data.name = 'GEO-' + name
    if mat is not None and hasattr(obj.data, 'materials'):
        if len(obj.data.materials) == 0:
            obj.data.materials.append(mat)
        else:
            obj.data.materials[0] = mat
    target = coll if coll is not None else HOLDER['coll']
    if target is not None:
        for c in list(obj.users_collection):
            c.objects.unlink(obj)
        target.objects.link(obj)
    return obj


def box(name, x0, x1, y0, y1, z0, z1, mat=None, coll=None, rot=None,
        bevel=0.0, segments=2):
    """由包围盒创建长方体；rot 为绕自身中心的欧拉角（弧度）。"""
    dx, dy, dz = x1 - x0, y1 - y0, z1 - z0
    if min(dx, dy, dz) <= 0.0:
        raise ValueError('box %s: bad extents (%.2f, %.2f, %.2f)' % (name, dx, dy, dz))
    bpy.ops.mesh.primitive_cube_add(size=1.0, location=(0.0, 0.0, 0.0))
    obj = bpy.context.active_object
    if rot is not None:
        obj.rotation_euler = Euler(rot, 'XYZ')
    obj.scale = (dx, dy, dz)
    bpy.context.view_layer.update()
    bpy.ops.object.transform_apply(location=False, rotation=True, scale=True)
    obj.location = ((x0 + x1) * 0.5, (y0 + y1) * 0.5, (z0 + z1) * 0.5)
    if bevel > 0.0:
        mod = obj.modifiers.new('Bevel', 'BEVEL')
        mod.width = bevel
        mod.segments = segments
        mod.limit_method = 'ANGLE'
        mod.angle_limit = math.radians(30.0)
    return _finish(obj, name, mat, coll)


def cyl(name, radius, z0, z1, x=0.0, y=0.0, mat=None, coll=None,
        axis='Z', segments=32, rot=None, shade=True):
    depth = z1 - z0
    if depth <= 0.0 or radius <= 0.0:
        raise ValueError('cyl %s: bad radius/depth (%.2f, %.2f)' % (name, radius, depth))
    if axis == 'Z':
        loc, rr = (x, y, (z0 + z1) * 0.5), (0.0, 0.0, 0.0)
    elif axis == 'Y':
        loc, rr = (x, (z0 + z1) * 0.5, y), (math.radians(90.0), 0.0, 0.0)
    elif axis == 'X':
        loc, rr = ((z0 + z1) * 0.5, x, y), (0.0, math.radians(90.0), 0.0)
    else:
        raise ValueError('bad axis ' + axis)
    bpy.ops.mesh.primitive_cylinder_add(vertices=segments, radius=radius,
                                        depth=depth, location=loc, rotation=rr)
    obj = bpy.context.active_object
    if rot is not None:
        obj.rotation_euler = Euler(rot, 'XYZ')
    bpy.context.view_layer.update()
    bpy.ops.object.transform_apply(location=False, rotation=True, scale=False)
    if shade:
        bpy.ops.object.shade_smooth()
    return _finish(obj, name, mat, coll)


def disc(name, radius, thickness, center, normal, mat=None, coll=None, segments=28):
    """在任意位置/法向上生成圆盘（用于斜置传感器杯口）。"""
    rot = tuple(Vector(normal).normalized().to_track_quat('Z', 'Y').to_euler())
    bpy.ops.mesh.primitive_cylinder_add(vertices=segments, radius=radius,
                                        depth=thickness,
                                        location=(0.0, 0.0, 0.0), rotation=rot)
    obj = bpy.context.active_object
    bpy.context.view_layer.update()
    bpy.ops.object.transform_apply(location=False, rotation=True, scale=False)
    obj.location = center
    if mat is not None:
        obj.data.materials.append(mat)
    return _finish(obj, name, mat, coll)


def wedge(name, x0, x1, y0, y1, z0, z1, low_at='min', mat=None, coll=None):
    """直角楔形块：斜面沿 Y 方向线性上升。"""
    dx, dy, dz = x1 - x0, y1 - y0, z1 - z0
    cx, cy, cz = (x0 + x1) * 0.5, (y0 + y1) * 0.5, (z0 + z1) * 0.5
    hx, hy, hz = dx * 0.5, dy * 0.5, dz * 0.5
    if low_at == 'min':
        verts = [(-hx, -hy, -hz), (hx, -hy, -hz), (hx, hy, -hz), (-hx, hy, -hz),
                 (-hx, hy, hz), (hx, hy, hz)]
        faces = [(0, 3, 2, 1), (3, 4, 5, 2), (0, 1, 5, 4), (0, 4, 3), (1, 2, 5)]
    else:
        verts = [(-hx, -hy, -hz), (hx, -hy, -hz), (hx, hy, -hz), (-hx, hy, -hz),
                 (-hx, -hy, hz), (hx, -hy, hz)]
        faces = [(0, 3, 2, 1), (0, 1, 5, 4), (3, 2, 5, 4), (0, 4, 5, 1), (2, 3, 4, 5)]
    mesh = bpy.data.meshes.new('GEO-' + name)
    mesh.from_pydata([(vx + cx, vy + cy, vz + cz) for vx, vy, vz in verts], [], faces)
    mesh.validate()
    mesh.update()
    obj = bpy.data.objects.new(name, mesh)
    (HOLDER['coll'] if coll is None else coll).objects.link(obj)
    bpy.ops.object.select_all(action='DESELECT')
    bpy.context.view_layer.objects.active = obj
    obj.select_set(True)
    bpy.ops.object.mode_set(mode='EDIT')
    bpy.ops.mesh.select_all(action='SELECT')
    bpy.ops.mesh.normals_make_consistent(inside=False)
    bpy.ops.object.mode_set(mode='OBJECT')
    return _finish(obj, name, mat, coll)


def empty(name, x, y, z, coll=None, size=60.0):
    obj = bpy.data.objects.new(name, None)
    obj.empty_display_type = 'PLAIN_AXES'
    obj.empty_display_size = size
    obj.location = (x, y, z)
    (HOLDER['coll'] if coll is None else coll).objects.link(obj)
    return obj


def parent(child, parent_obj):
    bpy.context.view_layer.update()
    mw = child.matrix_world.copy()
    child.parent = parent_obj
    child.matrix_parent_inverse = parent_obj.matrix_world.inverted()
    child.matrix_world = mw
    return child


def cable(name, points, radius=3.0, mat=None, coll=None):
    cu = bpy.data.curves.new('CUR-' + name, 'CURVE')
    cu.dimensions = '3D'
    cu.resolution_u = 6
    cu.bevel_depth = radius
    cu.bevel_resolution = 4
    cu.use_fill_caps = True
    sp = cu.splines.new('NURBS')
    sp.points.add(len(points) - 1)
    for i, p in enumerate(points):
        sp.points[i].co = (p[0], p[1], p[2], 1.0)
    sp.order_u = 3
    sp.use_endpoint_u = True
    obj = bpy.data.objects.new(name, cu)
    (HOLDER['coll'] if coll is None else coll).objects.link(obj)
    if mat is not None:
        obj.data.materials.append(mat)
    return obj


class RandomBook(object):
    """确定性伪随机书本尺寸生成器。"""

    def __init__(self, seed=20240517):
        self.rng = random.Random(seed)

    def thickness(self):
        return round(self.rng.uniform(P['BOOK_T_MIN'], P['BOOK_T_MAX']), 1)

    def height(self):
        return round(self.rng.uniform(P['BOOK_H_MIN'], P['BOOK_H_MAX']), 1)

    def color_index(self):
        return self.rng.randrange(0, 12)


# ============================================================================
# 4. 固定件：柜体 / 顶部进书口 / 控制面板
# ============================================================================
def build_cabinet(cols, M):
    set_holder(cols['00_Fixed'])
    C = P
    FT = C['FRAME_T']
    x0, x1 = -C['CAB_W'] / 2, C['CAB_W'] / 2
    y0, y1 = C['CAB_Y0'], C['CAB_Y1']
    h = C['CAB_H']
    ac, fr = M['MAT_AcrylicCabinet'], M['MAT_FrameDark']
    objs = []

    objs.append(box('Cabinet_Back', x0, x1, y0, y0 + FT, 0.0, h, ac))
    objs.append(box('Cabinet_SideLeft', x0, x0 + FT, y0, y1, 0.0, h, ac))
    objs.append(box('Cabinet_SideRight', x1 - FT, x1, y0, y1, 0.0, h, ac))
    objs.append(box('Cabinet_Bottom', x0, x1, y0, y1, 0.0, FT, fr))
    objs.append(box('Cabinet_Top', x0, x1, y0, y1, h - FT, h, fr))
    # 前立柱（框架开口边，扁平铝型材）—— 只保留后段，避免与前方工作平台干涉
    objs.append(box('Cabinet_PostFL', x0, x0 + 18.0, y1 - 18.0, y1, 0.0, h, fr))
    objs.append(box('Cabinet_PostFR', x1 - 18.0, x1, y1 - 18.0, y1, 0.0, h, fr))
    # 前横梁（上下各一道）
    objs.append(box('Cabinet_BeamF', x0, x1, y1 - 18.0, y1, h - 62.0, h - 20.0, fr))
    objs.append(box('Cabinet_BeamF_Low', x0, x1, y1 - 18.0, y1, 20.0, 62.0, fr))

    fw = 46.0
    for i, (fx, fy) in enumerate([(-1, -1), (1, -1), (-1, 1), (1, 1), (-1, 0), (1, 0)]):
        cx = fx * (C['CAB_W'] / 2 - fw / 2 - 6.0)
        cy = fy * (C['CAB_D'] / 2 - fw / 2 - 6.0)
        objs.append(box('Cabinet_Foot_%02d' % (i + 1), cx - fw / 2, cx + fw / 2,
                        cy - fw / 2, cy + fw / 2, -28.0, 0.0, fr))

    for sx in (-1, 1):
        px = sx * (C['CAB_W'] / 2 - FT)
        objs.append(box('Frame_Rail_%s' % ('L' if sx < 0 else 'R'),
                        px - 8.0, px + 8.0, y0 + FT, y0 + FT + 16.0,
                        20.0, h - 20.0, M['MAT_FrameAlu']))

    # --- 顶部进书口 ---
    objs.append(box('Top_Entry_WallL', C['ENTRY_X0'] - FT, C['ENTRY_X0'],
                    C['ENTRY_Y0'] - FT, C['ENTRY_Y1'] + FT, h, h + 60.0, fr))
    objs.append(box('Top_Entry_WallR', C['ENTRY_X1'], C['ENTRY_X1'] + FT,
                    C['ENTRY_Y0'] - FT, C['ENTRY_Y1'] + FT, h, h + 60.0, fr))
    objs.append(box('Top_Entry_WallB', C['ENTRY_X0'] - FT, C['ENTRY_X1'] + FT,
                    C['ENTRY_Y0'] - FT, C['ENTRY_Y0'], h, h + 60.0, fr))
    objs.append(box('Top_Entry_WallF', C['ENTRY_X0'] - FT, C['ENTRY_X1'] + FT,
                    C['ENTRY_Y1'], C['ENTRY_Y1'] + FT, h, h + 60.0, fr))
    objs.append(box('Top_Entry_ChuteL', C['ENTRY_X0'] - FT, C['ENTRY_X0'] + 14.0,
                    C['ENTRY_Y0'], C['ENTRY_Y1'], h - 46.0, h, ac))
    objs.append(box('Top_Entry_ChuteR', C['ENTRY_X1'] - 14.0, C['ENTRY_X1'] + FT,
                    C['ENTRY_Y0'], C['ENTRY_Y1'], h - 46.0, h, ac))
    objs.append(box('Top_Entry_ChuteB', C['ENTRY_X0'], C['ENTRY_X1'],
                    C['ENTRY_Y0'], C['ENTRY_Y0'] + 12.0, h - 46.0, h, ac))
    objs.append(box('Entry_LED_L', C['ENTRY_X0'] + 14.0, C['ENTRY_X0'] + 20.0,
                    C['ENTRY_Y0'] + 6.0, C['ENTRY_Y1'] - 6.0, h - 42.0, h - 34.0,
                    M['MAT_LED'], bevel=0.8))
    objs.append(box('Entry_LED_R', C['ENTRY_X1'] - 20.0, C['ENTRY_X1'] - 14.0,
                    C['ENTRY_Y0'] + 6.0, C['ENTRY_Y1'] - 6.0, h - 42.0, h - 34.0,
                    M['MAT_LED'], bevel=0.8))

    # --- 控制面板（前右下，位于工作平台之上）---
    objs.append(box('Control_Panel', 60.0, 212.0, y1 - 16.0, y1 - 2.0,
                    1010.0, 1170.0, M['MAT_ControlPanel'], bevel=1.5))
    objs.append(box('Control_Display', 74.0, 198.0, y1 - 20.0, y1 - 15.0,
                    1084.0, 1150.0, M['MAT_LensGlass']))
    for i in range(3):
        bx = 82.0 + i * 42.0
        objs.append(box('Control_Btn_%02d' % (i + 1), bx, bx + 26.0,
                        y1 - 21.0, y1 - 15.0, 1032.0, 1054.0,
                        M['MAT_Chip'], bevel=0.8))
    return objs


def build_shelves(cols, M):
    set_holder(cols['00_Fixed'])
    C = P
    objs = []
    board, alu = M['MAT_ShelfBoard'], M['MAT_FrameAlu']
    for i, zt in enumerate(C['SHELF_Z']):
        zb = zt - C['SHELF_T']
        n = i + 1
        objs.append(box('Shelf_Level%d_Board' % n, C['SHELF_X0'], C['SHELF_X1'],
                        C['SHELF_Y0'], C['SHELF_Y1'], zb, zt, board, bevel=1.0))
        # 侧挡板（防止书本倾倒）—— 后段，避开前方工作平台
        objs.append(box('Shelf_Level%d_SideL' % n, C['SHELF_X0'] - 8.0, C['SHELF_X0'],
                        C['SHELF_Y0'], -150.0, zb, zt + 110.0, alu))
        objs.append(box('Shelf_Level%d_SideR' % n, C['SHELF_X1'], C['SHELF_X1'] + 8.0,
                        C['SHELF_Y0'], -150.0, zb, zt + 110.0, alu))
        # 前挡边（书本基准面）
        objs.append(box('Shelf_Level%d_LipF' % n, C['SHELF_X0'], C['SHELF_X1'],
                        C['SHELF_Y1'], C['SHELF_Y1'] + 10.0, zb - 4.0, zt + 14.0, alu))
        # 层板后端加强梁
        objs.append(box('Shelf_Level%d_BeamB' % n, C['SHELF_X0'], C['SHELF_X1'],
                        C['SHELF_Y0'], C['SHELF_Y0'] + 14.0, zb - 22.0, zb, alu))
    return objs


# ============================================================================
# 5. 垂直升降机构（Z 轴）
# ============================================================================
def build_lift(cols, M, Y):
    set_holder(cols['01_Lifting'])
    C = P
    objs = []
    mx, my = C['MZ_X'], C['MZ_Y']

    # --- 42 步进电机（立式，轴朝上）---
    mz0 = C['MZ_Z0']
    mz1 = mz0 + C['M42_D']
    objs.append(box('Motor_Z_42_Body', mx - C['M42_W'] / 2, mx + C['M42_W'] / 2,
                    my - C['M42_D'] / 2, my + C['M42_D'] / 2, mz0, mz1,
                    M['MAT_MotorBlack'], bevel=1.2))
    objs.append(box('Motor_Z_42_FacePlate', mx - C['M42_W'] / 2 - 1.0,
                    mx + C['M42_W'] / 2 + 1.0, my - C['M42_D'] / 2 - 1.0,
                    my + C['M42_D'] / 2 + 1.0, mz1, mz1 + 3.0,
                    M['MAT_MotorEndCap'], bevel=0.6))
    objs.append(cyl('Motor_Z_42_Shaft', C['M42_SHAFT'] / 2, mz1 + 3.0,
                    C['SCREW_Z0'] + 12.0, x=mx, y=my, mat=M['MAT_ShaftSilver']))
    objs.append(box('Motor_Z_Bracket', mx - 34.0, mx + 34.0, my - 34.0, my + 34.0,
                    mz0 - 10.0, mz0, M['MAT_FrameAlu'], bevel=1.0))
    for i, (sx, sy) in enumerate([(-1, -1), (1, -1), (-1, 1), (1, 1)]):
        objs.append(cyl('Motor_Z_Screw_%d' % (i + 1), 2.0, mz0 - 18.0, mz0 - 6.0,
                        x=mx + sx * 15.5, y=my + sy * 15.5,
                        mat=M['MAT_Steel'], segments=16))

    # --- 刚性联轴器 ---
    cz0 = C['SCREW_Z0'] - 20.0
    objs.append(cyl('Coupling_Z', C['COUPLING_D'] / 2, cz0, cz0 + C['COUPLING_L'],
                    x=mx, y=my, mat=M['MAT_Aluminum']))
    objs.append(cyl('Coupling_Z_Clamp1', C['COUPLING_D'] / 2 + 1.6, cz0 + 3.0,
                    cz0 + 8.5, x=mx, y=my, mat=M['MAT_Steel'], segments=20))
    objs.append(cyl('Coupling_Z_Clamp2', C['COUPLING_D'] / 2 + 1.6, cz0 + 16.5,
                    cz0 + 22.0, x=mx, y=my, mat=M['MAT_Steel'], segments=20))

    # --- 丝杆（含简化螺纹环）---
    objs.append(cyl('Screw_Z', C['SCREW_D'] / 2, C['SCREW_Z0'], C['SCREW_Z1'],
                    x=mx, y=my, mat=M['MAT_Steel']))
    zs, k = C['SCREW_Z0'] + 6.0, 0
    while zs < C['SCREW_Z1'] - 8.0 and k < 70:
        objs.append(cyl('Screw_Z_Thread_%03d' % (k + 1), C['SCREW_D'] / 2 + 0.55,
                        zs, zs + 3.4, x=mx, y=my, mat=M['MAT_Aluminum'],
                        segments=20, shade=False))
        zs += 11.0
        k += 1
    objs.append(box('Screw_Z_TopBearing', mx - 24.0, mx + 24.0, my - 22.0,
                    my + 22.0, C['SCREW_Z1'], C['SCREW_Z1'] + 26.0,
                    M['MAT_FrameAlu'], bevel=1.0))
    objs.append(cyl('Screw_Z_TopBearingBore', C['SCREW_D'] / 2 + 3.0,
                    C['SCREW_Z1'] - 2.0, C['SCREW_Z1'] + 28.0, x=mx, y=my,
                    mat=M['MAT_Steel'], segments=24))

    # --- 滑块（丝杆螺母座 = Slide_Block_Z）---
    cx, cy = C['CARRIAGE_XC'], C['CARRIAGE_YC']
    z0, z1 = C['CARRIAGE_Z0'], C['CARRIAGE_Z1']
    objs.append(box('Slide_Block_Z', cx - C['CARRIAGE_W'] / 2, cx + C['CARRIAGE_W'] / 2,
                    cy - C['CARRIAGE_D'] / 2, cy + C['CARRIAGE_D'] / 2,
                    z0, z1, M['MAT_Aluminum'], bevel=1.5))
    objs.append(cyl('Slide_Block_Z_Nut', C['NUT_W'] / 2, z1 - 40.0, z1 - 6.0,
                    x=mx, y=my, mat=M['MAT_Steel'], segments=24))
    for i, (sx, sy) in enumerate([(-1, -1), (1, -1), (-1, 1), (1, 1)]):
        objs.append(cyl('Slide_Block_Z_Bolt_%d' % (i + 1), 2.6, z1 - 3.0, z1 + 1.0,
                        x=cx + sx * 28.0, y=cy + sy * 34.0,
                        mat=M['MAT_Steel'], segments=14))

    # --- 主平台（上表面 480 = 中层书格，前方为工作区）---
    pz0, pz1 = C['PLATFORM_Z0'], C['PLATFORM_Z0'] + C['PLATFORM_T']
    px0, px1 = C['PLATFORM_X0'], C['PLATFORM_X1']
    py0, py1 = C['PLATFORM_Y0'], C['PLATFORM_Y1']
    objs.append(box('Main_Platform', px0, px1, py0, py1, pz0, pz1,
                    M['MAT_Aluminum'], bevel=2.0))
    for i, yy in enumerate([45.0, 115.0, 185.0, 255.0, 325.0]):
        objs.append(box('Platform_Rib_%02d' % (i + 1), px0 + 8.0, px1 - 8.0,
                        yy - 5.0, yy + 5.0, pz0 - 16.0, pz0, M['MAT_Aluminum']))
    # 平台后缘与滑块之间的连接悬臂（跨越工作区与柜体后段）
    objs.append(box('Platform_CarriageArm', -34.0, 34.0, -175.0, 30.0,
                    330.0, 348.0, M['MAT_Aluminum'], bevel=1.0))
    for i, sx in enumerate([-1.0, 1.0]):
        objs.append(box('Platform_CarriageGusset_%d' % (i + 1),
                        sx * 34.0 - 4.0, sx * 34.0 + 4.0, -170.0, -110.0,
                        348.0, pz0 - 16.0, M['MAT_Aluminum']))
    objs.append(box('Platform_StopFront', px0 + 6.0, px1 - 6.0, py1 - 8.0, py1,
                    pz1, pz1 + 12.0, M['MAT_FrameAlu']))
    objs.append(box('Platform_SideGuardL', px0 + 8.0, px0 + 18.0, py0 + 6.0,
                    py1 - 14.0, pz1, pz1 + 10.0, M['MAT_FrameAlu']))
    objs.append(box('Platform_SideGuardR', px1 - 18.0, px1 - 8.0, py0 + 6.0,
                    py1 - 14.0, pz1, pz1 + 10.0, M['MAT_FrameAlu']))
    objs.append(box('Platform_RailArm', 176.0, 200.0, -60.0, 6.0, pz0, pz1 + 90.0,
                    M['MAT_Aluminum'], bevel=1.0))
    # 平台 ↔ 导轨滑块 的连接桥（抗倾覆受力路径）
    objs.append(box('Platform_Slider_Bridge', 180.0, 196.0, -155.0, -60.0,
                    pz0 + 20.0, pz1 + 30.0, M['MAT_Aluminum'], bevel=1.0))

    # --- 右侧垂直导轨 + 预载滑块（导向 / 抗倾覆）---
    objs.append(box('Guide_Rail_Right', C['RAIL_X0'], C['RAIL_X1'], C['RAIL_Y0'],
                    C['RAIL_Y1'], C['RAIL_Z0'], C['RAIL_Z1'],
                    M['MAT_GuideRail'], bevel=1.0))
    for i, zz in enumerate([40.0, 848.0]):
        objs.append(box('Guide_Rail_Bracket_%d' % (i + 1), 196.0, 226.0,
                        C['RAIL_Y0'] - 2.0, C['RAIL_Y1'] + 2.0, zz, zz + 16.0,
                        M['MAT_FrameAlu']))
    objs.append(box('Guide_Slider_Right', C['SLIDER_X0'], C['SLIDER_X1'],
                    C['SLIDER_Y0'], C['SLIDER_Y1'], C['SLIDER_Z0'],
                    C['SLIDER_Z1'], M['MAT_Aluminum'], bevel=1.2))
    for i in range(11):
        zz = C['RAIL_Z0'] + 30.0 + i * 80.0
        if zz > C['RAIL_Z1'] - 20.0:
            break
        objs.append(box('Guide_Rail_Tick_%02d' % (i + 1), C['RAIL_X0'] - 1.5,
                        C['RAIL_X0'], C['RAIL_Y1'] - 6.0, C['RAIL_Y1'] - 2.0,
                        zz, zz + 2.0, M['MAT_FrameDark']))

    # --- 轴心空物体 + 层级 ---
    bpy.context.view_layer.update()
    pivot_lift = empty('PIVOT_Lift_Z', 0.0, 0.0, 0.0, size=110.0)
    pivot_plat = empty('PIVOT_Platform', 0.0, 0.0, 0.0, size=80.0)
    parent(pivot_lift, Y['CABINET'])
    parent(pivot_plat, pivot_lift)
    Y['LIFT'] = pivot_lift
    Y['PLATFORM'] = pivot_plat

    movable = ['Slide_Block_Z', 'Slide_Block_Z_Nut', 'Main_Platform',
               'Platform_StopFront', 'Platform_RailArm', 'Guide_Slider_Right',
               'Platform_CarriageArm', 'Platform_SideGuardL', 'Platform_SideGuardR',
               'Platform_Slider_Bridge',
               'Platform_CarriageGusset_1', 'Platform_CarriageGusset_2']
    movable += ['Platform_Rib_%02d' % (i + 1) for i in range(5)]
    movable += ['Slide_Block_Z_Bolt_%d' % (i + 1) for i in range(4)]
    for n in movable:
        o = bpy.data.objects.get(n)
        if o is not None:
            parent(o, pivot_plat)
    bpy.context.view_layer.update()
    return objs


# ============================================================================
# 6. 水平推书机构（Y 轴：同步带 + 推板 + 斜块 + 限位）
# ============================================================================
def build_push(cols, M, Y):
    set_holder(cols['02_Push'])
    C = P
    objs = []
    px = C['PULLEY_X']
    pr = C['PULLEY_D'] / 2
    yd, yi = C['PULLEY_YD'], C['PULLEY_YI']
    bt = C['BELT_T']

    # --- Y 轴 42 电机（竖装于平台之上，轴朝下驱动主动轮）---
    mz0 = C['MY_Z0']
    mz1 = mz0 + C['M42_H']
    objs.append(box('Motor_Y_42_Body', C['MY_X'] - C['M42_W'] / 2,
                    C['MY_X'] + C['M42_W'] / 2, C['MY_Y'] - C['M42_D'] / 2,
                    C['MY_Y'] + C['M42_D'] / 2, mz0, mz1,
                    M['MAT_MotorBlack'], bevel=1.2))
    objs.append(box('Motor_Y_42_FacePlate', C['MY_X'] - C['M42_W'] / 2 - 1.0,
                    C['MY_X'] + C['M42_W'] / 2 + 1.0,
                    C['MY_Y'] - C['M42_D'] / 2 - 1.0,
                    C['MY_Y'] + C['M42_D'] / 2 + 1.0, mz0 - 3.0, mz0,
                    M['MAT_MotorEndCap'], bevel=0.6))
    objs.append(cyl('Motor_Y_42_Shaft', C['M42_SHAFT'] / 2, C['BELT_ZC'] + 14.0,
                    mz0 + 2.0, x=C['MY_X'], y=C['MY_Y'], mat=M['MAT_ShaftSilver']))
    objs.append(box('Motor_Y_Bracket', C['MY_X'] - 30.0, C['MY_X'] + 30.0,
                    C['MY_Y'] - 28.0, C['MY_Y'] + 28.0, C['MY_BRACKET_Z0'],
                    C['MY_BRACKET_Z1'], M['MAT_FrameAlu'], bevel=1.0))
    objs.append(box('Motor_Y_MountArm', C['MY_X'] - 20.0, C['MY_X'] + 20.0,
                    C['MY_Y'] + 28.0, 40.0, C['MY_BRACKET_Z0'] - 25.0,
                    C['MY_BRACKET_Z0'] + 3.0, M['MAT_FrameAlu'], bevel=1.0))

    # --- 同步带轮（主动 + 从动）---
    objs.append(cyl('Belt_Pulley_Drive', pr, C['BELT_ZC'] - C['PULLEY_W'] / 2,
                    C['BELT_ZC'] + C['PULLEY_W'] / 2, x=px, y=yd,
                    mat=M['MAT_Pulley'], segments=40))
    for i in range(14):
        ang = i * (2.0 * math.pi / 14.0)
        bx0 = px + math.sin(ang) * (pr + 0.6) - 1.0
        by0 = yd - math.cos(ang) * (pr + 0.6) - 1.0
        objs.append(box('Belt_Pulley_Drive_Tooth_%02d' % (i + 1), bx0, bx0 + 2.0,
                        by0, by0 + 2.0, C['BELT_ZC'] - C['PULLEY_W'] / 2,
                        C['BELT_ZC'] + C['PULLEY_W'] / 2, M['MAT_Steel']))
    objs.append(cyl('Belt_Pulley_Idler', pr, C['BELT_ZC'] - C['PULLEY_W'] / 2,
                    C['BELT_ZC'] + C['PULLEY_W'] / 2, x=px, y=yi,
                    mat=M['MAT_Pulley'], segments=40))
    objs.append(box('Belt_Idler_Bracket', px - 16.0, px + 16.0, yi + 12.0,
                    yi + 34.0, C['BELT_ZC'] - 24.0, C['BELT_ZC'] + 24.0,
                    M['MAT_FrameAlu'], bevel=1.0))
    objs.append(cyl('Belt_Idler_Axle', 4.0, C['BELT_ZC'] - 32.0,
                    C['BELT_ZC'] + 32.0, x=px, y=yi, mat=M['MAT_Steel'],
                    axis='Y', segments=18))
    objs.append(box('Belt_Mechanism_Upper', px - C['BELT_W'] / 2, px + C['BELT_W'] / 2,
                    yd, yi, C['BELT_ZC'] + pr - bt / 2, C['BELT_ZC'] + pr + bt / 2,
                    M['MAT_BeltRubber']))
    objs.append(box('Belt_Mechanism_Lower', px - C['BELT_W'] / 2, px + C['BELT_W'] / 2,
                    yd, yi, C['BELT_ZC'] - pr - bt / 2, C['BELT_ZC'] - pr + bt / 2,
                    M['MAT_BeltRubber']))
    objs.append(box('Belt_Tensioner', px - 14.0, px + 14.0, yi + 34.0, yi + 52.0,
                    C['BELT_ZC'] - 12.0, C['BELT_ZC'] + 12.0, M['MAT_FrameAlu']))
    objs.append(cyl('Belt_Tensioner_Screw', 3.0, C['BELT_ZC'] - 12.0,
                    C['BELT_ZC'] + 12.0, x=px, y=yi + 58.0, mat=M['MAT_Steel'],
                    axis='Y', segments=16))

    # --- 推书小车（同步带夹 + 竖直立柱 + 悬臂）---
    cy0, cy1 = C['CARRIAGE_Y0'], C['CARRIAGE_Y1']
    cz0, cz1 = C['CARRIAGE_Z0'], C['CARRIAGE_Z1']
    objs.append(box('Push_Carriage', C['CARRIAGE_X0'], C['CARRIAGE_X1'],
                    cy0, cy1, cz0, cz1, M['MAT_Aluminum'], bevel=1.0))
    objs.append(box('Push_Belt_Clamp', px - C['BELT_W'] / 2 - 2.0,
                    px + C['BELT_W'] / 2 + 2.0, (cy0 + cy1) / 2 - 20.0,
                    (cy0 + cy1) / 2 + 20.0,
                    C['BELT_ZC'] + pr - bt - 1.0, C['BELT_ZC'] + pr + bt + 1.0,
                    M['MAT_Steel']))
    objs.append(box('Push_Upright', C['UPRIGHT_X0'], C['UPRIGHT_X1'], cy0, cy1,
                    cz0, C['PUSH_Z1'], M['MAT_Aluminum'], bevel=1.0))
    objs.append(box('Push_Carriage_Arm', C['UPRIGHT_X0'] - 4.0,
                    C['UPRIGHT_X1'] + 4.0, cy1, C['PUSH_Y0'], C['PUSH_Z1'] - 26.0,
                    C['PUSH_Z1'], M['MAT_Aluminum']))
    objs.append(box('Push_Carriage_Arm_L', C['UPRIGHT_X0'] - 4.0,
                    C['UPRIGHT_X1'] + 4.0, cy1, C['PUSH_Y0'],
                    C['PUSH_Z0'] + 60.0, C['PUSH_Z0'] + 86.0, M['MAT_Aluminum']))
    # 线性导杆（推板行程导向）
    for i, sx in enumerate([C['PUSH_X0'] + 3.0, C['PUSH_X1'] - 3.0]):
        objs.append(cyl('Push_Rod_%d' % (i + 1), 4.0, C['BELT_ZC'],
                        C['PUSH_Z1'] + 8.0, x=sx, y=(cy0 + cy1) * 0.5,
                        mat=M['MAT_Steel'], segments=16))

    # --- 推板（双层：背板 + 前端接触条）---
    objs.append(box('Push_Plate', C['PUSH_X0'], C['PUSH_X1'], C['PUSH_Y0'],
                    C['PUSH_Y0'] + C['PUSH_T'], C['PUSH_Z0'], C['PUSH_Z1'],
                    M['MAT_PushPlate'], bevel=0.8))
    objs.append(box('Push_Plate_Face', C['PUSH_X0'] + 4.0, C['PUSH_X1'] - 4.0,
                    C['PUSH_Y0'] + C['PUSH_T'], C['PUSH_Y0'] + C['PUSH_T'] + 5.0,
                    C['PUSH_Z0'] + 22.0, C['PUSH_Z1'] - 16.0,
                    M['MAT_FrameAlu'], bevel=0.8))
    objs.append(box('Push_Plate_RibL', C['PUSH_X0'] + 18.0, C['PUSH_X0'] + 30.0,
                    C['PUSH_Y0'] - 8.0, C['PUSH_Y0'], C['PUSH_Z0'] + 26.0,
                    C['PUSH_Z1'] - 26.0, M['MAT_FrameAlu']))
    objs.append(box('Push_Plate_RibR', C['PUSH_X1'] - 30.0, C['PUSH_X1'] - 18.0,
                    C['PUSH_Y0'] - 8.0, C['PUSH_Y0'], C['PUSH_Z0'] + 26.0,
                    C['PUSH_Z1'] - 26.0, M['MAT_FrameAlu']))

    # --- 斜块（楔形，顶置推头：斜面自适应书厚，避免与书体干涉）---
    objs.append(wedge('Wedge_Block', C['WEDGE_X0'], C['WEDGE_X0'] + C['WEDGE_W'],
                      C['WEDGE_Y0'], C['WEDGE_Y0'] + C['WEDGE_T'],
                      C['PUSH_Z1'] + 2.0, C['PUSH_Z1'] + 2.0 + C['WEDGE_H'],
                      low_at='min', mat=M['MAT_WedgeAcrylic']))
    objs.append(box('Wedge_Block_Rib', C['WEDGE_X0'] + 16.0, C['WEDGE_X0'] + 84.0,
                    C['WEDGE_Y0'] - 5.0, C['WEDGE_Y0'],
                    C['PUSH_Z1'] + 40.0, C['PUSH_Z1'] + 58.0, M['MAT_FrameAlu']))
    objs.append(box('Wedge_Block_Screw', -24.0, 24.0, C['WEDGE_Y0'] + 2.0,
                    C['WEDGE_Y0'] + C['WEDGE_T'] - 2.0, C['PUSH_Z1'] - 6.0,
                    C['PUSH_Z1'] + 2.0, M['MAT_Steel']))

    # --- 限位挡板 / 限位调节器（独立微调，适应书厚）---
    lx, ly = C['LIMIT_X'], C['LIMIT_Y0']
    ptop = C['PLATFORM_Z0'] + C['PLATFORM_T']
    objs.append(box('Limit_L_Bracket', lx, lx + 6.0, ly - 30.0, ly + 30.0,
                    ptop, ptop + 270.0, M['MAT_FrameAlu'], bevel=0.8))
    objs.append(box('Limit_Adjuster', lx + 6.0, lx + 34.0, ly + 2.0, ly + 46.0,
                    ptop + 216.0, ptop + 244.0, M['MAT_Aluminum'], bevel=1.0))
    objs.append(box('Limit_Adjuster_Slot', lx + 5.0, lx + 7.0, ly + 10.0, ly + 40.0,
                    ptop + 220.0, ptop + 240.0, M['MAT_FrameDark']))
    objs.append(cyl('Limit_ThumbScrew', 6.0, 0.0, 12.0, x=ly + 24.0, y=0.0,
                    mat=M['MAT_Steel'], axis='X', segments=22))
    o = bpy.data.objects['Limit_ThumbScrew']
    o.location = (lx + 38.0, ly + 24.0, ptop + 230.0)
    bpy.context.view_layer.update()
    # 行程限位：微动开关 + 触发片
    objs.append(box('Limit_Flag', C['CARRIAGE_X0'] + 20.0, C['CARRIAGE_X0'] + 62.0,
                    cy1 - 4.0, cy1 + 2.0, 214.0, 222.0, M['MAT_FrameDark']))
    objs.append(box('Limit_Switch_Body', -110.0, -70.0, -192.0, -180.0, 352.0, 366.0,
                    M['MAT_PCB']))
    objs.append(box('Limit_Switch_Bracket', -114.0, -66.0, -196.0, -178.0,
                    324.0, 352.0, M['MAT_FrameAlu']))
    objs.append(box('Push_Stop_Block', -70.0, -20.0, 20.0, 56.0, 232.0, 246.0,
                    M['MAT_FrameDark']))

    # --- 层级：推书机构整体随主平台升降 ---
    bpy.context.view_layer.update()
    pivot_push = empty('PIVOT_Push_Y', 0.0, 0.0, 0.0, size=60.0)
    parent(pivot_push, Y['PLATFORM'])
    Y['PUSH'] = pivot_push
    platform_fixed = ('Motor_Y_Bracket', 'Motor_Y_MountArm', 'Belt_Pulley_Drive',
                      'Belt_Pulley_Idler', 'Belt_Idler_Bracket', 'Belt_Idler_Axle',
                      'Belt_Tensioner', 'Belt_Tensioner_Screw',
                      'Belt_Mechanism_Upper', 'Belt_Mechanism_Lower',
                      'Limit_Switch_Body', 'Limit_Switch_Bracket',
                      'Push_Stop_Block')
    platform_fixed += tuple('Belt_Pulley_Drive_Tooth_%02d' % (i + 1) for i in range(14))
    for ob in objs:
        if ob.name in platform_fixed:
            parent(ob, Y['PLATFORM'])
        else:
            parent(ob, pivot_push)
    pivot_push.location.y = -PUSH_EXT          # 静止姿态：沿 -Y 推出 120
    bpy.context.view_layer.update()
    return objs


# ============================================================================
# 7. 电控与传感
# ============================================================================
def build_electronics(cols, M, Y):
    set_holder(cols['03_Electronics'])
    C = P
    objs = []
    top = C['CAB_H']
    camx, camy = C['SCREW_X'], C['CAM_Y']

    # --- 顶部摄像头（朝下，对准进书口）---
    objs.append(box('Camera_Top_Body', camx - C['CAM_W'] / 2, camx + C['CAM_W'] / 2,
                    camy - C['CAM_D'] / 2, camy + C['CAM_D'] / 2, top,
                    top + C['CAM_H'], M['MAT_Camera'], bevel=1.2))
    objs.append(box('Camera_Top_Cover', camx - 26.0, camx + 26.0, camy - 26.0,
                    camy + 26.0, top + C['CAM_H'], top + C['CAM_H'] + 6.0,
                    M['MAT_Chip'], bevel=1.0))
    objs.append(cyl('Camera_Top_LensRing', 16.0, top - 2.0, top + 3.0, x=camx,
                    y=camy, mat=M['MAT_Steel'], segments=28))
    objs.append(cyl('Camera_Top_Lens', 13.0, top - 9.0, top - 1.5, x=camx, y=camy,
                    mat=M['MAT_LensGlass'], segments=28))
    for i, sx in enumerate([-1.0, 1.0]):
        objs.append(box('Camera_Top_Mount%d' % (i + 1), camx + sx * 30.0 - 5.0,
                        camx + sx * 30.0 + 5.0, camy - 4.0, camy + 4.0,
                        top + C['CAM_H'] + 6.0, top + C['CAM_H'] + 46.0,
                        M['MAT_FrameAlu']))

    # --- 顶部超声波测厚模块（斜置，指向进书口）---
    objs.append(box('Ultrasonic_Top_Body', -C['US_W'] / 2, C['US_W'] / 2,
                    C['US_Y'] - C['US_D'] / 2, C['US_Y'] + C['US_D'] / 2,
                    top + 12.0, top + 12.0 + C['US_H'], M['MAT_Ultrasonic'],
                    bevel=1.0, rot=(math.radians(C['US_TILT']), 0.0, 0.0)))
    t = math.radians(C['US_TILT'])
    nrm = (0.0, math.sin(t), math.cos(t))
    face_y = C['US_Y'] + math.cos(t) * (C['US_H'] / 2)
    face_z = top + 12.0 + C['US_H'] / 2 - math.sin(t) * (C['US_H'] / 2)
    for i, sx in enumerate([-1.0, 1.0]):
        objs.append(disc('Ultrasonic_Top_Cup_%d' % (i + 1), 8.5, 4.0,
                         (sx * 11.0, face_y + 0.6, face_z + 2.0), nrm,
                         mat=M['MAT_Steel'], segments=24))
    objs.append(box('Ultrasonic_Top_Mount', -30.0, 30.0, C['US_Y'] + 12.0,
                    C['US_Y'] + 20.0, top + 2.0, top + 26.0, M['MAT_FrameAlu']))

    # --- 主控板（柜体背面内壁）---
    pz, py0 = C['PCB_Z'], C['PCB_Y0']
    objs.append(box('Controller_Board', -C['PCB_W'] / 2, C['PCB_W'] / 2, py0,
                    py0 + C['PCB_T'], pz, pz + C['PCB_D'], M['MAT_PCB']))
    objs.append(box('Controller_Board_MCU', -24.0, 24.0, py0 + C['PCB_T'],
                    py0 + 11.0, pz + 46.0, pz + 78.0, M['MAT_Chip']))
    for i in range(5):
        yy = pz + 10.0 + i * 15.0
        objs.append(box('Controller_Board_Term_%d' % (i + 1),
                        -C['PCB_W'] / 2 + 6.0, -C['PCB_W'] / 2 + 16.0,
                        py0 + C['PCB_T'], py0 + 10.0, yy, yy + 8.0, M['MAT_Chip']))
    for i, sx in enumerate([-1.0, 1.0]):
        for j, sy in enumerate([-1.0, 1.0]):
            objs.append(cyl('Controller_Board_Stand_%d%d' % (i + 1, j + 1), 3.5,
                            pz - 10.0, pz, x=sx * 46.0,
                            y=py0 + 4.0 if sy < 0 else py0 + 12.0,
                            mat=M['MAT_Steel'], segments=14))
    objs.append(box('Controller_Heatsink', -34.0, 34.0, py0 + 4.0, py0 + 12.0,
                    pz + 84.0, pz + 96.0, M['MAT_Steel']))

    # --- 线缆束 ---
    pts = [(camx - 20.0, camy, top + 34.0),
           (150.0, -50.0, top + 16.0),
           (192.0, -176.0, top - 60.0),
           (192.0, -176.0, 620.0),
           (140.0, -172.0, 400.0),
           (62.0, -168.0, pz + 96.0)]
    objs.append(cable('Cable_Group', pts, radius=3.2, mat=M['MAT_Wire']))
    for i, zz in enumerate([300.0, 520.0, 740.0]):
        objs.append(box('Cable_Clip_%d' % (i + 1), 186.0, 198.0, -182.0, -170.0,
                        zz, zz + 5.0, M['MAT_Chip']))

    # --- 底部 Z 轴驱动板 ---
    objs.append(box('Driver_Z_Board', 60.0, 180.0, -190.0, -184.0, 60.0, 118.0,
                    M['MAT_PCB']))
    objs.append(box('Driver_Z_Chip', 76.0, 164.0, -184.0, -178.0, 74.0, 104.0,
                    M['MAT_Chip']))
    return objs


# ============================================================================
# 8. 书籍
# ============================================================================
def build_books(cols, M, Y):
    """
    书籍以【平躺】方式放置（书脊朝柜体前方 +Y，沿 Z 方向叠放）：
        X = 书高（书脊长度，210~300）
        Y = 书宽（书脊到书口，160）
        Z = 书厚（20~50，叠放方向）
    """
    set_holder(cols['04_Books'])
    C = P
    objs = []
    rng = RandomBook(seed=20240517)
    counts = {}
    d = C['BOOK_D']

    def make_book(name, xc, y0, z0, h, t, ci):
        x0, x1 = xc - h / 2.0, xc + h / 2.0
        objs.append(box(name, x0, x1, y0, y0 + d, z0, z0 + t,
                        M['MAT_Book_%02d' % ci], bevel=0.5))
        # 书脊色块（书脊 = +Y 外露面）
        lz = z0 + t * 0.5
        objs.append(box(name + '_Label', x0 + h * 0.14, x1 - h * 0.14,
                        y0 + d, y0 + d + 0.8, lz - min(C['BOOK_LABEL_H'],
                                                       t * 0.32) * 0.5,
                        lz + min(C['BOOK_LABEL_H'], t * 0.32) * 0.5,
                        M['MAT_BookLabel']))
        # 书口侧页纹（-Y 面）
        objs.append(box(name + '_Pages', x0 + h * 0.06, x1 - h * 0.06,
                        y0 - 0.8, y0, z0 + t * 0.14, z0 + t * 0.86,
                        M['MAT_BookLabel']))

    # --- 三层书格：每层一摞平躺书本，自层板向上叠放 ---
    for level, ztop in ((1, C['SHELF_Z'][0]), (2, C['SHELF_Z'][1]),
                        (3, C['SHELF_Z'][2])):
        z = ztop
        idx = 0
        while True:
            t = rng.thickness()
            if z + t > ztop + C['BOOK_STACK_H']:
                break
            h = rng.height()
            # 每本略微左右错位，形成自然错落的书堆
            xc = C['BOOK_XC'] + ((idx % 3) - 1) * 28.0
            make_book('Book_L%d_%02d' % (level, idx + 1), xc,
                      C['BOOK_Y0'], z, h, t, rng.color_index())
            z += t + 2.0
            idx += 1
        counts['level%d' % level] = idx

    # --- 平台上正在被推入中层书格的书（展现推书工作姿态）---
    bh, bt = C['BOOK_PLATFORM_H'], C['BOOK_PLATFORM_T']
    yb = 10.0 - PUSH_EXT        # 书脊位置：随推板一同前移
    make_book('Book_Platform_01', 0.0, yb, PLATFORM_TOP_Z, bh, bt, 3)
    counts['platform'] = 1

    for o in objs:
        parent(o, Y['CABINET'])
    return objs, counts


# ============================================================================
# 9. 结构树输出
# ============================================================================
def structure_tree():
    """输出集合 / 层级结构树（按名称确定性遍历，包含深层子级）。"""
    lines = []

    def kids(obj):
        return sorted([c for c in bpy.data.objects if c.parent is obj],
                      key=lambda c: c.name)

    def walk(obj, depth, seen):
        if obj.name in seen:
            lines.append('%s%s  [circular]' % ('    ' * depth, obj.name))
            return
        seen.add(obj.name)
        cols = ','.join(sorted(c.name for c in obj.users_collection))
        lines.append('%s%s  [%s]  {%s}' % ('    ' * depth, obj.name,
                                           obj.type.lower(), cols))
        for ch in kids(obj):
            walk(ch, depth + 1, seen)

    seen = set()
    for c in sorted(bpy.data.collections, key=lambda c: c.name):
        roots = sorted([o for o in c.objects if o.parent is None],
                       key=lambda o: o.name)
        lines.append('COL-%s  (直接成员 %d)' % (c.name, len(c.objects)))
        for o in roots:
            walk(o, 1, seen)
    for o in sorted(bpy.data.objects, key=lambda o: o.name):
        if o.parent is None and o.name not in seen:
            walk(o, 1, seen)
    return lines
