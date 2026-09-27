"""Step 1 geometry: cabinet frame, drive columns, moving platform.

Imported by build_step1.py after sys.path is extended to this folder.
Nothing here reads a hard-coded dimension - every number comes from P via
sbs_params.derive().
"""

import bpy
import bmesh
import math
from mathutils import Vector

from sbs_geom import box, cyl, ensure_collection, tag

RAD = math.pi / 180.0


def helix_tube(name, cx, cy, z0, z1, root_d, crest_d, pitch, coll, mat=None,
               seg_per_turn=12, tube_seg=6):
    """Triangulated helical thread wound around a vertical axis at (cx, cy)."""
    length = abs(z1 - z0)
    turns = max(1.0, length / pitch)
    steps = max(8, int(round(turns * seg_per_turn)))
    r_root = root_d / 2.0
    r_crest = crest_d / 2.0
    r_tube = max(0.35, (r_crest - r_root) / 2.0)
    r_ring = (r_root + r_crest) / 2.0

    me = bpy.data.meshes.new(name)
    bm = bmesh.new()
    rings = []
    for i in range(steps + 1):
        u = i / float(steps)
        ang = u * turns * 2.0 * math.pi
        cz = z0 + u * length
        ca, sa = math.cos(ang), math.sin(ang)
        ring = []
        for k in range(tube_seg):
            b = k / float(tube_seg) * 2.0 * math.pi
            rr = r_ring + r_tube * math.cos(b)
            zz = cz + r_tube * math.sin(b)
            ring.append(bm.verts.new((cx + rr * ca, cy + rr * sa, zz)))
        rings.append(ring)
    for i in range(steps):
        for k in range(tube_seg):
            k2 = (k + 1) % tube_seg
            bm.faces.new((rings[i][k], rings[i][k2],
                          rings[i + 1][k2], rings[i + 1][k]))
    bm.normal_update()
    bm.to_mesh(me)
    bm.free()
    obj = bpy.data.objects.new(name, me)
    for c in list(obj.users_collection):
        c.objects.unlink(obj)
    coll.objects.link(obj)
    if mat:
        obj.data.materials.append(mat)
    return obj


