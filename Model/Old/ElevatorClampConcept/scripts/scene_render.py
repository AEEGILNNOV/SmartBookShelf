"""Camera, three point lighting, render settings and export helpers."""

import math
import os

import bpy
from mathutils import Vector


# ------------------------------------------------------------------ world --
def setup_world(strength=0.85, color=(0.075, 0.085, 0.110, 1.0)):
    world = bpy.data.worlds.get('World') or bpy.data.worlds.new('World')
    bpy.context.scene.world = world
    world.use_nodes = True
    nt = world.node_tree
    nt.nodes.clear()
    out = nt.nodes.new('ShaderNodeOutputWorld')
    bg = nt.nodes.new('ShaderNodeBackground')
    bg.inputs['Color'].default_value = color
    bg.inputs['Strength'].default_value = strength
    nt.links.new(bg.outputs['Background'], out.inputs['Surface'])
    return world


def build_stage(P, M, coll):
    """Ground and backdrop - render only, never exported."""
    objs = []
    w = P['CAB_W'] * 6.0
    d = P['CAB_D'] * 8.0
    objs.append(_plane('Stage_Floor', (w, d), (0.0, 120.0, 0.0), coll,
                       M['floor']))
    objs.append(_plane('Stage_Backdrop', (w * 1.4, P['CAB_H'] * 1.6),
                       (0.0, 120.0 - d / 2.0, P['CAB_H'] * 0.8), coll,
                       M['backdrop'], rot=(math.pi / 2.0, 0.0, 0.0)))
    return objs


def _plane(name, size, loc, coll, material, rot=(0.0, 0.0, 0.0)):
    import bmesh
    from mathutils import Matrix
    bm = bmesh.new()
    sx, sy = size[0] / 2.0, size[1] / 2.0
    vs = [bm.verts.new(v) for v in ((-sx, -sy, 0.0), (sx, -sy, 0.0),
                                    (sx, sy, 0.0), (-sx, sy, 0.0))]
    bm.faces.new(vs)
    import geom
    obj = geom._finish(name, bm, coll, material, 'stage', name, None)
    obj.matrix_world = (Matrix.Translation(Vector(loc)) @
                        Matrix.Rotation(rot[0], 4, 'X') @
                        Matrix.Rotation(rot[2], 4, 'Z'))
    return obj


# ----------------------------------------------------------------- lights --
def three_point(P, M, coll):
    """Key / fill / rim suns with soft shadow angles + a soft top box."""
    lights = []
    lights.append(_sun('LGT_Key', coll, energy=4.2, angle=7.0,
                       color=(1.0, 0.955, 0.885),
                       rot=(math.radians(50.0), 0.0, math.radians(34.0))))
    lights.append(_sun('LGT_Fill', coll, energy=0.75, angle=38.0,
                       color=(0.74, 0.84, 1.0),
                       rot=(math.radians(74.0), 0.0, math.radians(-62.0))))
    lights.append(_sun('LGT_Rim', coll, energy=3.2, angle=14.0,
                       color=(0.80, 0.89, 1.0),
                       rot=(math.radians(116.0), 0.0, math.radians(198.0))))
    # large soft top box for interior fill (reads as a studio softbox)
    data = bpy.data.lights.new('LGT_TopSoftbox', type='AREA')
    data.shape = 'RECTANGLE'
    data.size = P['CAB_W'] * 2.2
    data.size_y = P['CAB_D'] * 2.0
    data.energy = 3.6e7
    data.color = (1.0, 0.98, 0.95)
    obj = bpy.data.objects.new('LGT_TopSoftbox', data)
    coll.objects.link(obj)
    obj.location = (300.0, -200.0, P['CAB_H'] * 2.1)
    obj.rotation_euler = (math.radians(12.0), math.radians(-8.0), 0.0)
    lights.append(obj)
    # low in-cabinet kicker so the transparent shell does not read flat
    data2 = bpy.data.lights.new('LGT_Bounce_Front', type='AREA')
    data2.shape = 'RECTANGLE'
    data2.size = P['CAB_W']
    data2.size_y = P['CAB_D']
    data2.energy = 5.0e6
    data2.color = (0.86, 0.92, 1.0)
    obj2 = bpy.data.objects.new('LGT_Bounce_Front', data2)
    coll.objects.link(obj2)
    obj2.location = (-140.0, 1500.0, 350.0)
    obj2.rotation_euler = (math.radians(74.0), 0.0, math.radians(-5.0))
    lights.append(obj2)
    # interior fill: keeps the mechanism readable through the acrylic
    data3 = bpy.data.lights.new('LGT_Interior_Fill', type='AREA')
    data3.shape = 'RECTANGLE'
    data3.size = P['CAB_W'] * 0.8
    data3.size_y = P['CAB_D'] * 0.8
    data3.energy = 1.1e6
    data3.color = (1.0, 0.97, 0.92)
    obj3 = bpy.data.objects.new('LGT_Interior_Fill', data3)
    coll.objects.link(obj3)
    obj3.location = (0.0, -60.0, P['CAB_H'] * 0.78)
    obj3.rotation_euler = (0.0, 0.0, 0.0)
    lights.append(obj3)
    # lower interior fill so the drive carriages read through the glass
    data4 = bpy.data.lights.new('LGT_Interior_Low', type='AREA')
    data4.shape = 'RECTANGLE'
    data4.size = P['CAB_W'] * 0.7
    data4.size_y = P['CAB_D'] * 0.6
    data4.energy = 1.4e6
    data4.color = (0.95, 0.96, 1.0)
    obj4 = bpy.data.objects.new('LGT_Interior_Low', data4)
    coll.objects.link(obj4)
    obj4.location = (0.0, 150.0, 300.0)
    obj4.rotation_euler = (math.radians(-70.0), 0.0, 0.0)
    lights.append(obj4)
    # accent aimed straight through the open middle port: lights the frozen
    # push pose (book / platform / limit plate / pusher) without blowing out
    data5 = bpy.data.lights.new('LGT_Port_Accent', type='AREA')
    data5.shape = 'RECTANGLE'
    data5.size = 420.0
    data5.size_y = 260.0
    data5.energy = 3.6e7
    data5.color = (1.0, 0.97, 0.93)
    obj5 = bpy.data.objects.new('LGT_Port_Accent', data5)
    coll.objects.link(obj5)
    obj5.location = (620.0, 1250.0, 660.0)
    obj5.rotation_euler = (math.radians(88.0), 0.0, math.radians(26.0))
    lights.append(obj5)
    return lights


