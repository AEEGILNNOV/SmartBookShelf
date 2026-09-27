# -*- coding: utf-8 -*-
"""
速定位书格 SmartBookshelf — 模块 03：摄像机 / 灯光 / 渲染 / 存档与导出
Module 03: camera framing, lighting, render, save and export.

用法（由 _build.py 调用）：
    build_camera_and_lights(M)
    render_preview(path, width=1920, height=1080, samples=128)
    save_and_export(blend_path, glb_path)
"""

import bpy
import math
import os
import importlib
from mathutils import Vector, Euler

_lib02 = importlib.import_module('_lib02')
P = _lib02.P


# ============================================================================
# 1. 场景包围盒
# ============================================================================
STAGE_OBJECTS = ('GEO_Floor', 'GEO_Backdrop', 'GEO_Bounce_L', 'CAM_Main')


def scene_bounds(extra_pad=0.0, exclude=STAGE_OBJECTS):
    mn = Vector((1e9, 1e9, 1e9))
    mx = Vector((-1e9, -1e9, -1e9))
    for obj in bpy.data.objects:
        if obj.type not in ('MESH', 'CURVE'):
            continue
        if exclude and obj.name in exclude:
            continue
        for corner in obj.bound_box:
            w = obj.matrix_world @ Vector(corner)
            for i in range(3):
                mn[i] = min(mn[i], w[i])
                mx[i] = max(mx[i], w[i])
    if extra_pad:
        mn -= Vector((extra_pad,) * 3)
        mx += Vector((extra_pad,) * 3)
    return mn, mx


# ============================================================================
# 2. 摄像机（等轴测 / 3-4 俯视视角）
# ============================================================================
def build_camera(M=None, azimuth=42.0, elevation=32.0, focal=48.0,
                 aspect=16.0 / 9.0, margin=1.12, target_frac=0.50):
    """
    以球坐标放置 3/4 俯视（等轴测风格）摄像机。
    通过把全部几何顶点投影到相机 right/up 轴，精确计算包含整机的距离。
    返回 (camera_object, info_dict)
    """
    scene = bpy.context.scene
    mn, mx = scene_bounds()
    center = (mn + mx) * 0.5
    size = mx - mn
    target = Vector((center.x, center.y, center.z + size.z * (target_frac - 0.5)))

    a, e = math.radians(azimuth), math.radians(elevation)
    offset = Vector((math.cos(e) * math.sin(a),
                     math.cos(e) * math.cos(a),
                     math.sin(e)))
    loc = target + offset
    direction = (target - loc).normalized()
    rot = direction.to_track_quat('-Z', 'Y').to_euler()

    up = rot.to_matrix() @ Vector((0.0, 1.0, 0.0))     # 相机 up 轴
    right = rot.to_matrix() @ Vector((1.0, 0.0, 0.0))  # 相机 right 轴
    # 所有网格顶点在 right / up 上的半展宽
    hu = hr = 0.0
    for obj in bpy.data.objects:
        if obj.type not in ('MESH', 'CURVE'):
            continue
        if obj.name in STAGE_OBJECTS:
            continue
        mw = obj.matrix_world
        me = getattr(obj, 'data', None)
        if obj.type == 'MESH' and me is not None:
            coords = [v.co for v in me.vertices]
        else:
            pts = []
            for corner in obj.bound_box:
                pts.append(Vector(corner))
            coords = pts
        for c in coords:
            w = mw @ c
            hr = max(hr, abs((w - target).dot(right)))
            hu = max(hu, abs((w - target).dot(up)))

    sensor_w = scene.camera.data.sensor_width if scene.camera else 36.0
    tan_h = (sensor_w * 0.5) / focal
    tan_v = (sensor_w * 0.5 / aspect) / focal
    dist = max(hr / tan_h, hu / tan_v) * margin + 60.0

    loc = target + offset * dist
    rot = (target - loc).normalized().to_track_quat('-Z', 'Y').to_euler()

    cam_data = bpy.data.cameras.new('CAM_Data_Main')
    cam_data.lens = focal
    cam_data.sensor_width = sensor_w
    cam_data.sensor_fit = 'AUTO'
    cam_data.clip_start = 20.0
    cam_data.clip_end = 40000.0
    cam = bpy.data.objects.new('CAM_Main', cam_data)
    scene.collection.objects.link(cam)
    cam.location = loc
    cam.rotation_euler = rot
    cam.data.dof.use_dof = False
    scene.camera = cam

    info = {'distance': dist, 'half_extent_up': hu, 'half_extent_right': hr,
            'target': tuple(target), 'location': tuple(loc),
            'azimuth': azimuth, 'elevation': elevation, 'focal': focal,
            'half_h_deg': math.degrees(math.atan(tan_h)),
            'half_v_deg': math.degrees(math.atan(tan_v))}
    print('[camera]', {k: (round(v, 3) if isinstance(v, float) else v)
                       for k, v in info.items()})
    return cam, info