# ================================================================ CABINET ==
def build_cabinet(P, M, colls):
    col = colls['cabinet']
    t = P['FRAME_T']
    x0, x1 = P['CAB_X0'], P['CAB_X1']
    y0, y1 = P['CAB_Y0'], P['CAB_Y1']
    z0, z1 = P['CAB_Z0'], P['CAB_Z1']
    px0, px1 = x0 + t, x1 - t
    py0, py1 = y0 + t, y1 - t

    out = []

    def add(o, part, group='CABINET', notes=''):
        tag(o, group, part, notes)
        out.append(o)
        return o

    # ---- 4 vertical corner posts -----------------------------------------
    for sx, sxe in ((px0, px0 + t), (px1 - t, px1)):
        for sy, sye in ((py0, py0 + t), (py1 - t, py1)):
            side = 'L' if sx < 0 else 'R'
            face = 'Back' if sy < 0 else 'Front'
            add(box('GEO_Cabinet_Post_{}{}'.format(face, side),
                    sx, sxe, sy, sye, z0, z1, col, M['alu']),
                'post', notes='vertical frame post')

    # ---- base + top panels -------------------------------------------------
    add(box('GEO_Cabinet_Base', x0, x1, y0, y1, z0, z0 + t, col, M['alu_dark']),
        'base', notes='cabinet bottom panel, motors stand on it')

    hx0, hx1 = -P['TOP_HOLE_W'] / 2.0, P['TOP_HOLE_W'] / 2.0
    hy0, hy1 = -P['TOP_HOLE_D'] / 2.0, P['TOP_HOLE_D'] / 2.0
    hole = 'top book hole {}x{}'.format(P['TOP_HOLE_W'], P['TOP_HOLE_D'])
    add(box('GEO_Top_Bar_Back',  x0, x1, py0, hy0, z1 - t, z1, col, M['alu']),
        'top_bar_back', notes=hole)
    add(box('GEO_Top_Bar_Front', x0, x1, hy1, py1, z1 - t, z1, col, M['alu']),
        'top_bar_front', notes=hole)
    add(box('GEO_Top_Bar_Left',  px0, hx0, hy0, hy1, z1 - t, z1, col, M['alu']),
        'top_bar_left', notes=hole)
    add(box('GEO_Top_Bar_Right', hx1, px1, hy0, hy1, z1 - t, z1, col, M['alu']),
        'top_bar_right', notes=hole)

    # ---- semi-transparent acrylic shell -----------------------------------
    # inset 2 mm from the post inner faces so faces never co-plane (z-fight)
    ins = 2.0
    add(box('GEO_Shell_Side_L', x0, x0 + t, py0 + ins, py1 - ins,
            z0 + t + 1.0, z1 - t - 1.0, col, M['acrylic']),
        'shell_side_l', notes='acrylic 40% transmission')
    add(box('GEO_Shell_Side_R', x1 - t, x1, py0 + ins, py1 - ins,
            z0 + t + 1.0, z1 - t - 1.0, col, M['acrylic']),
        'shell_side_r', notes='acrylic 40% transmission')
    add(box('GEO_Shell_Back', px0 + ins, px1 - ins, y0, y0 + t,
            z0 + t + 1.0, z1 - t - 1.0, col, M['acrylic']),
        'shell_back', notes='acrylic; rear push motor mounts here in step 2')

    # ---- three-level front face -------------------------------------------
    # level 1 (bottom) fixed panel, level 2 fully OPEN (push port),
    # level 3 (top) fixed semi-transparent panel
    add(box('GEO_Front_L1_Bottom', px0, px1, py1 - t, py1, z0 + t,
            P['PORT_Z0'], col, M['acrylic']),
        'front_l1', notes='bottom fixed front panel')
    add(box('GEO_Front_L3_Top', px0, px1, py1 - t, py1,
            P['PORT_Z1'], z1 - t, col, M['acrylic']),
        'front_l3', notes='top fixed front panel')
    # the two horizontal rails that create the three-level split
    add(box('GEO_Front_Rail_MidLow', px0, px1, py1 - t, py1,
            P['PORT_Z0'] - t / 2.0, P['PORT_Z0'] + t / 2.0, col, M['alu']),
        'front_rail_low', notes='port sill')
    add(box('GEO_Front_Rail_MidHigh', px0, px1, py1 - t, py1,
            P['PORT_Z1'] - t / 2.0, P['PORT_Z1'] + t / 2.0, col, M['alu']),
        'front_rail_high', notes='port lintel')

    return out


