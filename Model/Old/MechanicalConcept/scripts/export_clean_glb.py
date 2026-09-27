# -*- coding: utf-8 -*-
"""
从 .blend 导出仅含机械本体的干净 GLB（删除地面/背板/相机/灯光）。
Export a clean GLB containing only the mechanical assembly.

    blender -b --factory-startup -P export_clean_glb.py -- <blend> <glb>
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
blend_path, glb_path = argv[0], argv[1]

bpy.ops.wm.open_mainfile(filepath=blend_path)
_lib03.drop_stage_props()
bpy.ops.wm.save_as_mainfile(filepath=blend_path, compress=False, copy=False)
_lib03.export_glb(glb_path, overwrite=True)

from mathutils import Vector
meshes = [o for o in bpy.data.objects if o.type in ('MESH', 'CURVE')]
mn = [1e9] * 3
mx = [-1e9] * 3
for o in meshes:
    for c in o.bound_box:
        w = o.matrix_world @ Vector(c)
        for i in range(3):
            mn[i] = min(mn[i], w[i])
            mx[i] = max(mx[i], w[i])
print('CLEAN_OBJECTS', len(meshes))
print('CLEAN_SIZE', [round(mx[i] - mn[i], 1) for i in range(3)])
print('CLEAN_MIN', [round(v, 1) for v in mn])
print('CLEAN_MAX', [round(v, 1) for v in mx])
