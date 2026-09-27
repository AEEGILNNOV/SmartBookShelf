# -*- coding: utf-8 -*-
"""速定位书格 — 第 11 个集合：爆炸图 + 中文标注

爆炸顺序严格按提示词第十七节：
  柜体 → 书籍 → 升降平台 → 升降丝杆 → 电机3 → 固定推出机构 → 电机1 →
  推书丝杆 → 推书板 → 可调中间夹层 → 电机2 → 调节丝杆
"""
import math
import bpy
from mathutils import Vector

import sbs_params as SP
import sbs_lib as L

D = SP.D
COL = "11_EXPLODED_VIEW"
OFFSET_X = 1150.0          # 爆炸装配体整体平移到整机右侧，便于与装配态对照

# key, 精确名/前缀, 爆炸位移, 视图注记
EXPLODE = [
    dict(key="frame", pre=("Cabinet",), off=(0, 0, 0), lpos=(430, -560, 620),
         lab="① 柜体（铝型材框架）", lp=(0, -450, 330)),
    dict(key="base", pre=("Cabinet_BasePlate",), off=(0, 0, -170)),
    dict(key="top", pre=("TopPanel",), off=(0, 0, 330)),
    dict(key="ctrl", pre=("ControlPanel", "Camera_Module", "Electronics_02"), off=(0, 0, 600), lpos=(1150, -430, 1330),
         lab="顶部控制区（屏 / 摄像头 / 主控）", lp=(0, -330, 150)),
    dict(key="back", pre=("BackPanel",), off=(0, -330, 0)),
    dict(key="sideL", pre=("SidePanel_Left", "SideGlass_Left"), off=(-360, 0, 0)),
    dict(key="sideR", pre=("SidePanel_Right", "SideGlass_Right"), off=(360, 0, 0)),
    dict(key="front", pre=("FrontPanel_", "Port_Lip", "Port_Shelf"), off=(0, 360, 0), lpos=(1150, 760, 40),
         lab="前面板（含可调推出孔）", lp=(0, 430, 250)),
    dict(key="books", pre=("Book_", "Books_Stack"), off=(0, 0, 360), lpos=(1730, 330, 700),
         lab="② 书籍：5 本全部水平平躺", lp=(0, -420, 190)),
    dict(key="plat", pre=("LiftPlatform", "Lift_Guide_Slider", "Lift_NutBracket",
                          "Lift_ScrewNut"), off=(0, 0, 200), lpos=(430, 120, 500),
         lab="③ 升降平台（承载整摞书）", lp=(0, -420, 120)),
    dict(key="lscrew", pre=("Lift_LeadScrew", "BearingBlock_Bracket"), off=(0, 0, 90), lpos=(1730, 560, 430),
         lab="④ T8 升降丝杆 + 螺母", lp=(0, -380, 40)),
    dict(key="lrail", pre=("LiftPlatform_Guide",), off=(-300, 0, 0)),
    dict(key="motor3", pre=("Motor03_Lift_42", "Coupler", "MotorBracket"), off=(0, 0, -250), lpos=(470, -600, 130),
         lab="⑤ 42 步进电机3（升降）", lp=(0, -330, -110)),
    dict(key="pushfix", pre=("Motor01_Push_42", "Coupler_03", "MotorBracket_03",
                             "Push_LeadScrew", "BearingBlock_03", "Push_GuideRail"),
         off=(0, -470, 0),
         lpos=(360, -960, 600)),
    dict(key="push", pre=("Push_",), off=(0, -760, 0),
         lpos=(360, -1180, 250)),
    dict(key="gap", pre=("Middle_Layer", "Gap_Guide", "GapAdjust_"), off=(0, 470, 0), lpos=(1930, 950, 600),
         lab="⑧ 可调中间夹层（改变推出孔高度）", lp=(0, 400, 240)),
    dict(key="gapfix", pre=("Motor02_GapAdjust_42", "Coupler_02",
                            "BearingBlock_02"), off=(0, 760, 0),
         lpos=(1930, 1220, 250)),
    dict(key="elec", pre=("Electronics",), off=(0, -230, 0)),
    dict(key="sens", pre=("HC_SR04", "LimitSwitch"), off=(0, 180, 0),
         lpos=(1730, 220, 900)),
]

CENTER = {}


def _match(name):
    for g in EXPLODE:
        for p in g["pre"]:
            if name == p or name.startswith(p):
                return g
    return None


