# -*- coding: utf-8 -*-
"""速定位书格 — 几何 / 材质基础库（纯数据建模，确定性、可重复、无 bpy.ops 依赖）"""
import math
import bpy
from mathutils import Vector, Matrix

TAU = math.pi * 2.0

# (u, v, w) 局部坐标 → 世界坐标的基向量；w 永远是被驱动部件的“轴线 / 主轴”
AXES = {
    "X": (Vector((0, 1, 0)), Vector((0, 0, 1)), Vector((1, 0, 0))),
    "Y": (Vector((1, 0, 0)), Vector((0, 0, 1)), Vector((0, 1, 0))),
    "Z": (Vector((1, 0, 0)), Vector((0, 1, 0)), Vector((0, 0, 1))),
}

CREATED = []          # 本次会话创建的全部对象名
_MATS = {}


# --------------------------------------------------------------------- 集合
def get_collection(name, parent=None):
    col = bpy.data.collections.get(name)
    if col is None:
        col = bpy.data.collections.new(name)
        (parent or bpy.context.scene.collection).children.link(col)
    return col


def clear_all():
    for ob in list(bpy.data.objects):
        bpy.data.objects.remove(ob, do_unlink=True)
    for block in (bpy.data.meshes, bpy.data.curves, bpy.data.materials,
                  bpy.data.lights, bpy.data.cameras, bpy.data.fonts,
                  bpy.data.actions, bpy.data.images):
        for b in list(block):
            try:
                block.remove(b, do_unlink=True)
            except Exception:
                pass
    for col in list(bpy.data.collections):
        bpy.data.collections.remove(col)
    CREATED.clear()
    _MATS.clear()


# --------------------------------------------------------------------- 材质
def _set(node, name, value):
    for s in node.inputs:
        if s.name == name:
            s.default_value = value
            return True
    return False


def mat(name, base=(0.8, 0.8, 0.8), metallic=0.0, rough=0.5,
        transmission=0.0, ior=1.45, emission=None, emission_strength=0.0,
        alpha=1.0, coat=0.0):
    if name in _MATS:
        return _MATS[name]
    m = bpy.data.materials.new(name)
    m.use_nodes = True
    bsdf = m.node_tree.nodes.get("Principled BSDF")
    _set(bsdf, "Base Color", (base[0], base[1], base[2], 1.0))
    _set(bsdf, "Metallic", metallic)
    _set(bsdf, "Roughness", rough)
    _set(bsdf, "IOR", ior)
    _set(bsdf, "Alpha", alpha)
    if coat:
        _set(bsdf, "Coat Weight", coat)
    if transmission:
        _set(bsdf, "Transmission Weight", transmission)
    if emission is not None:
        _set(bsdf, "Emission Color", (emission[0], emission[1], emission[2], 1.0))
        _set(bsdf, "Emission Strength", emission_strength)
    m.diffuse_color = (base[0], base[1], base[2], alpha)
    m.roughness = rough
    m.metallic = metallic
    _MATS[name] = m
    return m


