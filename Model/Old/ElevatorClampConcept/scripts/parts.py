"""Part builders - one function per mechanical sub-assembly."""

import math

from mathutils import Matrix, Vector

import geom


# ============================================================ 1. cabinet ==
def build_cabinet(P, M, coll):
    objs = []
    x0, x1 = P['CAB_X0'], P['CAB_X1']
    y0, y1 = P['CAB_Y0'], P['CAB_Y1']
    z0, z1 = P['CAB_Z0'], P['CAB_Z1']
    ix0, ix1 = P['IN_X0'], P['IN_X1']
    iy0, iy1 = P['IN_Y0'], P['IN_Y1']
    t = P['FRAME_T']
    F = M['frame']
    A = M['alu']
    S = M['shell']

    # ---- structural frame: 4 posts + 8 rails (anodised aluminium)
    for sx, sy in ((-1, -1), (-1, 1), (1, -1), (1, 1)):
        ax0 = x0 if sx < 0 else ix1
        ax1 = ix0 if sx < 0 else x1
        ay0 = y0 if sy < 0 else iy1
        ay1 = iy0 if sy < 0 else y1
        objs.append(geom.box('Frame_Post_%s%s' % ('L' if sx < 0 else 'R',
                                                  'B' if sy < 0 else 'F'),
                             ((ax0, ax1), (ay0, ay1), (z0, z1)), coll, F,
                             'cabinet'))
    for zz0, zz1, tag in ((z0, z0 + t, 'Bottom'), (z1 - t, z1, 'Top')):
        objs.append(geom.box('Frame_Rail_%s_Back' % tag,
                             ((x0, x1), (y0, iy0), (zz0, zz1)), coll, F,
                             'cabinet'))
        objs.append(geom.box('Frame_Rail_%s_Front' % tag,
                             ((x0, x1), (iy1, y1), (zz0, zz1)), coll, F,
                             'cabinet'))
        objs.append(geom.box('Frame_Rail_%s_Left' % tag,
                             ((x0, ix0), (y0, y1), (zz0, zz1)), coll, F,
                             'cabinet'))
        objs.append(geom.box('Frame_Rail_%s_Right' % tag,
                             ((ix1, x1), (y0, y1), (zz0, zz1)), coll, F,
                             'cabinet'))

    # ---- translucent acrylic envelope
    objs.append(geom.box('Shell_Side_Left',
                         ((x0, ix0), (iy0, iy1), (z0 + t, z1 - t)), coll, S,
                         'cabinet'))
    objs.append(geom.box('Shell_Side_Right',
                         ((ix1, x1), (iy0, iy1), (z0 + t, z1 - t)), coll, S,
                         'cabinet'))
    objs.append(geom.box('Shell_Base',
                         ((ix0, ix1), (iy0, iy1), (z0, z0 + t)), coll, S,
                         'cabinet'))
    objs.append(geom.box('Shell_Back',
                         ((ix0, ix1), (y0, iy0), (z0 + t, z1 - t)), coll, S,
                         'cabinet'))

    # ---- front face: three levels, the middle one fully open (book port)
    objs.append(geom.box('Front_Level_Bottom',
                         ((ix0, ix1), (iy1, y1), (z0 + t, P['PORT_Z0'])),
                         coll, S, 'cabinet'))
    objs.append(geom.box('Front_Level_Top',
                         ((ix0, ix1), (iy1, y1), (P['PORT_Z1'], z1 - t)),
                         coll, S, 'cabinet'))
    objs.append(geom.box('Front_Port_Jamb_Left',
                         ((ix0, ix0 + t), (iy1, y1), (P['PORT_Z0'],
                                                     P['PORT_Z1'])),
                         coll, A, 'cabinet'))
    objs.append(geom.box('Front_Port_Jamb_Right',
                         ((ix1 - t, ix1), (iy1, y1), (P['PORT_Z0'],
                                                     P['PORT_Z1'])),
                         coll, A, 'cabinet'))
    objs.append(geom.box('Front_Port_Sill',
                         ((ix0, ix1), (iy1, y1),
                          (P['PORT_Z0'], P['PORT_Z0'] + 8.0)), coll, A,
                         'cabinet'))

    # ---- top face with the 250 x 80 book insertion hole
    hx0 = P['TOP_HOLE_CX'] - P['TOP_HOLE_W'] / 2.0
    hx1 = P['TOP_HOLE_CX'] + P['TOP_HOLE_W'] / 2.0
    hy0 = P['TOP_HOLE_CY'] - P['TOP_HOLE_D'] / 2.0
    hy1 = P['TOP_HOLE_CY'] + P['TOP_HOLE_D'] / 2.0
    objs.append(geom.box('Top_Plate_Rear',
                         ((ix0, ix1), (iy0, hy0), (z1 - t, z1)), coll, S,
                         'cabinet'))
    objs.append(geom.box('Top_Plate_Front',
                         ((ix0, ix1), (hy1, iy1), (z1 - t, z1)), coll, S,
                         'cabinet'))
    objs.append(geom.box('Top_Plate_Left',
                         ((ix0, hx0), (hy0, hy1), (z1 - t, z1)), coll, S,
                         'cabinet'))
    objs.append(geom.box('Top_Plate_Right',
                         ((hx1, ix1), (hy0, hy1), (z1 - t, z1)), coll, S,
                         'cabinet'))
    b = 11.0
    objs.append(geom.box('Top_Hole_Bezel_Left',
                         ((hx0 - b, hx0), (hy0 - b, hy1 + b),
                          (z1 - t, z1 + 8.0)), coll, A, 'cabinet',
                         part='InsertionHoleBezel'))
    objs.append(geom.box('Top_Hole_Bezel_Right',
                         ((hx1, hx1 + b), (hy0 - b, hy1 + b),
                          (z1 - t, z1 + 8.0)), coll, A, 'cabinet',
                         part='InsertionHoleBezel'))
    objs.append(geom.box('Top_Hole_Bezel_Rear',
                         ((hx0, hx1), (hy0 - b, hy0), (z1 - t, z1 + 8.0)),
                         coll, A, 'cabinet', part='InsertionHoleBezel'))
    objs.append(geom.box('Top_Hole_Bezel_Front',
                         ((hx0, hx1), (hy1, hy1 + b), (z1 - t, z1 + 8.0)),
                         coll, A, 'cabinet', part='InsertionHoleBezel'))

    # ---- operator panel on the top face (UI reference from the brief)
    objs.append(geom.box('Control_Panel',
                         ((-90.0, 90.0), (95.0, 185.0), (z1, z1 + 12.0)),
                         coll, M['motor_cap'], 'cabinet'))
    objs.append(geom.box('Control_Screen',
                         ((-72.0, 72.0), (107.0, 173.0), (z1 + 12.0,
                                                         z1 + 14.0)),
                         coll, M['clear'], 'cabinet'))

    # clear panels render but do not block light: the interior mechanism
    # has to stay readable in the preview
    for o in objs:
        if o.data.materials and o.data.materials[0] in (S, M['shell_top']):
            try:
                o.visible_shadow = False
            except Exception:
                pass
    return objs