def build_exploded():
    col = L.get_collection(COL)
    for ob in list(col.objects):
        bpy.data.objects.remove(ob, do_unlink=True)
    bpy.context.scene.frame_set(D["F_START"])
    bpy.context.view_layer.update()
    dg = bpy.context.evaluated_depsgraph_get()

    made = 0
    box = {}
    for src in list(bpy.data.objects):
        if src.type != "MESH" or src.name.startswith(("EXP_", "STAGE_", "Bolt_")):
            continue
        g = _match(src.name)
        if g is None:
            continue
        ev = src.evaluated_get(dg)
        try:
            me = bpy.data.meshes.new_from_object(ev)
        except Exception:
            continue
        me.transform(ev.matrix_world)
        me.name = "EXP_" + src.name
        ob = bpy.data.objects.new("EXP_" + src.name, me)
        ob.location = (g["off"][0] + OFFSET_X, g["off"][1], g["off"][2])
        ob["grp"] = "exploded"
        ob["part"] = "爆炸图：%s" % src.get("part", src.name)
        col.objects.link(ob)
        L.CREATED.append(ob.name)
        made += 1
        k = g["key"]
        for v in me.vertices:
            p = v.co + Vector(ob.location)
            b = box.setdefault(k, [1e9, -1e9, 1e9, -1e9, 1e9, -1e9])
            b[0] = min(b[0], p.x); b[1] = max(b[1], p.x)
            b[2] = min(b[2], p.y); b[3] = max(b[3], p.y)
            b[4] = min(b[4], p.z); b[5] = max(b[5], p.z)
    CENTER.clear()
    for g in EXPLODE:
        b = box.get(g["key"])
        if b:
            CENTER[g["key"]] = ((b[0] + b[1]) / 2, (b[2] + b[3]) / 2, (b[4] + b[5]) / 2, b)
    return {"exploded_objects": made, "groups": len(CENTER), "offset_x": OFFSET_X}


# ------------------------------------------------------------------ 标注
def _label(name, body, loc, size, col, M, azim_deg, plate=True):
    """竖直平面内正对相机方位角的中文标注（文字 + 深色衬板）。
    rotation_euler = (90°, 0, 180° − azim) 才能同时保证：法线朝向相机、文字正立不镜像。"""
    rz = math.radians(180.0 - azim_deg)
    rot = (math.radians(90.0), 0.0, rz)
    n = Vector((math.sin(math.radians(azim_deg)), math.cos(math.radians(azim_deg)), 0.0))
    if plate:
        w = max(1, sum(1 for ch in body if ord(ch) > 127)) * size * 1.05 + \
            max(1, sum(1 for ch in body if ord(ch) <= 127)) * size * 0.58 + size * 1.1
        h = size * 1.5
        cen = Vector(loc) - n * 2.0
        u = Vector((n.y, -n.x, 0.0))
        pts = []
        for sw in (-1, 1):
            for sv in (-1, 1):
                for su in (-1, 1):
                    p = cen + u * (su * w * 0.5) + Vector((0, 0, 1)) * (sv * h * 0.5) \
                        + n * (sw * 1.2)
                    pts.append((p.x, p.y, p.z))
        v = [pts[0], pts[1], pts[3], pts[2], pts[4], pts[5], pts[7], pts[6]]
        faces = [(0, 3, 2, 1), (4, 5, 6, 7), (0, 1, 5, 4),
                 (1, 2, 6, 5), (2, 3, 7, 6), (3, 0, 4, 7)]
        pl = L.mesh_obj(name + "_Plate", v, faces, col, M["label_bg"], "stage",
                        "标注衬板", smooth=[False])
        pl.hide_render = True
    ob = L.text_label(name, body, loc, size, col, M, rot=rot, material=M["label_w"])
    ob.hide_render = True
    return ob


def _plate(name, loc, w, h, right, up, fwd, col, M):
    """深色衬板：整体位于文字之后（fwd 为相机视线方向，正值 = 更远离相机）。"""
    pts = []
    for sw in (-1, 1):
        for sv in (-1, 1):
            for su in (-1, 1):
                p = Vector(loc) + right * (su * w * 0.5) + up * (sv * h * 0.5) \
                    + fwd * (3.0 + sw * 1.0)
                pts.append((p.x, p.y, p.z))
    v = [pts[0], pts[1], pts[3], pts[2], pts[4], pts[5], pts[7], pts[6]]
    faces = [(0, 3, 2, 1), (4, 5, 6, 7), (0, 1, 5, 4),
             (1, 2, 6, 5), (2, 3, 7, 6), (3, 0, 4, 7)]
    ob = L.mesh_obj(name, v, faces, col, M["label_bg"], "stage", "标注衬板",
                    smooth=[False])
    ob.hide_render = True
    return ob


