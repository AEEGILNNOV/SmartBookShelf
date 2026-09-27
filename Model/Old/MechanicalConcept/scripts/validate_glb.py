# -*- coding: utf-8 -*-
"""GLB 回读校验：干净进程重新导入并核查几何/材质/层级。"""
import bpy
import sys
import os

argv = sys.argv
argv = argv[argv.index('--') + 1:] if '--' in argv else []
glb = argv[0]

for o in list(bpy.data.objects):
    bpy.data.objects.remove(o, do_unlink=True)

bpy.ops.import_scene.gltf(filepath=glb)
meshes = [o for o in bpy.data.objects if o.type == 'MESH']
tris = sum(len(o.data.polygons) for o in meshes)
mats = set()
for o in meshes:
    for ms in o.data.materials:
        if ms:
            mats.add(ms.name)
mn = [1e9] * 3
mx = [-1e9] * 3
for o in meshes:
    for c in o.bound_box:
        w = o.matrix_world @ __import__('mathutils').Vector(c)
        for i in range(3):
            mn[i] = min(mn[i], w[i])
            mx[i] = max(mx[i], w[i])
roots = [o for o in bpy.data.objects if o.parent is None]
print('GLB_OBJECTS', len(bpy.data.objects))
print('GLB_MESHES', len(meshes), 'POLYS', tris)
print('GLB_MATERIALS', len(mats))
print('GLB_BBOX_MIN', [round(v, 1) for v in mn])
print('GLB_BBOX_MAX', [round(v, 1) for v in mx])
print('GLB_SIZE', [round(mx[i] - mn[i], 1) for i in range(3)])
print('GLB_ROOTS', [o.name for o in roots][:10])
names = sorted(o.name for o in meshes)
for key in ('Motor_Z_42', 'Motor_Y_42', 'Screw_Z', 'Belt_Mechanism',
            'Push_Plate', 'Wedge_Block', 'Guide_Rail_Right', 'Camera_Top',
            'Ultrasonic_Top', 'Main_Platform'):
    hit = [n for n in names if key.lower() in n.lower()]
    print('GLB_KEY %-22s %d  %s' % (key, len(hit), hit[:3]))
