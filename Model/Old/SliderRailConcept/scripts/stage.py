"""stage - studio world, three-point light rig, camera, render settings.

Everything is in millimetres, so an area light that would be 1 m across in a
metre-scale scene is 1000 units across here and its wattage has to be scaled
by roughly distance^2 (10^6 for a 1 m -> 1 mm change).  The sun lamps are
distance independent, which is why they carry most of the exposure.
"""

import math
import os

import bpy
from mathutils import Matrix, Vector

import geomlib as G

TARGET = Vector((0.0, 0.0, 0.0))


def _try(obj, attr, value):
    try:
        setattr(obj, attr, value)
        return True
    except Exception:
        return False


# ------------------------------------------------------------------- world --
def setup_world(scene, strength=1.0):
    """Vertical gradient environment: dark stage floor, bright studio sky.

    A gradient (rather than a flat colour) is what makes the polished metal
    read as metal - the silver parts reflect the bright upper half and stay
    dark in the lower half.
    """
    w = bpy.data.worlds.get('World') or bpy.data.worlds.new('World')
    scene.world = w
    w.use_nodes = True
    nt = w.node_tree
    nt.nodes.clear()

    out = nt.nodes.new('ShaderNodeOutputWorld')
    out.location = (500, 0)
    bg = nt.nodes.new('ShaderNodeBackground')
    bg.location = (300, 0)
    bg.inputs['Strength'].default_value = strength

    tc = nt.nodes.new('ShaderNodeTexCoord')
    tc.location = (-400, 0)
    sep = nt.nodes.new('ShaderNodeSeparateXYZ')
    sep.location = (-200, 0)
    ramp = nt.nodes.new('ShaderNodeValToRGB')
    ramp.location = (60, 0)
    nt.links.new(tc.outputs['Generated'], sep.inputs['Vector'])
    nt.links.new(sep.outputs['Z'], ramp.inputs['Fac'])
    nt.links.new(ramp.outputs['Color'], bg.inputs['Color'])
    nt.links.new(bg.outputs['Background'], out.inputs['Surface'])

    el = ramp.color_ramp.elements
    el[0].position = 0.00
    el[0].color = (0.010, 0.011, 0.014, 1.0)
    el[1].position = 1.00
    el[1].color = (0.190, 0.210, 0.250, 1.0)
    e = ramp.color_ramp.elements.new(0.44)
    e.color = (0.026, 0.029, 0.036, 1.0)
    e = ramp.color_ramp.elements.new(0.50)
    e.color = (0.130, 0.142, 0.165, 1.0)
    e = ramp.color_ramp.elements.new(0.58)
    e.color = (0.042, 0.047, 0.058, 1.0)
    return w


# ------------------------------------------------------------------- stage --
def build_stage(P, coll, M, x0=-24000.0, x1=24000.0,
                y0=-4200.0, y1=14000.0,
                back_y=-4200.0, back_w=160000.0, back_h=70000.0,
                back_cz=18000.0):
    """Bounded glossy floor + the backdrop wall standing on its far edge.

    The floor MUST be bounded.  With an (effectively) infinite ground plane and
    a 22 deg camera elevation the horizon never enters the frame, so the entire
    background is floor and reads as one flat pool of light.  Ending the floor
    at back_y puts the floor/backdrop junction just below the frame centre.
    """
    import bmesh
    objs = []

    bm = bmesh.new()
    vs = [bm.verts.new(p) for p in ((x0, y0, 0.0), (x1, y0, 0.0),
                                    (x1, y1, 0.0), (x0, y1, 0.0))]
    bm.faces.new(vs)
    me = bpy.data.meshes.new('STAGE_Floor')
    bm.to_mesh(me)
    bm.free()
    floor = bpy.data.objects.new('STAGE_Floor', me)
    coll.objects.link(floor)
    floor.data.materials.append(M['MAT-Stage_Floor'])
    floor['dsh_group'] = 'STAGE'
    objs.append(floor)

    bm = bmesh.new()
    hw, hh = back_w * 0.5, back_h * 0.5
    vs = [bm.verts.new(p) for p in ((-hw, 0.0, -hh), (hw, 0.0, -hh),
                                    (hw, 0.0, hh), (-hw, 0.0, hh))]
    bm.faces.new(vs)
    me = bpy.data.meshes.new('STAGE_Backdrop')
    bm.to_mesh(me)
    bm.free()
    back = bpy.data.objects.new('STAGE_Backdrop', me)
    coll.objects.link(back)
    back.data.materials.append(M['MAT-Stage_Backdrop'])
    back.location = (0.0, back_y, back_cz)
    back['dsh_group'] = 'STAGE'
    objs.append(back)
    return objs