# ============================================================================
# 3. 三点布光
# ============================================================================
def add_area_light(name, loc, target, energy, size, color=(1.0, 1.0, 1.0),
                   shape='SQUARE'):
    data = bpy.data.lights.new('LGT_' + name, type='AREA')
    data.energy = energy
    data.size = size
    data.shape = shape
    data.color = color
    obj = bpy.data.objects.new(name, data)
    bpy.context.scene.collection.objects.link(obj)
    obj.location = loc
    d = (Vector(target) - Vector(loc)).normalized()
    obj.rotation_euler = d.to_track_quat('-Z', 'Y').to_euler()
    return obj


def build_lights(target=None, energy_scale=1.0):
    """三点布光：主光（暖）+ 辅光（冷）+ 轮廓光 + 顶部柔光箱。"""
    if target is None:
        mn, mx = scene_bounds()
        target = (mn + mx) * 0.5
    tz = target[2]
    t = (0.0, 0.0, tz)
    lights = []
    # Key light — 前左上，暖白（主照度）
    lights.append(add_area_light('LGT_Key', (-2050.0, 2250.0, tz + 1500.0),
                                 t, 5200.0 * energy_scale, 1500.0,
                                 color=(1.0, 0.962, 0.905)))
    # Fill light — 前右，冷白（补光，压暗部）
    lights.append(add_area_light('LGT_Fill', (2300.0, 1500.0, tz + 520.0),
                                 t, 2400.0 * energy_scale, 2000.0,
                                 color=(0.84, 0.90, 1.0)))
    # Rim light — 后上方，勾边
    lights.append(add_area_light('LGT_Rim', (900.0, -2400.0, tz + 1500.0),
                                 (0.0, -60.0, tz + 120.0), 3200.0 * energy_scale,
                                 1300.0, color=(0.93, 0.96, 1.0), shape='RECTANGLE'))
    # Top soft box — 顶部/内部照明
    lights.append(add_area_light('LGT_Top', (0.0, 60.0, tz + 2300.0),
                                 (0.0, 0.0, tz), 3600.0 * energy_scale, 1800.0,
                                 color=(1.0, 1.0, 0.99)))
    # 内部补光 — 前方低角度，照亮丝杆/电机/同步带等内部机构
    lights.append(add_area_light('LGT_Front', (700.0, 2500.0, tz - 260.0),
                                 (0.0, -40.0, tz - 60.0), 3200.0 * energy_scale,
                                 1700.0, color=(0.97, 0.98, 1.0)))
    # 内部填充 — 柜体内部机构专用柔光
    lights.append(add_area_light('LGT_Interior', (-900.0, 1800.0, tz + 260.0),
                                 (0.0, -80.0, tz), 1800.0 * energy_scale,
                                 1200.0, color=(0.95, 0.97, 1.0)))

    # 世界环境（柔和渐变背景 + 环境补光，为金属/玻璃提供反射环境）
    world = bpy.data.worlds.new('W_Studio')
    bpy.context.scene.world = world
    world.use_nodes = True
    nt = world.node_tree
    for n in list(nt.nodes):
        if n.type not in ('OUTPUT_WORLD',):
            nt.nodes.remove(n)
    out = nt.nodes['World Output']
    tex = nt.nodes.new('ShaderNodeTexGradient')
    tex.gradient_type = 'LINEAR'
    tex.location = (-620.0, 0.0)
    mapping = nt.nodes.new('ShaderNodeMapping')
    mapping.location = (-840.0, 0.0)
    mapping.inputs['Rotation'].default_value = (math.radians(-90.0), 0.0, 0.0)
    ramp = nt.nodes.new('ShaderNodeValToRGB')
    ramp.location = (-380.0, 0.0)
    ramp.color_ramp.elements[0].position = 0.30
    ramp.color_ramp.elements[0].color = (0.085, 0.095, 0.120, 1.0)
    ramp.color_ramp.elements[1].position = 0.92
    ramp.color_ramp.elements[1].color = (0.62, 0.68, 0.78, 1.0)
    bg = nt.nodes.new('ShaderNodeBackground')
    bg.location = (-120.0, 0.0)
    bg.inputs['Strength'].default_value = 3.4
    nt.links.new(tex.inputs['Vector'], mapping.outputs['Vector'])
    nt.links.new(ramp.inputs['Fac'], tex.outputs['Fac'])
    nt.links.new(bg.inputs['Color'], ramp.outputs['Color'])
    nt.links.new(out.inputs['Surface'], bg.outputs['Background'])
    return lights


