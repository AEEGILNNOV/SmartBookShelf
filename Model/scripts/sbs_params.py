"""Parameter table - SmartBookShelf mechanical concept model.

Units : 1 Blender unit = 1 mm, scene unit scale = 0.001  (real world -> metres)
Axes  : X = width  (+X right) | Y = depth (+Y FRONT) | Z = height (+Z up)
Origin: bottom centre of the cabinet.

STEP 1 scope: cabinet frame + 3-level front face + top book hole +
              twin drive columns (screw / coupling / 42 stepper / passive rail) +
              PIVOT_Platform_Z (moving platform + 80x80 carriages + L-section guards).
NOT built yet: limit plate (step 2), rear push unit (user deferred), chute.

Every dimension named by the design brief is a key in P.  derive() adds the
values that follow arithmetically.  Nothing downstream hard-codes a number.
"""

P = {}

# ---------------------------------------------------------------- global --
P['UNIT_SCALE']       = 0.001   # scene unit scale (1 BU = 1 mm)
P['MIN_CLEARANCE']    = 2.0     # hard anti-clash margin, mm

# --------------------------------------------------------------- cabinet --
P['CAB_W']            = 600.0   # outer width            (brief)
P['CAB_D']            = 450.0   # outer depth            (brief)
P['CAB_H']            = 1200.0  # outer height           (brief)
P['FRAME_T']          = 20.0    # frame / panel thickness(brief)

# front face is split in three levels; the MIDDLE one is fully open = push port
P['PORT_CZ']          = 600.0   # fixed push-port centre height   (brief)
P['PORT_H']           = 160.0   # clear opening height of the port
P['FRONT_RAIL_T']     = 20.0    # horizontal front rail thickness

# top book-insertion hole.  Brief says 250 x 80, but a flat-laid book is
# 150 mm wide, so an 80 mm slot cannot pass it.  USER RULING: widen to 170.
P['TOP_HOLE_W']       = 250.0   # along X        (brief)
P['TOP_HOLE_D']       = 170.0   # along Y        (brief 80 -> user widened)
P['TOP_HOLE_CX']      = 0.0
P['TOP_HOLE_CY']      = 0.0

# ----------------------------------------------------------- lift columns --
# The driven carriage must sit CENTRED ON THE SCREW AXIS; the passive carriage
# sits centred on the RAIL AXIS.  Screw and rail therefore share one |X| but
# live at different depths so a bracket can reach from one to the plate.
P['COL_X']            = 215.0   # |X| of both vertical column axes
P['SCREW_Y']          = -175.0  # driven (screw) axis depth
P['RAIL_Y']           =  215.0  # passive (rail) axis depth, forward of plate
P['SCREW_LEN']        = 900.0   # brief
P['SCREW_D']          = 12.0    # lead-screw minor diameter
P['RAIL_LEN']         = 900.0   # brief
P['RAIL_SQ']          = 20.0    # rail track profile 20 x 20
P['RAIL_PLATE_W']     = 60.0    # rail mounting plate width  (X)
P['RAIL_PLATE_T']     = 30.0    # rail mounting plate depth  (Y)

# passive (non-driven) carriage - the "从动滑块" that keeps a column square
P['PCARR_H']          = 62.0
P['PCARR_W']          = 80.0
P['PCARR_D']          = 34.0
P['PCARR_T']          = 14.0

# 42 stepper, mounted vertically at the very bottom of each screw
P['M42']              = 42.0    # brief: 42 x 42 x 38
P['M42_H']            = 38.0
P['M42_SHAFT_D']      = 8.0
P['M42_SHAFT_L']      = 10.0
P['COUPLING_D']       = 22.0
P['COUPLING_H']       = 32.0    # motor + coupling = 70 mm (brief)
P['MOTOR_FOOT_Z0']    = 20.0    # sits on the base panel

# ============================================================ PLATFORM ====
# BRIEF / USER RULING: platform top face = 580 mm, left carriage = 480 mm
# (160 mm stagger, satisfies ">= 150"), right/limit carriage = 640 mm.
P['PLAT_TOP_Z']       = 580.0           # brief, frozen pose
P['PLAT_T']           = 10.0            # carrier plate thickness
P['PLATFORM_SLIDER_Z']= 480.0           # user ruling
P['LIMIT_SLIDER_Z']   = 640.0           # user ruling (step 2 uses it)
P['SLIDER_MIN_OFFSET']= 150.0           # brief
P['SHUTTLE_H']        = 80.0            # brief: 80 x 80 slider
P['SHUTTLE_W']        = 80.0

