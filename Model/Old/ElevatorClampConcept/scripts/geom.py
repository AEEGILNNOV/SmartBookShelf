"""Small deterministic geometry helpers built on bmesh (no bpy.ops).

Everything is created by hand so the build is reproducible in background mode
and never depends on operator context.
"""

import math

import bmesh
import bpy
from mathutils import Matrix, Vector

AXIS_ROT = {
    'X': Matrix.Rotation(math.pi / 2.0, 4, 'Y'),
    'Y': Matrix.Rotation(-math.pi / 2.0, 4, 'X'),
    'Z': Matrix.Identity(4),
}

CUBE_FACES = ((0, 1, 2, 3), (4, 5, 6, 7), (0, 1, 5, 4),
              (1, 2, 6, 5), (2, 3, 7, 6), (3, 0, 4, 7))


def reset_scene():
    """Drop every object and unused datablock - fresh build only."""
    for obj in list(bpy.data.objects):
        bpy.data.objects.remove(obj, do_unlink=True)
    for coll in list(bpy.data.collections):
        bpy.data.collections.remove(coll)
    for block in (bpy.data.meshes, bpy.data.materials, bpy.data.lights,
                  bpy.data.cameras):
        for item in list(block):
            if item.users == 0:
                block.remove(item)


def get_collection(name, parent=None):
    coll = bpy.data.collections.get(name)
    if coll is None:
        coll = bpy.data.collections.new(name)
        (parent or bpy.context.scene.collection).children.link(coll)
    return coll


def root_collection():
    return get_collection('COL-SmartBookshelf_ElevatorClamp')


def _finish(name, bm, coll, material, grp, part, matrix):
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
    me = bpy.data.meshes.new(name)
    bm.to_mesh(me)
    bm.free()
    me.validate()
    obj = bpy.data.objects.new(name, me)
    coll.objects.link(obj)
    if material is not None:
        me.materials.append(material)
    if matrix is not None:
        obj.matrix_world = matrix
    obj['grp'] = grp
    obj['part'] = part or name
    return obj


def box(name, bounds, coll, material=None, grp='misc', part=None,
        matrix=None):
    """Axis aligned box from ((x0, x1), (y0, y1), (z0, z1))."""
    (x0, x1), (y0, y1), (z0, z1) = bounds
    bm = bmesh.new()
    verts = [bm.verts.new(v) for v in (
        (x0, y0, z0), (x1, y0, z0), (x1, y1, z0), (x0, y1, z0),
        (x0, y0, z1), (x1, y0, z1), (x1, y1, z1), (x0, y1, z1))]
    for f in CUBE_FACES:
        bm.faces.new([verts[i] for i in f])
    return _finish(name, bm, coll, material, grp, part, matrix)


def box_local(name, size, coll, material=None, grp='misc', part=None,
              matrix=None):
    """Box centred on the origin, then placed by 'matrix'."""
    (sx, sy, sz) = size
    return box(name, ((-sx / 2.0, sx / 2.0), (-sy / 2.0, sy / 2.0),
                      (-sz / 2.0, sz / 2.0)), coll, material, grp, part,
               matrix)


def cyl(name, center, axis, radius, length, coll, material=None,
        grp='misc', segments=32, part=None):
    """Cylinder centred on 'center', its axis along 'axis'."""
    bm = bmesh.new()
    mat = Matrix.Translation(Vector(center)) @ AXIS_ROT[axis]
    bmesh.ops.create_cone(bm, cap_ends=True, cap_tris=False,
                          segments=segments, radius1=radius, radius2=radius,
                          depth=length, matrix=mat)
    return _finish(name, bm, coll, material, grp, part, None)


def world_bbox(obj):
    """Axis aligned world bounds of a mesh object: ((x0,x1),(y0,y1),(z0,z1))."""
    mw = obj.matrix_world
    pts = [mw @ Vector(c) for c in obj.bound_box]
    return ((min(p.x for p in pts), max(p.x for p in pts)),
            (min(p.y for p in pts), max(p.y for p in pts)),
            (min(p.z for p in pts), max(p.z for p in pts)))


def bbox_size(b):
    return tuple(b[i][1] - b[i][0] for i in range(3))


def bbox_center(b):
    return tuple((b[i][0] + b[i][1]) / 2.0 for i in range(3))


def reframe_mm(strings):
    return strings