# ======================================================= 2. drive columns ==
def build_drive(P, M, coll):
    """Lead screws, passive rails, bottom 42 steppers - all static."""
    objs = []
    cx = P['COL_X']
    sy = P['SCREW_Y']
    ry = P['RAIL_Y']
    A = M['alu_dark']
    ST = M['steel']
    RL = M['rail']
    MO = M['motor']
    SH = M['shaft']
    EC = M['motor_cap']

    for sx, tag in ((-1, 'Left'), (1, 'Right')):
        x = sx * cx
        objs.append(geom.cyl('Drive_Screw_%s' % tag,
                             (x, sy, (P['SCREW_Z0'] + P['SCREW_Z1']) / 2.0),
                             'Z', P['SCREW_D'] / 2.0, P['SCREW_LEN'], coll,
                             ST, 'drive', part='LeadScrew900'))
        objs.append(geom.box('Drive_Screw_Base_%s' % tag,
                             ((x - 20.0, x + 20.0), (sy - 20.0, sy + 20.0),
                              (P['COUPLING_BOT_Z1'],
                               P['COUPLING_BOT_Z1'] + 26.0)),
                             coll, A, 'drive', part='ScrewBaseBearing'))
        objs.append(geom.box('Drive_Rail_%s' % tag,
                             ((x - P['RAIL_SQ'] / 2.0, x + P['RAIL_SQ'] / 2.0),
                              (ry - P['RAIL_SQ'] / 2.0, ry + P['RAIL_SQ'] / 2.0),
                              (P['RAIL_Z0'], P['RAIL_Z1'])), coll, RL, 'drive',
                             part='LinearRail900'))
        objs.append(geom.box('Drive_Rail_Foot_%s' % tag,
                             ((x - 22.0, x + 22.0), (ry - 25.0, ry + 25.0),
                              (P['FRAME_T'], P['FRAME_T'] + 18.0)), coll, A,
                             'drive', part='RailFoot'))
        mz = P['MOTOR_FOOT_Z0']
        objs.append(geom.box('Drive_Motor42_%s_Body' % tag,
                             ((x - P['MOTOR_W'] / 2.0, x + P['MOTOR_W'] / 2.0),
                              (sy - P['MOTOR_L'] / 2.0, sy + P['MOTOR_L'] / 2.0),
                              (mz, mz + P['MOTOR_H'])), coll, MO, 'drive',
                             part='NEMA17_Vertical'))
        objs.append(geom.box('Drive_Motor42_%s_Cap' % tag,
                             ((x - P['MOTOR_W'] / 2.0 + 2.0,
                               x + P['MOTOR_W'] / 2.0 - 2.0),
                              (sy - P['MOTOR_L'] / 2.0 + 2.0,
                               sy + P['MOTOR_L'] / 2.0 - 2.0),
                              (mz + P['MOTOR_H'], mz + P['MOTOR_H'] + 6.0)),
                             coll, EC, 'drive', part='NEMA17_EndCap'))
        objs.append(geom.cyl('Drive_Motor42_%s_Shaft' % tag,
                             (x, sy, mz + P['MOTOR_H'] + 11.0), 'Z', 4.0,
                             22.0, coll, SH, 'drive', part='MotorShaft'))
        objs.append(geom.cyl('Drive_Coupling_%s' % tag,
                             (x, sy, (P['COUPLING_BOT_Z0'] +
                                      P['COUPLING_BOT_Z1']) / 2.0), 'Z',
                             11.0, P['COUPLING_H'], coll, A, 'drive',
                             part='FlexCoupling'))
    return objs


