"""Material library - Principled BSDF recipes for the concept model."""

import bpy

MAT_PREFIX = 'MAT-'


def _set(bsdf, name, value):
    if name in bsdf.inputs:
        bsdf.inputs[name].default_value = value
        return True
    return False


def make(name, base=(0.8, 0.8, 0.8, 1.0), metallic=0.0, roughness=0.5,
         transmission=0.0, ior=1.45, alpha=1.0, emission=None,
         emission_strength=0.0, coat=0.0, sheen=0.0):
    mat = bpy.data.materials.get(MAT_PREFIX + name)
    if mat is None:
        mat = bpy.data.materials.new(MAT_PREFIX + name)
    mat.use_nodes = True
    nt = mat.node_tree
    bsdf = None
    for node in nt.nodes:
        if node.type == 'BSDF_PRINCIPLED':
            bsdf = node
            break
    if bsdf is None:
        bsdf = nt.nodes.new('ShaderNodeBsdfPrincipled')
    _set(bsdf, 'Base Color', base)
    _set(bsdf, 'Metallic', metallic)
    _set(bsdf, 'Roughness', roughness)
    _set(bsdf, 'IOR', ior)
    _set(bsdf, 'Alpha', alpha)
    _set(bsdf, 'Transmission Weight', transmission)
    _set(bsdf, 'Coat Weight', coat)
    _set(bsdf, 'Sheen Weight', sheen)
    if emission is not None:
        _set(bsdf, 'Emission Color', emission)
        _set(bsdf, 'Emission Strength', emission_strength)
    # viewport / eevee hints
    mat.diffuse_color = (base[0], base[1], base[2], 1.0)
    mat.metallic = metallic
    mat.roughness = roughness
    if transmission > 0.0 or alpha < 1.0:
        try:
            mat.blend_method = 'BLEND'
        except Exception:
            pass
        try:
            mat.use_backface_culling = False
        except Exception:
            pass
    return mat


def make_glass(name, base=(0.86, 0.92, 0.98, 1.0), face_alpha=0.06,
               grazing_alpha=0.72, roughness=0.05, transmission=0.40,
               ior=1.49, blend=0.30):
    """Acrylic panel: nearly invisible face-on, bright at grazing angles.

    'transmission' records the specified light transmission (40 %).  The
    see-through look is driven by a Fresnel alpha so the interior mechanism
    stays legible instead of being hidden behind frosted refraction.
    """
    mat = bpy.data.materials.get(MAT_PREFIX + name)
    if mat is None:
        mat = bpy.data.materials.new(MAT_PREFIX + name)
    mat.use_nodes = True
    nt = mat.node_tree
    nt.nodes.clear()
    out = nt.nodes.new('ShaderNodeOutputMaterial')
    out.location = (420, 0)
    bsdf = nt.nodes.new('ShaderNodeBsdfPrincipled')
    bsdf.location = (120, 0)
    lw = nt.nodes.new('ShaderNodeLayerWeight')
    lw.location = (-460, -80)
    lw.inputs['Blend'].default_value = blend
    mr = nt.nodes.new('ShaderNodeMapRange')
    mr.location = (-240, -80)
    mr.inputs['From Min'].default_value = 0.0
    mr.inputs['From Max'].default_value = 1.0
    mr.inputs['To Min'].default_value = face_alpha
    mr.inputs['To Max'].default_value = grazing_alpha
    nt.links.new(lw.outputs['Facing'], mr.inputs['Value'])
    nt.links.new(mr.outputs['Result'], bsdf.inputs['Alpha'])
    nt.links.new(bsdf.outputs['BSDF'], out.inputs['Surface'])
    _set(bsdf, 'Base Color', base)
    _set(bsdf, 'Metallic', 0.0)
    _set(bsdf, 'Roughness', roughness)
    _set(bsdf, 'IOR', ior)
    _set(bsdf, 'Transmission Weight', transmission)
    _set(bsdf, 'Specular IOR Level', 0.35)
    mat.diffuse_color = (base[0], base[1], base[2], 0.25)
    mat.roughness = roughness
    _try_attr(mat, 'surface_render_method', 'BLENDED')
    _try_attr(mat, 'blend_method', 'BLEND')
    _try_attr(mat, 'show_transparent_back', True)
    _try_attr(mat, 'use_transparent_shadow', True)
    _try_attr(mat, 'use_backface_culling', False)
    mat.use_screen_refraction = False
    return mat


