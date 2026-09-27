"""SmartBookShelf - STEP 1 build.

    cabinet frame + three-level front face + top book hole
    twin drive columns (42 stepper / coupling / 900 mm screw / passive rail)
    PIVOT_Platform_Z  (moving platform, 80x80 carriages, L-section guards)
    book in the frozen pose

Run:
  & "D:/Application/3D/Blender/blender.exe" --background --factory-startup ^
      --python "<repo>/scripts/build_step1.py" -- [fast] [norender] [cycles] [openfront]

Gates: bounding-box anti-clash between rigid groups (>= MIN_CLEARANCE).
Nothing is animated: no keyframes, no timeline use, single static frame.
"""

import bpy
import bmesh
import math
import os
import sys
import json

HERE = None
if '__file__' in globals():
    HERE = os.path.dirname(os.path.abspath(__file__))
else:
    for a in sys.argv:
        if a.lower().endswith('build_step1.py'):
            HERE = os.path.dirname(os.path.abspath(a))
            break
if HERE and HERE not in sys.path:
    sys.path.insert(0, HERE)

import sbs_params as SP
import sbs_geom as G
import sbs_materials as SM
import sbs_clash as CL
import sbs_parts as PA

for m in (SP, G, SM, CL, PA):
    import importlib
    importlib.reload(m)

P = SP.derive()

# ------------------------------------------------------------------- args --
ARGS = sys.argv[sys.argv.index('--') + 1:] if '--' in sys.argv else []
FAST = 'fast' in ARGS
NORENDER = 'norender' in ARGS
FORCE_CYCLES = 'cycles' in ARGS
OPEN_FRONT = 'openfront' in ARGS

ROOT = os.path.dirname(HERE) if HERE else os.getcwd()
OUT_BLEND = os.path.join(ROOT, 'models', 'smartbookshelf_step1.blend')
OUT_PREVIEW = os.path.join(ROOT, 'renders', 'step1_preview_1920x1080.png')
OUT_DIAG = os.path.join(ROOT, 'renders', 'diag')
OUT_DOC = os.path.join(ROOT, 'docs')
for d in (os.path.dirname(OUT_BLEND), os.path.dirname(OUT_PREVIEW), OUT_DIAG, OUT_DOC):
    os.makedirs(d, exist_ok=True)

R_FAST = (960, 540)
R_FINAL = (1920, 1080)
RES = R_FAST if FAST else R_FINAL

print('=' * 78)
print('SmartBookShelf STEP 1  |  res={}x{}  render={}'.format(
    RES[0], RES[1], 'no' if NORENDER else 'yes'))
print('=' * 78)


# ================================================================== SCENE ==
scene = G.reset_scene(P['UNIT_SCALE'])
M = SM.build_library()

COL = {
    'cabinet':  G.ensure_collection('COL-01_Cabinet'),
    'drive':    G.ensure_collection('COL-02_DriveColumns'),
    'platform': G.ensure_collection('COL-03_MovingPlatform'),
    'book':     G.ensure_collection('COL-05_Book'),
    'stage':    G.ensure_collection('COL-90_RenderStage'),
}

print('[build] cabinet ...')
pa_cab = PA.build_cabinet(P, M, COL)
print('[build] drive columns ...')
pa_drv = PA.build_drive_columns(P, M, COL)
print('[build] moving platform ...')
pa_plt = PA.build_platform(P, M, COL)
print('[build] book ...')
pa_bok = PA.build_book(P, M, COL)

# --- optional: delete the front L1/L3 panels so the interior is unobstructed
if OPEN_FRONT:
    for name in ('GEO_Front_L1_Bottom', 'GEO_Front_L3_Top'):
        o = bpy.data.objects.get(name)
        if o:
            bpy.data.objects.remove(o, do_unlink=True)
    print('[build] --openfront : removed the two fixed front panels')

MECH = pa_cab + pa_drv + pa_plt + pa_bok
print('[build] mechanical objects: {}'.format(len(MECH)))