# ========================================================== 3. platform ===
def build_platform(P, M, coll):
    """L-profile carrier tray plus its driven and passive 80 mm carriages."""
    objs = []
    AL = M['alu']
    A = M['alu_dark']
    px0, px1 = P['PLAT_X0'], P['PLAT_X1']
    py0, py1 = P['PLAT_Y0'], P['PLAT_Y1']
    zt, zb = P['PLAT_TOP_Z'], P['PLAT_BOT_Z']
    sx = P['PLAT_SLOT_X']
    sy0, sy1 = P['PLAT_SLOT_Y0'], P['PLAT_SLOT_Y1']
    fz0, fz1 = P['PLAT_FLANGE_Z0'], P['PLAT_FLANGE_Z1']
    ft = P['PLAT_FLANGE_T']
    cx = P['CARR_X']
    bz0 = P['PLAT_BRACKET_Z0']

    # carrier plate: the rear band is split by the pusher slot
    objs.append(geom.box('Platform_Plate_RearLeft',
                         ((px0, -sx), (sy0, sy1), (zb, zt)), coll, AL,
                         'platform', part='CarrierPlate'))
    objs.append(geom.box('Platform_Plate_RearRight',
                         ((sx, px1), (sy0, sy1), (zb, zt)), coll, AL,
                         'platform', part='CarrierPlate'))
    objs.append(geom.box('Platform_Plate_Main',
                         ((px0, px1), (sy1, py1), (zb, zt)), coll, AL,
                         'platform', part='CarrierPlate'))
    # 10 mm retaining lips on the rear and left edges -> L profile
    objs.append(geom.box('Platform_Lip_RearLeft',
                         ((px0, -sx), (py0, py0 + ft), (fz0, fz1)), coll, AL,
                         'platform', part='RearRetainingLip'))
    objs.append(geom.box('Platform_Lip_RearRight',
                         ((sx, px1), (py0, py0 + ft), (fz0, fz1)), coll, AL,
                         'platform', part='RearRetainingLip'))
    objs.append(geom.box('Platform_Lip_Left',
                         ((px0, px0 + ft), (py0 + ft, py1), (fz0, fz1)),
                         coll, AL, 'platform', part='LeftRetainingLip'))
    # brackets butt the carriages and stop 2 mm short of the opposite column
    objs.append(geom.box('Platform_Bracket_Left',
                         ((-cx, px0), (-203.0, py0), (bz0, zt)), coll, A,
                         'platform', part='PlatformBracket'))
    objs.append(geom.box('Platform_Bracket_Right',
                         ((px1, cx), (-133.0, py0), (bz0, zt)), coll, A,
                         'platform', part='PlatformBracket'))
    objs.extend(_carriage(P, M, coll, 'Platform_Carriage_Left', 'platform',
                          P['PLAT_CARR_L']))
    objs.extend(_carriage(P, M, coll, 'Platform_Carriage_Right', 'platform',
                          P['PLAT_CARR_R']))
    return objs


