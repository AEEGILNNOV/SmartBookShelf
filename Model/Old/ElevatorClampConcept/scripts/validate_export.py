"""Re-import the exported GLB and verify the delivered model."""

import os
import sys

_HERE = os.path.dirname(os.path.abspath(__file__))
if _HERE not in sys.path:
    sys.path.insert(0, _HERE)

import bpy  # noqa: E402
from mathutils import Vector  # noqa: E402

PROJECT = os.path.dirname(_HERE)
GLB = os.path.join(PROJECT, 'models', 'elevator_clamp_v01.glb')

KEY = ['Push_Plate', 'Push_Rod', 'Push_Motor42_Body', 'Limit_Pad_Black',
       'Platform_Plate_Main', 'Book_Spine', 'Drive_Screw_Left',
       'Drive_Screw_Right', 'Drive_Rail_Left', 'Drive_Rail_Right',
       'Drive_Motor42_Left_Body', 'Drive_Motor42_Right_Body',
       'Platform_Carriage_Left', 'Platform_Carriage_Right',
       'Limit_Carriage_Left', 'Limit_Carriage_Right', 'Chute_Ramp',
       'Top_Hole_Bezel_Left', 'Front_Level_Top', 'Front_Level_Bottom']


def bounds(objs):
    pts = []
    for o in objs:
        for c in o.bound_box:
            pts.append(o.matrix_world @ Vector(c))
    return (min(p.x for p in pts), max(p.x for p in pts),
            min(p.y for p in pts), max(p.y for p in pts),
            min(p.z for p in pts), max(p.z for p in pts))


def main():
    for o in list(bpy.data.objects):
        bpy.data.objects.remove(o, do_unlink=True)
    bpy.ops.import_scene.gltf(filepath=GLB)
    meshes = [o for o in bpy.data.objects if o.type == 'MESH']
    names = {o.name.split('.')[0] for o in meshes}
    b = bounds(meshes)
    tris = sum(len(o.data.loop_triangles) if o.data.loop_triangles else 0
               for o in meshes)
    for o in meshes:
        o.data.calc_loop_triangles()
    tris = sum(len(o.data.loop_triangles) for o in meshes)
    mats = {m.name for o in meshes for m in o.data.materials if m}
    print('GLB           : %s (%d bytes)' % (GLB, os.path.getsize(GLB)))
    print('mesh objects  : %d' % len(meshes))
    print('triangles     : %d' % tris)
    print('materials     : %d -> %s' % (len(mats), sorted(mats)))
    print('bbox X        : %.1f .. %.1f  (%.1f mm)' % (b[0], b[1], b[1] - b[0]))
    print('bbox Y        : %.1f .. %.1f  (%.1f mm)' % (b[2], b[3], b[3] - b[2]))
    print('bbox Z        : %.1f .. %.1f  (%.1f mm)' % (b[4], b[5], b[5] - b[4]))
    missing = [k for k in KEY if k not in names]
    print('key parts     : %d/%d present' % (len(KEY) - len(missing), len(KEY)))
    if missing:
        print('MISSING       : %s' % missing)
    for name in ('Push_Plate', 'Push_Rod', 'Book_Spine', 'Limit_Pad_Black',
                 'Platform_Plate_Main'):
        for o in meshes:
            if o.name.split('.')[0] == name:
                bb = bounds([o])
                print('  %-20s X %.0f..%.0f  Y %.0f..%.0f  Z %.0f..%.0f'
                      % (name, bb[0], bb[1], bb[2], bb[3], bb[4], bb[5]))
                break
    print('GLB VALIDATION %s' % ('PASS' if not missing else 'FAIL'))
    if missing:
        sys.exit(1)


main()