P['PLAT_X0']          = -212.0
P['PLAT_X1']          =  212.0
P['PLAT_Y0']          =  -70.0          # rear edge
P['PLAT_Y1']          =  150.0          # front edge, clear of the rail column
P['PLAT_GUARD_H']     = 10.0            # brief: 10 mm metal guard
P['PLAT_GUARD_T']     = 10.0            # guard wall thickness
P['PLAT_RISE_T']      = 10.0            # X thickness of each carry bracket
P['PLAT_RIB_T']       =  8.0            # Y thickness of the L stiffening rib
P['RIB_X0'], P['RIB_X1'] = -222.0, -212.0   # left rib, inboard of the screw
P['RIB_Y']            = -140.0          # rib centre in Y
P['RIB_Z0']           = 440.0           # rib bottom
P['SCREW_PITCH']      =   4.0           # lead-screw thread pitch (T8 style)
P['SCREW_ROOT_D']     =  13.0           # thread root diameter
P['SCREW_CREST_D']    =  16.0           # thread crest diameter

# ================================================================= BOOK ====
P['BOOK_X']           = 210.0   # across the spine   -> X
P['BOOK_Y']           = 185.0   # spine -> fore edge -> Y
P['BOOK_T']           =  40.0   # thickness          -> Z
# spine plane faces the pusher.  Set so the fore edge stops 3 mm short of the
# rear guard wall, letting the guard brace the book without touching it.
P['BOOK_SPINE_Y']     = -50.0
P['BOOK_COVER_T']     = 3.0
P['BOOK_PAGE_INSET']  = 3.0

# ================================================ REAR PUSH UNIT (step 2) ==
# Reserved geometry driver.  The push unit is NOT modelled in step 1
# (user: "推书电机先不做") but the space is reserved so nothing else may take it.
P['PUSH_CZ']          = 600.0   # brief: fixed push height
P['PUSH_PLATE_W']     = 150.0   # brief
P['PUSH_PLATE_H']     =  80.0   # brief
P['PUSH_PLATE_T']     =   5.0   # brief
P['PUSH_STROKE']      =  80.0   # brief: exposed rod at the frozen pose
P['PUSH_ROD_D']       =  12.0

# ============================================== LIMIT PLATE (step 2) =======
# USER RULING: brief fixed the pad at 640 and the push-plate top edge at 640
# -> 0 mm gap.  Raise the pad the minimum 2 mm to 642 to honour MIN_CLEARANCE.
P['LIM_PAD_T']        = 3.0     # brief: 3 mm black rubber pad
P['LIM_BODY_T']       = 12.0
P['LIM_PAD_BOTTOM_Z'] = 642.0   # brief 640 -> user ruling 642
P['LIM_X0']           = -132.0
P['LIM_X1']           =  132.0
P['LIM_Y0']           =  -80.0
P['LIM_Y1']           =  175.0
P['LIM_CARR_X0']      = -212.0  # plate extension carrying the driven slider
P['LIM_CARR_X1']      = -132.0

# ============================================= TAKE-OFF CHUTE (step 3) ====
P['CHUTE_W']          = 280.0
P['CHUTE_LEN']        = 200.0
P['CHUTE_ANGLE']      = 30.0    # deg, sloping down and forward
P['CHUTE_T']          = 8.0
P['CHUTE_WALL_H']     = 25.0