# ============================================================ CLASH GATE ==
print('-' * 78)
# Declared contacts.  A moving sub-assembly must pass through the cabinet's
# swept volume (it lives inside it), and the book rests ON the platform.
allow = [
    ('PLATFORM', 'CABINET'),
    ('BOOK', 'CABINET'),
    ('BOOK', 'PLATFORM'),
]
failures, tested = CL.check(MECH, min_clearance=P['MIN_CLEARANCE'],
                            allow=allow, verbose=True)
if failures:
    print('')
    print('!!! ANTI-CLASH GATE FAILED - {} pair(s) violate the {:.0f} mm '
          'minimum clearance'.format(len(failures), P['MIN_CLEARANCE']))
    for f in failures[:12]:
        print('    {:>18}  {:<32} vs {:<32} dx={} dy={} dz={}'.format(
            f['kind'], f['a'], f['b'], f['dx'], f['dy'], f['dz']))
    print('    Fix the parameters in sbs_params.py and re-run.')
    print('    Build aborted so no clashing model is ever written.')
    sys.exit(2)
print('[clash] safety-net gate passed ({} tested pairs)'.format(tested))
print('-' * 78)

# ==================================================== DESIGN ASSERTIONS ==
assert_results, assert_bad = CL.assert_design(MECH, P, verbose=True)
print('-' * 78)
if assert_bad:
    print('!!! {} DESIGN ASSERTION(S) FAILED'.format(len(assert_bad)))
    for _, name, val, lim, ge, unit in assert_bad:
        print('    {}: {} = {} (needs {} {})'.format(
            name, 'value', round(val, 3), '>=' if ge else '<=', lim))
    sys.exit(3)
print('[assert] all {} design assertions PASSED'.format(len(assert_results)))
print('-' * 78)


# ============================================================== STAGE =====
def stage():
    col = COL['stage']
    big = 4000.0
    G.box('STAGE_Floor', -big, big, -big, big, -60.0, 0.0, col, M['floor'])
    G.box('STAGE_Backdrop', -big, big, -big - 200, -big, -60.0, big, col,
          M['backdrop'])
    # world - soft neutral gradient
    world = bpy.data.worlds.new('W_Studio')
    scene.world = world
    world.use_nodes = True
    nt = world.node_tree
    for n in list(nt.nodes):
        nt.nodes.remove(n)
    bg = nt.nodes.new('ShaderNodeBackground')
    bg.inputs[0].default_value = (0.055, 0.062, 0.075, 1.0)
    bg.inputs[1].default_value = 0.85
    out = nt.nodes.new('ShaderNodeOutputWorld')
    nt.links.new(bg.outputs[0], out.inputs[0])


def area_light(name, loc, target, size, energy, color):
    ld = bpy.data.lights.new(name, 'AREA')
    ld.energy = energy
    ld.size = size
    ld.color = color
    ob = bpy.data.objects.new(name, ld)
    COL['stage'].objects.link(ob)
    ob.location = loc
    d = (ob.location - target)
    ob.rotation_euler = d.to_track_quat('Z', 'Y').to_euler()
    return ob


def three_point():
    from mathutils import Vector
    tgt = Vector((0.0, 0.0, 600.0))
    k = 4.2e8 if not FAST else 1.9e8
    area_light('LGT_Key', Vector((2600.0, 3000.0, 3400.0)), tgt, 2200.0, k,
               (1.0, 0.955, 0.90))
    area_light('LGT_Fill', Vector((-3200.0, 2200.0, 1100.0)), tgt, 2600.0,
               k * 0.30, (0.86, 0.92, 1.0))
    area_light('LGT_Rim', Vector((-900.0, -3400.0, 3000.0)), tgt, 1800.0,
               k * 0.72, (0.90, 0.95, 1.0))
    area_light('LGT_Top', Vector((0.0, 300.0, 4200.0)), tgt, 3000.0,
               k * 0.42, (1.0, 0.99, 0.97))


def scene_bounds(objs):
    from mathutils import Vector
    lo = Vector((1e18, 1e18, 1e18))
    hi = Vector((-1e18, -1e18, -1e18))
    for o in objs:
        for c in o.bound_box:
            w = o.matrix_world @ Vector(c)
            for i in range(3):
                lo[i] = min(lo[i], w[i])
                hi[i] = max(hi[i], w[i])
    return lo, hi