def _label_screen(name, body, view, u, v, size, col, M, views):
    """按“屏幕归一化坐标”放置标注：文字严格平行于像平面，必然可读、不镜像、不越界。"""
    bpy.context.view_layer.update()          # 必须先求值，否则相机 matrix_world 仍是单位阵
    cam = bpy.data.objects["CAM_" + view]
    sc = bpy.context.scene
    ar = sc.render.resolution_x / float(sc.render.resolution_y)
    q = cam.matrix_world.to_quaternion()
    right = q @ Vector((1.0, 0.0, 0.0))
    up = q @ Vector((0.0, 1.0, 0.0))
    fwd = q @ Vector((0.0, 0.0, -1.0))
    target = Vector(views[view]["cam"]["target"])
    dist = (cam.location - target).length
    if cam.data.type == "ORTHO":
        hw = cam.data.ortho_scale * 0.5            # ortho_scale 作用于长边（水平）
        hh = hw / ar
        base = target + fwd * dist * 0.55
    else:
        dd = dist * 0.72
        hw = math.tan(cam.data.angle * 0.5) * dd   # sensor_fit=AUTO → angle 为水平视场角
        hh = hw / ar
        base = cam.location + fwd * dd
    loc = base + right * (u * hw) + up * (v * hh)

    def _width(sz):
        return sum(1 for ch in body if ord(ch) > 127) * sz * 1.06 + \
            sum(1 for ch in body if ord(ch) <= 127) * sz * 0.58 + sz * 1.2

    limit = hw * 1.72                      # 两侧各留 ~14% 余量，保证不越界
    while size > 8.0 and _width(size) > limit:
        size *= 0.92
    w = _width(size)
    _plate(name + "_Plate", loc, w, size * 1.55, right, up, fwd, col, M)
    ob = L.text_label(name, body, loc, size, col, M, rot=cam.rotation_euler,
                      material=M["label_w"])
    ob.hide_render = True
    return ob


def build_labels():
    stage = L.get_collection("90_RENDER_STAGE")
    M = L.build_materials()
    import sbs_stage as ST
    n = 0

    # ---- VIEW4：三电机结构说明
    for nm, body, u, v in (
            ("LBL_VIEW4_1", "① 电机1  固定推出机构：把目标书推出柜体", -0.44, 0.68),
            ("LBL_VIEW4_2", "② 电机2  推出孔厚度调节：中间夹层上下移动", 0.12, 0.86),
            ("LBL_VIEW4_3", "③ 电机3  升降平台驱动：书随平台一起升降", -0.44, -0.78)):
        _label_screen(nm, body, "VIEW4_三电机", u, v, 26.0, stage, M, ST.VIEWS)
        n += 1

    # ---- VIEW8：爆炸顺序（左右两列清单）
    left = [("① 柜体（铝型材框架）", 0.62),
            ("③ 升降平台（承载整摞书）", 0.31),
            ("⑤ 42 步进电机3（升降）", 0.00),
            ("⑥ 固定推出机构", -0.31),
            ("⑦ 推书板 / 推出小车", -0.62)]
    right = [("② 书籍：5 本全部水平平躺", 0.62),
             ("④ T8 升降丝杆 + 丝杆螺母", 0.31),
             ("⑧ 可调中间夹层（改变推出孔高度）", 0.00),
             ("⑨ 42 步进电机2（调节推出孔）", -0.31),
             ("⑩ 传感器：测厚 + 限位开关", -0.62)]
    for i, (body, v) in enumerate(left):
        _label_screen("LBL_VIEW8_L%d" % i, body, "VIEW8_爆炸图", -0.66, v, 25.0,
                      stage, M, ST.VIEWS)
        n += 1
    for i, (body, v) in enumerate(right):
        _label_screen("LBL_VIEW8_R%d" % i, body, "VIEW8_爆炸图", 0.66, v, 25.0,
                      stage, M, ST.VIEWS)
        n += 1

    # ---- VIEW3 / 5 / 6 / 7 图注
    for nm, body, view, u, v in (
            ("LBL_VIEW3_1", "半剖 A-A：升降 / 推出 / 调节三套丝杆传动在柜内的空间关系",
             "VIEW3_剖视", 0.0, -0.80),
            ("LBL_VIEW5_1", "升降平台 + 左右直线导轨 + T8 升降丝杆 + 42 步进电机3",
             "VIEW5_升降平台", 0.0, -0.80),
            ("LBL_VIEW6_1", "可调中间夹层（电机2 驱动）上下移动 → 改变推出孔高度",
             "VIEW6_推出孔", 0.0, -0.82),
            ("LBL_VIEW7_1", "电机1 → 联轴器 → 水平丝杆 → 丝杆螺母 → 推书板",
             "VIEW7_推出机构", 0.0, -0.80)):
        _label_screen(nm, body, view, u, v, 24.0, stage, M, ST.VIEWS)
        n += 1
    return {"labels": n}
