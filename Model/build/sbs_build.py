# -*- coding: utf-8 -*-
"""速定位书格 — 整机装配构建（严格按提示词确定的三电机机械原理）"""
import math
import bpy
from mathutils import Vector

import sbs_params as SP
import sbs_lib as L

D = SP.D
M = {}
COLS = {}

COL_NAMES = ["00_CABINET", "01_BOOK_STACK", "02_LIFT_PLATFORM", "03_MOTOR_01_PUSH",
             "04_MOTOR_02_GAP_ADJUST", "05_MOTOR_03_LIFT", "06_LINEAR_GUIDES",
             "07_LEAD_SCREWS", "08_SENSORS", "09_ELECTRONICS", "10_FASTENERS",
             "11_EXPLODED_VIEW", "12_ANIMATION", "90_RENDER_STAGE"]


# ===================================================================== 场景
def setup_scene():
    sc = bpy.context.scene
    sc.unit_settings.system = "METRIC"
    sc.unit_settings.scale_length = 0.001
    sc.unit_settings.length_unit = "MILLIMETERS"
    sc.render.fps = D["FPS"]
    sc.frame_start = D["F_START"]
    sc.frame_end = D["F_END"]
    sc.frame_current = D["F_START"]
    for name in COL_NAMES:
        COLS[name] = L.get_collection(name)


# ===================================================================== 柜体
def build_cabinet():
    c = COLS["00_CABINET"]
    mt = M
    ox, oy, oz = D["OUT_X"], D["OUT_Y"], D["CAB_H"]
    post = D["POST"]

    # ---- 铝型材框架：4 立柱 + 上下各 4 横梁
    spec = []
    for sx in (-1, 1):
        for sy in (-1, 1):
            x = sx * D["POST_XC"]; y = sy * D["POST_YC"]
            spec.append((x - post / 2, x + post / 2, y - post / 2, y + post / 2,
                         D["BASE_Z1"], D["TOP_Z0"]))
    z0, z1 = D["TOP_Z0"] - post, D["TOP_Z0"]
    zb0, zb1 = D["BASE_Z1"], D["BASE_Z1"] + post
    for sx in (-1, 1):
        x = sx * D["POST_XC"]
        for (a, b) in ((zb0, zb1), (z0, z1)):
            spec.append((x - post / 2, x + post / 2, -D["POST_YC"] + post / 2,
                         D["POST_YC"] - post / 2, a, b))
    for sy in (-1, 1):
        y = sy * D["POST_YC"]
        for (a, b) in ((zb0, zb1), (z0, z1)):
            spec.append((-D["POST_XC"] + post / 2, D["POST_XC"] - post / 2,
                         y - post / 2, y + post / 2, a, b))
    L.multi_box("Cabinet", spec, c, mt["alu_frame"], "envelope", "柜体铝型材框架")

    # ---- 底板 / 顶板
    L.box("Cabinet_BasePlate", -ox, ox, -oy, oy, 0, D["BASE_T"], c, mt["pvc"],
          "cabinet", "柜体底板")
    L.box("TopPanel", -ox, ox, -oy, oy, D["TOP_Z0"], D["TOP_Z1"], c, mt["pvc"],
          "cabinet", "柜体顶板")
    # ---- 背板
    L.box("BackPanel", -ox + post, ox - post, D["BACK_Y0"], D["BACK_Y1"], D["BASE_Z1"],
          D["TOP_Z0"], c, mt["pvc"], "cabinet", "柜体背板")
    # ---- 左右侧板（白 PVC 框 + 亚克力视窗）
    for sx, nm, gn in ((-1, "SidePanel_Left", "SideGlass_Left"),
                       (1, "SidePanel_Right", "SideGlass_Right")):
        x0, x1 = (D["SIDE_L_X0"], D["SIDE_L_X1"]) if sx < 0 else (D["SIDE_R_X0"], D["SIDE_R_X1"])
        yb, yf = D["WALL_Y_B"], D["WALL_Y_F"]
        win_y0, win_y1, win_z0, win_z1 = -100.0, 100.0, 120.0, 600.0
        L.multi_box(nm, [
            (x0, x1, yb, yf, D["BASE_Z1"], win_z0),
            (x0, x1, yb, yf, win_z1, D["TOP_Z0"]),
            (x0, x1, yb, win_y0, win_z0, win_z1),
            (x0, x1, win_y1, yf, win_z0, win_z1),
        ], c, mt["pvc"], "cabinet", "柜体侧板")
        gx0, gx1 = (x0 + 2.0, x0 + 6.0) if sx < 0 else (x1 - 6.0, x1 - 2.0)
        L.box(gn, gx0, gx1, win_y0, win_y1, win_z0, win_z1, c, mt["glass"],
              "cabinet", "侧面亚克力视窗")

    # ---- 前脸：上固定板 / 下固定板 / 中间两侧板 → 形成可调推出孔开口
    y0, y1 = D["FRONT_Y0"], D["FRONT_Y1"]
    wx = D["SIDE_R_X1"]
    px, pz0, pz1 = D["PORT_X"], D["PORT_Z0"], D["PORT_OPEN_Z1"]
    L.multi_box("FrontPanel_Lower", [(-wx, wx, y0, y1, D["BASE_Z1"], pz0)],
                c, mt["pvc"], "cabinet", "前面板下层（推出孔下沿）")
    L.multi_box("FrontPanel_Upper", [(-wx, wx, y0, y1, pz1, D["TOP_Z0"])],
                c, mt["pvc"], "cabinet", "前面板上层")
    L.multi_box("FrontPanel_MidLeft", [(-wx, -px, y0, y1, pz0, pz1)],
                c, mt["pvc"], "cabinet", "前面板中层左")
    L.multi_box("FrontPanel_MidRight", [(px, wx, y0, y1, pz0, pz1)],
                c, mt["pvc"], "cabinet", "前面板中层右")

    # ---- 推出孔铝制包边（拆成 4 条，保证包围盒贴合，同时充当推书口导向唇）
    for nm, spec, desc in (
        ("Port_Lip", [(-px - 10, px + 10, y0 - 3, y1 + 1, pz0 - 6, pz0 - 0.5)],
         "推出孔下唇（= 推出孔下沿基准）"),
        ("Port_Lip_02", [(-px - 10, px + 10, y0 - 3, y1 + 1, pz1 - 1, pz1 + 6)],
         "推出孔上唇"),
        ("Port_Lip_03", [(-px - 10, -px + 1, y0 - 3, y1 + 1, pz0 - 6, pz1 + 6)],
         "推出孔左唇"),
        ("Port_Lip_04", [(px - 1, px + 10, y0 - 3, y1 + 1, pz0 - 6, pz1 + 6)],
         "推出孔右唇"),
    ):
        L.multi_box(nm, spec, c, mt["alu_plate"], "cabinet", desc)

    # ---- 推出孔前的托书台（书被推出时由它承托）
    L.multi_box("Port_Shelf", [(-D["PLAT_X"], D["PLAT_X"], D["PLAT_Y1"] + 2,
                                D["FRONT_Y0"], pz0 - 8, pz0)],
                c, mt["alu_plate"], "cabinet", "推出孔前托书台")
    for ob in ("Cabinet_BasePlate", "TopPanel", "BackPanel", "FrontPanel_Lower",
               "FrontPanel_Upper"):
        L.bevel(bpy.data.objects[ob], 1.2)
    return {"cabinet_parts": 12}


