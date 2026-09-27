# -*- coding: utf-8 -*-
"""速定位书格 — 15 秒工作流程动画（24 fps / 360 帧）

严格按提示词第十五节时间轴：
  0–2 s   初始状态：一摞书平躺在升降平台上
  2–6 s   电机3 工作：升降平台带着整摞书上升到固定推出位
  6–8 s   电机2 工作：中间夹层上下移动，调整推出孔高度
  8–11 s  电机1 工作：推书板前移，目标书被推出推出孔
  11–13 s 推书板返回
  13–15 s 完成取书状态
丝杆转动量由被驱动件的实际位移逐帧反解，保证螺纹与直线运动严格同步。
"""
import math
import bpy

import sbs_params as SP
import sbs_lib as L

D = SP.D
TAU = math.pi * 2.0
O = lambda n: bpy.data.objects[n]


def _key(ob, path, idx, pairs, interp="BEZIER"):
    for f, v in pairs:
        if path == "location":
            ob.location[idx] = v
        else:
            ob.rotation_euler[idx] = v
        ob.keyframe_insert(data_path=path, index=idx, frame=f)
    if ob.animation_data and ob.animation_data.action:
        for fc in _fcurves(ob.animation_data.action):
            if not fc.data_path.endswith(path) or fc.array_index != idx:
                continue
            for kp in fc.keyframe_points:
                kp.interpolation = interp


def _fcurves(action):
    """Blender 5.x 分层 Action 兼容读取。"""
    if hasattr(action, "fcurves") and len(action.fcurves):
        return list(action.fcurves)
    out = []
    for layer in getattr(action, "layers", []):
        for strip in getattr(layer, "strips", []):
            for bag in getattr(strip, "channelbags", []):
                out.extend(getattr(bag, "fcurves", []))
    return out


def build_animation():
    sc = bpy.context.scene
    sc.render.fps = D["FPS"]
    sc.frame_start = D["F_START"]
    sc.frame_end = D["F_END"]

    plat, mid, car = O("LiftPlatform"), O("Middle_Layer"), O("Push_Carriage")
    book3 = O("Book_03")
    ls, gs, ps = O("Lift_LeadScrew"), O("GapAdjust_LeadScrew"), O("Push_LeadScrew")

    # ---------------------------------------------------------------- 数值
    PZ0 = D["PLAT_TOP_HOME"]          # 200
    PZ1 = D["PLAT_TOP_ATPORT"]        # 340 → 目标书下沿正好 380
    GZ0, GZMAX, GZT = D["GAP_BOT_HOME"], D["GAP_BOT_MAX"], D["GAP_BOT_TARGET"]
    CY0, CYC, CY1 = D["PUSH_NUT_Y_RET"], D["PUSH_CONTACT_NUT_Y"], D["PUSH_NUT_Y_EXT"]
    B3Y0 = D["BOOK_CY"]
    B3Y1 = B3Y0 + D["BOOK3_PUSH_TRAVEL"]

    fa, fb = D["F_LIFT_A"], D["F_LIFT_B"]
    fm, fgb = D["F_GAP_MID"], D["F_GAP_B"]
    fc, fpb, frb = D["F_PUSH_CONTACT"], D["F_PUSH_B"], D["F_RET_B"]
    fe = D["F_END"]

    # ------------------------------------------------- 电机3：升降平台（只动 Z）
    _key(plat, "location", 2, [(1, PZ0), (fa, PZ0), (fb, PZ1), (fe, PZ1)])

    # ---------------------------------------- 电机2：中间夹层（只动 Z，改变推出孔）
    _key(mid, "location", 2, [(1, GZ0), (fb, GZ0), (fm, GZMAX), (fgb, GZT), (fe, GZT)])

    # ------------------------------------------- 电机1：推出小车（只动 Y，前后推书）
    _key(car, "location", 1, [(1, CY0), (fgb, CY0), (fc, CYC), (fpb, CY1),
                              (frb, CY0), (fe, CY0)])

    # --------------------- 目标书 + 三根丝杆：逐帧由被驱动件反解，保证零穿透、零脱节
    frames = list(range(D["F_START"], fe + 1))
    for f in frames:
        sc.frame_set(f)
        bpy.context.view_layer.update()
        # 书脊始终紧贴推书板正面：推程 = 小车相对“接触点”的位移，负值取 0
        book3.location[1] = B3Y0 + max(0.0, car.location.y - CYC)
        book3.keyframe_insert(data_path="location", index=1, frame=f)
        # 丝杆转角由直线位移反解（T8 导程 4 mm）
        ls.rotation_euler[2] = -TAU * (plat.location.z - PZ0) / D["LIFT_PITCH"]
        ls.keyframe_insert(data_path="rotation_euler", index=2, frame=f)
        gs.rotation_euler[2] = -TAU * (mid.location.z - GZ0) / D["GAP_PITCH"]
        gs.keyframe_insert(data_path="rotation_euler", index=2, frame=f)
        ps.rotation_euler[1] = -TAU * (car.location.y - CY0) / D["PUSH_PITCH"]
        ps.keyframe_insert(data_path="rotation_euler", index=1, frame=f)
    for s in (book3, ls, gs, ps):
        if s.animation_data and s.animation_data.action:
            for fc_ in _fcurves(s.animation_data.action):
                for kp in fc_.keyframe_points:
                    kp.interpolation = "LINEAR"

    sc.frame_set(D["F_START"])
    bpy.context.view_layer.update()

    acts = sorted(a.name for a in bpy.data.actions)
    return {
        "actions": len(acts),
        "action_names": acts,
        "frames": [D["F_START"], fe],
        "fps": D["FPS"],
        "duration_s": round((fe - D["F_START"]) / float(D["FPS"]), 2),
        "platform_lift_mm": round(PZ1 - PZ0, 1),
        "slot_range_mm": [D["GAP_BOT_MIN"] - D["PORT_Z0"], D["GAP_BOT_MAX"] - D["PORT_Z0"]],
        "slot_at_target_mm": round(GZT - D["PORT_Z0"], 1),
        "push_stroke_mm": round(CY1 - CY0, 1),
        "book_protrusion_mm": round(D["BOOK_FRONT_OUT"], 1),
    }
