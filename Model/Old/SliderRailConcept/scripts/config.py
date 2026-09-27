"""SliderRailConcept - central parameter table.

Units   : millimetre.  One Blender unit = 1 mm  (unit_settings.scale_length = 0.001)
Axes    : X = left / right,  Y = depth,  Z = height (+Z up)
Origin  : every assembly is modelled around ITS OWN bottom centre (0, 0, 0)
          in a local frame; the world placement is carried by the parent Empty
          (Empty_Slider_Axis / Empty_Rail_Axis), so moving the Empty moves a
          whole component - that is what makes the parts re-usable.

Conflict resolutions (prompt wins; full table in docs/)
-----------------------------------------------------------------
C1  "screw length 900" vs "assembly bottom Z=0 / top Z=900" cannot both hold
    while motor + coupling sit coaxially underneath: 38 + 2 + 25 + 900 = 965.
    -> the 900 is kept as the certified AXIS ENVELOPE (identical for the rail)
       and the screw blank is trimmed to 843.5 mm so its crest still lands
       exactly on Z = 900.
C2  "stagger the two components in Y" vs "Y = 0 for both".  The stated
    PURPOSE (show both in one 16:9 frame) needs spread across the frame, so
    the Y stagger is honoured while X stays exactly -100 / +100 as specified.
    Set P['WORLD_RAIL_Y'] = 0.0 for the literal Y=0 layout.
"""

