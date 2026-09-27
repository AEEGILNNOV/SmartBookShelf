# -*- coding: utf-8 -*-
"""速定位书格 — 摄影棚 / 灯光 / 九个展示视图相机"""
import math
import os
import bpy
from mathutils import Vector

import sbs_params as SP
import sbs_lib as L

D = SP.D
STAGE = "90_RENDER_STAGE"


def build_stage():
    col = L.get_collection(STAGE)
    M = L.build_materials()
    # 地面 + 远景背板（背板必须远在全部相机轨道之外，否则会挡住主体）
    L.box("STAGE_Floor", -14000, 14000, -14000, 14000, -60, -20, col, M["stage"],
          "stage", "摄影棚地面")
    L.box("STAGE_Back", -16000, 16000, -8200, -8100, -20, 9000, col, M["stage"],
          "stage", "摄影棚远景背板")
    L.box("STAGE_Plinth", -430, 430, -270, 370, -20, 0, col, M["plastic_gry"],
          "stage", "设备底座台")
    # 世界
    w = bpy.data.worlds.get("World") or bpy.data.worlds.new("World")
    bpy.context.scene.world = w
    w.use_nodes = True
    bg = w.node_tree.nodes.get("Background")
    if bg:
        bg.inputs[0].default_value = (0.115, 0.125, 0.150, 1.0)
        bg.inputs[1].default_value = 1.0

    def light(name, kind, loc, energy, size=1400.0, color=(1, 1, 1), target=(0, 0, 360)):
        ld = bpy.data.lights.new(name, type=kind)
        ld.energy = energy
        ld.color = color
        if kind == "AREA":
            ld.size = size
        if kind == "SUN":
            ld.angle = math.radians(4.0)
        ob = bpy.data.objects.new(name, ld)
        ob.location = loc
        ob.rotation_euler = (Vector(target) - Vector(loc)).to_track_quat("-Z", "Y").to_euler()
        col.objects.link(ob)
        L.CREATED.append(name)
        return ob

    # 主光 / 补光 / 轮廓光用 SUN（强度与距离无关，便于稳定控制照度）
    light("LGT_Key", "SUN", (2600, 2600, 3400), 4.2, color=(1.0, 0.96, 0.90))
    light("LGT_Fill", "SUN", (-3200, 2400, 1400), 1.5, color=(0.82, 0.89, 1.0))
    light("LGT_Rim", "SUN", (-1500, -3400, 2800), 3.0, color=(0.92, 0.95, 1.0))
    light("LGT_Top", "AREA", (0, 300, 2300), 5200.0, 3000.0, target=(0, 60, 380))
    light("LGT_Interior", "AREA", (0, 700, 330), 500.0, 1200.0, target=(0, 0, 330))
    return col


# ------------------------------------------------------------------ 相机
def add_camera(name, target, azim, elev, focal=52.0, fit=900.0, margin=1.18,
               ortho=False, col=None):
    sc = bpy.context.scene
    cd = bpy.data.cameras.new(name)
    cd.lens = focal
    cd.clip_start = 5.0
    cd.clip_end = 200000.0
    rx, ry = sc.render.resolution_x, sc.render.resolution_y
    sensor = cd.sensor_width
    fov_h = 2.0 * math.atan(sensor / (2.0 * focal))
    fov_v = 2.0 * math.atan((sensor * ry / rx) / (2.0 * focal))
    fov = min(fov_h, fov_v)
    if ortho:
        cd.type = "ORTHO"
        cd.ortho_scale = fit * margin
        dist = 4200.0
    else:
        dist = (fit * margin) / (2.0 * math.tan(fov / 2.0))
    a, e = math.radians(azim), math.radians(elev)
    loc = Vector(target) + Vector((math.cos(e) * math.sin(a),
                                   math.cos(e) * math.cos(a),
                                   math.sin(e))) * dist
    ob = bpy.data.objects.new(name, cd)
    ob.location = loc
    ob.rotation_euler = (Vector(target) - loc).to_track_quat("-Z", "Y").to_euler()
    (col or L.get_collection(STAGE)).objects.link(ob)
    L.CREATED.append(name)
    bpy.context.view_layer.update()
    return ob