def build_materials():
    """第 10 节：材料 → 真实工程材料。"""
    M = {}
    M["alu_frame"] = mat("MAT_Alu_Frame", (0.62, 0.63, 0.66), 0.92, 0.34, coat=0.15)
    M["alu_plate"] = mat("MAT_Alu_Plate", (0.74, 0.76, 0.79), 0.88, 0.42)
    M["alu_anod"] = mat("MAT_Alu_Anodized", (0.46, 0.48, 0.52), 0.85, 0.38)
    M["steel"] = mat("MAT_Steel_Screw", (0.56, 0.57, 0.60), 1.0, 0.24)
    M["rail"] = mat("MAT_Steel_Rail", (0.78, 0.79, 0.82), 1.0, 0.14)
    M["motor_blk"] = mat("MAT_Motor_Black", (0.035, 0.036, 0.040), 0.55, 0.42, coat=0.2)
    M["motor_sil"] = mat("MAT_Motor_Silver", (0.80, 0.81, 0.84), 1.0, 0.28)
    M["pvc"] = mat("MAT_PVC_White", (0.895, 0.900, 0.910), 0.0, 0.36, coat=0.2)
    M["pvc_in"] = mat("MAT_PVC_Inner", (0.80, 0.81, 0.82), 0.0, 0.45)
    M["glass"] = mat("MAT_Acrylic_Clear", (0.86, 0.90, 0.94), 0.0, 0.03,
                     transmission=0.92, ior=1.49)
    M["mid"] = mat("MAT_MiddleLayer_Board", (0.90, 0.91, 0.92), 0.0, 0.40)
    M["plastic_blk"] = mat("MAT_Plastic_Black", (0.030, 0.030, 0.033), 0.0, 0.48)
    M["plastic_gry"] = mat("MAT_Plastic_Gray", (0.28, 0.29, 0.31), 0.0, 0.45)
    M["pcb"] = mat("MAT_PCB_Green", (0.045, 0.24, 0.11), 0.0, 0.44)
    M["screen"] = mat("MAT_Screen", (0.012, 0.016, 0.030), 0.0, 0.09,
                      emission=(0.10, 0.45, 0.80), emission_strength=0.55)
    M["rubber"] = mat("MAT_Rubber", (0.045, 0.045, 0.048), 0.0, 0.72)
    M["pages"] = mat("MAT_Book_Pages", (0.93, 0.90, 0.82), 0.0, 0.62)
    M["cover_r"] = mat("MAT_Book_Cover_Red", (0.60, 0.10, 0.09), 0.0, 0.46, coat=0.25)
    M["cover_b"] = mat("MAT_Book_Cover_Blue", (0.07, 0.20, 0.46), 0.0, 0.46, coat=0.25)
    M["cover_g"] = mat("MAT_Book_Cover_Green", (0.09, 0.34, 0.22), 0.0, 0.46, coat=0.25)
    M["cover_o"] = mat("MAT_Book_Cover_Orange", (0.72, 0.36, 0.06), 0.0, 0.46, coat=0.25)
    M["cover_p"] = mat("MAT_Book_Cover_Purple", (0.31, 0.14, 0.44), 0.0, 0.46, coat=0.25)
    M["label_w"] = mat("MAT_Label_White", (0.97, 0.97, 0.97), 0.0, 0.35,
                       emission=(1.0, 1.0, 1.0), emission_strength=0.35)
    M["label_bg"] = mat("MAT_Label_Backing", (0.030, 0.034, 0.045), 0.0, 0.62)
    M["label_r"] = mat("MAT_Label_Red", (0.80, 0.10, 0.08), 0.0, 0.40,
                       emission=(1.0, 0.18, 0.12), emission_strength=0.60)
    M["label_g"] = mat("MAT_Label_Green", (0.08, 0.62, 0.22), 0.0, 0.40,
                       emission=(0.15, 1.0, 0.35), emission_strength=0.60)
    M["label_b"] = mat("MAT_Label_Blue", (0.10, 0.35, 0.85), 0.0, 0.40,
                       emission=(0.20, 0.55, 1.0), emission_strength=0.60)
    M["stage"] = mat("MAT_Stage_Floor", (0.34, 0.35, 0.37), 0.0, 0.55)
    return M


# --------------------------------------------------------------------- 网格
def mesh_obj(name, verts, faces, col, material=None, grp="cabinet", part=None,
             smooth=None, origin=(0.0, 0.0, 0.0)):
    me = bpy.data.meshes.new(name)
    me.from_pydata([tuple(v) for v in verts], [], [tuple(f) for f in faces])
    me.validate(verbose=False)
    me.update()
    ob = bpy.data.objects.new(name, me)
    ob.location = origin
    if material is not None:
        me.materials.append(material)
    if smooth is not None:
        for i, p in enumerate(me.polygons):
            p.use_smooth = bool(smooth[i]) if i < len(smooth) else bool(smooth[0])
    ob["grp"] = grp
    ob["part"] = part or name
    col.objects.link(ob)
    CREATED.append(name)
    return ob


