# -*- coding: utf-8 -*-
"""
速定位书格 — 参数表
单位：1 Blender 单位 = 1 mm（scene.unit_settings.scale_length = 0.001）
坐标系：X = 宽（+X 右）；Y = 深（+Y 为柜体正面）；Z = 高（+Z 上）
原点：整机底面中心 (0, 0, 0)

机械原理（提示词最高优先级，不得更改）：
  1. 书全部平躺，叠放在一个可在 Z 方向升降的平台上。
  2. 电机3 → 垂直丝杆 → 丝杆螺母 → 升降平台（书随平台一起走）。
  3. 电机1 → 水平丝杆 → 丝杆螺母 → 推书板（位置固定，只做前后直线运动）。
  4. 电机2 → 垂直丝杆 → 丝杆螺母 → 前面板中间夹层（改变推出孔高度）。
"""

P = {}
P.update({
    # ------------------------------------------------------------------ 柜体
    "CAB_W": 600.0,
    "CAB_D": 350.0,
    "CAB_H": 700.0,
    "PANEL_T": 8.0,            # PVC / 亚克力板厚
    "POST": 20.0,              # 铝型材立柱截面
    "BASE_T": 15.0,            # 底板厚
    "TOP_T": 15.0,             # 顶板厚
    "FRAME_T": 4.0,            # 侧面视窗亚克力厚

    # ------------------------------------------------------------- 推出孔（可调）
    "PORT_X": 130.0,           # 前面板开口半宽 → 开口宽 260
    "PORT_Z0": 380.0,          # 推出孔下沿（前面板下层上缘，固定）
    "PORT_OPEN_Z1": 500.0,     # 前面板开口上沿

    # ---------------------------------------------------------------- 书籍
    "BOOK_N": 5,
    "BOOK_X": 180.0,           # 书长（X）
    "BOOK_Y": 150.0,           # 书宽（Y，书脊→书口）
    "BOOK_T": [15.0, 25.0, 30.0, 40.0, 50.0],   # 自下而上厚度
    "BOOK_Y0": -66.0,          # 书堆后缘 Y

    # ------------------------------------------------------------- 升降平台
    "PLAT_X": 170.0,           # 半宽
    "PLAT_Y0": -74.0,
    "PLAT_Y1": 104.0,
    "PLAT_T": 12.0,
    "PLAT_TOP_MIN": 150.0,
    "PLAT_TOP_MAX": 380.0,
    "PLAT_TOP_HOME": 200.0,    # 动画起始
    "PLAT_TOP_LOAD": 150.0,    # 最低（放书位）

    # -------------------------------------------------------- 电机3 / 升降丝杆
    "LIFT_SX": 0.0,
    "LIFT_SY": -120.0,
    "LIFT_SCREW_R": 4.0,
    "LIFT_THREAD_R": 4.9,
    "LIFT_PITCH": 4.0,
    "LIFT_MOTOR_Z0": 17.0,     # 42 机身 38 高（底面留 2 mm 垫片，不穿底板）
    "LIFT_MOTOR_H": 42.0,
    "LIFT_COUP_Z0": 60.0,
    "LIFT_COUP_Z1": 83.0,
    "LIFT_SCREW_Z0": 83.0,
    "LIFT_SCREW_Z1": 400.0,
    "LIFT_BRG_Z0": 400.0,
    "LIFT_BRG_Z1": 414.0,
    "LIFT_NUT_DZ": 52.0,       # 螺母中心 = 平台顶面 - 52

    # ------------------------------------------------------ 升降平台直线导轨
    "LRAIL_X": 190.0,          # 左右导轨中心
    "LRAIL_W": 14.0,           # 导轨宽（X）
    "LRAIL_D": 16.0,           # 导轨厚（Y）
    "LRAIL_Y": 20.0,           # 导轨中心 Y
    "LRAIL_Z0": 90.0,
    "LRAIL_Z1": 410.0,

    # -------------------------------------------------- 电机1 / 固定推出机构
    "PUSH_AX_X": 160.0,        # 推书丝杆轴线 X
    "PUSH_AX_Z": 387.5,        # 推书丝杆轴线 Z
    "PUSH_MOTOR_Y0": -156.0,   # 电机1 机身（38 长）
    "PUSH_MOTOR_Y1": -118.0,
    "PUSH_COUP_Y0": -112.0,    # 联轴器夹住电机轴
    "PUSH_COUP_Y1": -95.0,
    "PUSH_SCREW_Y0": -95.0,    # 推书丝杆螺纹段
    "PUSH_SCREW_Y1": 64.0,
    "PUSH_BRGF_Y0": 64.0,      # 前端轴承座（丝杆自由端支撑）
    "PUSH_BRGF_Y1": 76.0,
    "PUSH_NUT_Y_RET": -82.0,   # 螺母退回位
    "PUSH_NUT_Y_EXT": 50.0,    # 螺母推出位
    "PUSH_NUT_LEN": 20.0,
    "PUSH_SCREW_R": 4.0,
    "PUSH_THREAD_R": 4.9,
    "PUSH_PITCH": 4.0,
    "PUSH_BEAM_X": 150.0,      # 推出小车横梁半宽（避开 -160 处从动导轨）
    "PUSH_BEAM_T": 6.0,        # 横梁 Y 向厚
    "PUSH_PLATE_X": 75.0,      # 推书板半宽（150 宽）
    "PUSH_PLATE_T": 6.0,
    "PUSH_PLATE_H": 14.0,
    "PUSH_PLATE_Z0": 380.5,
    "PUSH_RAIL_X": -160.0,     # 推出机构从动直线导轨
    "PUSH_RAIL_W": 9.0,
    "PUSH_RAIL_D": 15.0,
    "PUSH_RAIL_Y0": -100.0,
    "PUSH_RAIL_Y1": 70.0,
    # ------------------------------------------- 电机2 / 中间夹层（推出孔调节）
    "GAP_X": 230.0,            # 中间夹层半宽
    "GAP_Y0": 143.0,
    "GAP_Y1": 151.0,
    "GAP_H": 112.0,            # 中间夹层高（最低位仍完全遮住开口上沿 500）
    "GAP_BOT_MIN": 398.0,      # 最低位 → 推出孔 18
    "GAP_BOT_MAX": 433.0,      # 最高位 → 推出孔 53
    "GAP_BOT_HOME": 398.0,
    "GAP_BOT_TARGET": 413.0,   # 30 mm 书 → 推出孔 33
    "GAP_SX": 200.0,           # 调节丝杆轴线 X
    "GAP_SY": 131.0,           # 调节丝杆轴线 Y
    "GAP_SCREW_R": 4.0,
    "GAP_THREAD_R": 4.9,
    "GAP_PITCH": 4.0,
    "GAP_BRG_Z0": 382.0,
    "GAP_BRG_Z1": 396.0,
    "GAP_SCREW_Z0": 396.0,
    "GAP_SCREW_Z1": 560.0,
    "GAP_COUP_Z0": 560.0,
    "GAP_COUP_Z1": 583.0,
    "GAP_MOTOR_Z0": 587.0,
    "GAP_MOTOR_Z1": 625.0,
    "GAP_NUT_DZ": 30.0,        # 螺母中心 = 中间夹层下沿 + 30
    "GG_X": 245.0,             # 中间夹层导轨中心
    "GG_W": 20.0,
    "GG_D": 14.0,
    "GG_Y0": 143.0,
    "GG_Y1": 157.0,
    "GG_Z0": 390.0,
    "GG_Z1": 530.0,

    # ------------------------------------------------------------------ 传感器
    "SONAR_X": 0.0,            # HC-SR04 探头中心（书堆正上方，向下测书堆高度）
    "SONAR_Y": 10.0,
    "SONAR_Z": 572.0,          # 探头中心 Z（高于书堆最高点 540）
    "SONAR_S": 40.0,           # 模块 40 × 20 × 40

    # ------------------------------------------------------------------ 电控
    "PSU_X0": -260.0, "PSU_X1": -150.0,
    "PSU_Y0": -150.0, "PSU_Y1": -30.0,
    "PSU_Z0": 15.0,   "PSU_Z1": 90.0,
    "DRV_X0": 150.0,  "DRV_X1": 260.0,
    "DRV_Y0": -150.0, "DRV_Y1": -30.0,
    "DRV_Z0": 15.0,   "DRV_Z1": 60.0,
    "MB_X0": 0.0,     "MB_X1": 250.0,
    "MB_Y0": 60.0,    "MB_Y1": 105.0,
    "MB_Z0": 630.0,   "MB_Z1": 660.0,

    # ------------------------------------------------------------ 控制面板
    "CP_X": 150.0,             # 面板半宽
    "CP_Z0": 600.0,
    "CP_Z1": 680.0,
    "SCR_X": 130.0,            # 屏幕半宽
    "SCR_Z0": 608.0,
    "SCR_Z1": 672.0,

    # ------------------------------------------------------------- 防穿模阈值
    "MIN_CLEARANCE": 1.0,      # 全局最小安全间隙 mm
    "CLASH_VOL": 1.0,          # 包围盒相交体积阈值 mm^3

    # ------------------------------------------------------------- 动画时间轴
    "FPS": 24,
    "F_START": 1,
    "F_END": 360,
    "F_LIFT_A": 48,            # 2 s  开始升降
    "F_LIFT_B": 144,           # 6 s  到达推出位
    "F_GAP_MID": 168,          # 7 s  中间夹层校准到最大
    "F_GAP_B": 192,            # 8 s  定位到目标厚度
    "F_PUSH_CONTACT": 198,     # 8.25 s 推书板接触书
    "F_PUSH_B": 264,           # 11 s 推出完成
    "F_RET_B": 312,            # 13 s 推书板退回
})


