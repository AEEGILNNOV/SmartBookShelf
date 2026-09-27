"""materials - PBR recipes for the two assemblies.

Blender 5.x Principled BSDF socket names are used, but every assignment is
guarded so the module keeps working if a socket is renamed.
"""

import bpy


def _s(node, key, value):
    try:
        node.inputs[key].default_value = value
    except Exception:
        pass


def _new(name):
    m = bpy.data.materials.get(name)
    if m is None:
        m = bpy.data.materials.new(name)
    m.use_nodes = True
    nt = m.node_tree
    nt.nodes.clear()
    out = nt.nodes.new('ShaderNodeOutputMaterial')
    out.location = (400, 0)
    bsdf = nt.nodes.new('ShaderNodeBsdfPrincipled')
    bsdf.location = (0, 0)
    nt.links.new(bsdf.outputs['BSDF'], out.inputs['Surface'])
    return m, nt, bsdf


def _metal(name, base, metallic=1.0, rough=0.2, ior=1.45):
    m, nt, b = _new(name)
    _s(b, 'Base Color', (base[0], base[1], base[2], 1.0))
    _s(b, 'Metallic', metallic)
    _s(b, 'Roughness', rough)
    _s(b, 'IOR', ior)
    _s(b, 'Coat Weight', 0.0)
    return m, nt, b


def _brushed_roughness(nt, b, lo, hi, scale=26.0, detail=4.0, stretch=22.0):
    """A weak anisotropic 'brushed metal' roughness variation."""
    tc = nt.nodes.new('ShaderNodeTexCoord')
    tc.location = (-900, -260)
    mp = nt.nodes.new('ShaderNodeMapping')
    mp.location = (-720, -260)
    _s(mp, 'Scale', (1.0, 1.0, stretch))
    nt.links.new(tc.outputs['Object'], mp.inputs['Vector'])
    nz = nt.nodes.new('ShaderNodeTexNoise')
    nz.location = (-540, -260)
    _s(nz, 'Scale', scale)
    _s(nz, 'Detail', detail)
    _s(nz, 'Roughness', 0.55)
    nt.links.new(mp.outputs['Vector'], nz.inputs['Vector'])
    mr = nt.nodes.new('ShaderNodeMapRange')
    mr.location = (-330, -260)
    _s(mr, 'From Min', 0.0)
    _s(mr, 'From Max', 1.0)
    _s(mr, 'To Min', lo)
    _s(mr, 'To Max', hi)
    nt.links.new(nz.outputs['Fac'], mr.inputs['Value'])
    nt.links.new(mr.outputs['Result'], b.inputs['Roughness'])


def _backdrop_gradient(nt, b, near=(0.030, 0.032, 0.038),
                       far=(0.0055, 0.0060, 0.0075)):
    """Soft vertical gradient: near-black overhead, a gentle lift near the floor.

    NOTE: a MapRange node outputs a FLOAT.  Feeding that straight into a Colour
    socket makes Blender treat it as flat GREY, which is exactly how this
    backdrop once rendered at 0.7 grey instead of 0.01.  Always go through a
    ColorRamp so real colours are emitted.
    """
    tc = nt.nodes.new('ShaderNodeTexCoord')
    tc.location = (-900, 400)
    sep = nt.nodes.new('ShaderNodeSeparateXYZ')
    sep.location = (-700, 400)
    nt.links.new(tc.outputs['Generated'], sep.inputs['Vector'])
    mr = nt.nodes.new('ShaderNodeMapRange')
    mr.location = (-500, 400)
    _s(mr, 'From Min', 0.14)
    _s(mr, 'From Max', 0.40)
    _s(mr, 'To Min', 0.0)
    _s(mr, 'To Max', 1.0)
    nt.links.new(sep.outputs['Z'], mr.inputs['Value'])
    ramp = nt.nodes.new('ShaderNodeValToRGB')
    ramp.location = (-280, 400)
    ramp.color_ramp.elements[0].position = 0.0
    ramp.color_ramp.elements[0].color = (near[0], near[1], near[2], 1.0)
    ramp.color_ramp.elements[1].position = 1.0
    ramp.color_ramp.elements[1].color = (far[0], far[1], far[2], 1.0)
    nt.links.new(mr.outputs['Result'], ramp.inputs['Fac'])
    nt.links.new(ramp.outputs['Color'], b.inputs['Base Color'])


def build_materials():
    """Returns {name: material}.  Names are stable and re-usable."""
    M = {}

    # ---- 42 stepper -------------------------------------------------------
    m, nt, b = _metal('MAT-Motor_Black', (0.028, 0.029, 0.033), 0.35, 0.44)
    _s(b, 'Specular IOR Level', 0.5)
    M[m.name] = m

    m, nt, b = _metal('MAT-Motor_Face', (0.075, 0.078, 0.085), 0.80, 0.34)
    M[m.name] = m

    m, nt, b = _metal('MAT-Motor_Shaft', (0.780, 0.796, 0.820), 1.0, 0.13)
    M[m.name] = m

    # ---- coupling ---------------------------------------------------------
    m, nt, b = _metal('MAT-Coupling_Silver', (0.860, 0.878, 0.900), 1.0, 0.17)
    M[m.name] = m

    # ---- lead screw: aluminium silver, weak brushed feel -------------------
    m, nt, b = _metal('MAT-Screw_Aluminium', (0.855, 0.872, 0.893), 1.0, 0.24)
    _brushed_roughness(nt, b, 0.20, 0.29, scale=30.0, stretch=34.0)
    M[m.name] = m

    # ---- slider / rail / carriage: bright silver --------------------------
    m, nt, b = _metal('MAT-Slider_Silver', (0.905, 0.920, 0.938), 1.0, 0.13)
    _brushed_roughness(nt, b, 0.11, 0.16, scale=18.0, stretch=10.0)
    M[m.name] = m

    m, nt, b = _metal('MAT-Rail_Silver', (0.880, 0.896, 0.915), 1.0, 0.17)
    _brushed_roughness(nt, b, 0.15, 0.20, scale=14.0, stretch=60.0)
    M[m.name] = m

    m, nt, b = _metal('MAT-Carriage_Silver', (0.912, 0.926, 0.944), 1.0, 0.12)
    _brushed_roughness(nt, b, 0.10, 0.15, scale=20.0, stretch=10.0)
    M[m.name] = m

    m, nt, b = _metal('MAT-Support_Silver', (0.835, 0.852, 0.874), 1.0, 0.22)
    M[m.name] = m

    # ---- fasteners --------------------------------------------------------
    m, nt, b = _metal('MAT-Bolt_Dark', (0.045, 0.047, 0.052), 0.85, 0.34)
    M[m.name] = m

    # ---- render stage -----------------------------------------------------
    m, nt, b = _metal('MAT-Stage_Floor', (0.022, 0.024, 0.029), 0.0, 0.16)
    M[m.name] = m

    m, nt, b = _metal('MAT-Stage_Backdrop', (0.0130, 0.0140, 0.0165), 0.0, 0.90)
    _backdrop_gradient(nt, b)
    M[m.name] = m

    return M