# ============================================================= 电机3 / 升降
def build_lift():
    c2, c5, c6, c7, c10 = (COLS["02_LIFT_PLATFORM"], COLS["05_MOTOR_03_LIFT"],
                           COLS["06_LINEAR_GUIDES"], COLS["07_LEAD_SCREWS"],
                           COLS["10_FASTENERS"])
    mt = M
    sx, sy = D["LIFT_SX"], D["LIFT_SY"]

    # 电机3：固定于柜体底部，垂直出轴
    L.stepper42("Motor03_Lift_42", (sx, sy), "Z", D["LIFT_MOTOR_Z0"], 1, c5,
                mt, "liftframe", "42 步进电机3（升降）")
    L.multi_box("MotorBracket", [(-38, 38, -160, -80, D["BASE_Z1"], D["BASE_Z1"] + 6)],
                c10, mt["alu_plate"], "liftframe", "电机3安装座")
    L.coupler("Coupler", (sx, sy), "Z", D["LIFT_COUP_Z0"], D["LIFT_COUP_Z1"], c7, mt,
              "liftframe", "刚性联轴器")
    L.lead_screw("Lift_LeadScrew", (sx, sy), D["LIFT_SCREW_Z0"], 408.0, "Z",
                 D["LIFT_SCREW_R"], D["LIFT_THREAD_R"], D["LIFT_PITCH"], c7, mt["steel"],
                 "liftframe", "T8 升降丝杆", origin=(sx, sy, 0.0))
    L.bearing_block("BearingBlock", (sx, sy), "Z", D["LIFT_BRG_Z0"], D["LIFT_BRG_Z1"],
                    c7, mt, "liftframe", "升降丝杆上轴承座")
    L.multi_box("BearingBlock_Bracket",
                [(-34, -8, -166, -108, D["LIFT_BRG_Z0"], D["LIFT_BRG_Z1"] + 2),
                 (8, 34, -166, -108, D["LIFT_BRG_Z0"], D["LIFT_BRG_Z1"] + 2)],
                c10, mt["alu_plate"], "liftframe", "上轴承座支架")

    # 升降直线导轨（左右各一）
    for s, nm in ((-1, "LiftPlatform_Guide_Left"), (1, "LiftPlatform_Guide_Right")):
        L.linear_rail(nm, (s * D["LRAIL_X"], D["LRAIL_Y"]), "Z", D["LRAIL_Z0"],
                      D["LRAIL_Z1"], D["LRAIL_W"], D["LRAIL_D"], c6, mt, "liftframe",
                      "升降平台直线导轨", bolt_n=8)

    # ---- 升降平台（可动件；原点 = 平台顶面中心，动画只改其 Z）
    pz = D["PLAT_TOP_HOME"]
    plat_org = (0.0, D["PLAT_CY"], pz)
    px_ = D["PLAT_X"]
    plat = L.multi_box("LiftPlatform", [
        (-px_, px_, D["PLAT_Y0"], D["PLAT_Y1"], pz - D["PLAT_T"], pz),
        (-px_, px_, D["PLAT_Y0"], D["PLAT_Y0"] + 6.0, pz - 26.0, pz - D["PLAT_T"]),
        (-180.0, -px_, D["PLAT_CY"] - 60.0, D["PLAT_CY"] + 60.0,
         pz - 26.0, pz - D["PLAT_T"] + 2),
        (px_, 180.0, D["PLAT_CY"] - 60.0, D["PLAT_CY"] + 60.0,
         pz - 26.0, pz - D["PLAT_T"] + 2),
    ], c2, mt["alu_plate"], "platform", "书籍升降平台", origin=plat_org)
    L.bevel(plat, 1.0)

    # 平台侧滑块（骑在左右升降导轨上）
    for s, nm in ((-1, "Lift_Guide_Slider_L"), (1, "Lift_Guide_Slider_R")):
        L.slider_block(nm, (s * D["LRAIL_X"], D["LRAIL_Y"]), "Z", pz - 44.0, pz - 8.0,
                       30.0, 34.0, c6, mt, "platform", "升降平台滑块", origin=plat_org)

    # 升降丝杆螺母 + 拨叉（随平台运动）
    L.ball_screw_nut("Lift_ScrewNut", (sx, sy), pz - 63.0, pz - 41.0, "Z", 11.0, c7, mt,
                     "platform", "升降丝杆螺母", origin=plat_org)
    L.multi_box("Lift_NutBracket", [
        (-30.0, -8.0, -134.0, -70.0, pz - 42.0, pz - 30.0),
        (-30.0, -16.0, -134.0, -70.0, pz - 70.0, pz - 42.0),
        (-30.0, -8.0, -74.0, -70.0, pz - 70.0, pz - 30.0),
    ], c2, mt["alu_plate"], "platform", "升降螺母拨叉（左半）", origin=plat_org)
    L.multi_box("Lift_NutBracket_R", [
        (8.0, 30.0, -134.0, -70.0, pz - 42.0, pz - 30.0),
        (16.0, 30.0, -134.0, -70.0, pz - 70.0, pz - 42.0),
        (8.0, 30.0, -74.0, -70.0, pz - 70.0, pz - 30.0),
    ], c2, mt["alu_plate"], "platform", "升降螺母拨叉（右半）", origin=plat_org)
    return plat