def make_camera(name, loc, target, lens=50.0):
    cd = bpy.data.cameras.new(name)
    cd.lens = lens
    ob = bpy.data.objects.new(name, cd)
    COL['stage'].objects.link(ob)
    from mathutils import Vector
    ob.location = Vector(loc)
    d = (ob.location - Vector(target))
    ob.rotation_euler = d.to_track_quat('Z', 'Y').to_euler()
    return ob


def make_ortho_camera(name, direction, lo, hi, scale=1.06, res=None):
    """Orthographic camera that exactly frames the AABB from 'direction'.

    ortho_scale sizes the HORIZONTAL extent, so the vertical extent it can
    cover is (height/width) times that.  Both must be solved for, otherwise a
    tall model is cropped no matter how the scale is guessed.
    """
    from mathutils import Vector
    d = Vector(direction).normalized()
    loc = Vector(((lo.x + hi.x) / 2.0, (lo.y + hi.y) / 2.0,
                  (lo.z + hi.z) / 2.0)) + d * 6000.0
    cd = bpy.data.cameras.new(name)
    cd.type = 'ORTHO'
    cd.clip_start = 1.0
    cd.clip_end = 300000.0
    ob = bpy.data.objects.new(name, cd)
    COL['stage'].objects.link(ob)
    ob.location = loc
    ob.rotation_euler = (loc - Vector(((lo.x + hi.x) / 2.0,
                                       (lo.y + hi.y) / 2.0,
                                       (lo.z + hi.z) / 2.0))
                         ).to_track_quat('Z', 'Y').to_euler()
    rx, ry = res if res else (scene.render.resolution_x,
                              scene.render.resolution_y)
    aspect = rx / float(ry)

    # camera basis
    fwd = -d
    up_hint = Vector((0.0, 0.0, 1.0))
    if abs(fwd.dot(up_hint)) > 0.999:
        up_hint = Vector((0.0, 1.0, 0.0))
    right = fwd.cross(up_hint).normalized()
    up = right.cross(fwd).normalized()

    need_w = 0.0
    need_h = 0.0
    ctr = Vector(((lo.x + hi.x) / 2.0, (lo.y + hi.y) / 2.0,
                  (lo.z + hi.z) / 2.0))
    for cx in (lo.x, hi.x):
        for cy in (lo.y, hi.y):
            for cz in (lo.z, hi.z):
                v = Vector((cx, cy, cz)) - ctr
                need_w = max(need_w, abs(v.dot(right)))
                need_h = max(need_h, abs(v.dot(up)))
    cd.ortho_scale = max(need_w * 2.0, need_h * 2.0 / aspect) * scale
    return ob, cd.ortho_scale


def cameras():
    from mathutils import Vector
    lo, hi = scene_bounds(MECH)
    ctr = (lo + hi) / 2.0
    span = max(hi.x - lo.x, hi.y - lo.y, hi.z - lo.z)
    diag = (hi - lo).length
    print('[camera] bounds {} -> {}  centre {}  diag {:.0f}'.format(
        tuple(round(v, 1) for v in lo), tuple(round(v, 1) for v in hi),
        tuple(round(v, 1) for v in ctr), diag))

    # 3/4 hero: front-right, ~34 deg above, showing the top hole and the
    # three-level front face.  Distance is solved from the vertical FOV so the
    # whole machine always fits.
    az, el = 42.0, 34.0
    lens = 45.0
    sensor_v = 24.0                       # Blender default sensor height, mm
    vfov = 2.0 * math.atan(sensor_v / (2.0 * lens))
    radius = (hi - lo).length / 2.0       # bounding sphere
    dist = radius / math.sin(vfov / 2.0) * 1.12
    d = Vector((math.sin(az * math.pi / 180.0) * math.cos(el * math.pi / 180.0),
                math.cos(az * math.pi / 180.0) * math.cos(el * math.pi / 180.0),
                math.sin(el * math.pi / 180.0)))
    cam = make_camera('CAM_Preview_3Q', ctr + d * dist, ctr, lens=lens)
    print('[camera] preview lens={} dist={:.0f} radius={:.0f}'.format(
        lens, dist, radius))

    # square diagnostic frames: the machine is 1200 tall but only 600 wide, so
    # a 16:9 ortho frame would crop the height badly.
    for name, direction in (('CAM_Diag_Front', (0.0, 1.0, 0.0)),
                            ('CAM_Diag_Left', (-1.0, 0.0, 0.0)),
                            ('CAM_Diag_Top', (0.0, 0.0, 1.0))):
        _, osc = make_ortho_camera(name, direction, lo, hi, res=(1000, 1000))
        print('[camera] {} ortho_scale={:.0f} (frame 1000x1000)'.format(
            name, osc))
    # perspective 3/4 of the OPEN interior, rendered with the front panels
    # hidden so the mechanism can be inspected without acrylic in the way
    make_camera('CAM_OpenInterior', ctr + d * dist * 0.78, ctr, lens=45.0)
    print('[camera] CAM_OpenInterior 3/4 perspective for mechanism review')

    scene.camera = cam
    return cam, ctr, diag


