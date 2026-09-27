"""diag_views - structure QA renders (NOT deliverables).

    blender.exe --background --python diag_views.py -- <file.blend>

Renders orthographic front/side views and close-ups of every joint so the
mechanism can actually be checked by eye instead of only by numbers.
"""

import math
import os
import sys

import bpy
from mathutils import Vector

HERE = os.path.dirname(os.path.abspath(__file__))
if HERE not in sys.path:
    sys.path.insert(0, HERE)
import stage as ST                                  # noqa: E402

ROOT = os.path.dirname(HERE)
DIAG = os.path.join(ROOT, 'renders', 'diag')

SLIDER = ('GEO_Slider_Motor42', 'GEO_Slider_Coupling', 'GEO_Slider_LeadScrew',
          'GEO_Slider_Block', 'GEO_Slider_TopBearing')
RAIL = ('GEO_Rail_Body', 'GEO_Rail_Carriage')


def mech(scene):
    out = []
    for o in scene.objects:
        if o.type == 'MESH' and o.name.startswith('GEO_'):
            out.append(o)
    return out


def shot(scene, objs, name, keep, target, dirv, res, ortho_scale=1000.0,
         lens=85.0, ortho=True):
    for o in objs:
        o.hide_render = (keep is not None and o.name not in keep)
    cam = bpy.data.objects.get('CAM_DIAG')
    if cam is None:
        cd = bpy.data.cameras.new('CAM_DIAG')
        cam = bpy.data.objects.new('CAM_DIAG', cd)
        scene.collection.objects.link(cam)
    cam.data.type = 'ORTHO' if ortho else 'PERSP'
    cam.data.ortho_scale = ortho_scale
    cam.data.lens = lens
    cam.data.clip_start = 1.0
    cam.data.clip_end = 900000.0
    cam.location = Vector(target) + Vector(dirv).normalized() * 12000.0
    cam.rotation_euler = ST._aim_euler(cam.location, Vector(target))
    scene.camera = cam
    scene.render.resolution_x, scene.render.resolution_y = res
    scene.render.resolution_percentage = 100
    scene.render.image_settings.file_format = 'PNG'
    scene.render.filepath = os.path.join(DIAG, name)
    bpy.ops.render.render(write_still=True)
    print('  diag -> %s' % scene.render.filepath, flush=True)


def main():
    argv = sys.argv
    tail = argv[argv.index('--') + 1:] if '--' in argv else []
    blend = tail[0] if tail else os.path.join(
        ROOT, 'models', 'slider_rail_draft.blend')
    bpy.ops.wm.open_mainfile(filepath=blend)
    scene = bpy.context.scene
    os.makedirs(DIAG, exist_ok=True)

    objs = mech(scene)
    # read the real component origins out of the file instead of hard-coding
    es = scene.objects['Empty_Slider_Axis'].location
    er = scene.objects['Empty_Rail_Axis'].location
    SL = (es.x, es.y)
    RL = (er.x, er.y)
    print('diag: %d mechanical objects   slider %s  rail %s'
          % (len(objs), SL, RL), flush=True)
    ST.fix_viewport_clip()
    try:
        scene.render.engine = 'BLENDER_EEVEE_NEXT'
    except Exception:
        scene.render.engine = 'BLENDER_EEVEE'
    ee = getattr(scene, 'eevee', None)
    if ee is not None:
        ST._try(ee, 'taa_render_samples', 32)
        ST._try(ee, 'use_gtao', True)
        ST._try(ee, 'use_shadows', True)

    # ---- whole pair, straight front elevation ---------------------------
    shot(scene, objs, '00_pair_front.png', None, ((SL[0] + RL[0]) * 0.5, (SL[1] + RL[1]) * 0.5, 460.0),
         (0.0, 1.0, 0.0), (1600, 900), ortho_scale=1400.0)
    # ---- slider assembly, two orthographic elevations -------------------
    shot(scene, objs, '01_slider_front.png', SLIDER, (SL[0], SL[1], 450.0),
         (0.0, 1.0, 0.0), (520, 1180), ortho_scale=1000.0)
    shot(scene, objs, '02_slider_side.png', SLIDER, (SL[0], SL[1], 450.0),
         (1.0, 0.0, 0.0), (520, 1180), ortho_scale=1000.0)
    # ---- slider close-ups ------------------------------------------------
    shot(scene, objs, '03_slider_base_closeup.png', SLIDER,
         (SL[0], SL[1], 70.0), (0.0, 1.0, 0.0), (900, 700),
         ortho_scale=180.0)
    shot(scene, objs, '04_slider_block_closeup.png', SLIDER,
         (SL[0], SL[1], 500.0), (0.35, 1.0, 0.25), (900, 700),
         ortho_scale=150.0)
    shot(scene, objs, '05_slider_base_3q.png', SLIDER,
         (SL[0], SL[1], 90.0), (0.7, 1.0, 0.55), (900, 700),
         ortho_scale=230.0)
    # ---- rail assembly ---------------------------------------------------
    shot(scene, objs, '06_rail_front.png', RAIL,
         (RL[0], RL[1], 450.0), (0.0, 1.0, 0.0), (520, 1180),
         ortho_scale=1000.0)
    shot(scene, objs, '07_rail_side.png', RAIL,
         (RL[0], RL[1], 450.0), (1.0, 0.0, 0.0), (420, 1180),
         ortho_scale=1000.0)
    shot(scene, objs, '08_carriage_closeup.png', RAIL,
         (RL[0], RL[1], 500.0), (0.55, 1.0, 0.35), (900, 700),
         ortho_scale=170.0)
    # ---- a real 3/4 beauty check of the whole pair ------------------------
    shot(scene, objs, '09_pair_3q.png', None, ((SL[0] + RL[0]) * 0.5, (SL[1] + RL[1]) * 0.5, 450.0),
         (0.60, 0.67, 0.44), (1200, 675), ortho=False, lens=85.0)
    print('diag: done', flush=True)


if __name__ == '__main__':
    main()
