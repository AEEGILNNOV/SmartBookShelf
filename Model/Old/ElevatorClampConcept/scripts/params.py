"""Parameter table - elevator-clamp smart bookshelf (concept model).

Units : 1 Blender unit = 1 mm, scene unit scale = 0.001 (real world -> metres)
Axes  : X = width (+X right) | Y = depth (+Y front, -Y back) | Z = height
Origin: bottom centre of the cabinet.

Every dimension the design brief names is a key in P so it can be retuned in
one place.  derive() adds the values that follow arithmetically.
"""

P = {}

# ---------------------------------------------------------------- global --
P['MIN_CLEARANCE'] = 2.0          # hard anti-clash margin, mm
P['UNIT_SCALE'] = 0.001

# --------------------------------------------------------------- cabinet --
P['CAB_W'] = 600.0                # outer width
P['CAB_D'] = 450.0                # outer depth
P['CAB_H'] = 1200.0               # outer height
P['FRAME_T'] = 20.0               # frame / panel thickness

# front face is split in three levels, the middle one is fully open
P['PORT_Z0'] = 540.0
P['PORT_Z1'] = 660.0
P['PORT_CZ'] = 600.0              # push port centre height (fixed)

# top book insertion hole (books drop through edge-first)
P['TOP_HOLE_W'] = 250.0
P['TOP_HOLE_D'] = 80.0
P['TOP_HOLE_CX'] = 0.0
P['TOP_HOLE_CY'] = 20.0

# ------------------------------------------------------- lift columns ----
P['COL_X'] = 255.0                # screw / passive rail column offset
P['SCREW_Y'] = -175.0             # driven column (lead screw) depth
P['RAIL_Y'] = -105.0              # passive column (linear rail) depth
P['SCREW_LEN'] = 900.0
P['SCREW_D'] = 12.0
P['RAIL_SQ'] = 20.0               # rail profile is square 20 x 20
P['RAIL_LEN'] = 900.0
P['CARR_X'] = 243.0               # inboard face of every 80 mm carriage

# 42 stepper mounted vertically at the very bottom of each screw
P['MOTOR_L'] = 42.4
P['MOTOR_W'] = 42.4
P['MOTOR_H'] = 38.0
P['COUPLING_H'] = 32.0            # -> motor + coupling = 70 mm
P['MOTOR_FOOT_Z0'] = 20.0         # sits on the base panel

# carriage blocks (the 80 x 80 sliders)
P['SLIDER_H'] = 80.0
P['SLIDER_T'] = 24.0
P['SLIDER_D'] = 56.0

# the two driven slides are offset vertically so their carriages clear
P['PLATFORM_SLIDER_Z'] = 480.0
P['LIMIT_SLIDER_Z'] = 640.0
P['SLIDER_MIN_OFFSET'] = 150.0

# ------------------------------------------------------------- platform --
P['PLAT_X0'], P['PLAT_X1'] = -212.0, 212.0
P['PLAT_Y0'], P['PLAT_Y1'] = -62.0, 165.0
P['PLAT_T'] = 10.0                # carrier plate thickness
P['PLAT_BRACKET_RISE'] = 100.0    # carriage centre -> platform top face
P['PLAT_FLANGE_H'] = 10.0         # 10 mm metal retaining lip
P['PLAT_FLANGE_T'] = 10.0
P['PLAT_SLOT_X'] = 88.0           # centre slot half width (pusher passage)
P['PLAT_SLOT_Y0'] = -62.0
P['PLAT_SLOT_Y1'] = -46.0

# ---------------------------------------------------------- limit plate --
P['LIM_PAD_T'] = 3.0              # black rubber pad under the limit plate
P['LIM_BODY_T'] = 12.0
P['LIM_X0'], P['LIM_X1'] = -132.0, 132.0
P['LIM_Y0'], P['LIM_Y1'] = -70.0, 180.0
P['LIM_PAD_BOTTOM_Z'] = 640.0     # brief value; auto-raised for clearance

# ------------------------------------------------------------ push unit --
P['PUSH_PLATE_W'] = 150.0         # across the spine (X)
P['PUSH_PLATE_H'] = 80.0          # across the port opening (Z)
P['PUSH_PLATE_T'] = 5.0           # along the push axis (Y)
P['PUSH_CZ'] = 600.0              # push axis height (fixed)
P['PUSH_STROKE'] = 80.0           # exposed rod at the frozen push pose
P['PUSH_ROD_D'] = 12.0
P['PUSH_CPL_D'] = 22.0
P['PUSH_CPL_LEN'] = 26.0
P['PUSH_MOUNT_T'] = 6.0           # motor flange against the inner back wall

# ---------------------------------------------------------------- book ---
P['BOOK_W'] = 150.0               # across the spine  (X)
P['BOOK_D'] = 210.0               # spine -> fore edge (Y)
P['BOOK_T'] = 40.0                # thickness          (Z)
P['BOOK_CX'] = 0.0
P['BOOK_SPINE_Y'] = -50.0         # back face of the book = spine plane
P['BOOK_PAGE_INSET'] = 3.0
P['BOOK_COVER_T'] = 3.0

# --------------------------------------------------------------- chute ---
P['CHUTE_W'] = 280.0
P['CHUTE_LEN'] = 200.0
P['CHUTE_ANGLE'] = 30.0           # degrees, sloping down and forward
P['CHUTE_T'] = 8.0
P['CHUTE_WALL_H'] = 25.0