P = {
    # ------------------------------------------------------------------ units
    'UNIT_SCALE': 0.001,
    'MIN_CLEARANCE': 2.0,        # mm - between independent parts
    'MATE_CLEARANCE': 0.15,      # mm - radial gap of a declared coaxial mate

    # --------------------------------------------- component 1: lead screw
    'MOTOR_W': 42.0,             # 42 stepper, X
    'MOTOR_D': 42.0,             # 42 stepper, Y
    'MOTOR_H': 38.0,             # 42 stepper, Z
    'MOTOR_SHAFT_D': 8.0,
    'MOTOR_SHAFT_L': 15.0,       # protrusion above the motor face
    'MOTOR_SHAFT_SINK': 8.0,     # hidden stub inside the motor (seamless join)
    'MOTOR_BOSS_D': 22.0,        # front bearing boss, drawn as a recess ring
    'MOTOR_BOSS_DEPTH': 1.2,
    'MOTOR_SCREW_XY': 15.5,      # NEMA17 31 mm hole square
    'MOTOR_SCREW_D': 5.5,
    'MOTOR_SCREW_H': 2.0,
    'MOTOR_SCREW_SEAT': 0.1,

    'COUPLING_D': 15.0,
    'COUPLING_L': 25.0,
    'COUPLING_GAP': 2.0,         # air gap to the motor face
    'COUPLING_BORE_BOT_D': 8.4,  # clearance bore for the 8 mm shaft
    'COUPLING_BORE_TOP_D': 12.4, # clearance bore for the 12 mm screw
    'COUPLING_SPLIT_FROM_TOP': 9.0,
    'COUPLING_JAM_Z': 6.0,       # jam screw height above the coupling base
    'COUPLING_JAM_D': 6.0,
    'COUPLING_JAM_L': 3.0,

    'SCREW_D': 12.0,             # crest (nominal) diameter
    'SCREW_CORE_D': 10.4,        # root diameter
    'SCREW_PITCH': 6.0,
    'SCREW_Z_TOP': 900.0,        # axis envelope top
    'SCREW_Z_BOT': 56.5,         # blank bottom (inside the coupling bore)
    'SCREW_THREAD_Z_MARGIN': 1.5,
    'SCREW_SEG': 64,
    'SCREW_STEPS_PER_TURN': 24,   # multiple of 4 -> axis-symmetric thread

    'BLOCK_X': 80.0,
    'BLOCK_Y': 80.0,
    'BLOCK_T': 25.0,             # block thickness along the screw
    'BLOCK_CZ': 500.0,           # block centre height - the reference datum
    'BLOCK_BORE_D': 13.0,        # matching-thread bore (0.5 mm radial gap)
    'BLOCK_BOSS_D': 40.0,
    'BLOCK_BOSS_H': 7.0,
    'BLOCK_BOSS_SINK': 1.5,
    'BLOCK_BOLT_XY': 28.0,
    'BLOCK_BOLT_HEAD_D': 9.0,
    'BLOCK_BOLT_HEAD_H': 4.0,
    'BLOCK_BOLT_HEX_AF': 5.0,    # hex socket across flats
    'BLOCK_BOLT_SEAT': 0.1,

    'TOP_SUPPORT': True,         # optional detail: screw top bearing block
    'TOP_SUPPORT_X': 44.0,
    'TOP_SUPPORT_Y': 44.0,
    'TOP_SUPPORT_H': 24.0,
    'TOP_SUPPORT_TOP_Z': 898.0,  # block top (screw tip protrudes to 900)
    'TOP_SUPPORT_BORE_D': 13.0,
    'TOP_SUPPORT_BOLT_XY': 15.0,
    'TOP_SUPPORT_BOLT_D': 7.0,
    'TOP_SUPPORT_BOLT_H': 2.0,   # stays under the 900 mm envelope top

    # --------------------------------------------------- component 2: rail
    'RAIL_L': 900.0,
    'RAIL_W': 40.0,              # X
    'RAIL_T': 15.0,              # Y
    'RAIL_BOT_Z': 0.0,
    'RAIL_BEVEL': 1.2,
    'RAIL_GROOVE_R': 2.5,        # raceway groove radius
    'RAIL_GROOVE_DEPTH': 1.8,
    'RAIL_HOLE_D': 6.6,
    'RAIL_CBORE_D': 11.0,
    'RAIL_CBORE_DEPTH': 6.0,
    'RAIL_HOLE_N': 15,
    'RAIL_HOLE_END': 30.0,       # first hole centre offset from each end

    'CARR_X': 60.0,
    'CARR_Y': 40.0,
    'CARR_H': 30.0,              # along the rail
    'CARR_CZ': 500.0,
    'CARR_CLR': 0.3,             # running clearance to the rail, per side
    'CARR_BEVEL': 1.5,
    'CARR_BOLT_X': 18.0,
    'CARR_BOLT_ZOFF': 9.0,
    'CARR_HOLE_D': 6.6,
    'CARR_CBORE_D': 11.0,
    'CARR_CBORE_DEPTH': 4.0,

    # -------------------------------------------- world placement of the pair
    'CAM_AZ': -58.0,             # camera azimuth (camera in -X / +Y)
    'CAM_EL': 22.0,
    'WORLD_SLIDER_X': -100.0,
    'WORLD_SLIDER_Y': 0.0,
    'WORLD_RAIL_X': 100.0,
    'WORLD_RAIL_Y': -1500.0,       # <- C2: the Y stagger asked for by the prompt.
                                 # Kept modest: the two modules must stay at
                                 # comparable camera distance or one swamps the other.

    # ------------------------------------------------------------ materials
    'MAT_MOTOR': 'MAT-Motor_Black',
    'MAT_MOTOR_FACE': 'MAT-Motor_Face',
    'MAT_SHAFT': 'MAT-Motor_Shaft',
    'MAT_COUPLING': 'MAT-Coupling_Silver',
    'MAT_SCREW': 'MAT-Screw_Aluminium',
    'MAT_BLOCK': 'MAT-Slider_Silver',
    'MAT_RAIL': 'MAT-Rail_Silver',
    'MAT_CARRIAGE': 'MAT-Carriage_Silver',
    'MAT_BOLT': 'MAT-Bolt_Dark',
    'MAT_SUPPORT': 'MAT-Support_Silver',
}

# declared coaxial sliding / clamped mates: these pairs are ALLOWED to share
# bounding-box space (a screw must pass through a nut) but must never share
# solid volume - the gate proves that with a mesh-level triangle test.
MATES = {
    ('MOTOR', 'COUPLING'): 'motor shaft runs inside the coupling bore',
    ('COUPLING', 'SCREW'): 'screw shank clamped inside the coupling',
    ('SCREW', 'BLOCK'): 'screw passes through the matching-thread bore',
    ('SCREW', 'TOPSUPPORT'): 'screw journal inside the top bearing block',
    ('RAIL', 'CARRIAGE'): 'carriage sleeve runs on the rail',
}