def render_settings():
    sc = scene
    sc.render.resolution_x, sc.render.resolution_y = RES
    sc.render.resolution_percentage = 100
    sc.render.image_settings.file_format = 'PNG'
    sc.render.film_transparent = False
    sc.view_settings.view_transform = 'AgX'
    sc.view_settings.look = 'AgX - Punchy'
    sc.view_settings.exposure = 0.35

    engine = None
    if not FORCE_CYCLES:
        for name in ('BLENDER_EEVEE_NEXT', 'BLENDER_EEVEE'):
            try:
                sc.render.engine = name
                engine = name
                break
            except Exception:
                continue
    if engine is None:
        sc.render.engine = 'CYCLES'
        engine = 'CYCLES'
    if engine.startswith('BLENDER_EEVEE'):
        try:
            sc.eevee.taa_render_samples = 32 if FAST else 96
        except Exception:
            pass
        for attr, val in (('use_gtao', True), ('use_raytracing', True),
                          ('use_shadows', True), ('use_bloom', False)):
            if hasattr(sc.eevee, attr):
                try:
                    setattr(sc.eevee, attr, val)
                except Exception:
                    pass
        if hasattr(sc, 'cycles'):
            pass
    else:
        sc.cycles.device = 'CPU'
        sc.cycles.samples = 48 if FAST else 180
        sc.cycles.use_adaptive_sampling = True
        sc.cycles.adaptive_threshold = 0.02
        sc.cycles.use_denoising = True
        try:
            sc.cycles.denoiser = 'OPENIMAGEDENOISE'
        except Exception:
            pass
        sc.cycles.max_bounces = 12
        sc.cycles.transmission_bounces = 12
    print('[render] engine={} samples={}'.format(
        sc.render.engine,
        getattr(sc.eevee, 'taa_render_samples', None) if
        engine.startswith('BLENDER_EEVEE') else sc.cycles.samples))
    return engine


def render_to(path, cam, res=None):
    keep = (scene.render.resolution_x, scene.render.resolution_y)
    if res:
        scene.render.resolution_x, scene.render.resolution_y = res
    scene.camera = cam
    scene.render.filepath = path
    print('[render] -> {}'.format(path))
    bpy.ops.render.render(write_still=True)
    if res:
        scene.render.resolution_x, scene.render.resolution_y = keep
    if os.path.exists(path):
        print('[render] ok {:.0f} KB'.format(os.path.getsize(path) / 1024.0))
    else:
        print('[render] FAILED (no file)')


def fix_viewport_clip(clip_end=300000.0):
    """1 BU = 1 mm, so the factory clip_end of 1000 slices the model away."""
    n = 0
    for scr in bpy.data.screens:
        for a in scr.areas:
            if a.type == 'VIEW_3D':
                for sp in a.spaces:
                    if hasattr(sp, 'clip_start'):
                        sp.clip_start = 1.0
                        sp.clip_end = clip_end
                        n += 1
    for cd in bpy.data.cameras:
        cd.clip_start = 1.0
        cd.clip_end = clip_end
    print('[view] viewport clip fixed on {} spaces (end={})'.format(n, clip_end))