# ------------------------------------------------------------------ lights --
def sun_euler(az_deg, el_deg):
    """Euler for a SUN whose -Z axis points from (az, el) toward the origin."""
    return (math.radians(90.0 - el_deg), 0.0, math.radians(az_deg))


def _sun(name, coll, az, el, energy, angle_deg, color):
    d = bpy.data.lights.new(name, type='SUN')
    d.energy = energy
    d.angle = math.radians(angle_deg)
    d.color = color
    d.use_shadow = True
    o = bpy.data.objects.new(name, d)
    coll.objects.link(o)
    o.location = (0.0, 0.0, 6000.0)
    o.rotation_euler = sun_euler(az, el)
    o['dsh_group'] = 'LIGHT'
    _hide_from_camera(o)
    return o


def _area(name, coll, size_x, size_y, energy, color, loc, aim):
    d = bpy.data.lights.new(name, type='AREA')
    d.shape = 'RECTANGLE'
    d.size = size_x
    d.size_y = size_y
    d.energy = energy
    d.color = color
    o = bpy.data.objects.new(name, d)
    coll.objects.link(o)
    o.location = loc
    o.rotation_euler = _aim_euler(Vector(loc), Vector(aim))
    o['dsh_group'] = 'LIGHT'
    _hide_from_camera(o)
    return o


def _hide_from_camera(o):
    """Keep a lamp OUT of the frame but still visible in the metal reflections.

    An area light 5 m wide sitting behind the subject is otherwise rendered as
    a huge white rectangle in the background.
    """
    _try(o, 'visible_camera', False)
    _try(o, 'visible_shadow', True)
    _try(o, 'visible_glossy', True)
    _try(o, 'visible_diffuse', True)
    _try(o, 'visible_transmission', True)
    _try(o, 'visible_volume_scatter', True)


def _aim_euler(eye, target):
    fwd = (target - eye).normalized()
    up = Vector((0.0, 0.0, 1.0))
    right = fwd.cross(up)
    if right.length < 1e-6:
        right = Vector((1.0, 0.0, 0.0))
    right.normalize()
    up2 = right.cross(fwd)
    return Matrix(((right.x, up2.x, -fwd.x),
                   (right.y, up2.y, -fwd.y),
                   (right.z, up2.z, -fwd.z))).to_euler()


def three_point(P, coll, T, cam_az, scale=1.0):
    """Three-point rig derived from the camera azimuth so it always wraps the
    lit side of the model.  The area boxes are the 'studio' the silver mirrors.
    """
    a = math.radians(cam_az)
    front = Vector((math.sin(a), math.cos(a), 0.0))    # subject -> camera
    right = Vector((-math.cos(a), math.sin(a), 0.0))   # screen right
    T = Vector(T)
    L = []
    L.append(_sun('LGT_Key', coll, cam_az + 34.0, 46.0, 6.2 * scale, 4.0,
                  (1.000, 0.955, 0.885)))
    L.append(_sun('LGT_Fill', coll, cam_az - 62.0, 18.0, 1.35 * scale, 34.0,
                  (0.720, 0.820, 1.000)))
    L.append(_sun('LGT_Rim', coll, cam_az + 166.0, 36.0, 4.6 * scale, 8.0,
                  (0.800, 0.890, 1.000)))
    L.append(_area('LGT_Softbox', coll, 5400.0, 3400.0, 8.5e7 * scale,
                   (1.000, 0.982, 0.955),
                   tuple(T + front * 2300.0 + Vector((0, 0, 4200))), tuple(T)))
    L.append(_area('LGT_Strip_L', coll, 360.0, 5000.0, 1.9e7 * scale,
                   (0.950, 0.972, 1.000),
                   tuple(T - right * 2500.0 + front * 900.0 + Vector((0, 0, 1300))),
                   tuple(T)))
    L.append(_area('LGT_Strip_R', coll, 360.0, 5000.0, 1.5e7 * scale,
                   (1.000, 0.965, 0.930),
                   tuple(T + right * 2600.0 + front * 1200.0 + Vector((0, 0, 1500))),
                   tuple(T)))
    L.append(_area('LGT_Front_Fill', coll, 3200.0, 2000.0, 1.0e7 * scale,
                   (0.900, 0.940, 1.000),
                   tuple(T + front * 3600.0 + Vector((0, 0, 260))), tuple(T)))
    L.append(_area('LGT_Base_Accent', coll, 1100.0, 850.0, 2.4e7 * scale,
                   (1.000, 0.975, 0.940),
                   tuple(T + front * 2300.0 + Vector((0, 0, 380))),
                   (P['WORLD_SLIDER_X'], P['WORLD_SLIDER_Y'], 110.0)))
    return L