# ============================================================================
# 4. 地面（承接阴影）
# ============================================================================
def build_floor(M, size=5200.0, z=-28.0):
    bpy.ops.mesh.primitive_plane_add(size=size, location=(0.0, 0.0, z))
    obj = bpy.context.active_object
    obj.name = 'GEO_Floor'
    obj.data.materials.append(M['MAT_Floor'])
    return obj


def build_backdrop(M, cabinet_w=500.0, cabinet_h=1200.0, dist_y=-2100.0):
    """摄影棚背板：为半透明柜体提供明亮环境与柔和反射。"""
    objs = []
    bpy.ops.mesh.primitive_plane_add(size=1.0, location=(0.0, dist_y, 810.0))
    back = bpy.context.active_object
    back.name = 'GEO_Backdrop'
    back.scale = (9000.0, 3600.0, 1.0)
    back.rotation_euler = (math.radians(90.0), 0.0, 0.0)
    bpy.context.view_layer.update()
    bpy.ops.object.transform_apply(location=False, rotation=True, scale=True)
    back.data.materials.append(M['MAT_Backdrop'])
    objs.append(back)
    return objs


# ============================================================================
# 5. 渲染设置与出图
# ============================================================================
def configure_render(width=1920, height=1080, samples=128,
                     use_raytracing=True, ao=True):
    scene = bpy.context.scene
    scene.render.resolution_x = width
    scene.render.resolution_y = height
    scene.render.resolution_percentage = 100
    scene.render.film_transparent = False
    scene.render.image_settings.file_format = 'PNG'
    scene.render.image_settings.color_mode = 'RGB'
    scene.render.image_settings.color_depth = '8'
    scene.render.filepath = '//renders/preview'

    ee = scene.eevee
    for attr, val in (('taa_render_samples', samples),
                      ('use_shadows', True),
                      ('use_raytracing', use_raytracing),
                      ('use_gtao', ao),
                      ('use_soft_shadows', True),
                      ('shadow_ray_count', 4),
                      ('shadow_step_count', 8),
                      ('use_volumetric_shadows', False)):
        if hasattr(ee, attr):
            try:
                setattr(ee, attr, val)
            except Exception:
                pass
    if hasattr(ee, 'gtao_distance'):
        ee.gtao_distance = 0.35
    if hasattr(ee, 'gtao_factor'):
        ee.gtao_factor = 1.1
    if hasattr(ee, 'use_raytracing') and use_raytracing:
        for attr, val in (('ray_tracing_method', 'SCREEN'),
                          ('use_raytracing_denoise', True)):
            if hasattr(ee, attr):
                try:
                    setattr(ee, attr, val)
                except Exception:
                    pass

    vs = scene.view_settings
    for vt in ('Standard', 'AgX', 'Filmic'):
        try:
            vs.view_transform = vt
            break
        except TypeError:
            continue
    try:
        vs.look = 'None'
    except TypeError:
        pass
    vs.exposure = 0.30
    print('[render] engine=%s res=%dx%d samples=%d view=%s'
          % (scene.render.engine, width, height, samples, vs.view_transform))
    return scene