# ========================================================= 电机1 / 固定推出
def build_push():
    c3, c6, c7, c10 = (COLS["03_MOTOR_01_PUSH"], COLS["06_LINEAR_GUIDES"],
                       COLS["07_LEAD_SCREWS"], COLS["10_FASTENERS"])
    mt = M
    ax = D["PUSH_AX_X"]; az = D["PUSH_AX_Z"]

    L.stepper42("Motor01_Push_42", (ax, az), "Y", D["PUSH_MOTOR_Y0"], 1, c3, mt,
                "pushframe", "42 步进电机1（推出）")
    L.multi_box("MotorBracket_03", [(128, 192, D["BACK_Y1"], D["BACK_Y1"] + 10,
                                     az - 42, az + 42)],
                c10, mt["alu_plate"], "pushframe", "电机1安装座")
    L.coupler("Coupler_03", (ax, az), "Y", D["PUSH_COUP_Y0"], D["PUSH_COUP_Y1"], c7,
              mt, "pushframe", "刚性联轴器")
    L.lead_screw("Push_LeadScrew", (ax, az), D["PUSH_SCREW_Y0"], D["PUSH_SCREW_Y1"], "Y",
                 D["PUSH_SCREW_R"], D["PUSH_THREAD_R"], D["PUSH_PITCH"], c7, mt["steel"],
                 "pushframe", "T8 推书丝杆", origin=(ax, 0.0, az))
    L.bearing_block("BearingBlock_03", (ax, az), "Y", D["PUSH_BRGF_Y0"],
                    D["PUSH_BRGF_Y1"], c7, mt, "pushframe", "推书丝杆前轴承座")
    L.linear_rail("Push_GuideRail", (D["PUSH_RAIL_X"], az), "Y", D["PUSH_RAIL_Y0"],
                  D["PUSH_RAIL_Y1"], D["PUSH_RAIL_W"], D["PUSH_RAIL_D"], c6, mt,
                  "pushframe", "推出机构直线导轨", bolt_n=6)

    # ---- 推出小车（可动件；原点 = 退回位螺母中心）
    org = (0.0, D["PUSH_NUT_Y_RET"], 0.0)
    ny = D["PUSH_NUT_Y_RET"]
    bt, pt = D["PUSH_BEAM_T"], D["PUSH_PLATE_T"]
    L.multi_box("Push_Carriage_Beam",
                [(-D["PUSH_BEAM_X"], D["PUSH_BEAM_X"], ny - bt, ny + bt,
                  D["PORT_Z0"] + 0.5, 398.0),
                 (-D["PUSH_BEAM_X"], -D["PUSH_BEAM_X"] + 14, ny - bt, ny + bt, 398.0, 406.0),
                 (D["PUSH_BEAM_X"] - 14, D["PUSH_BEAM_X"], ny - bt, ny + bt, 398.0, 406.0)],
                c3, mt["alu_plate"], "pushcarriage", "推出小车横梁", origin=org)
    L.box("Push_Plate", -D["PUSH_PLATE_X"], D["PUSH_PLATE_X"], ny + bt, ny + bt + pt,
          D["PUSH_PLATE_Z0"], D["PUSH_PLATE_Z0"] + D["PUSH_PLATE_H"], c3, mt["alu_anod"],
          "pushcarriage", "推书板 150×14×6", origin=org)
    L.cylinder("Push_ScrewNut", (ax, az), 11.0, ny - 10, ny + 10, "Y", c7, mt["steel"],
               "pushcarriage", "推书丝杆螺母", seg=24, origin=org)
    L.multi_box("Push_ScrewNut_Flange",
                [(ax - 26, ax - 8, ny - 10, ny + 10, 375.0, 400.0),
                 (ax + 8, ax + 26, ny - 10, ny + 10, 375.0, 400.0)],
                c7, mt["steel"], "pushcarriage", "螺母法兰", origin=org)
    L.slider_block("Push_Carriage_Slider", (D["PUSH_RAIL_X"], az), "Y", ny - 14, ny + 14,
                   30.0, 30.0, c6, mt, "pushcarriage", "推出小车滑块", origin=org)
    return org


