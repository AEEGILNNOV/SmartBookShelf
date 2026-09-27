# -*- coding: utf-8 -*-
"""
速定位书格 — 智能图书存取系统  模块 01：材质
SmartBookshelf mechanical concept / module 01: materials

在执行构建前可单独运行本模块，用于快速检查 Principled BSDF 插槽兼容性。
"""

import bpy


# ----------------------------------------------------------------------------
# socket 安全赋值（跨 Blender 4.x / 5.x）
# ----------------------------------------------------------------------------
def set_in(bsdf, names, value):
    """按候选名称列表设置输入插槽，全部不存在时静默跳过。"""
    for name in names:
        socket = next((s for s in bsdf.inputs if s.name == name), None)
        if socket is not None:
            socket.default_value = value
            return True
    return False


def _new(name):
    mat = bpy.data.materials.get(name)
    if mat is not None:
        return mat
    mat = bpy.data.materials.new(name)
    mat.use_nodes = True
    return mat


def _bsdf(mat):
    return next(n for n in mat.node_tree.nodes if n.type == 'BSDF_PRINCIPLED')


def principled(name,
               base=(0.8, 0.8, 0.8, 1.0),
               metallic=0.0,
               roughness=0.5,
               transmission=0.0,
               ior=1.45,
               emission=(0.0, 0.0, 0.0, 1.0),
               emission_strength=0.0,
               alpha=1.0,
               specular=0.5,
               coat=0.0,
               sheen=0.0,
               blend=False):
    """创建一个 Principled BSDF 材质。"""
    mat = _new(name)
    b = _bsdf(mat)
    set_in(b, ('Base Color',), base)
    set_in(b, ('Metallic',), metallic)
    set_in(b, ('Roughness',), roughness)
    set_in(b, ('IOR',), ior)
    set_in(b, ('Alpha',), alpha)
    # Blender 4.0+ 把 "Transmission" 改名为 "Transmission Weight"
    set_in(b, ('Transmission Weight', 'Transmission'), transmission)
    set_in(b, ('Specular IOR Level', 'Specular'), specular)
    set_in(b, ('Coat Weight', 'Clearcoat'), coat)
    set_in(b, ('Sheen Weight', 'Sheen'), sheen)
    set_in(b, ('Emission Color', 'Emission'), emission)
    set_in(b, ('Emission Strength',), emission_strength)

    # 视口/EEVEE 透明度设置（属性名在 5.x 中被精简，因此做存在性判断）
    if blend or alpha < 1.0 or transmission > 0.0:
        if hasattr(mat, 'blend_method'):
            try:
                mat.blend_method = 'BLEND'
            except TypeError:
                pass
        for attr, val in (('show_transparent_back', False),
                          ('use_transparency_overlap', False),
                          ('use_backface_culling', False),
                          ('use_screen_refraction', transmission > 0.0)):
            if hasattr(mat, attr):
                try:
                    setattr(mat, attr, val)
                except Exception:
                    pass
    mat.use_backface_culling = False
    return mat