# ------------------------------------------------------------------ 视图定义
VIEWS = {
    "VIEW1_外观": dict(
        cam=dict(target=(0, 0, 372), azim=34, elev=20, focal=52, fit=820),
        hide=(), only=None, label="完整外观图"),
    "VIEW2_内部结构": dict(
        cam=dict(target=(0, 0, 372), azim=22, elev=16, focal=50, fit=820),
        hide=("FrontPanel_", "Port_Lip"), only=None, label="去掉前面板后的内部结构图"),
    "VIEW3_剖视": dict(
        cam=dict(target=(0, 0, 358), azim=90, elev=0, fit=1180, ortho=True),
        hide=("Cabinet", "SidePanel_", "SideGlass_", "FrontPanel_", "Port_Lip",
              "ControlPanel", "Camera_Module", "BackPanel", "Cabinet_BasePlate"),
        cut_x=232.0, only=None, label="机械结构剖视图（半剖 A-A，右视正交）"),
    "VIEW4_三电机": dict(
        cam=dict(target=(20, 0, 320), azim=34, elev=12, focal=48, fit=800),
        hide=("Middle_Layer",),
        only=("Motor01_Push_42", "Motor02_GapAdjust_42", "Motor03_Lift_42", "Coupler",
              "Push_LeadScrew", "Lift_LeadScrew", "GapAdjust_LeadScrew", "BearingBlock",
              "Push_Plate", "Push_Carriage_Beam", "Push_ScrewNut", "Push_GuideRail",
              "Middle_Layer", "Gap_Guide", "LiftPlatform_Guide", "LiftPlatform",
              "Book_", "Books_Stack", "LBL_", "Bolt_M"),
        label="三电机结构说明图（剖去柜体）"),
    "VIEW5_升降平台": dict(
        cam=dict(target=(0, 10, 300), azim=28, elev=18, focal=58, fit=540),
        hide=("FrontPanel_", "SidePanel_Left", "SideGlass_Left", "Port_Lip"),
        only=None, label="升降平台结构放大图"),
    "VIEW6_推出孔": dict(
        cam=dict(target=(0, 128, 442), azim=24, elev=6, focal=62, fit=390),
        hide=("SidePanel_", "SideGlass_"),
        only=None, label="可调推出孔结构放大图"),
    "VIEW7_推出机构": dict(
        cam=dict(target=(60, -60, 388), azim=-46, elev=26, focal=56, fit=520),
        hide=("FrontPanel_", "SidePanel_", "SideGlass_", "Port_Lip", "Port_Shelf",
              "Middle_Layer", "Gap_Guide", "GapAdjust_"),
        only=None, label="固定推出机构放大图"),
    "VIEW8_爆炸图": dict(
        cam=dict(target=(1150, 0, 470), azim=30, elev=14, focal=50, fit=1820),
        hide=(), only=("EXP_", "LBL_"),
        label="整机爆炸图"),
    "VIEW9_流程": dict(
        cam=dict(target=(0, 0, 372), azim=26, elev=14, focal=50, fit=820),
        hide=("FrontPanel_", "Port_Lip"), only=None, label="工作流程图"),
}


def set_visibility(view):
    cfg = VIEWS[view]
    only = cfg.get("only")
    hide = tuple(cfg.get("hide", ()))
    cut_x = cfg.get("cut_x")
    tag = view.split("_")[0]          # VIEW4 / VIEW8 ...
    for ob in bpy.data.objects:
        if ob.type not in ("MESH", "FONT", "CURVE"):
            continue
        nm = ob.name
        if nm.startswith(("STAGE_", "LGT_", "CAM_")):
            ob.hide_render = False
            continue
        if nm.startswith("EXP_"):                       # 爆炸件只在爆炸图里出现
            ob.hide_render = view != "VIEW8_爆炸图"
            continue
        if nm.startswith("LBL_"):                       # 标注只在本视图出现
            ob.hide_render = not nm.startswith("LBL_" + tag + "_")
            continue
        if only is not None:
            vis = any(nm.startswith(p) for p in only)
        else:
            vis = True
        if vis and hide:
            vis = not any(nm.startswith(p) for p in hide)
        if vis and cut_x is not None:                   # 半剖：切掉近侧一半
            bb = ob.bound_box
            xs = [bb[i][0] for i in range(8)]
            if (min(xs) + max(xs)) * 0.5 + ob.location.x > cut_x:
                vis = False
        ob.hide_render = not vis


def apply_render_settings(w=1920, h=1080, samples=64, raytracing=True):
    sc = bpy.context.scene
    for cand in ("BLENDER_EEVEE_NEXT", "BLENDER_EEVEE"):
        try:
            sc.render.engine = cand
            break
        except (TypeError, ValueError):
            continue
    sc.render.resolution_x = w
    sc.render.resolution_y = h
    sc.render.resolution_percentage = 100
    sc.render.film_transparent = False
    ims = sc.render.image_settings
    if hasattr(ims, "media_type"):        # Blender 5.x：FFMPEG 只对 VIDEO 媒体类型开放
        ims.media_type = "IMAGE"
    ims.file_format = "PNG"
    ims.color_mode = "RGB"
    ee = getattr(sc, "eevee", None)
    if ee is not None:
        for attr, val in (("taa_render_samples", samples), ("use_raytracing", raytracing),
                          ("use_shadows", True), ("use_gtao", True),
                          ("use_volumetric_shadows", False)):
            if hasattr(ee, attr):
                try:
                    setattr(ee, attr, val)
                except Exception:
                    pass
    vs = sc.view_settings
    try:
        vs.view_transform = "Standard"
    except Exception:
        pass
    vs.exposure = 0.0
    vs.look = "None"
    return {"engine": sc.render.engine, "res": [w, h], "raytracing": raytracing}


def build_cameras():
    col = L.get_collection(STAGE)
    cams = {}
    for name, cfg in VIEWS.items():
        cams[name] = add_camera("CAM_" + name, col=col, **cfg["cam"])
    bpy.context.scene.camera = cams["VIEW1_外观"]
    return cams


def render_view(view, out_path, samples=64, w=1920, h=1080):
    sc = bpy.context.scene
    apply_render_settings(w, h, samples)
    set_visibility(view)
    sc.camera = bpy.data.objects["CAM_" + view]
    sc.render.filepath = out_path
    bpy.ops.render.render(write_still=True)
    return {"view": view, "path": out_path,
            "bytes": os.path.getsize(out_path) if os.path.exists(out_path) else 0}
