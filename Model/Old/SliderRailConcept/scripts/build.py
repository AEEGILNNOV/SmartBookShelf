"""build - SliderRailConcept main entry point.

    blender.exe --background --factory-startup --python build.py -- [flags]

Flags:  fast      EEVEE + 960x540 draft (iteration)
        cycles    force Cycles (the default for the delivered image)
        norender  model + gates + .blend only (about 10 s, daily use)
        noglb     skip the portable GLB export
"""

import math
import os
import sys
import time

import bpy
from mathutils import Matrix, Vector

HERE = os.path.dirname(os.path.abspath(__file__))
if HERE not in sys.path:
    sys.path.insert(0, HERE)

import clash                                    # noqa: E402
import config                                   # noqa: E402
import geomlib as G                             # noqa: E402
import materials as MAT                         # noqa: E402
import parts as PARTS                           # noqa: E402
import stage as ST                              # noqa: E402

ROOT = os.path.dirname(HERE)
MODELS = os.path.join(ROOT, 'models')
RENDERS = os.path.join(ROOT, 'renders')
DIAG = os.path.join(RENDERS, 'diag')
DOCS = os.path.join(ROOT, 'docs')


def flags():
    argv = sys.argv
    return set(argv[argv.index('--') + 1:]) if '--' in argv else set()


def log(msg=''):
    print(msg, flush=True)


# --------------------------------------------------------------------------
def new_collection(name, parent):
    c = bpy.data.collections.new(name)
    parent.children.link(c)
    return c


def new_empty(name, loc, coll, size=140.0):
    o = bpy.data.objects.new(name, None)
    o.empty_display_type = 'PLAIN_AXES'
    o.empty_display_size = size
    o.location = Vector(loc)
    coll.objects.link(o)
    return o