class MeshAcc(object):
    """多材质网格累加器：把组合件（电机 / 轴承座 / 导轨滑块）合成单一命名零件。"""

    def __init__(self):
        self.v = []
        self.f = []
        self.s = []
        self.m = []
        self.cur = None

    def use(self, material):
        self.cur = material
        return self

    def _push(self, pts, faces, smooth):
        b = len(self.v)
        self.v.extend(pts)
        for fc in faces:
            self.f.append(tuple(b + i for i in fc))
            self.s.append(smooth)
            self.m.append(self.cur)

    def box(self, c, half, axis):
        U, V, W = AXES[axis]
        cu, cv, cw = c
        hu, hv, hw = half
        pts = []
        for sw in (-1, 1):
            for sv in (-1, 1):
                for su in (-1, 1):
                    p = U * (cu + su * hu) + V * (cv + sv * hv) + W * (cw + sw * hw)
                    pts.append((p.x, p.y, p.z))
        v = [pts[0], pts[1], pts[3], pts[2], pts[4], pts[5], pts[7], pts[6]]
        self._push(v, [(0, 3, 2, 1), (4, 5, 6, 7), (0, 1, 5, 4),
                       (1, 2, 6, 5), (2, 3, 7, 6), (3, 0, 4, 7)], False)
        return self

    def cyl(self, center, r0, r1, a0, a1, axis, seg=32, cap=True):
        U, V, W = AXES[axis]
        pts = []
        for i in range(seg):
            a = TAU * i / seg
            ca, sa = math.cos(a), math.sin(a)
            p0 = U * (center[0] + r0 * ca) + V * (center[1] + r0 * sa) + W * a0
            p1 = U * (center[0] + r1 * ca) + V * (center[1] + r1 * sa) + W * a1
            pts.append((p0.x, p0.y, p0.z))
            pts.append((p1.x, p1.y, p1.z))
        faces = [(i * 2, ((i + 1) % seg) * 2, ((i + 1) % seg) * 2 + 1, i * 2 + 1)
                 for i in range(seg)]
        self._push(pts, faces, True)
        if cap:
            if r1 > 1e-6:
                self._push([pts[2 * i + 1] for i in range(seg)], [tuple(range(seg))], False)
            if r0 > 1e-6:
                self._push([pts[2 * i] for i in range(seg)],
                           [tuple(range(seg - 1, -1, -1))], False)
        return self

    def finish(self, name, col, material, grp="cabinet", part=None,
               origin=(0.0, 0.0, 0.0)):
        ox, oy, oz = origin
        v = [(p[0] - ox, p[1] - oy, p[2] - oz) for p in self.v]
        mats = []
        for m in self.m:
            if m is not None and m not in mats:
                mats.append(m)
        if not mats:
            mats = [material]
        ob = mesh_obj(name, v, self.f, col, mats[0], grp, part, smooth=self.s,
                      origin=origin)
        for extra in mats[1:]:
            ob.data.materials.append(extra)
        idx = {m.name: i for i, m in enumerate(mats)}
        for i, p in enumerate(ob.data.polygons):
            if i < len(self.m) and self.m[i] is not None:
                p.material_index = idx[self.m[i].name]
        return ob


def box(name, x0, x1, y0, y1, z0, z1, col, material=None, grp="cabinet",
        part=None, origin=(0.0, 0.0, 0.0)):
    x0, x1 = min(x0, x1), max(x0, x1)
    y0, y1 = min(y0, y1), max(y0, y1)
    z0, z1 = min(z0, z1), max(z0, z1)
    ox, oy, oz = origin
    v = [(x0 - ox, y0 - oy, z0 - oz), (x1 - ox, y0 - oy, z0 - oz),
         (x1 - ox, y1 - oy, z0 - oz), (x0 - ox, y1 - oy, z0 - oz),
         (x0 - ox, y0 - oy, z1 - oz), (x1 - ox, y0 - oy, z1 - oz),
         (x1 - ox, y1 - oy, z1 - oz), (x0 - ox, y1 - oy, z1 - oz)]
    f = [(0, 3, 2, 1), (4, 5, 6, 7), (0, 1, 5, 4), (1, 2, 6, 5), (2, 3, 7, 6), (3, 0, 4, 7)]
    return mesh_obj(name, v, f, col, material, grp, part, smooth=[False], origin=origin)


