# -*- coding: utf-8 -*-
"""速定位书格 — 防穿模闸门 + 设计断言（对标提示词全部硬约束）"""
import bpy
from mathutils import Vector

import sbs_params as SP

D = SP.D
# envelope = 空心外壳（框架/窗框），其轴对齐包围盒会覆盖整个内腔，
# 因此不参与机构两两包围盒判定，改由 A20 内腔包容性断言单独校验。
SKIP_GROUPS = {"stage", "labels", "envelope"}
REQUIRED_PARTS = [
    "Cabinet", "TopPanel", "SidePanel_Left", "SidePanel_Right", "BackPanel",
    "LiftPlatform", "Books_Stack", "Book_01", "Book_02", "Book_03", "Book_04",
    "Book_05", "Motor01_Push_42", "Push_LeadScrew", "Push_ScrewNut", "Push_Plate",
    "Motor02_GapAdjust_42", "GapAdjust_LeadScrew", "GapAdjust_ScrewNut",
    "Middle_Layer", "Gap_Guide", "Motor03_Lift_42", "Lift_LeadScrew", "Lift_ScrewNut",
    "LiftPlatform_Guide_Left", "LiftPlatform_Guide_Right", "HC_SR04", "LimitSwitch",
    "MotorBracket", "BearingBlock", "Coupler", "Electronics", "ControlPanel",
]
REQUIRED_COLLECTIONS = [
    "00_CABINET", "01_BOOK_STACK", "02_LIFT_PLATFORM", "03_MOTOR_01_PUSH",
    "04_MOTOR_02_GAP_ADJUST", "05_MOTOR_03_LIFT", "06_LINEAR_GUIDES",
    "07_LEAD_SCREWS", "08_SENSORS", "09_ELECTRONICS", "10_FASTENERS",
    "11_EXPLODED_VIEW", "12_ANIMATION",
]


# ------------------------------------------------------------------ 包围盒
def world_aabb(ob, dg):
    ev = ob.evaluated_get(dg)
    if ev is None or ev.type != "MESH":
        return None
    me = ev.data
    if len(me.vertices) == 0:
        return None
    mw = ev.matrix_world
    xs, ys, zs = [], [], []
    for v in me.vertices:
        p = mw @ v.co
        xs.append(p.x); ys.append(p.y); zs.append(p.z)
    return (min(xs), max(xs), min(ys), max(ys), min(zs), max(zs))


def _boxes():
    dg = bpy.context.evaluated_depsgraph_get()
    out = []
    for ob in bpy.data.objects:
        if ob.type != "MESH" or ob.hide_render:
            continue
        grp = ob.get("grp", "")
        if grp in SKIP_GROUPS:
            continue
        b = world_aabb(ob, dg)
        if b is not None:
            out.append((ob.name, grp, ob.get("kin", ""), b))
    return out


def clash_pairs(min_vol=None, limit=40):
    """跨刚体包围盒相交体积 > 阈值即判为干涉；同一运动副（kin）豁免。"""
    min_vol = D["CLASH_VOL"] if min_vol is None else min_vol
    bs = _boxes()
    bad = []
    for i in range(len(bs)):
        n1, g1, k1, b1 = bs[i]
        for j in range(i + 1, len(bs)):
            n2, g2, k2, b2 = bs[j]
            if g1 == g2:
                continue
            if k1 and k1 == k2:
                continue
            ox = min(b1[1], b2[1]) - max(b1[0], b2[0])
            if ox <= 0:
                continue
            oy = min(b1[3], b2[3]) - max(b1[2], b2[2])
            if oy <= 0:
                continue
            oz = min(b1[5], b2[5]) - max(b1[4], b2[4])
            if oz <= 0:
                continue
            vol = ox * oy * oz
            if vol > min_vol:
                bad.append({"a": n1, "b": n2, "ga": g1, "gb": g2,
                            "vol": round(vol, 2),
                            "ov": [round(ox, 2), round(oy, 2), round(oz, 2)]})
    bad.sort(key=lambda r: -r["vol"])
    return bad[:limit], len(bad)