# ------------------------------------------------------------------ camera --
def make_camera(scene, coll, name='CAM_Preview_3Q', lens=85.0):
    d = bpy.data.cameras.new(name)
    d.lens = lens
    d.sensor_width = 36.0
    d.clip_start = 1.0
    d.clip_end = 900000.0
    o = bpy.data.objects.new(name, d)
    coll.objects.link(o)
    scene.camera = o
    o['dsh_group'] = 'CAMERA'
    return o


def _project(scene, cam, points):
    from bpy_extras.object_utils import world_to_camera_view
    xs, ys = [], []
    for p in points:
        v = world_to_camera_view(scene, cam, Vector(p))
        xs.append(v.x)
        ys.append(v.y)
    return min(xs), max(xs), min(ys), max(ys)


def _fit_distance(scene, cam, points, aim, dirv, d, margin, iters=28,
                  roll_deg=0.0, shift_y=0.0):
    for _ in range(iters):
        _place(scene, cam, aim, dirv, d, roll_deg, shift_y)
        x0, x1, y0, y1 = _project(scene, cam, points)
        worst = max(abs(x1 - 0.5) * 2.0, abs(x0 - 0.5) * 2.0,
                    abs(y1 - 0.5) * 2.0, abs(y0 - 0.5) * 2.0)
        if worst <= 1e-9:
            break
        factor = worst / margin
        if abs(factor - 1.0) < 1e-4:
            break
        d *= factor
    return d


def fit_camera(scene, cam, points, az_deg, el_deg, margin=0.90, dist=6000.0,
               iters=28, roll_deg=0.0, shift_y=0.0, aim=None):
    """Pull back until everything is inside margin, then nudge the aim until the
    projected silhouette is actually centred.

    Fitting the distance alone is not enough: the two towers are diagonal, so
    their projected bounding box is asymmetric about the camera ray and the pair
    ends up sitting off to one side of the frame.
    """
    if aim is None:
        aim = Vector((sum(p[0] for p in points) / len(points),
                      sum(p[1] for p in points) / len(points),
                      sum(p[2] for p in points) / len(points)))
    else:
        aim = Vector(aim)
    az = math.radians(az_deg)
    el = math.radians(el_deg)
    dirv = Vector((math.sin(az) * math.cos(el),
                   math.cos(az) * math.cos(el),
                   math.sin(el)))
    d = float(dist)
    for _ in range(5):
        d = _fit_distance(scene, cam, points, aim, dirv, d, margin, iters,
                          roll_deg, shift_y)
        x0, x1, y0, y1 = _project(scene, cam, points)
        cx, cy = (x0 + x1) * 0.5, (y0 + y1) * 0.5
        if abs(cx - 0.5) < 1.5e-3 and abs(cy - 0.5) < 1.5e-3:
            break
        m = cam.matrix_world.to_3x3()
        right = (m @ Vector((1.0, 0.0, 0.0))).normalized()
        up = (m @ Vector((0.0, 1.0, 0.0))).normalized()
        w = d * cam.data.sensor_width / cam.data.lens
        h = w * scene.render.resolution_y / scene.render.resolution_x
        aim = aim + right * (cx - 0.5) * w + up * (cy - 0.5) * h
    _place(scene, cam, aim, dirv, d, roll_deg, shift_y)
    return cam, d


