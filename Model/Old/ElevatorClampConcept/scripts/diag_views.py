"""Diagnostic renders - orthographic checks of the frozen push pose."""

import math
import os
import sys

_HERE = os.path.dirname(os.path.abspath(__file__))
if _HERE not in sys.path:
    sys.path.insert(0, _HERE)

import bpy  # noqa: E402
from mathutils import Vector  # noqa: E402

import scene_render  # noqa: E402

PROJECT = os.path.dirname(_HERE)
BLEND = os.path.join(PROJECT, 'models', 'elevator_clamp_v01.blend')
OUT = os.path.join(PROJECT, 'renders', 'diag')


def _try(o, a, v):
    try:
        setattr(o, a, v)
        return True
    except Exception:
        return False


def make_cam(name, loc, target, ortho_scale=None, lens=58.0):
    data = bpy.data.cameras.new(name)
    data.lens = lens
    data.clip_start = 1.0
    data.clip_end = 500000.0
    if ortho_scale:
        data.type = 'ORTHO'
        data.ortho_scale = ortho_scale
    obj = bpy.data.objects.new(name, data)
    bpy.context.scene.collection.objects.link(obj)
    obj.location = Vector(loc)
    obj.rotation_euler = scene_render._look_at(Vector(loc), Vector(target))
    return obj


def main():
    argv = sys.argv[sys.argv.index('--') + 1:] if '--' in sys.argv else []
    hide_shell = 'noshell' in argv
    if not os.path.exists(BLEND):
        print('missing blend:', BLEND)
        sys.exit(1)
    bpy.ops.wm.open_mainfile(filepath=BLEND)
    scene = bpy.context.scene
    scene.render.engine = 'CYCLES'
    scene.cycles.samples = 48
    scene.cycles.use_denoising = True
    scene.render.resolution_x = 760
    scene.render.resolution_y = 760
    scene.render.image_settings.file_format = 'PNG'

    if hide_shell:
        for o in bpy.data.objects:
            if o.get('grp') == 'cabinet' and o.name.startswith(('Shell_',
                                                               'Front_',
                                                               'Top_Plate')):
                o.hide_render = True

    views = [
        ('01_front_ortho', (0.0, 4200.0, 620.0), (0.0, 0.0, 620.0), 1500.0),
        ('02_left_ortho', (-4200.0, 60.0, 620.0), (0.0, 60.0, 620.0), 1500.0),
        ('03_top_ortho', (0.0, -40.0, 4200.0), (0.0, -40.0, 600.0), 900.0),
        ('04_port_closeup', (1500.0, 1700.0, 1250.0), (0.0, -60.0, 610.0),
         None),
    ]
    os.makedirs(OUT, exist_ok=True)
    for name, loc, tgt, osc in views:
        cam = make_cam('CAM_DIAG_' + name, loc, tgt, osc)
        scene.camera = cam
        scene.render.filepath = os.path.join(OUT, name + '.png')
        bpy.ops.render.render(write_still=True)
        print('  diag %s -> %s' % (name, scene.render.filepath))
    print('DIAG DONE')


main()