def derive(p=None):
    """Fill in every value that follows arithmetically from the keys above."""
    if p is None:
        p = P

    # cabinet halves
    p['CAB_X0'], p['CAB_X1'] = -p['CAB_W'] / 2.0, p['CAB_W'] / 2.0
    p['CAB_Y0'], p['CAB_Y1'] = -p['CAB_D'] / 2.0, p['CAB_D'] / 2.0
    p['CAB_Z0'], p['CAB_Z1'] = 0.0, p['CAB_H']
    t = p['FRAME_T']
    p['IN_X0'], p['IN_X1'] = p['CAB_X0'] + t, p['CAB_X1'] - t
    p['IN_Y0'], p['IN_Y1'] = p['CAB_Y0'] + t, p['CAB_Y1'] - t

    # lead screws: 900 mm long, starting just above the coupling
    p['COUPLING_BOT_Z0'] = p['MOTOR_FOOT_Z0'] + p['MOTOR_H']
    p['COUPLING_BOT_Z1'] = p['COUPLING_BOT_Z0'] + p['COUPLING_H']
    p['SCREW_Z0'] = p['COUPLING_BOT_Z1']
    p['SCREW_Z1'] = p['SCREW_Z0'] + p['SCREW_LEN']
    p['RAIL_Z0'] = p['FRAME_T']
    p['RAIL_Z1'] = p['RAIL_Z0'] + p['RAIL_LEN']
    p['MOTOR_TOP_Z'] = p['COUPLING_BOT_Z1']

    # platform geometry - frozen pose sits at the fixed port height
    p['PLAT_TOP_Z'] = 580.0
    p['PLAT_BOT_Z'] = p['PLAT_TOP_Z'] - p['PLAT_T']
    p['PLAT_BRACKET_Z0'] = p['PLATFORM_SLIDER_Z'] - p['SLIDER_H'] / 2.0
    p['PLAT_FLANGE_Z0'] = p['PLAT_TOP_Z']
    p['PLAT_FLANGE_Z1'] = p['PLAT_TOP_Z'] + p['PLAT_FLANGE_H']

    # carriage centres: each bracket butts the carriage and stops 2 mm short
    # of the opposite column so no bracket ever crosses a rail
    p['PLAT_CARR_L'] = (-p['COL_X'], p['SCREW_Y'], p['PLATFORM_SLIDER_Z'])
    p['PLAT_CARR_R'] = (p['COL_X'], p['RAIL_Y'], p['PLATFORM_SLIDER_Z'])
    p['LIM_CARR_L'] = (-p['COL_X'], p['RAIL_Y'], p['LIMIT_SLIDER_Z'])
    p['LIM_CARR_R'] = (p['COL_X'], p['SCREW_Y'], p['LIMIT_SLIDER_Z'])

    # push plate must live between the platform top and the limit plate pad
    p['PUSH_PLATE_Z0'] = p['PUSH_CZ'] - p['PUSH_PLATE_H'] / 2.0
    p['PUSH_PLATE_Z1'] = p['PUSH_CZ'] + p['PUSH_PLATE_H'] / 2.0
    p['PUSH_PLATE_Y1'] = p['BOOK_SPINE_Y']                 # face on the spine
    p['PUSH_PLATE_Y0'] = p['PUSH_PLATE_Y1'] - p['PUSH_PLATE_T']
    p['PUSH_PLATE_X0'] = p['BOOK_CX'] - p['PUSH_PLATE_W'] / 2.0
    p['PUSH_PLATE_X1'] = p['BOOK_CX'] + p['PUSH_PLATE_W'] / 2.0

    # the brief fixes the pad at 640 and the plate top at 640 -> resolve the
    # 0 mm conflict by raising the pad the minimum 2 mm.
    need = p['PUSH_PLATE_Z1'] + p['MIN_CLEARANCE']
    p['LIM_PAD_BOTTOM_Z'] = max(p['LIM_PAD_BOTTOM_Z'], need)
    p['LIM_PAD_TOP_Z'] = p['LIM_PAD_BOTTOM_Z'] + p['LIM_PAD_T']
    p['LIM_BODY_Z0'] = p['LIM_PAD_TOP_Z']
    p['LIM_BODY_Z1'] = p['LIM_BODY_Z0'] + p['LIM_BODY_T']

    # push unit: motor sits on the INSIDE of the back wall so the mechanism
    # (and the 80 mm of exposed rod) stays visible through the acrylic
    p['PUSH_MOUNT_Y0'] = p['IN_Y0']
    p['PUSH_MOUNT_Y1'] = p['IN_Y0'] + p['PUSH_MOUNT_T']
    p['PUSH_MOTOR_Y0'] = p['PUSH_MOUNT_Y1']
    p['PUSH_MOTOR_Y1'] = p['PUSH_MOTOR_Y0'] + p['MOTOR_H']
    p['COUPLING_Y0'] = p['PUSH_MOTOR_Y1']
    p['COUPLING_Y1'] = p['COUPLING_Y0'] + p['PUSH_CPL_LEN']
    p['ROD_Y0'] = p['COUPLING_Y1']
    p['ROD_Y1'] = p['ROD_Y0'] + p['PUSH_STROKE']

    # book
    p['BOOK_X0'] = p['BOOK_CX'] - p['BOOK_W'] / 2.0
    p['BOOK_X1'] = p['BOOK_CX'] + p['BOOK_W'] / 2.0
    p['BOOK_Y0'] = p['BOOK_SPINE_Y']
    p['BOOK_Y1'] = p['BOOK_Y0'] + p['BOOK_D']
    p['BOOK_Z0'] = p['PLAT_TOP_Z']
    p['BOOK_Z1'] = p['BOOK_Z0'] + p['BOOK_T']

    # safety: lowest the platform assembly may travel
    p['PLAT_MIN_Z'] = 150.0
    return p