def _place(scene, cam, aim, dirv, d, roll_deg, shift_y):
    cam.location = aim + dirv * d
    cam.rotation_euler = _aim_euler(cam.location, aim)
    cam.data.shift_y = shift_y
    if roll_deg:
        m = cam.rotation_euler.to_matrix() @ Matrix.Rotation(
            math.radians(roll_deg), 3, 'Z')
        cam.rotation_euler = m.to_euler()
    bpy.context.view_layer.update()


def subject_points(parts):
    """Per-object AABB corners, NOT the corners of the union AABB.

    The union of these two towers is 270 x 1540 x 900 mm, but most of that box
    is empty air (the rail sits 1.5 m behind the slider).  Framing on the union
    corners reserves room for phantom geometry and the model ends up tiny in the
    middle of the frame.  Seven tight per-object boxes frame it properly.
    """
    pts = []
    lo = [1e30] * 3
    hi = [-1e30] * 3
    for o in parts:
        mw = o.matrix_world
        for v in o.data.vertices:
            p = mw @ v.co
            for i in range(3):
                if p[i] < lo[i]:
                    lo[i] = p[i]
                if p[i] > hi[i]:
                    hi[i] = p[i]
        a, b = G.world_aabb(o)
        pts.extend((x, y, z) for x in (a[0], b[0])
                   for y in (a[1], b[1]) for z in (a[2], b[2]))
    return pts, tuple(lo), tuple(hi)


# ------------------------------------------------------------ render setup --
def configure_render(scene, width=1920, height=1080, samples=192,
                     engine='CYCLES', ao=True):
    scene.render.resolution_x = width
    scene.render.resolution_y = height
    scene.render.resolution_percentage = 100
    scene.render.film_transparent = False
    scene.render.image_settings.file_format = 'PNG'
    scene.render.image_settings.color_mode = 'RGB'
    scene.render.image_settings.color_depth = '8'
    if engine.upper() == 'CYCLES':
        _cycles(scene, samples, ao)
    else:
        _eevee(scene, samples, ao)

    vs = scene.view_settings
    for vt in ('AgX', 'Filmic', 'Standard'):
        if _try(vs, 'view_transform', vt):
            break
    for look in ('AgX - Medium High Contrast', 'AgX - Punchy',
                 'Medium High Contrast', 'None'):
        if _try(vs, 'look', look):
            break
    _try(vs, 'exposure', 0.95)
    _try(vs, 'gamma', 1.0)

    scene.unit_settings.system = 'METRIC'
    scene.unit_settings.scale_length = 0.001
    scene.unit_settings.length_unit = 'MILLIMETERS'
    return scene


def _cycles(scene, samples, ao):
    scene.render.engine = 'CYCLES'
    cy = scene.cycles
    _try(cy, 'device', 'CPU')
    _try(cy, 'samples', samples)
    _try(cy, 'use_adaptive_sampling', True)
    _try(cy, 'adaptive_threshold', 0.01)
    _try(cy, 'use_denoising', True)
    _try(cy, 'denoiser', 'OPENIMAGEDENOISE')
    _try(cy, 'max_bounces', 10)
    _try(cy, 'diffuse_bounces', 5)
    _try(cy, 'glossy_bounces', 6)
    _try(cy, 'transmission_bounces', 8)
    _try(cy, 'caustics_reflective', False)
    _try(cy, 'caustics_refractive', False)
    _try(cy, 'blur_glossy', 1.0)
    _try(cy, 'sample_clamp_indirect', 3.0)
    # explicit ambient occlusion term (Cycles already resolves occlusion by
    # path tracing; this turns on the extra AO light contribution when the
    # Blender build still exposes the legacy world light settings)
    if ao and scene.world is not None:
        ls = getattr(scene.world, 'light_settings', None)
        if ls is not None:
            _try(ls, 'use_ambient_occlusion', True)
            _try(ls, 'ao_factor', 0.35)
            _try(ls, 'distance', 600.0)