# ----------------------------------------------------------------------------
# 材质库
# ----------------------------------------------------------------------------
def build_materials():
    """建立全部材质并返回 名称 -> 材质 字典。"""
    M = {}

    # --- 柜体 / 结构 ---
    # 半透明亚克力：以 Alpha 透明为主（保证内部机构清晰可见），
    # 配合低粗糙度与玻璃级 IOR 提供亚克力的清透高光。
    M['MAT_AcrylicCabinet'] = principled(
        'MAT_AcrylicCabinet', base=(0.95, 0.975, 1.0, 1.0),
        metallic=0.0, roughness=0.035, transmission=0.0, ior=1.49,
        alpha=0.085, specular=0.82, blend=True)
    M['MAT_AcrylicClear'] = principled(
        'MAT_AcrylicClear', base=(0.96, 0.98, 1.0, 1.0),
        metallic=0.0, roughness=0.030, transmission=0.0, ior=1.49,
        alpha=0.065, specular=0.88, blend=True)
    M['MAT_AcrylicFrost'] = principled(
        'MAT_AcrylicFrost', base=(0.95, 0.97, 0.98, 1.0),
        metallic=0.0, roughness=0.30, transmission=0.0, ior=1.48,
        alpha=0.30, specular=0.5, blend=True)
    M['MAT_FrameDark'] = principled(
        'MAT_FrameDark', base=(0.185, 0.200, 0.228, 1.0),
        metallic=0.30, roughness=0.34)
    M['MAT_FrameAlu'] = principled(
        'MAT_FrameAlu', base=(0.70, 0.725, 0.765, 1.0),
        metallic=1.0, roughness=0.28)
    M['MAT_ShelfBoard'] = principled(
        'MAT_ShelfBoard', base=(0.88, 0.89, 0.90, 1.0),
        metallic=0.0, roughness=0.45, alpha=1.0)
    M['MAT_ControlPanel'] = principled(
        'MAT_ControlPanel', base=(0.09, 0.10, 0.12, 1.0),
        metallic=0.20, roughness=0.30)

    # --- 金属运动件 ---
    M['MAT_Aluminum'] = principled(
        'MAT_Aluminum', base=(0.855, 0.875, 0.905, 1.0),
        metallic=1.0, roughness=0.24)
    M['MAT_Steel'] = principled(
        'MAT_Steel', base=(0.915, 0.925, 0.945, 1.0),
        metallic=1.0, roughness=0.13)
    M['MAT_GuideRail'] = principled(
        'MAT_GuideRail', base=(0.76, 0.78, 0.81, 1.0),
        metallic=1.0, roughness=0.28)

    # --- 步进电机 ---
    M['MAT_MotorBlack'] = principled(
        'MAT_MotorBlack', base=(0.045, 0.048, 0.055, 1.0),
        metallic=0.10, roughness=0.38, specular=0.45)
    M['MAT_MotorEndCap'] = principled(
        'MAT_MotorEndCap', base=(0.30, 0.31, 0.34, 1.0),
        metallic=1.0, roughness=0.32)
    M['MAT_ShaftSilver'] = principled(
        'MAT_ShaftSilver', base=(0.88, 0.89, 0.92, 1.0),
        metallic=1.0, roughness=0.10)

    # --- 同步带 / 滑轮 ---
    M['MAT_BeltRubber'] = principled(
        'MAT_BeltRubber', base=(0.032, 0.032, 0.036, 1.0),
        metallic=0.0, roughness=0.78, sheen=0.15)
    M['MAT_Pulley'] = principled(
        'MAT_Pulley', base=(0.84, 0.86, 0.89, 1.0),
        metallic=1.0, roughness=0.22)

    # --- 推书机构 ---
    M['MAT_PushPlate'] = principled(
        'MAT_PushPlate', base=(0.95, 0.97, 0.99, 1.0),
        metallic=0.0, roughness=0.09, transmission=0.0, ior=1.47,
        alpha=0.52, specular=0.7, blend=True)
    M['MAT_WedgeAcrylic'] = principled(
        'MAT_WedgeAcrylic', base=(0.98, 0.94, 0.87, 1.0),
        metallic=0.0, roughness=0.16, transmission=0.0, ior=1.47,
        alpha=0.80, specular=0.6, blend=True)

    # --- 电控 / 传感 ---
    M['MAT_PCB'] = principled(
        'MAT_PCB', base=(0.028, 0.13, 0.075, 1.0),
        metallic=0.05, roughness=0.55)
    M['MAT_Chip'] = principled(
        'MAT_Chip', base=(0.06, 0.06, 0.07, 1.0),
        metallic=0.25, roughness=0.40)
    M['MAT_Camera'] = principled(
        'MAT_Camera', base=(0.055, 0.055, 0.062, 1.0),
        metallic=0.15, roughness=0.35)
    M['MAT_LensGlass'] = principled(
        'MAT_LensGlass', base=(0.02, 0.03, 0.06, 1.0),
        metallic=0.30, roughness=0.03, specular=1.0)
    M['MAT_Ultrasonic'] = principled(
        'MAT_Ultrasonic', base=(0.10, 0.28, 0.62, 1.0),
        metallic=0.05, roughness=0.45)
    M['MAT_Wire'] = principled(
        'MAT_Wire', base=(0.055, 0.055, 0.060, 1.0),
        metallic=0.0, roughness=0.70)
    M['MAT_LED'] = principled(
        'MAT_LED', base=(1.0, 0.98, 0.92, 1.0),
        metallic=0.0, roughness=0.30,
        emission=(1.0, 0.97, 0.90, 1.0), emission_strength=9.0)

    # --- 书本（12 种随机纯色书脊）---
    book_colors = [
        (0.62, 0.13, 0.13), (0.13, 0.28, 0.58), (0.11, 0.42, 0.28),
        (0.82, 0.58, 0.12), (0.44, 0.16, 0.50), (0.10, 0.42, 0.48),
        (0.78, 0.30, 0.12), (0.20, 0.24, 0.32), (0.55, 0.47, 0.12),
        (0.24, 0.45, 0.18), (0.60, 0.20, 0.34), (0.30, 0.32, 0.60),
    ]
    for i, c in enumerate(book_colors):
        key = 'MAT_Book_%02d' % i
        M[key] = principled(key, base=(c[0], c[1], c[2], 1.0),
                            metallic=0.0, roughness=0.62)
    M['MAT_BookLabel'] = principled(
        'MAT_BookLabel', base=(0.95, 0.94, 0.90, 1.0),
        metallic=0.0, roughness=0.55)

    # --- 地面 / 摄影棚背板 ---
    M['MAT_Floor'] = principled(
        'MAT_Floor', base=(0.135, 0.142, 0.158, 1.0),
        metallic=0.0, roughness=0.72)
    M['MAT_Backdrop'] = principled(
        'MAT_Backdrop', base=(0.62, 0.665, 0.735, 1.0),
        metallic=0.0, roughness=0.62)

    return M


MATS = None


def ensure_materials():
    global MATS
    MATS = build_materials()
    return MATS


if __name__ == '__main__':
    mats = ensure_materials()
    print('MODULE 01 OK — %d materials' % len(mats))
    for k in sorted(mats):
        print('   ', k)