# ==================================================== 电机2 / 中间夹层调节
def build_gap():
    c4, c6, c7, c10 = (COLS["04_MOTOR_02_GAP_ADJUST"], COLS["06_LINEAR_GUIDES"],
                       COLS["07_LEAD_SCREWS"], COLS["10_FASTENERS"])
    mt = M
    gx, gy = D["GAP_SX"], D["GAP_SY"]

    L.stepper42("Motor02_GapAdjust_42", (gx, gy), "Z", D["GAP_MOTOR_Z1"], -1, c4, mt,
                "gapframe", "42 步进电机2（推出孔调节）")
    L.multi_box("MotorBracket_02", [(gx - 30, gx + 30, gy - 24, D["FRONT_Y0"] - 1,
                                     D["GAP_MOTOR_Z1"] - 6, D["GAP_MOTOR_Z1"] + 4)],
                c10, mt["alu_plate"], "gapframe", "电机2安装座")
    L.coupler("Coupler_02", (gx, gy), "Z", D["GAP_COUP_Z0"], D["GAP_COUP_Z1"], c7, mt,
              "gapframe", "刚性联轴器")
    L.lead_screw("GapAdjust_LeadScrew", (gx, gy), D["GAP_SCREW_Z0"], D["GAP_SCREW_Z1"],
                 "Z", D["GAP_SCREW_R"], D["GAP_THREAD_R"], D["GAP_PITCH"], c7, mt["steel"],
                 "gapframe", "T8 推出孔调节丝杆", origin=(gx, gy, 0.0))
    L.bearing_block("BearingBlock_02", (gx, gy), "Z", D["GAP_BRG_Z0"], D["GAP_BRG_Z1"],
                    c7, mt, "gapframe", "调节丝杆下轴承座")
    L.multi_box("BearingBlock_02_Bracket", [
        (gx - 28, gx - 6, gy - 3, D["FRONT_Y0"] - 1, D["GAP_BRG_Z0"], D["GAP_BRG_Z1"]),
        (gx + 6, gx + 28, gy - 3, D["FRONT_Y0"] - 1, D["GAP_BRG_Z0"], D["GAP_BRG_Z1"]),
        (gx - 28, gx + 28, D["FRONT_Y0"] - 30, D["FRONT_Y0"] - 1,
         D["GAP_BRG_Z0"], D["GAP_BRG_Z1"]),
    ], c10, mt["alu_plate"], "gapframe", "调节丝杆轴承座支架")

    # 中间夹层导轨（左右）
    for s, nm in ((-1, "Gap_Guide_Mirror"), (1, "Gap_Guide")):
        L.linear_rail(nm, (s * D["GG_X"], (D["GG_Y0"] + D["GG_Y1"]) / 2.0), "Z",
                      D["GG_Z0"], D["GG_Z1"], D["GG_W"], D["GG_D"], c6, mt, "gapframe",
                      "中间夹层导轨", bolt_n=6)

    # ---- 可调中间夹层（可动件；原点 = 夹层下沿中心，动画只改其 Z）
    gz = D["GAP_BOT_HOME"]
    gy0, gy1 = D["GAP_Y0"], D["GAP_Y1"]
    org = (0.0, (gy0 + gy1) / 2.0, gz)
    ml = L.multi_box("Middle_Layer", [
        (-D["GAP_X"], D["GAP_X"], gy0, gy1, gz, gz + D["GAP_H"]),
        (-D["GAP_X"], D["GAP_X"], gy0 - 2.0, gy1 + 2.0, gz, gz + 8.0),
        (-D["GAP_X"] + 2, -D["GAP_X"] + 16, gy1, gy1 + 4.0, gz + 10.0, gz + D["GAP_H"] - 4),
        (D["GAP_X"] - 16, D["GAP_X"] - 2, gy1, gy1 + 4.0, gz + 10.0, gz + D["GAP_H"] - 4),
    ], c4, mt["mid"], "middlelayer", "可调中间夹层（改变推出孔高度）", origin=org)
    L.bevel(ml, 0.8)
    for s, nm in ((-1, "Middle_Layer_Slider_L"), (1, "Middle_Layer_Slider_R")):
        L.slider_block(nm, (s * D["GG_X"], (gy0 + gy1) / 2.0), "Z", gz + 8.0, gz + 62.0,
                       30.0, 26.0, c6, mt, "middlelayer", "中间夹层滑块", origin=org)
    L.cylinder("GapAdjust_ScrewNut", (gx, gy), 11.0, gz + D["GAP_NUT_DZ"] - 10.0,
               gz + D["GAP_NUT_DZ"] + 10.0, "Z", c7, mt["steel"], "middlelayer",
               "调节丝杆螺母", seg=24, origin=org)
    L.multi_box("GapAdjust_ScrewNut_Flange",
                [(gx - 20, gx - 6, gy - 8.0, gy1, gz + D["GAP_NUT_DZ"] - 12.0,
                  gz + D["GAP_NUT_DZ"] + 12.0),
                 (gx + 6, gx + 20, gy - 8.0, gy1, gz + D["GAP_NUT_DZ"] - 12.0,
                  gz + D["GAP_NUT_DZ"] + 12.0)],
                c7, mt["steel"], "middlelayer", "调节螺母法兰", origin=org)
    return org