def _eevee(scene, samples, ao):
    for name in ('BLENDER_EEVEE_NEXT', 'BLENDER_EEVEE'):
        try:
            scene.render.engine = name
            break
        except Exception:
            continue
    ee = getattr(scene, 'eevee', None)
    if ee is None:
        return
    _try(ee, 'taa_render_samples', samples)
    _try(ee, 'use_raytracing', True)
    _try(ee, 'use_shadows', True)
    if ao:
        _try(ee, 'use_gtao', True)
        _try(ee, 'gtao_distance', 260.0)
        _try(ee, 'gtao_factor', 1.0)
    _try(ee, 'use_fast_gi', True)
    _try(ee, 'fast_gi_method', 'AMBIENT_OCCLUSION_ONLY')
    _try(ee, 'fast_gi_distance', 0.0)
    _try(ee, 'shadow_ray_count', 4)
    _try(ee, 'shadow_step_count', 12)
    _try(ee, 'shadow_resolution_scale', 2.0)
    _try(ee, 'ray_tracing_method', 'SCREEN')


def setup_compositor(scene, strength=0.24, threshold=1.15, size=6):
    """Subtle fog-glow so specular hits on silver bloom like product photography.

    Blender 5.x replaced the old scene.node_tree / CompositorNodeComposite pair
    with a compositing NODE GROUP whose output is a group interface socket.
    Everything is guarded: if this build names anything differently the
    compositor is switched back off and the render still goes through.
    """
    try:
        ng = getattr(scene, 'compositing_node_group', None)
        if ng is None:
            ng = bpy.data.node_groups.new('SR_Compositing', 'CompositorNodeTree')
            scene.compositing_node_group = ng
        for n in list(ng.nodes):
            ng.nodes.remove(n)
        rl = ng.nodes.new('CompositorNodeRLayers')
        rl.location = (-520, 0)
        gl = ng.nodes.new('CompositorNodeGlare')
        gl.location = (-160, 0)
        _try_sock(gl, 'Type', 'FOG_GLOW')
        _try_sock(gl, 'Quality', 'HIGH')
        _try_sock(gl, 'Threshold', threshold)
        _try_sock(gl, 'Size', float(size))
        _try_sock(gl, 'Strength', strength)
        out = ng.nodes.new('NodeGroupOutput')
        out.location = (220, 0)
        if not [s for s in ng.interface.items_tree if s.in_out == 'OUTPUT']:
            ng.interface.new_socket('Image', in_out='OUTPUT',
                                    socket_type='NodeSocketColor')
        ng.links.new(rl.outputs['Image'], gl.inputs['Image'])
        ng.links.new(gl.outputs['Image'], out.inputs[0])
        return True
    except Exception as exc:
        print('  compositor disabled: %s' % exc, flush=True)
        try:
            scene.compositing_node_group = None
        except Exception:
            pass
        return False


def _try_sock(node, name, value):
    try:
        node.inputs[name].default_value = value
    except Exception:
        pass


def render_to(scene, path):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    scene.render.filepath = os.path.abspath(path)
    bpy.ops.render.render(write_still=True)
    return path


def save_blend(path):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    bpy.ops.wm.save_as_mainfile(filepath=os.path.abspath(path))
    return path


def export_glb(path, objs):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    for o in bpy.context.view_layer.objects:
        o.select_set(False)
    for o in objs:
        o.select_set(True)
    bpy.context.view_layer.objects.active = objs[0] if objs else None
    bpy.ops.export_scene.gltf(filepath=os.path.abspath(path),
                              export_format='GLB', use_selection=True,
                              export_apply=True)
    return path


# -------------------------------------------------------- viewport clipping --
VIEW_CLIP_START = 1.0
VIEW_CLIP_END = 400000.0


def fix_viewport_clip(clip_start=VIEW_CLIP_START, clip_end=VIEW_CLIP_END):
    """1 BU = 1 mm, so the 1 m scene is 1000 units tall while Blender's
    factory viewport clip_end is 1000 - zooming out makes the model vanish.
    MUST run before the .blend is written or the fix is not persisted."""
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
    for cam in bpy.data.cameras:
        _try(cam, 'clip_start', clip_start)
        if getattr(cam, 'clip_end', 0.0) < clip_end:
            _try(cam, 'clip_end', clip_end)
    return patched