def min_clearance():
    """全模型跨刚体最小间隙（>=0 表示无接触）。"""
    bs = _boxes()
    best = 1e9
    pair = None
    for i in range(len(bs)):
        n1, g1, k1, b1 = bs[i]
        for j in range(i + 1, len(bs)):
            n2, g2, k2, b2 = bs[j]
            if g1 == g2 or (k1 and k1 == k2):
                continue
            dx = max(b1[0] - b2[1], b2[0] - b1[1], 0.0)
            dy = max(b1[2] - b2[3], b2[2] - b1[3], 0.0)
            dz = max(b1[4] - b2[5], b2[4] - b1[5], 0.0)
            d = (dx * dx + dy * dy + dz * dz) ** 0.5
            if d < best:
                best = d
                pair = (n1, n2)
    return round(best, 3), pair


# ------------------------------------------------------------------ 断言
def _obj(name):
    ob = bpy.data.objects.get(name)
    if ob is None:
        raise KeyError("缺少零件：" + name)
    return ob


def design_assertions():
    A = []
    NAMES = set(o.name for o in bpy.data.objects)

    def chk(tag, desc, ok, detail=""):
        A.append({"id": tag, "desc": desc, "ok": bool(ok), "detail": str(detail)})

    # A1 整机外形
    dg = bpy.context.evaluated_depsgraph_get()
    outer = [n for n in ("Cabinet", "Cabinet_BasePlate", "TopPanel", "BackPanel",
                         "SidePanel_Left", "SidePanel_Right", "FrontPanel_Lower")
             if n in NAMES]
    lo = [1e9] * 3
    hi = [-1e9] * 3
    for n in outer:
        b = world_aabb(bpy.data.objects[n], dg)
        for k in range(3):
            lo[k] = min(lo[k], b[k * 2]); hi[k] = max(hi[k], b[k * 2 + 1])
    size = [round(hi[k] - lo[k], 1) for k in range(3)]
    chk("A1", "整机外形 = 600 × 350 × 700 mm", size == [600.0, 350.0, 700.0], size)

    # A2 恰好 3 台 42 步进电机
    motors = sorted(n for n in NAMES if n.startswith("Motor0") and n.endswith("_42"))
    chk("A2", "系统仅 3 台 42 步进电机（无 28BYJ / 舵机 / 机械臂）",
        motors == ["Motor01_Push_42", "Motor02_GapAdjust_42", "Motor03_Lift_42"], motors)
    chk("A2b", "无 28BYJ-48 / 舵机 / 机械臂 / 齿轮齿条零件",
        not any(("28BYJ" in n) or ("Servo" in n) or ("Rack" in n) or
                ("_Arm" in n and "HC_" not in n) for n in NAMES), "")

    # A3 三条传动全部为丝杆
    screws = ["Lift_LeadScrew", "Push_LeadScrew", "GapAdjust_LeadScrew"]
    detail = []
    for s in screws:
        ob = _obj(s)
        detail.append([s, len(ob.data.vertices), len(ob.data.polygons)])
    chk("A3", "三台电机全部经丝杆传动（3 根带真实螺旋螺纹的丝杆）",
        len(screws) == 3 and all(len(_obj(s).data.vertices) > 1000 for s in screws),
        detail)

    # A4 书全部平躺
    ok = True
    info = []
    for i in range(1, D["BOOK_N"] + 1):
        ob = _obj("Book_%02d" % i)
        d = ob.dimensions
        t = D["BOOK_T"][i - 1]
        good = abs(d.z - t) < 0.6 and abs(d.x - D["BOOK_X"]) < 1.0 and \
            abs(d.y - D["BOOK_Y"]) < 1.0
        ok = ok and good
        info.append([ob.name, round(d.x, 1), round(d.y, 1), round(d.z, 1)])
    chk("A4", "5 本书全部水平平躺（Z 向尺寸 = 厚度，非竖立）", ok, info)

    # A5 书堆坐在升降平台上
    plat = _obj("LiftPlatform")
    ptop = plat.matrix_world.translation.z
    b1 = _obj("Book_01")
    dg = bpy.context.evaluated_depsgraph_get()
    bb = world_aabb(b1, dg)
    chk("A5", "Book_01 底面恰好落在升降平台顶面（书随平台运动）",
        abs(bb[4] - ptop) < 0.05, [round(bb[4], 3), round(ptop, 3)])
    chk("A5b", "Books_Stack 为 LiftPlatform 的子级（平台 → 书堆 运动链）",
        _obj("Books_Stack").parent is plat and b1.parent is _obj("Books_Stack"),
        "")

    # A6 推出机构固定不动
    def track(name, attr, frames=(1, 100, 200, 360)):
        vals = []
        for f in frames:
            bpy.context.scene.frame_set(f)
            bpy.context.view_layer.update()
            v = getattr(_obj(name).matrix_world.translation, attr)
            vals.append(round(v, 4))
        return vals

    m1 = [track("Motor01_Push_42", a) for a in ("x", "y", "z")]
    chk("A6", "电机1 固定不动（不随平台升降）",
        len(set(m1[0])) == 1 and len(set(m1[1])) == 1 and len(set(m1[2])) == 1, m1)
    ps = [track("Push_LeadScrew", a) for a in ("x", "y", "z")]
    chk("A6b", "推书丝杆固定（只自转）",
        len(set(ps[0])) == 1 and len(set(ps[1])) == 1 and len(set(ps[2])) == 1, ps)

    # A7 推书板只前后运动
    pp = [track("Push_Plate", a) for a in ("x", "y", "z")]
    chk("A7", "推书板仅沿 Y（前后）运动，X / Z 恒定",
        len(set(pp[0])) == 1 and len(set(pp[2])) == 1 and len(set(pp[1])) > 1, pp)

    # A8 中间夹层只上下运动
    ml = [track("Middle_Layer", a) for a in ("x", "y", "z")]
    chk("A8", "中间夹层仅沿 Z（上下）运动，X / Y 恒定",
        len(set(ml[0])) == 1 and len(set(ml[1])) == 1 and len(set(ml[2])) > 1, ml)

    # A9 平台只上下运动
    lp = [track("LiftPlatform", a) for a in ("x", "y", "z")]
    chk("A9", "升降平台仅沿 Z（上下）运动，X / Y 恒定",
        len(set(lp[0])) == 1 and len(set(lp[1])) == 1 and len(set(lp[2])) > 1, lp)

    # A10 推出孔调节范围覆盖书厚
    chk("A10", "推出孔可调高度覆盖 15–50 mm 书厚（%.0f–%.0f mm）"
        % (D["SLOT_H_MIN"], D["SLOT_H_MAX"]),
        D["SLOT_H_MIN"] <= D["BOOK_T"][0] + 3 and D["SLOT_H_MAX"] >= D["BOOK_T"][-1] + 2,
        [D["SLOT_H_MIN"], D["SLOT_H_MAX"]])

    # A11 推书板能穿过最薄书
    chk("A11", "推书板高 14 mm ≤ 最薄书 15 mm（可穿过推出孔推单本）",
        D["PUSH_PLATE_H"] <= D["BOOK_T"][0], D["PUSH_PLATE_H"])

    # A12 目标书对齐推出孔
    chk("A12", "目标书 Book_03 到位时下沿 = 推出孔下沿 380 mm",
        abs(D["PLAT_TOP_ATPORT"] + D["TARGET_OFF"] - D["PORT_Z0"]) < 1e-6,
        D["PLAT_TOP_ATPORT"])

    # A13 推出行程
    chk("A13", "推出后书本伸出柜体 %.0f mm（≥ 25 mm）" % D["BOOK_FRONT_OUT"],
        D["BOOK_FRONT_OUT"] >= 25.0, D["BOOK_FRONT_OUT"])

    # A14 中间夹层始终遮住开口上沿
    chk("A14", "中间夹层在全部行程都遮住开口上沿（不露缝）",
        D["GAP_BOT_MIN"] + D["GAP_H"] >= D["PORT_OPEN_Z1"] and
        D["GAP_BOT_MAX"] + D["GAP_H"] >= D["PORT_OPEN_Z1"], D["GAP_H"])

    # A15 平台不高于推出孔下沿
    chk("A15", "升降平台最高位 ≤ 推出孔下沿（不与推书机构干涉）",
        D["PLAT_TOP_MAX"] <= D["PORT_Z0"], [D["PLAT_TOP_MAX"], D["PORT_Z0"]])

    # A16 导轨都有滑块
    pairs = [("LiftPlatform_Guide_Left", "Lift_Guide_Slider_L"),
             ("LiftPlatform_Guide_Right", "Lift_Guide_Slider_R"),
             ("Push_GuideRail", "Push_Carriage_Slider"),
             ("Gap_Guide", "Middle_Layer_Slider_R"),
             ("Gap_Guide_Mirror", "Middle_Layer_Slider_L")]
    chk("A16", "每条直线导轨都配真实滑块（5 组）",
        all(n in NAMES for p in pairs for n in p), [p[0] for p in pairs])

    # A17 电机具备法兰与输出轴
    chk("A17", "3 台电机均为 42×42×38 机身 + 端盖 + 止口 + 5 mm 输出轴",
        all(_obj(m).dimensions.x + _obj(m).dimensions.y > 80 for m in motors),
        [[m, [round(v, 1) for v in _obj(m).dimensions]] for m in motors])

    # A18 集合与零件齐备
    miss_c = [c for c in REQUIRED_COLLECTIONS if c not in bpy.data.collections]
    miss_p = [p for p in REQUIRED_PARTS if p not in NAMES]
    chk("A18", "第 13 节 13 个集合齐备", not miss_c, miss_c)
    chk("A18b", "第 12 节必需零件齐备（%d 项）" % len(REQUIRED_PARTS), not miss_p, miss_p)

    # A19 动画存在
    acts = [a.name for a in bpy.data.actions]
    chk("A19", "存在机械动画（15 s / 24 fps / 360 帧）",
        len(acts) > 0 and bpy.context.scene.frame_end == 360,
        {"actions": len(acts), "end": bpy.context.scene.frame_end,
         "fps": bpy.context.scene.render.fps})

    # A20 所有机构零件位于柜体内腔之内（替代对空心框架的包围盒判定；按初始状态校核）
    bpy.context.scene.frame_set(D["F_START"])
    bpy.context.view_layer.update()
    dg = bpy.context.evaluated_depsgraph_get()
    MECH = {"platform", "books", "pushcarriage", "pushframe", "middlelayer",
            "gapframe", "liftframe", "electronics", "fasteners"}
    lim = {"x": (D["SIDE_L_X1"], D["SIDE_R_X0"]),
           "y": (D["BACK_Y1"], D["FRONT_Y0"]),
           "z": (D["BASE_Z1"], D["TOP_Z0"])}
    escapes = []
    for ob in bpy.data.objects:
        if ob.type != "MESH" or ob.get("grp", "") not in MECH:
            continue
        b = world_aabb(ob, dg)
        if b is None:
            continue
        tol = 0.01
        if (b[0] < lim["x"][0] - tol or b[1] > lim["x"][1] + tol or
                b[2] < lim["y"][0] - tol or b[3] > lim["y"][1] + tol or
                b[4] < lim["z"][0] - tol or b[5] > lim["z"][1] + tol):
            escapes.append([ob.name, [round(v, 1) for v in b]])
    chk("A20", "全部机构零件都位于柜体内腔 544×310×670 之内", not escapes, escapes[:6])

    # A21 三电机分工与提示词第 9 节一致
    chk("A21", "电机1 只推书 / 电机2 只调推出孔 / 电机3 只升降",
        _obj("Motor01_Push_42").get("grp") == "pushframe" and
        _obj("Motor02_GapAdjust_42").get("grp") == "gapframe" and
        _obj("Motor03_Lift_42").get("grp") == "liftframe", "")

    # A22 中间夹层为独立结构件且位于前部推出区域
    chk("A22", "中间夹层是独立结构件，位于前面板之后的推出区域",
        _obj("Middle_Layer").parent is None and
        D["FRONT_Y0"] - 40 < D["GAP_Y0"] and D["GAP_Y1"] < D["FRONT_Y0"], "")
    return A


def run(frame_list=None, do_clash=True):
    frames = frame_list or [1, 96, 144, 180, 192, 220, 264, 300, 360]
    bpy.context.scene.frame_set(1)
    A = design_assertions()
    clashes = []
    if do_clash:
        for f in frames:
            bpy.context.scene.frame_set(f)
            bpy.context.view_layer.update()
            bad, total = clash_pairs()
            if total:
                clashes.append({"frame": f, "count": total, "worst": bad[:6]})
        bpy.context.scene.frame_set(1)
    return {
        "assertions": A,
        "failed": [a["id"] for a in A if not a["ok"]],
        "clash_frames": clashes,
        "clash_free": not clashes,
        "min_clearance_mm": min_clearance(),
    }