# ===================================================================== 书堆
def build_books():
    c1 = COLS["01_BOOK_STACK"]
    mt = M
    covers = ["cover_r", "cover_b", "cover_g", "cover_o", "cover_p"]
    stack_org = (0.0, D["PLAT_CY"], D["PLAT_TOP_HOME"])
    stack = L.empty("Books_Stack", stack_org, c1, "books",
                    "书籍堆（随升降平台同步运动）", size=40.0)

    bx = D["BOOK_X"] / 2.0
    by = D["BOOK_Y"] / 2.0
    books = []
    for i in range(D["BOOK_N"]):
        t = D["BOOK_T"][i]
        zc = D["BOOK_CUM"][i] + t / 2.0
        org = (0.0, D["BOOK_CY"], D["PLAT_TOP_HOME"] + zc)
        nm = "Book_%02d" % (i + 1)
        # 网格以书本几何中心为局部原点，再整体平移到 (0, BOOK_CY, 平台顶面 + 累计厚度)
        for sub, specs, material, pname in (
            ("", [(-bx, bx, -by, by, -t / 2.0, -t / 2.0 + 1.6),
                  (-bx, bx, -by, by, t / 2.0 - 1.6, t / 2.0)],
             mt[covers[i]], "平躺书籍 %d（厚 %.0f mm）" % (i + 1, t)),
            ("_Pages", [(-bx + 2.0, bx - 2.0, -by + 1.0, by - 1.2,
                         -t / 2.0 + 1.6, t / 2.0 - 1.6)], mt["pages"], "书页"),
            ("_Spine", [(-bx, -bx + 3.0, -by, by, -t / 2.0, t / 2.0)],
             mt[covers[i]], "书脊"),
        ):
            ob = L.multi_box(nm + sub, specs, c1, material, "books", pname)
            ob.location = org
            if sub == "":
                books.append(ob)
    for b in books:
        L.parent_to(b, stack)
        # 书页 / 书脊必须是书本本体的子级，否则目标书被推出时只有封面会动
        for suf in ("_Pages", "_Spine"):
            L.parent_to(bpy.data.objects[b.name + suf], b)
    return stack, books


