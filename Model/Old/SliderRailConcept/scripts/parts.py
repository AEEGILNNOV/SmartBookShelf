"""parts - geometry of Slider_Assembly and Linear_Rail_Assembly.

Each named component is exactly ONE mesh object with its own material slots,
so a bolt head never needs a fake "0 mm" air gap from the plate it seats on,
and the anti-clash gate sees seven unambiguous bodies.
"""

import math

import geomlib as G

TAU = 2.0 * math.pi


def _recolor(part, mat_name, pred):
    """Re-assign a material slot to every face whose centre satisfies pred."""
    mi = part.slot(mat_name)
    for f in part.bm.faces:
        if pred(f.calc_center_median()):
            f.material_index = mi


# ==========================================================================
#  COMPONENT 1 - Slider_Assembly  (lead screw slide)
# ==========================================================================
def build_slider(P, sub, cut_coll, M):
    A = {}
    MW, MD, MH = P['MOTOR_W'], P['MOTOR_D'], P['MOTOR_H']
    hx, hy = MW * 0.5, MD * 0.5

    # ---------------------------------------------------------- 42 stepper
    p = G.Part('GEO_Slider_Motor42')
    p.box(-hx, hx, -hy, hy, P['MOTOR_Z0'], P['MOTOR_Z1'],
          P['MAT_MOTOR'], bevel=1.8, bseg=2)
    # two-tone: the machined front plate reads lighter than the black body
    _recolor(p, P['MAT_MOTOR_FACE'], lambda c: c.z >= P['MOTOR_Z1'] - 4.5)
    # output shaft, sunk 8 mm into the body so the join is seamless
    p.cyl(P['MOTOR_SHAFT_D'] * 0.5, P['SHAFT_Z0'], P['SHAFT_Z1'],
          P['MAT_SHAFT'], seg=36, bevel=0.4)
    # NEMA17 face screws
    for sx in (-P['MOTOR_SCREW_XY'], P['MOTOR_SCREW_XY']):
        for sy in (-P['MOTOR_SCREW_XY'], P['MOTOR_SCREW_XY']):
            p.cyl(P['MOTOR_SCREW_D'] * 0.5,
                  P['MOTOR_Z1'] - 0.1, P['MOTOR_Z1'] - 0.1 + P['MOTOR_SCREW_H'],
                  P['MAT_BOLT'], seg=20, bevel=0.35, cx=sx, cy=sy)
    motor = p.to_object(sub['motor'], M, 'MOTOR', 'Motor42')
    A['motor'] = motor
    # pilot-boss pocket (annular so the shaft survives)
    rb, rf = P['MOTOR_BOSS_D'] * 0.5, P['MOTOR_SHAFT_D'] * 0.5 + 0.6
    z0 = P['MOTOR_Z1'] - P['MOTOR_BOSS_DEPTH']
    z1 = P['MOTOR_Z1'] + 2.0
    G.boolean(motor, G.make_cutter(
        'CUT_MotorBoss',
        lambda c: c.revolve([(rb, z0), (rb, z1), (rf, z1), (rf, z0)],
                            P['MAT_MOTOR'], seg=48),
        cut_coll))

    # ------------------------------------------------------------- coupling
    p = G.Part('GEO_Slider_Coupling')
    ro = P['COUPLING_D'] * 0.5
    z0, z1 = P['COUPLING_Z0'], P['COUPLING_Z1']
    zs = P['COUPLING_SPLIT_Z']
    rbot = P['COUPLING_BORE_BOT_D'] * 0.5
    rtop = P['COUPLING_BORE_TOP_D'] * 0.5
    p.revolve([(ro, z0), (ro, z1), (rtop, z1), (rtop, zs),
               (rbot, zs), (rbot, z0)],
              P['MAT_COUPLING'], seg=48, bevel=0.5, bseg=2)
    # two radial jam screws
    jz = z0 + P['COUPLING_JAM_Z']
    jr = P['COUPLING_JAM_D'] * 0.5
    x_in = ro - 0.4
    x_out = x_in + P['COUPLING_JAM_L']
    xc = (x_in + x_out) * 0.5
    for sgn in (-1.0, 1.0):
        p.cyl(jr, -P['COUPLING_JAM_L'] * 0.5, P['COUPLING_JAM_L'] * 0.5,
              P['MAT_BOLT'], seg=18, bevel=0.3,
              loc=(sgn * xc, 0.0, jz), rot=[('Y', 90.0)])
    A['coupling'] = p.to_object(sub['coupling'], M, 'COUPLING', 'Coupling')

    # ------------------------------------------------------------ lead screw
    p = G.Part('GEO_Slider_LeadScrew')
    p.cyl(P['SCREW_CORE_D'] * 0.5, P['SCREW_Z_BOT'], P['SCREW_Z_TOP'],
          P['MAT_SCREW'], seg=P['SCREW_SEG'])
    p.helix(P['THREAD_Z0'], P['THREAD_Z1'], P['SCREW_PITCH'],
            P['SCREW_CORE_D'] * 0.5 - 0.15, P['SCREW_D'] * 0.5,
            P['MAT_SCREW'], seg=P['SCREW_SEG'],
            steps_per_turn=P['SCREW_STEPS_PER_TURN'])
    A['screw'] = p.to_object(sub['screw'], M, 'SCREW', 'LeadScrew')

    # ----------------------------------------------------------- slider block
    p = G.Part('GEO_Slider_Block')
    bx, by = P['BLOCK_X'] * 0.5, P['BLOCK_Y'] * 0.5
    p.box(-bx, bx, -by, by, P['BLOCK_Z0'], P['BLOCK_Z1'],
          P['MAT_BLOCK'], bevel=2.2, bseg=2)
    # nut boss on top, sunk 1.5 mm so there is no coplanar boolean face
    p.cyl(P['BLOCK_BOSS_D'] * 0.5, P['BLOCK_BOSS_Z0'], P['BLOCK_BOSS_Z1'],
          P['MAT_BLOCK'], seg=48, bevel=0.9)
    # four socket head cap screws on the top face
    bxy = P['BLOCK_BOLT_XY']
    for sx in (-bxy, bxy):
        for sy in (-bxy, bxy):
            p.cyl(P['BLOCK_BOLT_HEAD_D'] * 0.5,
                  P['BLOCK_BOLT_Z0'], P['BLOCK_BOLT_Z1'],
                  P['MAT_BOLT'], seg=24, bevel=0.5, cx=sx, cy=sy)
    block = p.to_object(sub['block'], M, 'BLOCK', 'SliderBlock')
    A['block'] = block
    # central matching-thread bore, straight through block + boss
    G.boolean(block, G.make_cutter(
        'CUT_BlockBore',
        lambda c: c.cyl(P['BLOCK_BORE_D'] * 0.5,
                        P['BLOCK_Z0'] - 12.0, P['BLOCK_BOSS_Z1'] + 12.0,
                        P['MAT_BLOCK'], seg=48),
        cut_coll))
    # hex sockets in the four screw heads
    hexr = (P['BLOCK_BOLT_HEX_AF'] * 0.5) / math.cos(math.radians(30.0))
    def _hexes(c):
        for sx in (-bxy, bxy):
            for sy in (-bxy, bxy):
                c.cyl(hexr, P['BLOCK_BOLT_Z1'] - 2.6,
                      P['BLOCK_BOLT_Z1'] + 1.0, P['MAT_BOLT'],
                      seg=6, cx=sx, cy=sy)
    G.boolean(block, G.make_cutter('CUT_BlockHex', _hexes, cut_coll))

    # ------------------------------------------------- screw top bearing block
    if P['TOP_SUPPORT']:
        p = G.Part('GEO_Slider_TopBearing')
        tx, ty = P['TOP_SUPPORT_X'] * 0.5, P['TOP_SUPPORT_Y'] * 0.5
        p.box(-tx, tx, -ty, ty, P['TOP_SUPPORT_Z0'], P['TOP_SUPPORT_Z1'],
              P['MAT_SUPPORT'], bevel=2.0, bseg=2)
        for sx in (-P['TOP_SUPPORT_BOLT_XY'], P['TOP_SUPPORT_BOLT_XY']):
            for sy in (-P['TOP_SUPPORT_BOLT_XY'], P['TOP_SUPPORT_BOLT_XY']):
                p.cyl(P['TOP_SUPPORT_BOLT_D'] * 0.5,
                      P['TOP_SUPPORT_BOLT_Z0'], P['TOP_SUPPORT_BOLT_Z1'],
                      P['MAT_BOLT'], seg=20, bevel=0.4, cx=sx, cy=sy)
        sup = p.to_object(sub['top_support'], M, 'TOPSUPPORT', 'TopBearingBlock')
        G.boolean(sup, G.make_cutter(
            'CUT_TopBore',
            lambda c: c.cyl(P['TOP_SUPPORT_BORE_D'] * 0.5,
                            P['TOP_SUPPORT_Z0'] - 12.0,
                            P['TOP_SUPPORT_Z1'] + 12.0, P['MAT_SUPPORT'], seg=48),
            cut_coll))
        A['top_support'] = sup

    return A