def derive(p=None):
    """推导派生尺寸并自检参数表内部的几何自洽性。"""
    d = dict(P if p is None else p)

    d["OUT_X"] = d["CAB_W"] / 2.0                  # 300
    d["OUT_Y"] = d["CAB_D"] / 2.0                  # 175
    d["IN_X"] = d["OUT_X"] - d["POST"] - d["PANEL_T"]     # 272
    d["IN_Y"] = d["OUT_Y"] - d["POST"]                    # 155  → 面板内侧 155? 见下
    # 侧板/背板贴型材内侧：型材中心 ±290 / ±165，内侧型材面 = 280 / 155
    d["POST_XC"] = d["OUT_X"] - d["POST"] / 2.0     # 290
    d["POST_YC"] = d["OUT_Y"] - d["POST"] / 2.0     # 165
    d["IN_X"] = d["OUT_X"] - d["POST"]              # 280 型材内表面
    d["IN_Y_BACK"] = -(d["OUT_Y"] - d["POST"])      # -155 背型材内表面
    d["IN_Y_FRONT"] = d["OUT_Y"] - d["POST"]        # +155 前型材内表面

    # 面板
    d["SIDE_L_X0"] = -d["OUT_X"] + d["POST"]        # -280
    d["SIDE_L_X1"] = d["SIDE_L_X0"] + d["PANEL_T"]  # -272
    d["SIDE_R_X1"] = d["OUT_X"] - d["POST"]         # 280
    d["SIDE_R_X0"] = d["SIDE_R_X1"] - d["PANEL_T"]  # 272
    d["BACK_Y0"] = -d["OUT_Y"]
    d["BACK_Y1"] = d["BACK_Y0"] + d["PANEL_T"]      # -167
    d["FRONT_Y0"] = d["OUT_Y"] - d["PANEL_T"]       # 167
    d["FRONT_Y1"] = d["OUT_Y"]                      # 175
    d["WALL_X"] = d["SIDE_L_X1"]                    # -272 内腔半宽
    d["WALL_Y_B"] = d["BACK_Y1"]                    # -167
    d["WALL_Y_F"] = d["FRONT_Y0"]                   # +167
    d["BASE_Z1"] = d["BASE_T"]
    d["TOP_Z0"] = d["CAB_H"] - d["TOP_T"]
    d["TOP_Z1"] = d["CAB_H"]

    # 书
    d["BOOK_Y1"] = d["BOOK_Y0"] + d["BOOK_Y"]
    d["BOOK_CY"] = (d["BOOK_Y0"] + d["BOOK_Y1"]) / 2.0
    d["BOOK_STACK_H"] = sum(d["BOOK_T"])
    d["BOOK_CUM"] = []
    acc = 0.0
    for t in d["BOOK_T"]:
        d["BOOK_CUM"].append(acc)
        acc += t
    # 目标书（书堆中间 → Book_03）
    d["TARGET_IDX"] = 2
    d["TARGET_OFF"] = d["BOOK_CUM"][d["TARGET_IDX"]]
    d["TARGET_T"] = d["BOOK_T"][d["TARGET_IDX"]]
    # 使 Book_03 下沿对齐推出孔下沿时，平台顶面高度
    d["PLAT_TOP_ATPORT"] = d["PORT_Z0"] - d["TARGET_OFF"]
    d["BOOK3_TOP_ATPORT"] = d["PORT_Z0"] + d["TARGET_T"]

    # 平台
    d["PLAT_CY"] = (d["PLAT_Y0"] + d["PLAT_Y1"]) / 2.0
    d["PLAT_LIFT"] = d["PLAT_TOP_ATPORT"] - d["PLAT_TOP_HOME"]   # 动画升程
    d["BOOK3_PUSH_TRAVEL"] = 0.0  # 由 derive 末尾计算

    # 推出孔
    d["SLOT_H_MIN"] = d["GAP_BOT_MIN"] - d["PORT_Z0"]     # 18
    d["SLOT_H_MAX"] = d["GAP_BOT_MAX"] - d["PORT_Z0"]     # 53

    # 推书板行程 & 书伸出量
    d["PUSH_STROKE"] = d["PUSH_NUT_Y_EXT"] - d["PUSH_NUT_Y_RET"]          # 126
    d["PUSH_PLATE_FRONT_RET"] = d["PUSH_NUT_Y_RET"] + d["PUSH_BEAM_T"] + d["PUSH_PLATE_T"]
    d["PUSH_CONTACT_NUT_Y"] = d["BOOK_Y0"] - d["PUSH_BEAM_T"] - d["PUSH_PLATE_T"]
    d["BOOK3_PUSH_TRAVEL"] = d["PUSH_NUT_Y_EXT"] - d["PUSH_CONTACT_NUT_Y"]
    d["BOOK_FRONT_OUT"] = d["BOOK_Y1"] + d["BOOK3_PUSH_TRAVEL"] - d["OUT_Y"]  # 伸出柜体量

    # 校验：推出孔必须能通过目标书
    assert d["SLOT_H_MAX"] >= d["BOOK_T"][-1] + 2.0, "推出孔最大高度不足以通过最厚书"
    assert d["GAP_BOT_MIN"] >= d["PORT_Z0"] + 15.0 + 2.0, "推出孔最小高度小于最薄书+间隙"
    # 校验：推书板必须能通过最薄书
    assert d["PUSH_PLATE_Z0"] > d["PORT_Z0"], "推书板低于推出孔下沿"
    assert d["PUSH_PLATE_Z0"] + d["PUSH_PLATE_H"] <= d["PORT_Z0"] + d["BOOK_T"][0], \
        "推书板高于最薄书顶面 → 会碰撞上层书"
    # 校验：平台最高位不得撞推书机构
    assert d["PLAT_TOP_MAX"] <= d["PORT_Z0"], "平台顶面超过推出孔下沿"
    # 校验：中间夹层在最低位仍完全遮住开口上沿
    assert d["GAP_BOT_MAX"] + d["GAP_H"] >= d["PORT_OPEN_Z1"] + 8.0, "中间夹层最高位遮不住开口"
    assert d["GAP_BOT_MIN"] + d["GAP_H"] >= d["PORT_OPEN_Z1"] + 8.0, "中间夹层最低位遮不住开口"
    assert d["SONAR_Z"] - d["SONAR_S"] / 2.0 >= \
        d["PLAT_TOP_MAX"] + d["BOOK_STACK_H"] + 4.0, "超声波探头与最高书堆干涉"
    return d


D = derive()

if __name__ == "__main__":
    import json
    print(json.dumps(D, ensure_ascii=False, indent=1))