def _carriage(P, M, coll, name, grp, center):
    cx, cy, cz = center
    t = P['SLIDER_T'] / 2.0
    d = P['SLIDER_D'] / 2.0
    h = P['SLIDER_H'] / 2.0
    body = geom.box(name, ((cx - t, cx + t), (cy - d, cy + d),
                           (cz - h, cz + h)), coll, M['alu_dark'], grp,
                      part='SlideCarriage80')
    block = geom.box(name + '_Nut', ((cx - t + 4.0, cx + t - 4.0),
                                     (cy - 12.0, cy + 12.0),
                                     (cz - h, cz + h)), coll, M['steel'],
                     grp, part='ScrewNut')
    return [body, block]


# ======================================================= 4. limit plate ===
def build_limit(P, M, coll):
    """Limit plate, its 3 mm rubber pad, driven from the right-hand screw."""
    objs = []
    AL = M['alu_dark']
    A = M['alu_dark']
    lx0, lx1 = P['LIM_X0'], P['LIM_X1']
    ly0, ly1 = P['LIM_Y0'], P['LIM_Y1']
    pz0, pz1 = P['LIM_PAD_BOTTOM_Z'], P['LIM_PAD_TOP_Z']
    bz0, bz1 = P['LIM_BODY_Z0'], P['LIM_BODY_Z1']
    cx = P['CARR_X']

    objs.append(geom.box('Limit_Pad_Black', ((lx0, lx1), (ly0, ly1),
                                             (pz0, pz1)), coll,
                         M['rubber'], 'limit', part='RubberPad3mm'))
    objs.append(geom.box('Limit_Plate_Body', ((lx0, lx1), (ly0, ly1),
                                              (bz0, bz1)), coll, AL, 'limit',
                         part='LimitPlate'))
    objs.append(geom.box('Limit_Bracket_Left',
                         ((-cx, lx0), (-133.0, ly0), (bz0 - 5.0, bz1)),
                         coll, A, 'limit', part='LimitBracket'))
    objs.append(geom.box('Limit_Bracket_Right',
                         ((lx1, cx), (-203.0, ly0), (bz0 - 5.0, bz1)),
                         coll, A, 'limit', part='LimitBracket'))
    objs.extend(_carriage(P, M, coll, 'Limit_Carriage_Left', 'limit',
                          P['LIM_CARR_L']))
    objs.extend(_carriage(P, M, coll, 'Limit_Carriage_Right', 'limit',
                          P['LIM_CARR_R']))
    return objs


# ========================================================== 5. push unit ==
def build_push(P, M, coll):
    """Rear push motor on the inner back wall, rod and push plate."""
    objs = []
    AL = M['alu']
    MO = M['motor']
    EC = M['motor_cap']
    SH = M['shaft']
    CL = M['clear']
    cz = P['PUSH_CZ']

    objs.append(geom.box('Push_Motor_Mount',
                         ((-45.0, 45.0), (P['PUSH_MOUNT_Y0'],
                                          P['PUSH_MOUNT_Y1']),
                          (550.0, 650.0)), coll, AL, 'push',
                         part='PushMotorFlange'))
    objs.append(geom.box('Push_Motor42_Body',
                         ((-P['MOTOR_W'] / 2.0, P['MOTOR_W'] / 2.0),
                          (P['PUSH_MOTOR_Y0'], P['PUSH_MOTOR_Y1']),
                          (cz - P['MOTOR_L'] / 2.0, cz + P['MOTOR_L'] / 2.0)),
                         coll, MO, 'push', part='NEMA17_Horizontal'))
    objs.append(geom.box('Push_Motor42_Cap',
                         ((-P['MOTOR_W'] / 2.0 + 2.0,
                           P['MOTOR_W'] / 2.0 - 2.0),
                          (P['PUSH_MOTOR_Y1'] - 6.0, P['PUSH_MOTOR_Y1']),
                          (cz - P['MOTOR_L'] / 2.0 + 2.0,
                           cz + P['MOTOR_L'] / 2.0 - 2.0)), coll, EC, 'push',
                         part='NEMA17_EndCap'))
    objs.append(geom.cyl('Push_Coupling',
                         (0.0, (P['COUPLING_Y0'] + P['COUPLING_Y1']) / 2.0,
                          cz), 'Y', P['PUSH_CPL_D'] / 2.0,
                         P['COUPLING_Y1'] - P['COUPLING_Y0'], coll, AL,
                         'push', part='FlexCoupling'))
    rod_len = P['ROD_Y1'] - P['ROD_Y0']
    objs.append(geom.cyl('Push_Rod', (0.0, (P['ROD_Y0'] + P['ROD_Y1']) / 2.0,
                         cz), 'Y', P['PUSH_ROD_D'] / 2.0, rod_len, coll, SH,
                         'push', part='PushRod80mmStroke'))
    objs.append(geom.cyl('Push_Rod_LeadScrew',
                         (0.0, (P['ROD_Y0'] + P['ROD_Y1']) / 2.0, cz), 'Y',
                         3.0, rod_len - 6.0, coll, M['steel'], 'push',
                         part='PushLeadScrew'))
    objs.append(geom.box('Push_Plate_Hub',
                         ((-24.0, 24.0), (P['PUSH_PLATE_Y0'] - 14.0,
                                          P['PUSH_PLATE_Y0']),
                          (cz - 24.0, cz + 24.0)), coll, AL, 'push',
                         part='PushPlateHub'))
    objs.append(geom.box('Push_Plate',
                         ((P['PUSH_PLATE_X0'], P['PUSH_PLATE_X1']),
                          (P['PUSH_PLATE_Y0'], P['PUSH_PLATE_Y1']),
                          (P['PUSH_PLATE_Z0'], P['PUSH_PLATE_Z1'])), coll,
                         CL, 'push', part='PushPlate150x80x5'))
    return objs