# ================================================================== MAIN ==
stage()
three_point()
cam, ctr, diag = cameras()
engine = render_settings()
fix_viewport_clip()

if not NORENDER:
    SQ = (1000, 1000)
    render_to(os.path.join(OUT_DIAG, '01_front_ortho.png'),
              bpy.data.objects['CAM_Diag_Front'], res=SQ)
    render_to(os.path.join(OUT_DIAG, '02_left_ortho.png'),
              bpy.data.objects['CAM_Diag_Left'], res=SQ)
    render_to(os.path.join(OUT_DIAG, '03_top_ortho.png'),
              bpy.data.objects['CAM_Diag_Top'], res=SQ)
    render_to(OUT_PREVIEW, cam)

    # --- mechanism-review shot: hide the two fixed front panels + the shell
    hidden = []
    for name in ('GEO_Front_L1_Bottom', 'GEO_Front_L3_Top',
                 'GEO_Shell_Side_L', 'GEO_Shell_Side_R'):
        o = bpy.data.objects.get(name)
        if o:
            hidden.append((o, o.hide_render))
            o.hide_render = True
    render_to(os.path.join(OUT_DIAG, '04_interior_review.png'),
              bpy.data.objects['CAM_OpenInterior'], res=(1400, 1000))
    for o, was in hidden:
        o.hide_render = was
    print('[render] interior review shot finished (panels restored)')

# ------------------------------------------------------------------ save --
G.freeze_transforms()
G.purge_orphans()
fix_viewport_clip()
bpy.ops.wm.save_as_mainfile(filepath=OUT_BLEND)
print('[save] {} ({:.0f} KB)'.format(OUT_BLEND,
                                     os.path.getsize(OUT_BLEND) / 1024.0))

# ------------------------------------------------------------- documents --
stats = G.report_mesh()
lines = []
lines.append('SmartBookShelf - STEP 1 structure tree and parameter table')
lines.append('=' * 74)
lines.append('')
lines.append('Units: 1 Blender unit = 1 mm, unit scale = %s' % P['UNIT_SCALE'])
lines.append('Axes : X width (+X right) | Y depth (+Y FRONT) | Z height')
lines.append('Origin: bottom centre')
lines.append('Scope: cabinet + drive columns + moving platform + book.')
lines.append('       limit plate / rear pusher / chute = later steps')
lines.append('')
lines.append('OBJECT TREE')
lines.append('-' * 74)
for cname in ('COL-01_Cabinet', 'COL-02_DriveColumns',
              'COL-03_MovingPlatform', 'COL-05_Book', 'COL-90_RenderStage'):
    c = bpy.data.collections.get(cname)
    lines.append('[%s]' % cname)
    if not c:
        continue
    for o in sorted(c.objects, key=lambda x: x.name):
        d = o.dimensions
        lines.append('   %-38s %7.1f x %7.1f x %7.1f  grp=%s' % (
            o.name, d.x, d.y, d.z, o.get('dsh_group', '-')))
lines.append('')
lines.append('PARAMETERS')
lines.append('-' * 74)
for k in sorted(P):
    lines.append('  %-22s = %s' % (k, P[k]))
lines.append('')
lines.append('CHECKS')
lines.append('-' * 74)
lines.append('  cross-group pairs tested : %d' % tested)
lines.append('  min clearance required   : %.1f mm' % P['MIN_CLEARANCE'])
lines.append('  clash gate               : PASSED')
lines.append('  mesh objects             : %d' % stats['objects'])
lines.append('  polygons                 : %d' % stats['polys'])
lines.append('  render engine            : %s' % scene.render.engine)
lines.append('  resolution               : %dx%d' % (RES[0], RES[1]))
lines.append('  animation                : NONE (no keyframes, single frame)')
txt = '\n'.join(lines)
with open(os.path.join(OUT_DOC, 'step1_structure_and_parameters.txt'), 'w',
          encoding='utf-8') as fh:
    fh.write(txt)
print(txt)
print('[done] step 1 complete')
