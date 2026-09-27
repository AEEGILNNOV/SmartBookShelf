# -*- coding: utf-8 -*-
"""
相机角度候选对比：为最终 1920x1080 预览图挑选最佳 3/4 视角。
Camera-angle candidates for the final hero render.

    blender -b --factory-startup -P cam_candidates.py -- <blend_path> <out_dir>
"""
import bpy
import sys
import os
import importlib

HERE = os.path.dirname(os.path.abspath(__file__))
if HERE not in sys.path:
    sys.path.insert(0, HERE)
_lib03 = importlib.import_module('_lib03')

argv = sys.argv
argv = argv[argv.index('--') + 1:] if '--' in argv else []
blend_path, out_dir = argv[0], argv[1]
os.makedirs(out_dir, exist_ok=True)

bpy.ops.wm.open_mainfile(filepath=blend_path)

# 复现主流程的相机/灯光设置
_lib03.build_lights()
cands = [
    ('a_az42_el32', 42.0, 32.0),
    ('b_az55_el22', 55.0, 22.0),
    ('c_az34_el26', 34.0, 26.0),
    ('d_az62_el30', 62.0, 30.0),
]
for name, az, el in cands:
    cam, info = _lib03.build_camera(None, azimuth=az, elevation=el,
                                    focal=48.0, margin=1.10)
    _lib03.render_preview(os.path.join(out_dir, 'cam_%s.png' % name),
                          1280, 720, 24)
    bpy.data.objects.remove(cam, do_unlink=True)
    print('[cam] %s dist=%.0f' % (name, info['distance']))
print('[cam] complete')