# --------------------------------------------------------------------------
def main():
    t0 = time.time()
    F = flags()
    fast = 'fast' in F
    norender = 'norender' in F
    if 'eevee' in F:
        engine = 'EEVEE'
    else:
        engine = 'CYCLES'          # drafts use the delivered engine too
    samples = 48 if fast else 144
    res = (880, 495) if fast else (1920, 1080)

    P = config.P
    config.derive(P)

    log('=' * 78)
    log('SliderRailConcept  -  lead-screw slide + linear rail, concept model')
    log('=' * 78)

    # ---------------------------------------------------------------- [S1]
    bpy.ops.wm.read_factory_settings(use_empty=True)
    scene = bpy.context.scene
    scene.frame_start = 1
    scene.frame_end = 1
    scene.frame_current = 1
    scene.unit_settings.system = 'METRIC'
    scene.unit_settings.scale_length = P['UNIT_SCALE']
    scene.unit_settings.length_unit = 'MILLIMETERS'
    log('[S1] scene reset   units = metric, scale_length = %g (1 BU = 1 mm)'
        % P['UNIT_SCALE'])

    # ---------------------------------------------------------------- [S2]
    M = MAT.build_materials()
    log('[S2] materials     %d Principled materials' % len(M))

    # ---------------------------------------------------------------- [S3]
    col_sl = new_collection('COL-01_Slider_Assembly', scene.collection)
    col_rl = new_collection('COL-02_Rail_Assembly', scene.collection)
    col_stg = new_collection('COL-90_RenderStage', scene.collection)
    col_cut = new_collection('COL-99_BooleanCutters', scene.collection)
    sub = {
        'motor': new_collection('COL-01-A_Motor', col_sl),
        'coupling': new_collection('COL-01-B_Coupling', col_sl),
        'screw': new_collection('COL-01-C_LeadScrew', col_sl),
        'block': new_collection('COL-01-D_SliderBlock', col_sl),
        'top_support': new_collection('COL-01-E_TopBearing', col_sl),
        'rail': new_collection('COL-02-A_RailBody', col_rl),
        'carriage': new_collection('COL-02-B_RailCarriage', col_rl),
    }
    empty_sl = new_empty('Empty_Slider_Axis', P['ORIGIN_SLIDER'], col_sl)
    empty_rl = new_empty('Empty_Rail_Axis', P['ORIGIN_RAIL'], col_rl)
    empties = {'SLIDER': empty_sl, 'RAIL': empty_rl}
    log('[S3] collections   COL-01_Slider_Assembly / COL-02_Rail_Assembly / '
        'COL-90_RenderStage')
    log('     empties       Empty_Slider_Axis %s   Empty_Rail_Axis %s'
        % (tuple(round(v, 1) for v in P['ORIGIN_SLIDER']),
           tuple(round(v, 1) for v in P['ORIGIN_RAIL'])))

    # ---------------------------------------------------------------- [S4]
    A = {}
    A.update(PARTS.build_slider(P, sub, col_cut, M))
    log('[S4] Slider_Assembly     5 parts  (motor / coupling / screw / '
        'block / top bearing)')

    # ---------------------------------------------------------------- [S5]
    A.update(PARTS.build_rail(P, sub, col_cut, M))
    log('[S5] Linear_Rail_Assembly 2 parts  (rail body / carriage)')

    # ---------------------------------------------------------------- [S6]
    for o in A.values():
        target = empty_sl if o['dsh_group'] in clash.SLIDER_GROUPS else empty_rl
        o.parent = target
        o.matrix_parent_inverse = Matrix.Identity(4)
    parts = [A[k] for k in ('motor', 'coupling', 'screw', 'block')]
    if 'top_support' in A:
        parts.append(A['top_support'])
    parts += [A['rail'], A['carriage']]
    log('[S6] parenting     7 parts -> 2 Empties (independent transforms kept)')

    tris = sum(sum(len(p.vertices) - 2 for p in o.data.polygons) for o in parts)
    verts = sum(len(o.data.vertices) for o in parts)
    log('     geometry      %d objects, %d verts, %d tris'
        % (len(parts), verts, tris))

    # ---------------------------------------------------------------- [S7]
    aabb_fails, clashes = clash.run_gates(P, parts, log=log)

    # ---------------------------------------------------------------- [S8]
    bad, rows = clash.assert_design(P, parts, empties, log=log)

    # ---------------------------------------------------------------- [S9]
    txt = clash.structure_text(P, parts, empties)
    os.makedirs(DOCS, exist_ok=True)
    with open(os.path.join(DOCS, 'structure_tree_and_parameters.txt'), 'w',
              encoding='utf-8') as fh:
        fh.write(txt + '\n')

    if aabb_fails or clashes or bad:
        log('')
        log('!' * 78)
        if aabb_fails:
            log('!!! ANTI-CLASH GATE 1 FAILED - %d pair(s) below the %.1f mm '
                'minimum' % (len(aabb_fails), P['MIN_CLEARANCE']))
            for n1, n2, gap in aabb_fails:
                log('      %s <-> %s   gap %.3f mm' % (n1, n2, gap))
        if clashes:
            log('!!! ANTI-CLASH GATE 2 FAILED - %d interpenetrating pair(s)'
                % len(clashes))
            for n1, n2, n in clashes:
                log('      %s <-> %s   %d intersecting triangles' % (n1, n2, n))
        if bad:
            log('!!! DESIGN ASSERTIONS FAILED - %d assertion(s)' % bad)
        log('!!! build terminated, nothing written')
        log('!' * 78)
        raise SystemExit(2)

    # --------------------------------------------------------------- [S10]
    ST.setup_world(scene)
    stage_objs = ST.build_stage(P, col_stg, M)
    pts, lo, hi = ST.subject_points(parts)
    T = Vector(((lo[0] + hi[0]) * 0.5, (lo[1] + hi[1]) * 0.5,
                (lo[2] + hi[2]) * 0.5))
    cam = ST.make_camera(scene, col_stg, lens=85.0)
    cam, dist = ST.fit_camera(scene, cam, pts, az_deg=P['CAM_AZ'],
                              el_deg=P['CAM_EL'], margin=0.93, dist=9000.0,
                              shift_y=0.0)
    ST.three_point(P, col_stg, T, P['CAM_AZ'])
    log('')
    log('[S10] camera       lens 85 mm  az %.0f deg  el %.0f deg  dist %.0f mm'
        % (P['CAM_AZ'], P['CAM_EL'], dist))
    log('      subject bbox %.0f x %.0f x %.0f mm  centre (%.0f, %.0f, %.0f)'
        % (hi[0] - lo[0], hi[1] - lo[1], hi[2] - lo[2],
           T.x, T.y, T.z))

    ST.configure_render(scene, res[0], res[1], samples, engine)
    log('      compositor   %s' % ('fog glow on' if
        ST.setup_compositor(scene) else 'off'))
    log('      engine       %s  %dx%d  %d samples  denoise on  AO on'
        % (engine, res[0], res[1], samples))

    # --------------------------------------------------------------- [S11]
    n_clip = ST.fix_viewport_clip()
    log('      viewport     clip_start/end patched on %d 3D viewports '
        '(persisted before save)' % n_clip)

    tag = 'draft' if fast else 'v01'
    blend = os.path.join(MODELS, 'slider_rail_%s.blend' % tag)
    png = os.path.join(RENDERS, ('draft_%dx%d.png' % res if fast else
                                 'slider_rail_preview_1920x1080.png'))
    ST.save_blend(blend)
    log('[S11] saved        %s' % blend)

    if not norender:
        t1 = time.time()
        ST.render_to(scene, png)
        log('      rendered     %s  (%.1f s)' % (png, time.time() - t1))
        # the viewport clip fix must survive the render+saves
        ST.fix_viewport_clip()
        ST.save_blend(blend)

    if not norender and 'noglb' not in F:
        glb = os.path.join(MODELS, 'slider_rail_%s.glb' % tag)
        ST.export_glb(glb, parts)
        log('      exported     %s (%.1f MB)'
            % (glb, os.path.getsize(glb) / 1e6))

    log('')
    log('ALL GATES PASSED  -  total %.1f s' % (time.time() - t0))
    log('structure + parameter table -> docs/structure_tree_and_parameters.txt')


if __name__ == '__main__':
    main()