def multi_box(name, specs, col, material=None, grp="cabinet", part=None,
              origin=(0.0, 0.0, 0.0)):
    """把若干轴对齐盒体合并成同一个网格对象（框架、支座、拨叉等组合件）。"""
    ox, oy, oz = origin
    verts, faces = [], []
    for (x0, x1, y0, y1, z0, z1) in specs:
        x0, x1 = min(x0, x1) - ox, max(x0, x1) - ox
        y0, y1 = min(y0, y1) - oy, max(y0, y1) - oy
        z0, z1 = min(z0, z1) - oz, max(z0, z1) - oz
        b = len(verts)
        verts += [(x0, y0, z0), (x1, y0, z0), (x1, y1, z0), (x0, y1, z0),
                  (x0, y0, z1), (x1, y0, z1), (x1, y1, z1), (x0, y1, z1)]
        faces += [(b + 0, b + 3, b + 2, b + 1), (b + 4, b + 5, b + 6, b + 7),
                  (b + 0, b + 1, b + 5, b + 4), (b + 1, b + 2, b + 6, b + 5),
                  (b + 2, b + 3, b + 7, b + 6), (b + 3, b + 0, b + 4, b + 7)]
    return mesh_obj(name, verts, faces, col, material, grp, part, smooth=[False],
                    origin=origin)


def cylinder(name, center, radius, z0, z1, axis, col, material=None, grp="cabinet",
             part=None, seg=32, origin=(0.0, 0.0, 0.0), cap=True, r2=None):
    A = MeshAcc().use(material)
    A.cyl(center, radius, radius if r2 is None else r2, z0, z1, axis, seg, cap)
    return A.finish(name, col, material, grp, part, origin)


def lead_screw(name, center, a0, a1, axis, core_r, thread_r, pitch, col,
               material=None, grp="screws", part=None, origin=(0.0, 0.0, 0.0),
               seg_per_turn=16):
    """真实螺旋丝杆：核心圆柱 + 螺旋梯形螺纹（螺距可辨识）。"""
    U, V, W = AXES[axis]
    A = MeshAcc().use(material)
    # 核心圆柱
    A.cyl(center, core_r, core_r, a0, a1, axis, 32, True)
    # 螺旋螺纹
    L = a1 - a0
    turns = L / pitch
    n = max(24, int(turns * seg_per_turn))
    h = pitch * 0.5
    prof = [(-h * 0.62, core_r * 0.97), (-h * 0.30, thread_r),
            (h * 0.30, thread_r), (h * 0.62, core_r * 0.97)]
    m = len(prof)
    verts = []
    for i in range(n + 1):
        t = i / n
        ang = TAU * turns * t
        a = a0 + L * t
        ca, sa = math.cos(ang), math.sin(ang)
        for (dz, r) in prof:
            p = U * (center[0] + r * ca) + V * (center[1] + r * sa) + W * (a + dz)
            verts.append((p.x, p.y, p.z))
    faces = []
    for i in range(n):
        for j in range(m):
            j2 = (j + 1) % m
            faces.append((i * m + j, i * m + j2, (i + 1) * m + j2, (i + 1) * m + j))
    A._push(verts, faces, True)
    return A.finish(name, col, material, grp, part, origin)


# --------------------------------------------------------------- 标准零部件
def stepper42(name, origin_pt, axis, a0, sign, col, M, grp="liftframe", part=None,
              body=38.0, origin=(0.0, 0.0, 0.0)):
    """42 步进电机（NEMA-17）：黑色机身 + 银端盖 + 22 止口 + 5 输出轴 + 4 安装螺孔。
    单一零件对象，多材质槽。a0 = 机身起始坐标，sign 决定出轴方向。"""
    s = float(sign)
    a0 = float(a0)
    a1 = a0 + body * s
    cu, cv = float(origin_pt[0]), float(origin_pt[1])
    half = 21.15
    A = MeshAcc()
    A.use(M["motor_blk"]).box((cu, cv, (a0 + a1) * 0.5), (half, half, body * 0.5), axis)
    cap0, cap1 = a1, a1 + 6.0 * s
    A.use(M["motor_sil"]).box((cu, cv, (cap0 + cap1) * 0.5), (half, half, 3.0), axis)
    A.cyl((cu, cv), 11.0, 11.0, cap1, cap1 + 2.0 * s, axis, 24)
    A.cyl((cu, cv), 2.5, 2.5, cap1 + 2.0 * s, cap1 + 22.0 * s, axis, 20)
    A.use(M["plastic_blk"])
    for su in (-1, 1):
        for sv in (-1, 1):
            A.cyl((cu + su * 15.5, cv + sv * 15.5), 1.7, 1.7, cap1 - 0.4 * s,
                  cap1 + 0.8 * s, axis, 12)
    A.use(M["motor_blk"]).cyl((cu, cv), 11.0, 11.0, a0 - 1.2 * s, a0, axis, 24)
    return A.finish(name, col, M["motor_blk"], grp, part, origin)


