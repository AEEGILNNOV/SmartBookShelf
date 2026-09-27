# -*- coding: utf-8 -*-
"""速定位书格 — 自动生成结构树 / 参数表 / 运动链 / 校验结论（中文文档）"""
import io
import os
import bpy

import sbs_params as SP
import sbs_lib as L

D = SP.D

GROUPS = [
    ("一、整机外形与柜体", ["CAB_W", "CAB_D", "CAB_H", "PANEL_T", "POST", "BASE_T",
                            "TOP_T", "FRAME_T", "PORT_X", "PORT_Z0", "PORT_OPEN_Z1"]),
    ("二、书籍（全部水平平躺叠放）", ["BOOK_N", "BOOK_X", "BOOK_Y", "BOOK_T", "BOOK_Y0",
                                       "BOOK_Y1", "BOOK_CY", "BOOK_STACK_H", "TARGET_IDX",
                                       "TARGET_OFF", "TARGET_T"]),
    ("三、升降平台与电机3", ["PLAT_X", "PLAT_Y0", "PLAT_Y1", "PLAT_T", "PLAT_TOP_MIN",
                             "PLAT_TOP_MAX", "PLAT_TOP_HOME", "PLAT_TOP_ATPORT",
                             "PLAT_LIFT", "LIFT_SX", "LIFT_SY", "LIFT_SCREW_R",
                             "LIFT_THREAD_R", "LIFT_PITCH", "LIFT_SCREW_Z0",
                             "LIFT_SCREW_Z1", "LIFT_NUT_DZ", "LRAIL_X", "LRAIL_Z0",
                             "LRAIL_Z1"]),
    ("四、固定推出机构与电机1", ["PUSH_AX_X", "PUSH_AX_Z", "PUSH_MOTOR_Y0", "PUSH_SCREW_Y0",
                                  "PUSH_SCREW_Y1", "PUSH_PITCH", "PUSH_NUT_Y_RET",
                                  "PUSH_NUT_Y_EXT", "PUSH_CONTACT_NUT_Y", "PUSH_STROKE",
                                  "PUSH_PLATE_X", "PUSH_PLATE_H", "PUSH_PLATE_Z0",
                                  "PUSH_RAIL_X", "BOOK3_PUSH_TRAVEL", "BOOK_FRONT_OUT"]),
    ("五、可调推出孔与电机2", ["GAP_X", "GAP_Y0", "GAP_Y1", "GAP_H", "GAP_BOT_MIN",
                                "GAP_BOT_MAX", "GAP_BOT_HOME", "GAP_BOT_TARGET",
                                "SLOT_H_MIN", "SLOT_H_MAX", "GAP_SX", "GAP_SY",
                                "GAP_PITCH", "GAP_SCREW_Z0", "GAP_SCREW_Z1",
                                "GAP_NUT_DZ", "GG_X", "GG_Z0", "GG_Z1"]),
    ("六、传感 / 电控 / 控制面板", ["SONAR_X", "SONAR_Y", "SONAR_Z", "SONAR_S",
                                     "PSU_X0", "PSU_X1", "PSU_Z0", "PSU_Z1",
                                     "DRV_X0", "DRV_X1", "MB_X0", "MB_X1", "MB_Z0",
                                     "MB_Z1", "CP_X", "CP_Z0", "CP_Z1"]),
    ("七、动画时间轴（24 fps）", ["F_START", "F_END", "F_LIFT_A", "F_LIFT_B", "F_GAP_MID",
                                   "F_GAP_B", "F_PUSH_CONTACT", "F_PUSH_B", "F_RET_B"]),
]

CHAIN = [
    ("电机1 Motor01_Push_42", "固定推出机构", "固定于柜体后部",
     "42步进电机 → 联轴器 → 水平 T8 丝杆 → 丝杆螺母 → 推出小车 → 推书板",
     "推书板沿 Y 前后移动，把已到位的目标书从推出孔推出柜体；电机与丝杆自身不动"),
    ("电机2 Motor02_GapAdjust_42", "推出孔厚度调节", "固定于前面板内侧上方",
     "42步进电机 → 联轴器 → 垂直 T8 丝杆 → 丝杆螺母 → 可调中间夹层",
     "中间夹层沿 Z 上下移动，改变前面板推出孔的有效高度；推出机构完全不动"),
    ("电机3 Motor03_Lift_42", "书籍升降平台", "固定于柜体底部",
     "42步进电机 → 联轴器 → 垂直 T8 丝杆 → 丝杆螺母 → 升降平台 → 书籍堆",
     "平台带整摞书沿 Z 升降，把目标书送到固定推出位；平台不水平移动、不旋转"),
]


def param_table():
    out = []
    for title, keys in GROUPS:
        out.append("")
        out.append("【%s】" % title)
        out.append("-" * 78)
        for k in keys:
            if k not in D:
                continue
            v = D[k]
            if isinstance(v, float):
                v = round(v, 2)
            out.append("  %-22s = %s" % (k, v))
    return "\n".join(out)


def structure_tree():
    out = []
    for col in sorted(bpy.data.collections, key=lambda c: c.name):
        if col.name.startswith("90_"):
            continue
        objs = sorted(col.objects, key=lambda o: o.name)
        out.append("")
        out.append("● %s（%d 个对象）" % (col.name, len(objs)))
        for o in objs:
            kind = {"MESH": "网格", "EMPTY": "空物体", "FONT": "文字",
                    "CURVE": "文字"}.get(o.type, o.type)
            par = ("  ←父级:%s" % o.parent.name) if o.parent else ""
            out.append("    - %-34s [%s] %s%s"
                       % (o.name, kind, o.get("part", ""), par))
    return "\n".join(out)


