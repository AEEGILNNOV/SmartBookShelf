"""Elevator-clamp smart bookshelf - concept model build entry point.

Usage:
    blender --background --factory-startup --python build.py
    blender --background --factory-startup --python build.py -- fast
    blender --background --factory-startup --python build.py -- norender

Stage order:  parameters -> materials -> geometry -> anti-clash gate ->
              structure report -> .blend -> .glb -> render.
No animation of any kind is created.
"""

import os
import sys

_HERE = os.path.dirname(os.path.abspath(__file__))
if _HERE not in sys.path:
    sys.path.insert(0, _HERE)

import bpy  # noqa: E402

import anti_clash  # noqa: E402
import geom  # noqa: E402
import mat_lib  # noqa: E402
import params  # noqa: E402
import parts  # noqa: E402
import scene_render  # noqa: E402

PROJECT = os.path.dirname(_HERE)
MODELS = os.path.join(PROJECT, 'models')
RENDERS = os.path.join(PROJECT, 'renders')
DOCS = os.path.join(PROJECT, 'docs')

GROUP_COLLECTION = [
    ('cabinet', 'COL-01_Cabinet'),
    ('drive', 'COL-02_DriveColumns'),
    ('platform', 'COL-03_MovingPlatform'),
    ('limit', 'COL-04_LimitPlate'),
    ('push', 'COL-05_RearPushUnit'),
    ('book', 'COL-06_Book'),
    ('chute', 'COL-07_TakeOffChute'),
    ('stage', 'COL-90_RenderStage'),
]

GROUPS = {
    'cabinet': parts.build_cabinet,
    'drive': parts.build_drive,
    'platform': parts.build_platform,
    'limit': parts.build_limit,
    'push': parts.build_push,
    'book': parts.build_book,
    'chute': parts.build_chute,
}


# --------------------------------------------------------------- reporting --
def structure_tree():
    lines = ['SmartBookshelf_ElevatorClamp_Root   (origin = bottom centre, '
             'unit = mm)']
    total = 0
    for grp, cname in GROUP_COLLECTION:
        coll = bpy.data.collections.get(cname)
        if coll is None:
            continue
        names = sorted(o.name for o in coll.objects)
        total += len(names)
        lines.append('  %s   [%s]  %d objects' % (cname, grp, len(names)))
        for i, n in enumerate(names):
            lines.append('      %s %s' % ('+-' if i < len(names) - 1 else
                                          '\\-', n))
    lines.append('  TOTAL mesh objects: %d' % total)
    return lines


def parameter_table(P, keys):
    lines = ['%-28s %12s  %s' % ('PARAMETER', 'VALUE', 'UNIT/NOTE')]
    lines.append('-' * 62)
    for k in keys:
        if k not in P:
            continue
        v = P[k]
        if isinstance(v, float):
            lines.append('%-28s %12s' % (k, ('%.4g' % v)))
        elif isinstance(v, int):
            lines.append('%-28s %12d' % (k, v))
        elif isinstance(v, (tuple, list)) and len(v) == 2 and \
                all(isinstance(x, (int, float)) for x in v):
            lines.append('%-28s %12s' % (k, '(%.1f, %.1f)' % (v[0], v[1])))
        elif isinstance(v, str):
            lines.append('%-28s %12s' % (k, v))
    return lines