# ==================================================== 传感器 / 电控 / 紧固件
def build_misc():
    c8, c9, c10 = COLS["08_SENSORS"], COLS["09_ELECTRONICS"], COLS["10_FASTENERS"]
    mt = M
    sx, sy, sz = D["SONAR_X"], D["SONAR_Y"], D["SONAR_Z"]
    s = D["SONAR_S"]

    # ---- HC-SR04：安装在前面板内侧支架上，向下对准书堆顶面（换算书本厚度）
    L.multi_box("HC_SR04_Arm", [(-16, 16, sy + s / 2.0, D["FRONT_Y0"] + 4,
                                 sz + 4, sz + 14)], c8, mt["alu_plate"], "cabinet",
                "超声波模块支架")
    L.multi_box("HC_SR04", [
        (sx - 20, sx + 20, sy - s / 2.0, sy + s / 2.0, sz - s / 2.0, sz + s / 2.0)],
        c8, mt["pcb"], "cabinet", "HC-SR04 超声波测厚")
    for sgn in (-1, 1):
        L.cylinder("HC_SR04_T%d" % (sgn > 0), (sx + sgn * 11.0, sy), 8.0,
                   sz - s / 2.0 - 5.0, sz - s / 2.0 + 2.0, "Z", c8, mt["motor_sil"],
                   "cabinet", "超声换能器", seg=20)

    # ---- 限位开关（均固定在柜体上）
    defs = [
        ("LimitSwitch", (-215, -195, 30, 50, 90, 104), "升降下限位"),
        ("LimitSwitch_02", (-215, -195, 30, 50, 378, 394), "升降上限位"),
        ("LimitSwitch_03", (140, 166, -106, -90, 410, 424), "推书后退限位"),
        ("LimitSwitch_04", (-258, -236, D["GG_Y0"], D["GG_Y1"], 532, 546), "夹层上限位"),
    ]
    for nm, (x0, x1, y0, y1, z0, z1), desc in defs:
        L.multi_box(nm, [(x0, x1, y0, y1, z0, z1)], c8, mt["plastic_gry"], "cabinet", desc)
        L.multi_box(nm + "_Lever", [(x0 + 4, x1 - 4, y0 - 8, y0, z0 + 3, z1 - 3)],
                    c8, mt["motor_sil"], "cabinet", desc + "触杆")

    # ---- 电控（拆成左右两块，避免合并包围盒横跨内腔中心）
    L.multi_box("Electronics", [
        (D["PSU_X0"], D["PSU_X1"], D["PSU_Y0"], D["PSU_Y1"], D["PSU_Z0"], D["PSU_Z1"]),
        (D["PSU_X0"] + 10, D["PSU_X1"] - 10, D["PSU_Y0"] + 8, D["PSU_Y1"] - 8,
         D["PSU_Z1"], D["PSU_Z1"] + 10),
    ], c9, mt["plastic_gry"], "electronics", "底部电控区 · 开关电源")
    L.multi_box("Electronics_Drivers", [
        (D["DRV_X0"], D["DRV_X1"], D["DRV_Y0"], D["DRV_Y1"], D["DRV_Z0"], D["DRV_Z1"])],
        c9, mt["pcb"], "electronics", "底部电控区 · 步进驱动器")
    L.multi_box("Electronics_Fins", [
        (D["DRV_X0"] + 12 + i * 16, D["DRV_X0"] + 22 + i * 16, D["DRV_Y0"] + 8,
         D["DRV_Y1"] - 8, D["DRV_Z1"], D["DRV_Z1"] + 14) for i in range(5)
    ], c9, mt["alu_plate"], "electronics", "步进驱动器散热片")
    L.multi_box("Electronics_02_TopControl", [
        (D["MB_X0"], D["MB_X1"], D["MB_Y0"], D["MB_Y1"], D["MB_Z0"], D["MB_Z1"])],
        c9, mt["pcb"], "electronics", "顶部控制区主控板")
    L.multi_box("Electronics_02_Shield", [
        (D["MB_X0"] + 40, D["MB_X1"] - 20, D["MB_Y0"] + 6, D["MB_Y1"] - 6,
         D["MB_Z1"], D["MB_Z1"] + 12)], c9, mt["motor_blk"], "electronics", "主控屏蔽罩")

    # ---- 控制面板（显示屏 + 按键 + 摄像头）
    cx = D["CP_X"]
    L.multi_box("ControlPanel", [
        (-cx, cx, D["FRONT_Y0"] - 1, D["FRONT_Y1"] + 1, D["CP_Z0"], D["CP_Z1"])],
        c9, mt["pvc"], "cabinet", "顶部控制面板")
    L.box("ControlPanel_Screen", -D["SCR_X"], D["SCR_X"], D["FRONT_Y1"] + 0.5,
          D["FRONT_Y1"] + 3.5, D["SCR_Z0"], D["SCR_Z1"], c9, mt["screen"], "cabinet",
          "显示触摸屏")
    for i, x in enumerate((-84, -28, 28, 84)):
        L.cylinder("ControlPanel_Button_%d" % (i + 1), (x, D["FRONT_Y1"] + 2), 9.0,
                   D["CP_Z0"] - 26, D["CP_Z0"] - 8, "Z", c9, mt["plastic_blk"], "cabinet",
                   "操作按键", seg=20)
    L.multi_box("Camera_Module", [(-206, -166, D["FRONT_Y1"] - 6, D["FRONT_Y1"] + 5,
                                   604, 640)], c9, mt["motor_blk"], "cabinet", "识别摄像头")
    L.cylinder("Camera_Module_Lens", (-186, D["FRONT_Y1"] + 5), 12.0,
               D["FRONT_Y1"] + 4, D["FRONT_Y1"] + 12, "Y", c9, mt["screen"], "cabinet",
               "摄像头镜头", seg=24)

    # ---- 紧固件（10_FASTENERS）：电机安装螺栓 / 面板自攻钉 / 侧板螺钉
    bolt_jobs = [
        ("M03", (D["LIFT_SX"], D["LIFT_SY"]), "Z", D["LIFT_MOTOR_Z0"] + 44.0, 1.0, "liftframe"),
        ("M02", (D["GAP_SX"], D["GAP_SY"]), "Z", D["GAP_MOTOR_Z0"] - 44.0, -1.0, "gapframe"),
        ("M01", (D["PUSH_AX_X"], D["PUSH_AX_Z"]), "Y", D["PUSH_MOTOR_Y1"] + 44.0, 1.0, "pushframe"),
    ]
    for tag, (cu, cv), ax, face, sgn, grp in bolt_jobs:
        for i, (su, sv) in enumerate(((-1, -1), (1, -1), (1, 1), (-1, 1))):
            u = cu + su * 15.5
            v = cv + sv * 15.5
            nm = "Bolt_%s_%d" % (tag, i + 1)
            if ax == "Y":
                L.cylinder(nm, (u, v), 2.0, face, face + 3.0 * sgn, "Y", c10,
                           mt["motor_sil"], grp, "电机安装螺栓", seg=6)
            else:
                L.cylinder(nm, (u, v), 2.0, face, face + 3.0 * sgn, "Z", c10,
                           mt["motor_sil"], grp, "电机安装螺栓", seg=6)

    for i, x in enumerate((-250, -150, -50, 50, 150, 250)):
        for j, z in enumerate((60.0, 640.0)):
            L.cylinder("Bolt_Panel_%d%d" % (i + 1, j + 1), (x, z), 2.2,
                       D["FRONT_Y1"] + 0.5, D["FRONT_Y1"] + 3.5, "Y", c10,
                       mt["motor_sil"], "cabinet", "前面板紧固螺钉", seg=8)
    for i, z in enumerate((160.0, 320.0, 480.0, 640.0)):
        L.cylinder("Bolt_Side_L_%02d" % (i + 1), (-60.0, z), 2.2,
                   D["SIDE_L_X0"] - 1.0, D["SIDE_L_X0"] + 2.0, "X", c10,
                   mt["motor_sil"], "cabinet", "侧板紧固螺钉", seg=8)
        L.cylinder("Bolt_Side_R_%02d" % (i + 1), (60.0, z), 2.2,
                   D["SIDE_R_X1"] - 2.0, D["SIDE_R_X1"] + 1.0, "X", c10,
                   mt["motor_sil"], "cabinet", "侧板紧固螺钉", seg=8)
    return True