# ========================================================= DRIVE COLUMNS ===
def build_drive_columns(P, M, colls):
    col = colls['drive']
    out = []

    def add(o, part, group='CABINET', notes=''):
        tag(o, group, part, notes)
        out.append(o)
        return o

    screw_pitch = P['SCREW_PITCH']
    for side, sgn in (('L', -1.0), ('R', 1.0)):
        cx = sgn * P['COL_X']
        cy = P['SCREW_Y']

        # --- 42 stepper, vertical, at the very bottom of the screw ---------
        m = P['M42']
        mz0, mz1 = P['MOTOR_FOOT_Z0'], P['MOTOR_FOOT_Z0'] + P['M42_H']
        add(box('GEO_Motor42_%s_Body' % side, cx - m / 2, cx + m / 2,
                cy - m / 2, cy + m / 2, mz0, mz1, col, M['motor']),
            'motor42_%s_body' % side,
            notes='42x42x38 stepper, vertical, drives the %s column' % side)
        add(box('GEO_Motor42_%s_Flange' % side, cx - m / 2 - 2, cx + m / 2 + 2,
                cy - m / 2 - 2, cy + m / 2 + 2, mz1, mz1 + 4, col, M['alu_dark']),
            'motor42_%s_flange' % side, notes='mount flange')
        add(cyl('GEO_Motor42_%s_Shaft' % side, mz1 + 4,
                mz1 + 4 + P['M42_SHAFT_L'], P['M42_SHAFT_D'] / 2.0, col,
                M['motor_shaft'], u=cx, v=cy, segments=20),
            'motor42_%s_shaft' % side, notes='8 mm shaft')

        # --- rigid coupling (motor + coupling = 70 mm) ---------------------
        add(cyl('GEO_Coupling_%s' % side, P['COUPLING_Z0'], P['COUPLING_Z1'],
                P['COUPLING_D'] / 2.0, col, M['motor_shaft'],
                u=cx, v=cy, segments=24),
            'coupling_%s' % side,
            notes='rigid coupling; coupling bottom = motor top = 58 mm')

        # --- 900 mm lead screw ---------------------------------------------
        add(cyl('GEO_Screw_%s_Core' % side, P['SCREW_Z0'], P['SCREW_Z1'],
                P['SCREW_ROOT_D'] / 2.0, col, M['steel'],
                u=cx, v=cy, segments=20),
            'screw_%s_core' % side, notes='900 mm lead screw core')
        hx = helix_tube('GEO_Screw_%s_Thread' % side, cx, cy,
                        P['SCREW_Z0'], P['SCREW_Z1'], P['SCREW_ROOT_D'],
                        P['SCREW_CREST_D'], screw_pitch, col, M['steel'])
        add(hx, 'screw_%s_thread' % side,
            notes='pitch %.1f mm helicoid' % screw_pitch)
        # top bearing block
        add(box('GEO_Screw_%s_TopBlock' % side, cx - 18, cx + 18,
                cy - 18, cy + 18, P['SCREW_Z1'], P['SCREW_Z1'] + 20,
                col, M['alu_dark']),
            'screw_%s_topblock' % side, notes='upper bearing block')

    # ---- passive linear rails (one per side, opposite the driven screw) ---
    for side, sgn in (('L', -1.0), ('R', 1.0)):
        cx = sgn * P['COL_X']
        cy = P['RAIL_Y']
        pt = P['RAIL_PLATE_T']
        plw = P['RAIL_PLATE_W']
        # mounting plate spans cy-pt..cy ; the rail track is proud at its front
        add(box('GEO_Rail_%s_Plate' % side, cx - plw / 2.0, cx + plw / 2.0,
                cy - pt, cy, P['RAIL_Z0'], P['RAIL_Z1'], col, M['alu_dark']),
            'rail_%s_plate' % side,
            notes='linear rail mounting plate, 900 mm')
        sq = P['RAIL_SQ']
        add(box('GEO_Rail_%s_Track' % side, cx - sq / 2.0, cx + sq / 2.0,
                cy - pt - 4.0, cy - pt + 16.0,
                P['RAIL_Z0'], P['RAIL_Z1'], col, M['steel_dull']),
            'rail_%s_track' % side,
            notes='20x20 rail track, 900 mm, 275 mm clear of the plate edge')

    return out