# --------------------------------------------------------------------- main --
def main():
    argv = sys.argv[sys.argv.index('--') + 1:] if '--' in sys.argv else []
    fast = 'fast' in argv
    do_render = 'norender' not in argv
    do_glb = 'noglb' not in argv
    engine = 'CYCLES' if 'cycles' in argv else 'EEVEE'
    res = (960, 540) if fast else (1920, 1080)
    samples = 64 if fast else (160 if engine == 'CYCLES' else 160)

    print('=' * 78)
    print('SMART BOOKSHELF - OPTION A: ELEVATOR CLAMP CONCEPT MODEL')
    print('Blender %s | unit 1 = 1 mm | unit scale 0.001 | no animation'
          % bpy.app.version_string)
    print('=' * 78)

    geom.reset_scene()
    P = params.derive(dict(params.P))
    M = mat_lib.build_all()

    root_coll = geom.root_collection()
    root_coll['unit'] = 'mm'
    root_coll['unit_scale'] = P['UNIT_SCALE']
    root_empty = bpy.data.objects.new('SmartBookshelf_ElevatorClamp_Root',
                                      None)
    root_empty.empty_display_size = 200.0
    bpy.context.scene.collection.objects.link(root_empty)

    objs_by_group = {}
    for grp, cname in GROUP_COLLECTION:
        if grp == 'stage':
            continue
        coll = geom.get_collection(cname, root_coll)
        built = GROUPS[grp](P, M, coll)
        objs_by_group[grp] = built
        for o in built:
            o.parent = root_empty
        print('  built %-9s -> %-26s %3d objects' % (grp, cname, len(built)))

    model_objs = [o for g in objs_by_group.values() for o in g]

    # ------------------------------------------------ anti-clash gate
    report = anti_clash.run(P, model_objs)
    passed = anti_clash.print_report(report)

    print()
    print('PARAMETER TABLE')
    print('\n'.join(parameter_table(P, sorted(P.keys()))))
    print()
    print('STRUCTURE TREE')
    print('\n'.join(structure_tree()))

    os.makedirs(DOCS, exist_ok=True)
    with open(os.path.join(DOCS, 'structure_tree_and_parameters.txt'), 'w',
              encoding='utf-8') as fh:
        fh.write('\n'.join(structure_tree()))
        fh.write('\n\n')
        fh.write('\n'.join(parameter_table(P, sorted(P.keys()))))
        fh.write('\n')

    if not passed:
        print('BUILD ABORTED: anti-clash validation failed. Adjust the '
              'parameters and re-run.')
        sys.exit(1)

    # ------------------------------------------------ render stage
    stage_coll = geom.get_collection('COL-90_RenderStage', root_coll)
    stage_objs = scene_render.build_stage(P, M, stage_coll)
    lights = scene_render.three_point(P, M, stage_coll)

    scene = bpy.context.scene
    cam = scene_render.make_camera(scene, stage_coll)
    pts = scene_render.scene_points(model_objs)
    cam, dist = scene_render.fit_camera(scene, cam, pts, az_deg=40.0,
                                        el_deg=30.0, margin=0.95)
    print('  camera: 58 mm, az 40 deg, el 30 deg, distance %.0f mm'
          % dist)
    scene_render.configure_render(scene, res[0], res[1], samples, engine)
    print('  render engine: %s @ %dx%d, %d samples'
          % (engine, res[0], res[1], samples))

    # ------------------------------------------------ deliverables
    # the viewport clip fix has to be in place BEFORE the file is written,
    # otherwise the saved .blend keeps clip_end = 1000 and zooming out
    # makes the whole machine vanish (looks like a black screen)
    patched = scene_render.fix_viewport_clip()
    print('  viewport clip: patched %d 3D viewport(s) -> clip_start %.0f / '
          'clip_end %.0f' % (patched, scene_render.VIEW_CLIP_START,
                             scene_render.VIEW_CLIP_END))
    blend = scene_render.save_blend(os.path.join(MODELS,
                                                 'elevator_clamp_v01.blend'))
    print('  saved  %s' % blend)
    if do_glb:
        glb = scene_render.export_glb(os.path.join(MODELS,
                                                   'elevator_clamp_v01.glb'),
                                      model_objs)
        print('  saved  %s (%d bytes)' % (glb, os.path.getsize(glb)))
    if do_render:
        out = os.path.join(RENDERS, 'preview_1920x1080.png' if not fast
                           else 'preview_fast.png')
        scene_render.render_to(scene, out)
        print('  saved  %s (%d bytes)' % (out, os.path.getsize(out)))
    print('=' * 78)
    print('DONE' if passed else 'FAILED')
    print('=' * 78)


main()