def coupler(name, origin_pt, axis, a0, a1, col, M, grp, part=None, r=10.0,
            origin=(0.0, 0.0, 0.0)):
    """刚性联轴器：铝制圆柱 + 两道夹紧槽。"""
    A = MeshAcc()
    A.use(M["motor_sil"]).cyl(origin_pt, r, r, a0, a1, axis, 24, True)
    mid = (a0 + a1) * 0.5
    A.use(M["plastic_blk"])
    for dz in (-(a1 - a0) * 0.28, (a1 - a0) * 0.28):
        A.cyl(origin_pt, r * 1.02, r * 1.02, mid + dz - 0.7, mid + dz + 0.7, axis, 24, True)
    return A.finish(name, col, M["motor_sil"], grp, part, origin)


def bearing_block(name, origin_pt, axis, a0, a1, col, M, grp, part=None,
                  half=(17.0, 13.0), origin=(0.0, 0.0, 0.0)):
    """轴承座：铝块 + 轴承外圈 + 内孔。"""
    c = (a0 + a1) * 0.5
    A = MeshAcc()
    A.use(M["alu_plate"]).box((origin_pt[0], origin_pt[1], c),
                              (half[0], half[1], (a1 - a0) * 0.5), axis)
    A.use(M["rail"]).cyl(origin_pt, 11.0, 11.0, a0 - 0.5, a1 + 0.5, axis, 20, True)
    A.use(M["plastic_blk"]).cyl(origin_pt, 5.0, 5.0, a0 - 1.0, a1 + 1.0, axis, 16, True)
    return A.finish(name, col, M["alu_plate"], grp, part, origin)


def linear_rail(name, origin_pt, axis, a0, a1, w, d, col, M, grp, part=None,
                origin=(0.0, 0.0, 0.0), bolt_n=6):
    """直线导轨：底座 + 淬硬轨头 + 均布安装螺孔。"""
    c = (a0 + a1) * 0.5
    Lh = (a1 - a0) * 0.5
    A = MeshAcc()
    A.use(M["alu_plate"]).box((origin_pt[0], origin_pt[1], c), (w * 0.5, d * 0.5, Lh), axis)
    A.use(M["rail"]).box((origin_pt[0], origin_pt[1], c),
                         (w * 0.30, d * 0.62, Lh * 0.995), axis)
    A.use(M["plastic_blk"])
    for i in range(bolt_n):
        a = a0 + (a1 - a0) * (i + 0.5) / bolt_n
        A.cyl(origin_pt, w * 0.17, w * 0.17, a - 0.6, a + 0.6, axis, 10, True)
    return A.finish(name, col, M["alu_plate"], grp, part, origin)


def slider_block(name, origin_pt, axis, a0, a1, w, d, col, M, grp, part=None,
                 origin=(0.0, 0.0, 0.0), bolts=True):
    """直线导轨滑块：本体 + 回珠凸缘 + 4 颗安装螺钉。"""
    c = (a0 + a1) * 0.5
    Lh = (a1 - a0) * 0.5
    A = MeshAcc()
    A.use(M["alu_anod"]).box((origin_pt[0], origin_pt[1], c), (w * 0.5, d * 0.5, Lh), axis)
    A.use(M["plastic_gry"]).box((origin_pt[0], origin_pt[1], c),
                                (w * 0.30, d * 0.62, Lh * 0.98), axis)
    if bolts:
        A.use(M["motor_sil"])
        for su in (-1, 1):
            for sv in (-1, 1):
                p = (origin_pt[0] + su * w * 0.33, origin_pt[1] + sv * d * 0.33)
                A.cyl(p, 1.7, 1.7, c - Lh * 0.92, c + Lh * 0.92, axis, 10, True)
    return A.finish(name, col, M["alu_anod"], grp, part, origin)