def _sun(name, coll, energy, angle, color, rot):
    data = bpy.data.lights.new(name, type='SUN')
    data.energy = energy
    data.angle = math.radians(angle)
    data.color = color
    data.use_shadow = True
    obj = bpy.data.objects.new(name, data)
    coll.objects.link(obj)
    obj.location = (0.0, 0.0, 3000.0)
    obj.rotation_euler = rot
    return obj


# ----------------------------------------------------------------- camera --
def make_camera(scene, coll, name='CAM_Preview_3Q'):
    data = bpy.data.cameras.new(name)
    data.lens = 58.0
    data.sensor_width = 36.0
    data.clip_start = 1.0
    data.clip_end = 500000.0
    obj = bpy.data.objects.new(name, data)
    coll.objects.link(obj)
    scene.camera = obj
    return obj


def fit_camera(scene, cam, points, az_deg, el_deg, margin=0.90,
               dist=3000.0, iters=24):
    """Push the camera out along az/el until every point sits in frame."""
    from bpy_extras.object_utils import world_to_camera_view
    az = math.radians(az_deg)
    el = math.radians(el_deg)
    dirv = Vector((math.sin(az) * math.cos(el),
                   math.cos(az) * math.cos(el),
                   math.sin(el)))
    target = Vector((sum(p[0] for p in points) / len(points),
                     sum(p[1] for p in points) / len(points),
                     sum(p[2] for p in points) / len(points)))
    cam.rotation_euler = _look_at(target + dirv, target)
    d = dist
    for _ in range(iters):
        cam.location = target + dirv * d
        bpy.context.view_layer.update()
        worst = 0.0
        for p in points:
            v = world_to_camera_view(scene, cam, Vector(p))
            worst = max(worst, abs(v.x - 0.5) * 2.0, abs(v.y - 0.5) * 2.0)
        if worst <= 1e-6:
            break
        factor = worst / margin
        d *= factor
        if abs(factor - 1.0) < 2e-3:
            break
    cam.location = target + dirv * d
    bpy.context.view_layer.update()
    return cam, d


def _look_at(eye, target):
    from mathutils import Matrix
    fwd = (Vector(target) - Vector(eye)).normalized()
    up = Vector((0.0, 0.0, 1.0))
    right = fwd.cross(up)
    if right.length < 1e-6:
        right = Vector((1.0, 0.0, 0.0))
    right.normalize()
    up2 = right.cross(fwd)
    m = Matrix(((right.x, up2.x, -fwd.x),
                (right.y, up2.y, -fwd.y),
                (right.z, up2.z, -fwd.z)))
    return m.to_euler()


def scene_points(objs):
    """All bounding box corners of the given objects."""
    pts = []
    for o in objs:
        for c in o.bound_box:
            pts.append(tuple(o.matrix_world @ Vector(c)))
    return pts


# ------------------------------------------------------------ render setup --
def _try(obj, attr, value):
    try:
        setattr(obj, attr, value)
        return True
    except Exception:
        return False