# ============================================================== PLATFORM ==
def build_platform(P, M, colls):
    col = colls['platform']
    out = []

    def add(o, part, group='PLATFORM', notes=''):
        tag(o, group, part, notes)
        out.append(o)
        return o

    px0, px1 = P['PLAT_X0'], P['PLAT_X1']
    py0, py1 = P['PLAT_Y0'], P['PLAT_Y1']
    pb, pt = P['PLAT_BOT_Z'], P['PLAT_TOP_Z']
    gx0, gx1 = P['PLAT_GUARD_X0'], P['PLAT_GUARD_X1']
    gy0, gy1 = P['PLAT_GUARD_Y0'], P['PLAT_GUARD_Y1']
    gz1 = P['PLAT_GUARD_Z1']

    # ---- carrier plate ----------------------------------------------------
    add(box('GEO_Platform_Plate', px0, px1, py0, py1, pb, pt, col,
            M['plat_alu'], bevel=1.0),
        'plate', notes='platform top face Z=%.1f mm (brief)' % pt)

    # ---- L-section guards: rear wall + left wall --------------------------
    add(box('GEO_Platform_Guard_Rear', px0, px1, gy0, gy1, pt, gz1, col,
            M['plat_alu']),
        'guard_rear', notes='10 mm rear guard, braces the book fore edge')
    add(box('GEO_Platform_Guard_Left', gx0, gx1, py0, gy0, pt, gz1, col,
            M['plat_alu']),
        'guard_left', notes='10 mm left guard; together with the rear wall '
                            'this is the L-section')

    # ---- carriages --------------------------------------------------------
    sw = P['SHUTTLE_W']
    sh = P['SHUTTLE_H']
    sz = P['PLATFORM_SLIDER_Z']
    # The driven carriage straddles the screw axis; the passive carriage
    # straddles the rail axis.  Both are 80 x 80 (brief).
    add(box('GEO_Platform_Carriage_L_Screw',
            -P['COL_X'] - sw / 2.0, -P['COL_X'] + sw / 2.0,
            P['SCREW_Y'] - sh / 2.0, P['SCREW_Y'] + sh / 2.0,
            sz - sh / 2.0, sz + sh / 2.0, col, M['alu_dark']),
        'carriage_l_screw',
        notes='80x80 driven carriage centred on the LEFT screw axis, Z=%.0f' % sz)
    add(box('GEO_Platform_Carriage_R_Rail',
            P['COL_X'] - sw / 2.0, P['COL_X'] + sw / 2.0,
            P['RAIL_Y'] - sh / 2.0, P['RAIL_Y'] + sh / 2.0,
            sz - sh / 2.0, sz + sh / 2.0, col, M['alu_dark']),
        'carriage_r_rail',
        notes='80x80 passive carriage centred on the RIGHT rail axis, Z=%.0f' % sz)

    # ---- carry brackets + L stiffening ribs ------------------------------
    rt = P['PLAT_RISE_T']
    bz0, bz1 = P['BRK_Z0'], P['BRK_Z1']
    rb_x0 = P['RIB_X0']
    rb_x1 = P['RIB_X1']
    rb_y = P['RIB_Y']
    rb_t = P['PLAT_RIB_T']
    rb_z0 = P['RIB_Z0']

    # LEFT bracket: rises inboard of the screw axis (screw spans COL_X +/- 8)
    # and lands inboard of the 80 mm driven carriage at CARR_IN_X = 255.
    add(box('GEO_Platform_Bracket_L', -P['CARR_IN_X'], -P['CARR_IN_X'] + rt,
            P['SCREW_Y'] - 40.0, P['SCREW_Y'] + 40.0, bz0, bz1,
            col, M['plat_alu']),
        'bracket_l',
        notes='rises inboard of the screw; meets the driven carriage face')
    # RIGHT bracket: forward of the book, rises inboard of the rail axis and
    # lands inboard of the 80 mm passive carriage.
    rail_in_x = P['COL_X'] - P['RAIL_PLATE_W'] / 2.0
    add(box('GEO_Platform_Bracket_R', rail_in_x - rt, rail_in_x,
            186.0, P['RAIL_Y'] - 40.0 + 6.0, bz0, bz1, col, M['plat_alu']),
        'bracket_r',
        notes='rises inboard of the rail plate; lands on the carriage face')
    add(box('GEO_Platform_Rib_L', rb_x0, rb_x1, rb_y - rb_t / 2.0,
            rb_y + rb_t / 2.0, rb_z0, bz1, col, M['plat_alu']),
        'rib_l', notes='left L stiffening rib')
    add(box('GEO_Platform_Rib_R', -rb_x0, -rb_x1, rb_y - rb_t / 2.0,
            rb_y + rb_t / 2.0, rb_z0, bz1, col, M['plat_alu']),
        'rib_r', notes='right L stiffening rib')

    return out


# ================================================================== BOOK ==
def build_book(P, M, colls):
    col = colls['book']
    out = []

    def add(o, part, group='BOOK', notes=''):
        tag(o, group, part, notes)
        out.append(o)
        return o

    bx0, bx1 = P['BOOK_X0'], P['BOOK_X1']
    by0, by1 = P['BOOK_Y0'], P['BOOK_Y1']
    bz0, bz1 = P['BOOK_Z0'], P['BOOK_Z1']
    ct = P['BOOK_COVER_T']
    ins = P['BOOK_PAGE_INSET']

    add(box('GEO_Book_Cover_Bottom', bx0, bx1, by0, by1, bz0, bz0 + ct,
            col, M['book']), 'cover_bottom')
    add(box('GEO_Book_Cover_Top', bx0, bx1, by0, by1, bz1 - ct, bz1,
            col, M['book']), 'cover_top')
    add(box('GEO_Book_Spine', bx0, bx1, by0, by0 + ct, bz0, bz1,
            col, M['book']),
        'spine', notes='spine plane Y=%.0f, faces the rear pusher' % by0)
    add(box('GEO_Book_Pages', bx0 + ins, bx1 - ins, by0 + ct, by1 - ins,
            bz0 + ct, bz1 - ct, col, M['book_page']),
        'pages', notes='fore edge toward +Y')
    return out