def render_preview(path, width=1920, height=1080, samples=128):
    scene = configure_render(width, height, samples)
    os.makedirs(os.path.dirname(path), exist_ok=True)
    scene.render.filepath = path
    bpy.ops.render.render(write_still=True)
    ok = os.path.exists(path) and os.path.getsize(path) > 0
    print('[render] wrote %s (%s bytes) ok=%s'
          % (path, os.path.getsize(path) if os.path.exists(path) else -1, ok))
    return path


# ============================================================================
# 6. 存档与导出
# ============================================================================
def select_model_only():
    """选中机械本体（排除摄影棚道具/相机/灯光），用于选择性导出。"""
    stage = set(STAGE_OBJECTS)
    bpy.ops.object.select_all(action='DESELECT')
    n = 0
    for o in bpy.data.objects:
        if o.name in stage or o.type in ('LIGHT', 'CAMERA'):
            o.select_set(False)
            continue
        o.select_set(True)
        n += 1
    if n:
        bpy.context.view_layer.objects.active = next(
            o for o in bpy.data.objects if o.select_get())
    print('[export] model objects selected:', n)
    return n


def fix_viewport_clip(margin_start=2000.0, reach=500.0):
    """
    修正 Blender 视口/相机的 3D 裁剪距离。

    本模型 1 unit = 1 mm，整机高约 1318 单位，而视口默认 clip_end = 1000，
    拉远视图时模型会被远裁剪面切掉（表现为"黑屏 / 模型消失"）。
    """
    mn, mx = scene_bounds()
    size = max((mx - mn)[i] for i in range(3))
    clip_start = max(1.0, size / margin_start)
    clip_end = size * reach
    n_space = 0
    for wm in bpy.data.window_managers:
        for window in wm.windows:
            for area in window.screen.areas:
                if area.type != 'VIEW_3D':
                    continue
                for space in area.spaces:
                    if space.type != 'VIEW_3D':
                        continue
                    if hasattr(space, 'clip_start'):
                        space.clip_start = clip_start
                    if hasattr(space, 'clip_end'):
                        space.clip_end = clip_end
                    if hasattr(space, 'region_3d'):
                        try:
                            space.region_3d.view_location = tuple((mn + mx) * 0.5)
                        except Exception:
                            pass
                    n_space += 1
    for cam in bpy.data.cameras:
        cam.clip_start = min(cam.clip_start, clip_start)
        cam.clip_end = max(cam.clip_end, clip_end)
    print('[viewport] clip_start=%.1f clip_end=%.0f applied to %d 3D viewports '
          'and %d cameras' % (clip_start, clip_end, n_space, len(bpy.data.cameras)))
    return clip_start, clip_end


def save_blend(path, overwrite=True):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    if os.path.exists(path) and not overwrite:
        raise RuntimeError('exists: ' + path)
    bpy.ops.wm.save_as_mainfile(filepath=path, compress=False,
                                copy=False)
    print('[save] .blend ->', path, os.path.getsize(path), 'bytes')
    return path


def export_glb(path, overwrite=True, selection_only=False):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    if os.path.exists(path) and not overwrite:
        raise RuntimeError('exists: ' + path)
    op = getattr(bpy.ops.export_scene, 'gltf', None)
    if op is None:
        print('[export] gltf operator unavailable — skipped')
        return None
    if selection_only:
        select_model_only()
    op(filepath=path, export_format='GLB', use_selection=bool(selection_only),
       export_apply=True, export_yup=True, export_materials='EXPORT',
       export_cameras=False, export_lights=False, export_animations=False)
    print('[export] .glb ->', path, os.path.getsize(path), 'bytes')
    return path