# --------------------------------------------------- 运动副（允许互穿的成对零件）
# 同一条运动副内的零件在几何上必然互相嵌套（丝杆穿螺母、导轨穿滑块），
# 因此防穿模闸门对同一 kin 标记的零件对不做包围盒相交判定。
KINEMATIC_LINKS = {
    "K_LIFT_SCREW": ["Lift_LeadScrew", "Lift_ScrewNut", "BearingBlock", "Coupler",
                     "Motor03_Lift_42"],
    "K_LIFT_RAIL": ["LiftPlatform_Guide_Left", "LiftPlatform_Guide_Right",
                    "Lift_Guide_Slider_L", "Lift_Guide_Slider_R", "LiftPlatform"],
    "K_PUSH_SCREW": ["Push_LeadScrew", "Push_ScrewNut", "Push_ScrewNut_Flange",
                     "BearingBlock_03", "Coupler_03", "Motor01_Push_42"],
    "K_PUSH_RAIL": ["Push_GuideRail", "Push_Carriage_Slider"],
    "K_GAP_SCREW": ["GapAdjust_LeadScrew", "GapAdjust_ScrewNut",
                    "GapAdjust_ScrewNut_Flange", "BearingBlock_02", "Coupler_02",
                    "Motor02_GapAdjust_42"],
    "K_GAP_RAIL": ["Gap_Guide", "Gap_Guide_Mirror", "Middle_Layer_Slider_L",
                   "Middle_Layer_Slider_R"],
}