def derive(p=None):
    """Fill in every value that follows arithmetically from the keys above."""
    if p is None:
        p = P

    # ---- cabinet faces -----------------------------------------------------
    p['CAB_X0'], p['CAB_X1'] = -p['CAB_W'] / 2.0, p['CAB_W'] / 2.0
    p['CAB_Y0'], p['CAB_Y1'] = -p['CAB_D'] / 2.0, p['CAB_D'] / 2.0
    p['CAB_Z0'], p['CAB_Z1'] = 0.0, p['CAB_H']
    t = p['FRAME_T']
    p['IN_X0'], p['IN_X1'] = p['CAB_X0'] + t, p['CAB_X1'] - t
    p['IN_Y0'], p['IN_Y1'] = p['CAB_Y0'] + t, p['CAB_Y1'] - t
    p['RAIL_X_IN'] = p['IN_X1'] - p['RAIL_PLATE_T']   # inner face of rail plate

    # ---- three-level front face -------------------------------------------
    p['PORT_Z0'] = p['PORT_CZ'] - p['PORT_H'] / 2.0   # 520
    p['PORT_Z1'] = p['PORT_CZ'] + p['PORT_H'] / 2.0   # 680

    # ---- drive columns ----------------------------------------------------
    # screw starts right above the coupling so motor+coupling = 70 mm exactly
    p['COUPLING_Z0'] = p['MOTOR_FOOT_Z0'] + p['M42_H']
    p['COUPLING_Z1'] = p['COUPLING_Z0'] + p['COUPLING_H']
    p['SCREW_Z0']    = p['COUPLING_Z1']
    p['SCREW_Z1']    = p['SCREW_Z0'] + p['SCREW_LEN']
    p['MOTOR_TOP_Z'] = p['COUPLING_Z1']
    p['RAIL_Z0']     = p['FRAME_T']
    p['RAIL_Z1']     = p['RAIL_Z0'] + p['RAIL_LEN']
    # LEFT column is driven by a screw, RIGHT column is the passive rail.
    p['SCREW_L'] = (-p['COL_X'], p['SCREW_Y'])
    p['SCREW_R'] = ( p['COL_X'], p['SCREW_Y'])
    p['RAIL_L']  = (-p['COL_X'], p['RAIL_Y'])
    p['RAIL_R']  = ( p['COL_X'], p['RAIL_Y'])
    # the 80 mm driven carriage is centred on the screw axis, so it spans
    # SCREW_Y +/- 40 and its INBOARD face sits at COL_X + 40 = 255
    p['CARR_IN_X'] = p['COL_X'] + 40.0
    p['CARR_OUT_X'] = p['COL_X'] - 40.0

    # ---- platform ---------------------------------------------------------
    p['PLAT_BOT_Z']    = p['PLAT_TOP_Z'] - p['PLAT_T']
    p['PLAT_GUARD_Z1'] = p['PLAT_TOP_Z'] + p['PLAT_GUARD_H']
    p['SHUTTLE_Z0']    = p['PLATFORM_SLIDER_Z'] - p['SHUTTLE_H'] / 2.0
    p['SHUTTLE_Z1']    = p['PLATFORM_SLIDER_Z'] + p['SHUTTLE_H'] / 2.0
    p['BRK_Z1']        = p['PLAT_BOT_Z'] + 4.0        # bracket tucks into plate
    p['BRK_Z0']        = p['SHUTTLE_Z0'] - 4.0        # and into the shuttle

    # rear guard wall (Y) and left guard wall (X) form the L section
    p['PLAT_GUARD_Y0'] = p['PLAT_Y1'] - p['PLAT_GUARD_T']
    p['PLAT_GUARD_Y1'] = p['PLAT_Y1']
    p['PLAT_GUARD_X0'] = p['PLAT_X0']
    p['PLAT_GUARD_X1'] = p['PLAT_X0'] + p['PLAT_GUARD_T']

    # ---- book -------------------------------------------------------------
    p['BOOK_Z0'] = p['PLAT_TOP_Z']
    p['BOOK_Z1'] = p['PLAT_TOP_Z'] + p['BOOK_T']
    p['BOOK_Y0'] = p['BOOK_SPINE_Y']
    p['BOOK_Y1'] = p['BOOK_SPINE_Y'] + p['BOOK_Y']
    p['BOOK_X0'] = -p['BOOK_X'] / 2.0
    p['BOOK_X1'] =  p['BOOK_X'] / 2.0

    # ---- push unit (reserved space only) ----------------------------------
    p['PUSH_PLATE_Z0'] = p['PUSH_CZ'] - p['PUSH_PLATE_H'] / 2.0   # 560
    p['PUSH_PLATE_Z1'] = p['PUSH_CZ'] + p['PUSH_PLATE_H'] / 2.0   # 640
    p['PUSH_PLATE_Y1'] = p['BOOK_SPINE_Y']                        # touches spine
    p['PUSH_PLATE_Y0'] = p['PUSH_PLATE_Y1'] - p['PUSH_PLATE_T']

    # ---- limit plate ------------------------------------------------------
    need = p['PUSH_PLATE_Z1'] + p['MIN_CLEARANCE']
    if p['LIM_PAD_BOTTOM_Z'] < need:
        print('[params] WARNING limit pad floor {} < required {} '
              '-> raised to honour MIN_CLEARANCE'
              .format(p['LIM_PAD_BOTTOM_Z'], need))
        p['LIM_PAD_BOTTOM_Z'] = need
    p['LIM_PAD_TOP_Z'] = p['LIM_PAD_BOTTOM_Z'] + p['LIM_PAD_T']
    p['LIM_BODY_Z0']   = p['LIM_PAD_TOP_Z']
    p['LIM_BODY_Z1']   = p['LIM_BODY_Z0'] + p['LIM_BODY_T']

    return p


# Values that must survive into the .blend for QA / documentation.
CHECK_KEYS = [
    'CAB_W', 'CAB_D', 'CAB_H', 'FRAME_T', 'PORT_CZ',
    'PLAT_TOP_Z', 'PLATFORM_SLIDER_Z', 'LIMIT_SLIDER_Z',
    'BOOK_Z0', 'BOOK_Z1', 'BOOK_SPINE_Y',
    'PUSH_PLATE_Z0', 'PUSH_PLATE_Z1', 'PUSH_PLATE_Y1',
    'LIM_PAD_BOTTOM_Z', 'SCREW_Z0', 'SCREW_Z1', 'COUPLING_Z0', 'COUPLING_Z1',
    'PLAT_Y1', 'PLAT_GUARD_Y0', 'PLAT_GUARD_Z1',
]


if __name__ == '__main__':
    derive()
    w = max(len(k) for k in P)
    for k in sorted(P):
        print('{:>{w}} = {}'.format(k, P[k], w=w))