def make_backdrop(name='Stage_Backdrop'):
    """Self-lit studio backdrop: smooth vertical gradient, no shadow bands."""
    mat = bpy.data.materials.get(MAT_PREFIX + name)
    if mat is None:
        mat = bpy.data.materials.new(MAT_PREFIX + name)
    mat.use_nodes = True
    nt = mat.node_tree
    nt.nodes.clear()
    out = nt.nodes.new('ShaderNodeOutputMaterial')
    out.location = (400, 0)
    em = nt.nodes.new('ShaderNodeEmission')
    em.location = (150, 0)
    tc = nt.nodes.new('ShaderNodeTexCoord')
    tc.location = (-520, 0)
    sep = nt.nodes.new('ShaderNodeSeparateXYZ')
    sep.location = (-330, 0)
    ramp = nt.nodes.new('ShaderNodeValToRGB')
    ramp.location = (-140, 0)
    nt.links.new(tc.outputs['Generated'], sep.inputs['Vector'])
    nt.links.new(sep.outputs['Y'], ramp.inputs['Fac'])
    nt.links.new(ramp.outputs['Color'], em.inputs['Color'])
    nt.links.new(em.outputs['Emission'], out.inputs['Surface'])
    el = ramp.color_ramp.elements
    el[0].position = 0.0
    el[0].color = (0.016, 0.020, 0.028, 1.0)
    el[1].position = 1.0
    el[1].color = (0.115, 0.135, 0.180, 1.0)
    em.inputs['Strength'].default_value = 1.0
    return mat


def _try_attr(obj, attr, value):
    try:
        setattr(obj, attr, value)
        return True
    except Exception:
        return False


def build_all():
    """Return a dict of every material used by the model."""
    m = {}
    # translucent acrylic shell - 40 % nominal light transmission
    m['shell'] = make_glass('Acrylic_Shell', base=(0.52, 0.66, 0.80, 1.0),
                            transmission=0.40, roughness=0.03,
                            face_alpha=0.025, grazing_alpha=0.34)
    m['shell_top'] = make_glass('Acrylic_Shell_Top',
                                base=(0.52, 0.66, 0.80, 1.0),
                                transmission=0.40, roughness=0.03,
                                face_alpha=0.045, grazing_alpha=0.30)
    # structural frame is anodised aluminium so the box reads crisply
    m['frame'] = make('Aluminium_Frame', base=(0.34, 0.355, 0.385, 1.0),
                      metallic=0.70, roughness=0.42)
    m['alu'] = make('Aluminium', base=(0.76, 0.775, 0.80, 1.0),
                    metallic=0.95, roughness=0.26)
    m['alu_dark'] = make('Aluminium_Dark', base=(0.26, 0.27, 0.29, 1.0),
                         metallic=0.88, roughness=0.42)
    m['steel'] = make('Steel_Screw', base=(0.86, 0.87, 0.90, 1.0),
                      metallic=1.0, roughness=0.15)
    m['rail'] = make('Rail_Steel', base=(0.88, 0.89, 0.92, 1.0),
                     metallic=1.0, roughness=0.18)
    m['motor'] = make('Motor_Black', base=(0.035, 0.037, 0.042, 1.0),
                      metallic=0.30, roughness=0.42)
    m['motor_cap'] = make('Motor_EndCap', base=(0.12, 0.12, 0.13, 1.0),
                          metallic=0.55, roughness=0.35)
    m['shaft'] = make('Motor_Shaft', base=(0.90, 0.91, 0.93, 1.0),
                      metallic=1.0, roughness=0.12)
    # white acrylic for the pusher (brief allows clear or white)
    m['clear'] = make('Acrylic_White', base=(0.90, 0.92, 0.94, 1.0),
                      metallic=0.0, roughness=0.16, transmission=0.0,
                      alpha=0.78, ior=1.49)
    m['rubber'] = make('Rubber_Black', base=(0.022, 0.022, 0.025, 1.0),
                       metallic=0.0, roughness=0.92)
    m['book'] = make('Book_Cover_DarkRed', base=(0.46, 0.055, 0.065, 1.0),
                     metallic=0.0, roughness=0.48)
    m['pages'] = make('Book_Pages', base=(0.95, 0.91, 0.79, 1.0),
                      metallic=0.0, roughness=0.75)
    m['chute'] = make('Chute_White', base=(0.90, 0.91, 0.93, 1.0),
                      metallic=0.05, roughness=0.35)
    # render-only dressing
    m['floor'] = make('Stage_Floor', base=(0.045, 0.048, 0.056, 1.0),
                      metallic=0.0, roughness=0.38)
    m['backdrop'] = make_backdrop()
    return m
