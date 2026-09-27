"""Geometry + material helpers shared by every build step.

Conventions
-----------
* 1 Blender unit = 1 mm.
* Every mesh is named GEO-<Group>_<Part>_<Side> so parts are greppable.
* box() takes MIN/MAX corners - no centring arithmetic at call sites.
* Objects are linked into exactly one named collection.
"""

import bpy
import bmesh
from mathutils import Vector, Euler


# ================================================================ scene ====
def reset_scene(unit_scale=0.001):
    """Factory-clean scene with metric units and 1 BU = 1 mm."""
    bpy.ops.wm.read_factory_settings(use_empty=True)
    sc = bpy.context.scene
    sc.unit_settings.system = 'METRIC'
    sc.unit_settings.scale_length = unit_scale
    sc.unit_settings.length_unit = 'MILLIMETERS'
    sc.render.fps = 24
    sc.frame_start = 1
    sc.frame_end = 1
    return sc


def ensure_collection(name, parent=None):
    if name in bpy.data.collections:
        return bpy.data.collections[name]
    col = bpy.data.collections.new(name)
    (parent or bpy.context.scene.collection).children.link(col)
    return col


def link(obj, coll):
    for c in list(obj.users_collection):
        c.objects.unlink(obj)
    coll.objects.link(obj)
    return obj


# ============================================================ primitives ===
def _finish(obj, coll, mat, bevel):
    link(obj, coll)
    if mat is not None:
        obj.data.materials.append(mat)
    if bevel and bevel > 0.0:
        m = obj.modifiers.new('Bevel', 'BEVEL')
        m.width = bevel
        m.segments = 2
        m.limit_method = 'ANGLE'
        m.angle_limit = 0.5236            # 30 deg
    return obj


def box(name, x0, x1, y0, y1, z0, z1, coll, mat=None, bevel=0.6):
    """Axis-aligned box from explicit min/max corners (mm)."""
    if x1 < x0: x0, x1 = x1, x0
    if y1 < y0: y0, y1 = y1, y0
    if z1 < z0: z0, z1 = z1, z0
    dx, dy, dz = x1 - x0, y1 - y0, z1 - z0
    me = bpy.data.meshes.new(name)
    bm = bmesh.new()
    bmesh.ops.create_cube(bm, size=1.0)
    bmesh.ops.scale(bm, vec=Vector((dx, dy, dz)), verts=bm.verts)
    bmesh.ops.translate(bm, vec=Vector(((x0 + x1) / 2.0,
                                        (y0 + y1) / 2.0,
                                        (z0 + z1) / 2.0)), verts=bm.verts)
    bm.to_mesh(me)
    bm.free()
    obj = bpy.data.objects.new(name, me)
    return _finish(obj, coll, mat, bevel)


def cyl(name, a0, a1, radius, coll, mat=None, axis='Z', segments=32,
        bevel=0.4, u=0.0, v=0.0):
    """Cylinder spanning a0..a1 along 'axis'.

    axis='Z' -> a0/a1 are Z, and 'u'/'v' are the (X, Y) centre
    axis='Y' -> a0/a1 are Y, and 'u'/'v' are the (X, Z) centre
    axis='X' -> a0/a1 are X, and 'u'/'v' are the (Y, Z) centre
    """
    a0, a1 = float(a0), float(a1)
    length = abs(a1 - a0)
    if length <= 0.0:
        length = 1e-4
    me = bpy.data.meshes.new(name)
    bm = bmesh.new()
    bmesh.ops.create_cone(bm, cap_ends=True, cap_tris=False,
                          segments=segments, radius1=radius, radius2=radius,
                          depth=length)
    if axis == 'Y':
        bmesh.ops.rotate(bm, verts=bm.verts, cent=(0, 0, 0),
                         matrix=Euler((-1.5707963268, 0.0, 0.0), 'XYZ').to_matrix())
    elif axis == 'X':
        bmesh.ops.rotate(bm, verts=bm.verts, cent=(0, 0, 0),
                         matrix=Euler((0.0, 1.5707963268, 0.0), 'XYZ').to_matrix())
    mid = (a0 + a1) / 2.0
    if axis == 'Z':
        loc = (u, v, mid)
    elif axis == 'Y':
        loc = (u, mid, v)
    else:
        loc = (mid, u, v)
    bmesh.ops.translate(bm, vec=Vector(loc), verts=bm.verts)
    bm.to_mesh(me)
    bm.free()
    obj = bpy.data.objects.new(name, me)
    return _finish(obj, coll, mat, bevel)


# ============================================================== metadata ===
def tag(obj, group, part, notes=''):
    obj['dsh_group'] = group
    obj['dsh_part'] = part
    if notes:
        obj['dsh_notes'] = notes
    return obj


def freeze_transforms():
    """Apply scale so downstream bbox maths is exact."""
    for o in bpy.data.objects:
        if o.type == 'MESH' and any(abs(s - 1.0) > 1e-9 for s in o.scale):
            bpy.context.view_layer.objects.active = o
            o.select_set(True)
            bpy.ops.object.transform_apply(location=False, rotation=False,
                                           scale=True)
            o.select_set(False)


def purge_orphans():
    for block in (bpy.data.meshes, bpy.data.materials):
        for b in list(block):
            if b.users == 0:
                block.remove(b)


def report_mesh():
    meshes = [o for o in bpy.data.objects if o.type == 'MESH']
    tris = sum(len(o.data.polygons) for o in meshes)
    return {'objects': len(meshes), 'polys': tris}