def ball_screw_nut(name, center, a0, a1, axis, r_out, col, M, grp, part=None,
                   origin=(0.0, 0.0, 0.0), flange=None):
    """丝杆螺母：法兰圆柱 + 可选连接法兰块（flange = (u0,u1,v0,v1)）。"""
    A = MeshAcc()
    A.use(M["steel"]).cyl(center, r_out, r_out, a0, a1, axis, 24, True)
    A.use(M["rail"]).cyl(center, r_out * 0.55, r_out * 0.55, a0 - 1.2, a1 + 1.2,
                         axis, 16, True)
    if flange:
        U, V, W = AXES[axis]
        c = (a0 + a1) * 0.5
        A.use(M["steel"]).box((center[0], center[1], c),
                              ((flange[1] - flange[0]) * 0.5, (flange[3] - flange[2]) * 0.5,
                               (a1 - a0) * 0.5), axis)
    return A.finish(name, col, M["steel"], grp, part, origin)


# --------------------------------------------------------------- 标注 / 工具
def text_label(name, body, loc, size, col, M, rot=(math.radians(90), 0, 0),
               grp="stage", part=None, font_path="C:/Windows/Fonts/simhei.ttf",
               align="CENTER", extrude=0.4, material=None):
    """中文标注：使用系统 SimHei 字体（Blender 自带字体无中文字形）。"""
    cu = bpy.data.curves.new(name, type="FONT")
    cu.body = body
    cu.size = size
    cu.align_x = align
    cu.align_y = "CENTER"
    cu.extrude = extrude
    try:
        f = bpy.data.fonts.get("SBS_CJK") or bpy.data.fonts.load(font_path)
        f.name = "SBS_CJK"
        cu.font = f
    except Exception:
        pass
    ob = bpy.data.objects.new(name, cu)
    ob.location = loc
    ob.rotation_euler = rot
    ob.data.materials.append(material or M["label_w"])
    ob["grp"] = grp
    ob["part"] = part or name
    col.objects.link(ob)
    CREATED.append(name)
    return ob


def bevel(ob, width=0.8, segments=1, angle=40.0):
    if ob.type != "MESH":
        return ob
    m = ob.modifiers.new("Bevel", "BEVEL")
    m.width = width
    m.segments = segments
    m.limit_method = "ANGLE"
    m.angle_limit = math.radians(angle)
    m.harden_normals = False
    return ob


def apply_modifiers():
    """用 depsgraph 评估结果替换网格数据（后台模式无操作符上下文也可用）。"""
    n = 0
    dg = bpy.context.evaluated_depsgraph_get()
    for ob in list(bpy.data.objects):
        if ob.type != "MESH" or not ob.modifiers:
            continue
        ev = ob.evaluated_get(dg)
        try:
            me = bpy.data.meshes.new_from_object(ev)
        except Exception:
            continue
        old = ob.data
        me.name = old.name + "_baked"
        ob.data = me
        ob.modifiers.clear()
        if old.users == 0:
            bpy.data.meshes.remove(old)
        n += 1
    return n


def parent_to(child, parent):
    """建立真实父子关系。父级均为纯平移（无旋转/缩放），
    因此 matrix_parent_inverse 可直接由父级位移反解，无需求助 depsgraph。"""
    child.parent = parent
    child.matrix_parent_inverse = Matrix.Translation(-Vector(parent.location))
    return child


def empty(name, loc, col, grp="cabinet", part=None, size=30.0, dtype="PLAIN_AXES"):
    ob = bpy.data.objects.new(name, None)
    ob.empty_display_type = dtype
    ob.empty_display_size = size
    ob.location = loc
    ob["grp"] = grp
    ob["part"] = part or name
    col.objects.link(ob)
    CREATED.append(name)
    return ob


def stats():
    tot_v = tot_f = 0
    for ob in bpy.data.objects:
        if ob.type == "MESH":
            tot_v += len(ob.data.vertices)
            tot_f += len(ob.data.polygons)
    return {"objects": len(bpy.data.objects), "verts": tot_v, "faces": tot_f}