# ============================================================== 6. book ===
def build_book(P, M, coll):
    objs = []
    CV = M['book']
    PG = M['pages']
    bx0, bx1 = P['BOOK_X0'], P['BOOK_X1']
    by0, by1 = P['BOOK_Y0'], P['BOOK_Y1']
    bz0, bz1 = P['BOOK_Z0'], P['BOOK_Z1']
    t = P['BOOK_COVER_T']
    sp = 5.0

    objs.append(geom.box('Book_Cover_Bottom',
                         ((bx0, bx1), (by0, by1), (bz0, bz0 + t)), coll, CV,
                         'book', part='BookCover'))
    objs.append(geom.box('Book_Cover_Top',
                         ((bx0, bx1), (by0, by1), (bz1 - t, bz1)), coll, CV,
                         'book', part='BookCover'))
    objs.append(geom.box('Book_Spine',
                         ((bx0, bx1), (by0, by0 + sp), (bz0, bz1)), coll, CV,
                         'book', part='BookSpine'))
    objs.append(geom.box('Book_Pages',
                         ((bx0 + 3.0, bx1 - 3.0), (by0 + sp, by1 - 6.0),
                          (bz0 + t, bz1 - t)), coll, PG, 'book',
                         part='BookPages'))
    return objs


# ============================================================= 7. chute ===
def build_chute(P, M, coll):
    """30 degree take-off ramp extending forward from the open middle port."""
    objs = []
    ang = math.radians(P['CHUTE_ANGLE'])
    tt = P['CHUTE_T'] / 2.0
    z0 = P['PLAT_TOP_Z'] - P['MIN_CLEARANCE']
    y0 = P['CAB_Y1']
    y1 = y0 + P['CHUTE_LEN'] * math.cos(ang)
    z1 = z0 - P['CHUTE_LEN'] * math.sin(ang)
    cy = (y0 + y1) / 2.0
    cz = (z0 + z1) / 2.0
    n = Vector((0.0, math.sin(ang), math.cos(ang))) * tt
    base = Matrix.Translation(Vector((0.0, cy - n.y, cz - n.z))) @ \
        Matrix.Rotation(-ang, 4, 'X')
    objs.append(geom.box_local('Chute_Ramp',
                               (P['CHUTE_W'], P['CHUTE_LEN'], P['CHUTE_T']),
                               coll, M['chute'], 'chute',
                               part='TakeOffRamp', matrix=base))
    for sx, tag in ((-1, 'Left'), (1, 'Right')):
        wx = sx * (P['CHUTE_W'] / 2.0 - 10.0)
        m = base @ Matrix.Translation(Vector((wx, 0.0,
                                              P['CHUTE_T'] / 2.0 +
                                              P['CHUTE_WALL_H'] / 2.0)))
        objs.append(geom.box_local('Chute_Wall_%s' % tag,
                                   (20.0, P['CHUTE_LEN'],
                                    P['CHUTE_WALL_H']), coll, M['chute'],
                                   'chute', part='ChuteSideWall', matrix=m))
    return objs