def write_docs(path, validation=None):
    st = L.stats()
    buf = io.StringIO()
    w = buf.write
    w("=" * 78 + "\n")
    w("  “速定位书格” 智能图书存取系统 · 机械结构工程模型说明\n")
    w("  Blender %s  |  1 Blender 单位 = 1 mm（unit scale = 0.001）\n"
      % bpy.app.version_string)
    w("  坐标约定：X = 宽（+X 右）、Y = 深（+Y 为柜体正面）、Z = 高（+Z 上）\n")
    w("  原点：整机底面中心 (0, 0, 0)\n")
    w("=" * 78 + "\n")

    w("\n〇、核心机械原理（不得更改）\n" + "-" * 78 + "\n")
    w("  1. 书全部水平平躺，叠放在一个可升降的平台上。\n")
    w("  2. 移动的是「书 + 承载书的升降平台」，不是推出机构。\n")
    w("  3. 推出机构位置固定，推出孔位置固定。\n")
    w("  4. 中间夹层上下移动，改变推出孔的有效高度，以适配不同厚度的书。\n")
    w("  5. 全机只有 3 台 42 步进电机，全部采用丝杆传动。\n")

    w("\n一、运动链（三台电机分工）\n" + "-" * 78 + "\n")
    for name, sub, mount, chain, note in CHAIN:
        w("  %s\n" % name)
        w("      子系统：%s\n" % sub)
        w("      安装：  %s\n" % mount)
        w("      传动：  %s\n" % chain)
        w("      作用：  %s\n\n" % note)

    w("\n二、推导得到的派生尺寸\n" + "-" * 78 + "\n")
    for k, v in (("平台升程（动画）", D["PLAT_LIFT"]),
                 ("目标书到位时平台顶面高度", D["PLAT_TOP_ATPORT"]),
                 ("推出孔高度可调范围", "%s – %s" % (D["SLOT_H_MIN"], D["SLOT_H_MAX"])),
                 ("目标书厚度 / 设定推出孔", "%s / %s" % (D["TARGET_T"],
                                                          D["SLOT_H_MAX"] * 0 + D["GAP_BOT_TARGET"] - D["PORT_Z0"])),
                 ("推书板行程", D["PUSH_STROKE"]),
                 ("目标书推出位移", D["BOOK3_PUSH_TRAVEL"]),
                 ("书本伸出柜体量", D["BOOK_FRONT_OUT"]),
                 ("书堆总高", D["BOOK_STACK_H"])):
        w("  %-26s : %s\n" % (k, (round(v, 2) if isinstance(v, float) else v)))

    w("\n三、完整参数表\n" + "-" * 78 + "\n")
    w(param_table() + "\n")

    w("\n四、集合与结构树（第 13 节 13 个集合 + 渲染舞台）\n" + "-" * 78 + "\n")
    w("  机械网格对象 %d 个，顶点 %d，面 %d\n"
      % (st["objects"], st["verts"], st["faces"]))
    w(structure_tree() + "\n")

    w("\n五、动画（第 15 节 15 秒工作流程）\n" + "-" * 78 + "\n")
    w("  24 fps，帧 %d – %d，共 %.2f 秒\n"
      % (D["F_START"], D["F_END"], (D["F_END"] - D["F_START"]) / float(D["FPS"])))
    w("  0–2 s   初始状态：一摞书平躺在升降平台上\n")
    w("  2–6 s   电机3 工作，平台带整摞书上升 %g mm，目标书到达固定推出位\n" % D["PLAT_LIFT"])
    w("  6–8 s   电机2 工作，中间夹层上下移动，推出孔由 %g mm 调至 %g mm\n"
      % (D["SLOT_H_MIN"], D["GAP_BOT_TARGET"] - D["PORT_Z0"]))
    w("  8–11 s  电机1 工作，推书板前移 %g mm，目标书被推出推出孔\n"
      % D["BOOK3_PUSH_TRAVEL"])
    w("  11–13 s 推书板返回原位\n")
    w("  13–15 s 完成取书状态：书本留在柜外\n")
    w("  三根丝杆的转角按位移逐帧反解（T8 导程 %g mm），螺纹与直线运动严格同步\n"
      % D["LIFT_PITCH"])

    if validation:
        w("\n六、防穿模闸门与设计断言（自动校验）\n" + "-" * 78 + "\n")
        w("  断言通过：%d / %d\n" % (validation.get("passed", 0),
                                     validation.get("total", 0)))
        w("  跨刚体包围盒干涉：%s\n"
          % ("无" if validation.get("clash_free") else validation.get("clash")))
        w("  最小跨刚体间隙：%s mm\n" % (validation.get("min_clearance_mm"),))
        for a in validation.get("assertions", []):
            w("    [%s] %s %s\n" % ("PASS" if a["ok"] else "FAIL", a["id"], a["desc"]))
        w("\n  说明：同一运动副内的零件（丝杆↔螺母、导轨↔滑块）在几何上必然互相嵌套，\n")
        w("        已用 kin 标记声明为允许互穿；空心外壳（铝型材框架）不参与两两包围盒\n")
        w("        判定，改由 A20「全部机构零件位于柜体内腔之内」断言单独校验。\n")

    txt = buf.getvalue()
    with open(path, "w", encoding="utf-8") as f:
        f.write(txt)
    return {"path": path, "bytes": len(txt.encode("utf-8")), "lines": txt.count("\n")}