# ==========================================================================
#  COMPONENT 2 - Linear_Rail_Assembly
# ==========================================================================
def build_rail(P, sub, cut_coll, M):
    A = {}
    x0, x1 = P['RAIL_X0'], P['RAIL_X1']
    y0, y1 = P['RAIL_Y0'], P['RAIL_Y1']
    za, zb = P['RAIL_Z0'], P['RAIL_Z1']

    # ---------------------------------------------------------------- rail
    p = G.Part('GEO_Rail_Body')
    p.box(x0, x1, y0, y1, za, zb, P['MAT_RAIL'],
          bevel=P['RAIL_BEVEL'], bseg=2)
    rail = p.to_object(sub['rail'], M, 'RAIL', 'RailBody')
    A['rail'] = rail

    # raceway grooves down both narrow faces
    gx = P['RAIL_GROOVE_AXIS_X']
    gr = P['RAIL_GROOVE_R']
    def _grooves(c):
        for sgn in (-1.0, 1.0):
            c.cyl(gr, za - 10.0, zb + 10.0, P['MAT_RAIL'],
                  seg=32, cx=sgn * gx)
    G.boolean(rail, G.make_cutter('CUT_RailGroove', _grooves, cut_coll))

    # counter-bored mounting holes along the rail
    n = P['RAIL_HOLE_N']
    z_first, z_last = P['RAIL_HOLE_END'], P['RAIL_L'] - P['RAIL_HOLE_END']
    zs = [z_first + (z_last - z_first) * i / (n - 1) for i in range(n)]
    hr = P['RAIL_HOLE_D'] * 0.5
    cr = P['RAIL_CBORE_D'] * 0.5
    cd = P['RAIL_CBORE_DEPTH']
    def _holes(c):
        for z in zs:
            c.cyl(hr, -18.0, 18.0, P['MAT_RAIL'], seg=24,
                  loc=(0.0, 0.0, z), rot=[('X', 90.0)])
    G.boolean(rail, G.make_cutter('CUT_RailHoles', _holes, cut_coll))
    def _cbores(c):
        for z in zs:
            # cut from y = 1.5 outward, i.e. t in [-1.5 - depth ... ]
            c.cyl(cr, -(y1 - cd + 3.0), -(y1 - cd), P['MAT_RAIL'], seg=24,
                  loc=(0.0, 0.0, z), rot=[('X', 90.0)])
    G.boolean(rail, G.make_cutter('CUT_RailCbores', _cbores, cut_coll))

    # ------------------------------------------------------------ carriage
    p = G.Part('GEO_Rail_Carriage')
    cx0, cx1 = P['CARR_X0'], P['CARR_X1']
    cy0, cy1 = P['CARR_Y0'], P['CARR_Y1']
    cz0, cz1 = P['CARR_Z0'], P['CARR_Z1']
    p.box(cx0, cx1, cy0, cy1, cz0, cz1, P['MAT_CARRIAGE'],
          bevel=P['CARR_BEVEL'], bseg=2)
    carr = p.to_object(sub['carriage'], M, 'CARRIAGE', 'RailCarriage')
    A['carriage'] = carr

    # rectangular sleeve cavity, sharp corners (unbeveled cutter)
    ix0, ix1 = P['CARR_IN_X0'], P['CARR_IN_X1']
    iy0, iy1 = P['CARR_IN_Y0'], P['CARR_IN_Y1']
    G.boolean(carr, G.make_cutter(
        'CUT_CarrSleeve',
        lambda c: c.box(ix0, ix1, iy0, iy1, cz0 - 5.0, cz1 + 5.0,
                        P['MAT_CARRIAGE']),
        cut_coll))

    # four counter-bored mounting holes on the load face (+Y)
    bx = P['CARR_BOLT_X']
    bz = P['CARR_BOLT_ZOFF']
    def _choles(c):
        for sx in (-bx, bx):
            for sz in (-bz, bz):
                c.cyl(P['CARR_HOLE_D'] * 0.5, -22.0, -11.0, P['MAT_CARRIAGE'],
                      seg=24, loc=(sx, 0.0, P['CARR_CZ'] + sz),
                      rot=[('X', 90.0)])
    G.boolean(carr, G.make_cutter('CUT_CarrHoles', _choles, cut_coll))
    def _ccbores(c):
        for sx in (-bx, bx):
            for sz in (-bz, bz):
                c.cyl(P['CARR_CBORE_D'] * 0.5, -23.0,
                      -(cy1 - P['CARR_CBORE_DEPTH']), P['MAT_CARRIAGE'],
                      seg=24, loc=(sx, 0.0, P['CARR_CZ'] + sz),
                      rot=[('X', 90.0)])
    G.boolean(carr, G.make_cutter('CUT_CarrCbores', _ccbores, cut_coll))

    return A
