# -*- coding: utf-8 -*-
"""
材质快速微调 + 渲染（用于视觉迭代，不重建几何）。
Fast material tuning + render for visual iteration.

    blender -b --factory-startup -P mat_tune.py -- <blend> <out_dir>
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


def tint(name, base=None, metallic=None, roughness=None, alpha=None):
    mat = bpy.data.materials.get(name)
    if mat is None:
        print('  missing', name)
        return
    b = next((n for n in mat.node_tree.nodes if n.type == 'BSDF_PRINCIPLED'), None)
    if b is None:
        return
    for key, val in (('Base Color', base), ('Metallic', metallic),
                     ('Roughness', roughness), ('Alpha', alpha)):
        if val is None:
            continue
        s = next((x for x in b.inputs if x.name == key), None)
        if s is not None:
            s.default_value = val


variants = {
    'v1_darkframe': dict(frame=(0.155, 0.170, 0.195, 1.0)),
    'v2_midframe': dict(frame=(0.235, 0.255, 0.290, 1.0)),
    'v3_darkframe_alu': dict(frame=(0.155, 0.170, 0.195, 1.0),
                             alu=(0.52, 0.545, 0.585, 1.0, 0.34)),
}

for vname, cfg in variants.items():
    # 复位基色
    tint('MAT_FrameDark', base=(0.385, 0.410, 0.450, 1.0), metallic=0.30,
         roughness=0.34)
    tint('MAT_FrameAlu', base=(0.70, 0.725, 0.765, 1.0), metallic=1.0,
         roughness=0.28)
    tint('MAT_Aluminum', base=(0.585, 0.610, 0.650, 1.0), metallic=1.0,
         roughness=0.30)
    if 'frame' in cfg:
        tint('MAT_FrameDark', base=cfg['frame'])
    if 'alu' in cfg:
        a = cfg['alu']
        tint('MAT_Aluminum', base=a[:4], metallic=1.0, roughness=a[4])
    tint('MAT_Backdrop', base=(0.44, 0.475, 0.535, 1.0))
    cam, info = _lib03.build_camera(None, azimuth=30.0, elevation=24.0,
                                    focal=58.0, margin=1.06)
    _lib03.render_preview(os.path.join(out_dir, 'tune_%s.png' % vname),
                          1280, 720, 24)
    bpy.data.objects.remove(cam, do_unlink=True)
    print('[tune]', vname, 'done')
print('[tune] complete')
