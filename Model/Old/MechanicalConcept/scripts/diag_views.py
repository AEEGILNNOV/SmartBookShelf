# -*- coding: utf-8 -*-
"""
诊断视图：从多个角度渲染装配体，用于机械结构核查。
Diagnostic multi-view render for structural QA.

    blender -b --factory-startup -P diag_views.py -- <blend_path> <out_dir>
"""
import bpy
import sys
import os
import math
import importlib
from mathutils import Vector

HERE = os.path.dirname(os.path.abspath(__file__))
if HERE not in sys.path:
    sys.path.insert(0, HERE)
_lib03 = importlib.import_module('_lib03')

argv = sys.argv
argv = argv[argv.index('--') + 1:] if '--' in argv else []
blend_path = argv[0]
out_dir = argv[1]
os.makedirs(out_dir, exist_ok=True)

bpy.ops.wm.open_mainfile(filepath=blend_path)

scene = bpy.context.scene
# 诊断模式下把柜体外壳隐藏，直接观察内部机构
if '--ghost' in argv:
    for o in bpy.data.objects:
        if o.name.startswith('Cabinet_') or o.name.startswith('Shelf_'):
            o.hide_render = True

views = [
    ('iso', 42.0, 32.0),
    ('front', 0.0, 6.0),
    ('left', -78.0, 8.0),
    ('right', 88.0, 8.0),
    ('back', 180.0, 10.0),
    ('top', 20.0, 68.0),
]
samples = 24
for name, az, el in views:
    cam, info = _lib03.build_camera(None, azimuth=az, elevation=el,
                                    focal=48.0, margin=1.06)
    _lib03.render_preview(os.path.join(out_dir, 'diag_%s.png' % name),
                          1280, 720, samples)
    bpy.data.objects.remove(cam, do_unlink=True)
    print('[diag] %s done  dist=%.0f' % (name, info['distance']))
print('[diag] complete')
