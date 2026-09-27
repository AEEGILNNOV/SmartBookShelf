"""Material library - Principled BSDF, Blender 4.x / 5.x tolerant.

Input socket names moved between 4.0 and 5.x, so every assignment goes through
_set() which tries a list of candidate names and skips silently if none match.
"""

import bpy

MAT_CACHE = {}


def _set(node, names, value):
    if isinstance(names, str):
        names = [names]
    for n in names:
        if n in node.inputs:
            try:
                node.inputs[n].default_value = value
                return True
            except Exception:
                pass
    return False


def make(name, base, metallic=0.0, rough=0.5, transmission=0.0,
         ior=1.45, alpha=1.0, emission=None, emission_strength=0.0,
         coat=0.0, sheen=0.0):
    if name in MAT_CACHE:
        return MAT_CACHE[name]
    m = bpy.data.materials.new(name)
    m.use_nodes = True
    bsdf = m.node_tree.nodes.get('Principled BSDF')
    if bsdf is None:
        for n in m.node_tree.nodes:
            if n.type == 'BSDF_PRINCIPLED':
                bsdf = n
                break
    b = tuple(base) + (1.0,) if len(base) == 3 else tuple(base)

    _set(bsdf, 'Base Color', b)
    _set(bsdf, 'Metallic', metallic)
    _set(bsdf, 'Roughness', rough)
    _set(bsdf, ['IOR', 'Index of Refraction'], ior)
    _set(bsdf, ['Coat Weight', 'Clearcoat'], coat)
    _set(bsdf, ['Sheen Weight', 'Sheen'], sheen)

    if transmission > 0.0:
        _set(bsdf, ['Transmission Weight', 'Transmission'], transmission)
        # keep the surface readable in EEVEE/Cycles
        _set(bsdf, ['Specular IOR Level', 'Specular'], 0.5)
        # not every engine exposes these; set when present
        for attr, val in (('use_raytrace_refraction', True),
                          ('use_screen_refraction', True),
                          ('use_raytrace_transmission', True),
                          ('blend_method', 'BLEND')):
            if hasattr(m, attr):
                try:
                    setattr(m, attr, val)
                except Exception:
                    pass
    if alpha < 1.0:
        _set(bsdf, 'Alpha', alpha)
        if hasattr(m, 'blend_method'):
            try:
                m.blend_method = 'BLEND'
            except Exception:
                pass
        for attr, val in (('use_backface_culling', False),
                          ('show_transparent_back', True)):
            if hasattr(m, attr):
                setattr(m, attr, val)

    if emission is not None:
        _set(bsdf, ['Emission Color', 'Emission'], tuple(emission) + (1.0,))
        _set(bsdf, ['Emission Strength'], emission_strength)

    MAT_CACHE[name] = m
    return m


def build_library():
    """Create every material used by step 1.  Returns a name -> material map."""
    L = {}
    # cabinet outer shell - semi-transparent acrylic, ~40 % transmission
    # brief: 透光率 40%
    L['acrylic']   = make('MAT_Acrylic_Clear', (0.90, 0.95, 0.98),
                          metallic=0.0, rough=0.02, transmission=0.40,
                          ior=1.49, coat=0.08)
    # frame / posts / rails - brushed aluminium
    L['alu']       = make('MAT_Alu_Silver', (0.76, 0.78, 0.80),
                          metallic=0.92, rough=0.30)
    # dark anodised aluminium for brackets
    L['alu_dark']  = make('MAT_Alu_Dark', (0.22, 0.23, 0.25),
                          metallic=0.85, rough=0.42)
    # lead screw - polished steel
    L['steel']     = make('MAT_Steel_Polished', (0.70, 0.71, 0.73),
                          metallic=1.0, rough=0.16)
    # linear rail track - hardened steel, duller
    L['steel_dull']= make('MAT_Steel_Rail', (0.55, 0.56, 0.58),
                          metallic=1.0, rough=0.34)
    # 42 stepper body - black
    L['motor']     = make('MAT_Motor_Black', (0.045, 0.045, 0.05),
                          metallic=0.1, rough=0.45)
    # stepper shaft / coupling hub - silver
    L['motor_shaft'] = make('MAT_Motor_Shaft', (0.80, 0.81, 0.83),
                            metallic=1.0, rough=0.20)
    # L-section platform - light aluminium sheet
    L['plat_alu']  = make('MAT_Platform_Alu', (0.80, 0.82, 0.84),
                          metallic=0.90, rough=0.26)
    # book - dark red cloth cover
    L['book']      = make('MAT_Book_Red', (0.30, 0.045, 0.05),
                          rough=0.75)
    L['book_page'] = make('MAT_Book_Pages', (0.92, 0.89, 0.82),
                          rough=0.85)
    # black rubber pad (step 2)
    L['rubber']    = make('MAT_Rubber_Black', (0.03, 0.03, 0.03),
                          rough=0.90)
    # studio stage
    L['floor']     = make('MAT_Stage_Floor', (0.045, 0.05, 0.055),
                          rough=0.55)
    L['backdrop']  = make('MAT_Stage_Backdrop', (0.075, 0.085, 0.10),
                          rough=0.80)
    return L