def assign_kinematics():
    n = 0
    for kin, names in KINEMATIC_LINKS.items():
        for nm in names:
            ob = bpy.data.objects.get(nm)
            if ob is not None:
                ob["kin"] = kin
                n += 1
    return n


# ===================================================================== 总装
def build_all():
    L.clear_all()
    M.clear()
    M.update(L.build_materials())
    setup_scene()
    build_cabinet()
    plat = build_lift()
    push_org = build_push()
    gap_org = build_gap()
    stack, books = build_books()
    build_misc()

    # ---- 真实父子关系（运动链：平台 → 书堆；推出小车 → 推书板 / 螺母）
    L.parent_to(stack, plat)

    car = L.empty("Push_Carriage", push_org, COLS["03_MOTOR_01_PUSH"], "pushcarriage",
                  "推出小车（只做前后直线运动）", size=30.0)
    for nm in ("Push_Carriage_Beam", "Push_Plate", "Push_ScrewNut",
               "Push_ScrewNut_Flange", "Push_Carriage_Slider"):
        L.parent_to(bpy.data.objects[nm], car)

    n_apply = L.apply_modifiers()
    n_kin = assign_kinematics()
    bpy.context.view_layer.update()
    st = L.stats()
    st.update({"parented_books": len(books), "modifiers_applied": n_apply,
               "kinematic_nodes": n_kin,
               "materials": len([m for m in bpy.data.materials if m.name.startswith("MAT_")]),
               "collections": len([c for c in bpy.data.collections])})
    return st