def configure_render(scene, width=1920, height=1080, samples=128,
                    engine='EEVEE'):
    scene.render.resolution_x = width
    scene.render.resolution_y = height
    scene.render.resolution_percentage = 100
    scene.render.film_transparent = False
    scene.render.image_settings.file_format = 'PNG'
    scene.render.image_settings.color_mode = 'RGB'
    if engine.upper() == 'CYCLES':
        _configure_cycles(scene, samples)
    else:
        _configure_eevee(scene, samples)
    # colour management - prefer AgX when the OCIO config offers it
    vs = scene.view_settings
    for vt in ('AgX', 'Filmic', 'Standard'):
        if _try(vs, 'view_transform', vt):
            break
    for look in ('AgX - Medium High Contrast', 'AgX - Punchy',
                 'Medium High Contrast', 'None'):
        if _try(vs, 'look', look):
            break
    _try(vs, 'exposure', 0.0)
    _try(vs, 'gamma', 1.0)
    scene.unit_settings.system = 'METRIC'
    scene.unit_settings.scale_length = 0.001
    scene.unit_settings.length_unit = 'MILLIMETERS'
    return scene


def _configure_cycles(scene, samples):
    scene.render.engine = 'CYCLES'
    cy = scene.cycles
    _try(cy, 'device', 'CPU')
    _try(cy, 'samples', samples)
    _try(cy, 'use_adaptive_sampling', True)
    _try(cy, 'adaptive_threshold', 0.02)
    _try(cy, 'use_denoising', True)
    _try(cy, 'max_bounces', 8)
    _try(cy, 'diffuse_bounces', 4)
    _try(cy, 'glossy_bounces', 4)
    _try(cy, 'transmission_bounces', 12)
    _try(cy, 'transparent_max_bounces', 24)
    _try(cy, 'caustics_reflective', False)
    _try(cy, 'caustics_refractive', False)
    _try(cy, 'blur_glossy', 1.0)
    _try(cy, 'sample_clamp_indirect', 4.0)
    _try(cy, 'use_fast_gi', False)
    return scene


def _configure_eevee(scene, samples):
    scene.render.engine = 'BLENDER_EEVEE'
    ee = scene.eevee
    _try(ee, 'taa_render_samples', samples)
    _try(ee, 'use_raytracing', True)
    _try(ee, 'use_shadows', True)
    _try(ee, 'use_fast_gi', True)
    _try(ee, 'fast_gi_method', 'AMBIENT_OCCLUSION_ONLY')
    _try(ee, 'fast_gi_distance', 0.0)
    _try(ee, 'shadow_ray_count', 4)
    _try(ee, 'shadow_step_count', 8)
    _try(ee, 'shadow_resolution_scale', 2.0)
    _try(ee, 'use_shadow_jitter_viewport', True)
    _try(ee, 'ray_tracing_method', 'SCREEN')
    return scene


def render_to(scene, path):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    scene.render.filepath = path
    bpy.ops.render.render(write_still=True)
    return path


def save_blend(path):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    bpy.ops.wm.save_as_mainfile(filepath=path)
    return path


def export_glb(path, objs):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    bpy.ops.object.select_all(action='DESELECT')
    for o in objs:
        o.select_set(True)
    bpy.context.view_layer.objects.active = objs[0] if objs else None
    kwargs = dict(filepath=path, export_format='GLB', use_selection=True)
    try:
        bpy.ops.export_scene.gltf(export_apply=True, **kwargs)
    except TypeError:
        bpy.ops.export_scene.gltf(**kwargs)
    return path


VIEW_CLIP_START = 1.0
VIEW_CLIP_END = 300000.0


def fix_viewport_clip(clip_start=VIEW_CLIP_START, clip_end=VIEW_CLIP_END):
    """Raise the 3D viewport / camera clipping range.

    This project uses 1 Blender unit = 1 mm, so the machine is ~1214 units
    tall while Blender's default viewport clip_end is 1000.  Zooming out
    therefore cuts the whole model away and the viewport looks black.

    Returns the number of 3D viewports patched.  MUST be called before the
    .blend is saved, otherwise the fix is not persisted.
    """
    patched = 0
    screens = list(bpy.data.screens)
    for wm in bpy.data.window_managers:
        for win in wm.windows:
            if win.screen is not None and win.screen not in screens:
                screens.append(win.screen)
    for screen in screens:
        for area in screen.areas:
            if area.type != 'VIEW_3D':
                continue
            for space in area.spaces:
                if space.type == 'VIEW_3D':
                    _try(space, 'clip_start', clip_start)
                    _try(space, 'clip_end', clip_end)
                    patched += 1
    # every camera gets a usable range too (the preview camera is set in
    # make_camera, this covers any camera the user adds later)
    for cam in bpy.data.cameras:
        if getattr(cam, 'clip_end', 0.0) < clip_end:
            _try(cam, 'clip_end', clip_end)
    return patched