def derive(P):
    """Fill in every derived value.  Called once before the build."""
    # ---- motor ------------------------------------------------------------
    P['MOTOR_Z0'] = 0.0
    P['MOTOR_Z1'] = P['MOTOR_H']
    P['SHAFT_Z1'] = P['MOTOR_Z1'] + P['MOTOR_SHAFT_L']
    P['SHAFT_Z0'] = P['MOTOR_Z1'] - P['MOTOR_SHAFT_SINK']

    # ---- coupling ---------------------------------------------------------
    P['COUPLING_Z0'] = P['MOTOR_Z1'] + P['COUPLING_GAP']
    P['COUPLING_Z1'] = P['COUPLING_Z0'] + P['COUPLING_L']
    P['COUPLING_SPLIT_Z'] = P['COUPLING_Z1'] - P['COUPLING_SPLIT_FROM_TOP']

    # ---- screw ------------------------------------------------------------
    P['SCREW_LEN'] = P['SCREW_Z_TOP'] - P['SCREW_Z_BOT']
    P['THREAD_Z0'] = P['SCREW_Z_BOT'] + P['SCREW_THREAD_Z_MARGIN']
    P['THREAD_Z1'] = P['SCREW_Z_TOP'] - P['SCREW_THREAD_Z_MARGIN']

    # ---- slider block -----------------------------------------------------
    P['BLOCK_Z0'] = P['BLOCK_CZ'] - P['BLOCK_T'] * 0.5
    P['BLOCK_Z1'] = P['BLOCK_CZ'] + P['BLOCK_T'] * 0.5
    P['BLOCK_BOSS_Z0'] = P['BLOCK_Z1'] - P['BLOCK_BOSS_SINK']
    P['BLOCK_BOSS_Z1'] = P['BLOCK_Z1'] + P['BLOCK_BOSS_H']
    P['BLOCK_BOLT_Z0'] = P['BLOCK_Z1'] - P['BLOCK_BOLT_SEAT']
    P['BLOCK_BOLT_Z1'] = P['BLOCK_BOLT_Z0'] + P['BLOCK_BOLT_HEAD_H']

    # ---- top bearing block ------------------------------------------------
    P['TOP_SUPPORT_Z1'] = P['TOP_SUPPORT_TOP_Z']
    P['TOP_SUPPORT_Z0'] = P['TOP_SUPPORT_Z1'] - P['TOP_SUPPORT_H']
    P['TOP_SUPPORT_BOLT_Z0'] = P['TOP_SUPPORT_Z1'] - 0.1
    P['TOP_SUPPORT_BOLT_Z1'] = P['TOP_SUPPORT_BOLT_Z0'] + P['TOP_SUPPORT_BOLT_H']

    # ---- rail -------------------------------------------------------------
    P['RAIL_Z0'] = P['RAIL_BOT_Z']
    P['RAIL_Z1'] = P['RAIL_Z0'] + P['RAIL_L']
    P['RAIL_X0'] = -P['RAIL_W'] * 0.5
    P['RAIL_X1'] = P['RAIL_W'] * 0.5
    P['RAIL_Y0'] = -P['RAIL_T'] * 0.5
    P['RAIL_Y1'] = P['RAIL_T'] * 0.5
    P['RAIL_GROOVE_AXIS_X'] = P['RAIL_X1'] + P['RAIL_GROOVE_R'] - P['RAIL_GROOVE_DEPTH']

    # ---- carriage ---------------------------------------------------------
    P['CARR_Z0'] = P['CARR_CZ'] - P['CARR_H'] * 0.5
    P['CARR_Z1'] = P['CARR_CZ'] + P['CARR_H'] * 0.5
    P['CARR_X0'] = -P['CARR_X'] * 0.5
    P['CARR_X1'] = P['CARR_X'] * 0.5
    P['CARR_Y0'] = -P['CARR_Y'] * 0.5
    P['CARR_Y1'] = P['CARR_Y'] * 0.5
    P['CARR_IN_X0'] = P['RAIL_X0'] - P['CARR_CLR']
    P['CARR_IN_X1'] = P['RAIL_X1'] + P['CARR_CLR']
    P['CARR_IN_Y0'] = P['RAIL_Y0'] - P['CARR_CLR']
    P['CARR_IN_Y1'] = P['RAIL_Y1'] + P['CARR_CLR']

    # ---- world placement --------------------------------------------------
    P['ORIGIN_SLIDER'] = (P['WORLD_SLIDER_X'], P['WORLD_SLIDER_Y'], 0.0)
    P['ORIGIN_RAIL'] = (P['WORLD_RAIL_X'], P['WORLD_RAIL_Y'], 0.0)
    return P
