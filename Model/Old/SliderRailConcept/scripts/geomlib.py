"""geomlib - deterministic bmesh primitive builders.

Every mechanical part is ONE mesh object assembled from several beveled
primitives, each primitive tagged with its own material slot.  Keeping a
logical part in a single object means:
  * no fake "0 mm gap" between a bolt head and the plate it seats on,
  * a clean, unambiguous set of bodies for the anti-clash gate,
  * one object per named component in the Outliner, as the brief asks for.
"""

import math

import bmesh
import bpy
from mathutils import Matrix, Vector

TAU = 2.0 * math.pi


# --------------------------------------------------------------------------
def _bevel(bm, width, segments, profile=0.5):
    if width <= 0.0:
        return
    geom = list(bm.verts) + list(bm.edges) + list(bm.faces)
    try:
        bmesh.ops.bevel(bm, geom=geom, offset=width, offset_type='OFFSET',
                        segments=segments, profile=profile, affect='EDGES',
                        clamp_overlap=True)
    except TypeError:
        try:
            bmesh.ops.bevel(bm, geom=geom, offset=width, segments=segments,
                            clamp_overlap=True)
        except Exception:
            pass
    except Exception:
        pass


def _xform(bm, loc=(0.0, 0.0, 0.0), rot=None):
    if loc == (0.0, 0.0, 0.0) and not rot:
        return
    m = Matrix.Translation(Vector(loc))
    if rot:
        for axis, deg in rot:
            m = m @ Matrix.Rotation(math.radians(deg), 4, axis)
    bmesh.ops.transform(bm, matrix=m, verts=list(bm.verts))


# --------------------------------------------------------------------------
class Part:
    """Accumulates primitives into one bmesh, one material slot per material."""

    def __init__(self, name):
        self.name = name
        self.bm = bmesh.new()
        self.slots = []
        self._slot = {}

    # -- material slots ----------------------------------------------------
    def slot(self, mat_name):
        if mat_name not in self._slot:
            self._slot[mat_name] = len(self.slots)
            self.slots.append(mat_name)
        return self._slot[mat_name]

    # -- merge a temporary bmesh into the part ------------------------------
    def _merge(self, tbm, mat_name, bevel=0.0, bseg=2, smooth=False):
        if bevel > 0.0:
            _bevel(tbm, bevel, bseg)
        bmesh.ops.recalc_face_normals(tbm, faces=list(tbm.faces))
        mi = self.slot(mat_name)
        tbm.verts.ensure_lookup_table()
        vmap = {}
        for v in tbm.verts:
            vmap[v] = self.bm.verts.new(v.co)
        self.bm.verts.ensure_lookup_table()
        for f in tbm.faces:
            try:
                nf = self.bm.faces.new([vmap[v] for v in f.verts])
            except ValueError:
                continue
            nf.material_index = mi
            nf.smooth = smooth
        tbm.free()

    # -- primitives --------------------------------------------------------
    def box(self, x0, x1, y0, y1, z0, z1, mat, bevel=0.0, bseg=2,
            loc=(0.0, 0.0, 0.0), rot=None):
        t = bmesh.new()
        vs = [t.verts.new(p) for p in (
            (x0, y0, z0), (x1, y0, z0), (x1, y1, z0), (x0, y1, z0),
            (x0, y0, z1), (x1, y0, z1), (x1, y1, z1), (x0, y1, z1))]
        for f in ((0, 3, 2, 1), (4, 5, 6, 7), (0, 1, 5, 4),
                  (1, 2, 6, 5), (2, 3, 7, 6), (3, 0, 4, 7)):
            t.faces.new([vs[i] for i in f])
        _xform(t, loc, rot)
        self._merge(t, mat, bevel, bseg)

    def cyl(self, r, z0, z1, mat, seg=48, bevel=0.0, bseg=2,
            cx=0.0, cy=0.0, caps=True,
            loc=(0.0, 0.0, 0.0), rot=None, smooth=False):
        t = bmesh.new()
        bot, top = [], []
        for i in range(seg):
            a = TAU * i / seg
            x, y = cx + r * math.cos(a), cy + r * math.sin(a)
            bot.append(t.verts.new((x, y, z0)))
            top.append(t.verts.new((x, y, z1)))
        for i in range(seg):
            j = (i + 1) % seg
            t.faces.new((bot[i], bot[j], top[j], top[i]))
        if caps:
            t.faces.new(list(reversed(bot)))
            t.faces.new(top)
        _xform(t, loc, rot)
        self._merge(t, mat, bevel, bseg, smooth)

    def revolve(self, profile, mat, seg=64, bevel=0.0, bseg=2,
                loc=(0.0, 0.0, 0.0), rot=None, smooth=True):
        """Solid of revolution from a CLOSED (r, z) profile."""
        t = bmesh.new()
        rings = []
        for (r, z) in profile:
            ring = []
            for i in range(seg):
                a = TAU * i / seg
                ring.append(t.verts.new((r * math.cos(a), r * math.sin(a), z)))
            rings.append(ring)
        n = len(rings)
        for k in range(n):
            a, b = rings[k], rings[(k + 1) % n]
            for i in range(seg):
                j = (i + 1) % seg
                t.faces.new((a[i], a[j], b[j], b[i]))
        _xform(t, loc, rot)
        self._merge(t, mat, bevel, bseg, smooth)

    def rect_tube(self, xo0, xo1, yo0, yo1, xi0, xi1, yi0, yi1, z0, z1, mat,
                  bevel=0.0, bseg=2, loc=(0.0, 0.0, 0.0), rot=None):
        t = bmesh.new()

        def ring(z, x0, x1, y0, y1):
            return [t.verts.new(p) for p in
                    ((x0, y0, z), (x1, y0, z), (x1, y1, z), (x0, y1, z))]

        bo = ring(z0, xo0, xo1, yo0, yo1)
        to = ring(z1, xo0, xo1, yo0, yo1)
        bi = ring(z0, xi0, xi1, yi0, yi1)
        ti = ring(z1, xi0, xi1, yi0, yi1)
        for k in range(4):
            k2 = (k + 1) % 4
            t.faces.new((bo[k], bo[k2], to[k2], to[k]))     # outer wall
            t.faces.new((bi[k2], bi[k], ti[k], ti[k2]))     # inner wall
            t.faces.new((bo[k2], bo[k], bi[k], bi[k2]))     # bottom ring
            t.faces.new((to[k], to[k2], ti[k2], ti[k]))     # top ring
        _xform(t, loc, rot)
        self._merge(t, mat, bevel, bseg)

    def helix(self, z0, z1, pitch, r_root, r_crest, mat, seg=48,
              steps_per_turn=24, root_half=0.30, crest_half=0.13,
              loc=(0.0, 0.0, 0.0), rot=None):
        """Swept trapezoidal thread - a real helical ridge."""
        t = bmesh.new()
        prof = ((r_root, -root_half * pitch),
                (r_crest, -crest_half * pitch),
                (r_crest, crest_half * pitch),
                (r_root, root_half * pitch))
        # the trapezoid sticks out dz either side of the sweep centre line,
        # so inset the centre line to keep the finished thread inside z0..z1
        pad = max(abs(dz) for _, dz in prof)
        z0, z1 = z0 + pad, z1 - pad
        # an integer number of turns keeps the sampled extremes symmetric about
        # the axis (steps_per_turn is a multiple of 4), so the thread does not
        # bias the measured axis position
        turns = max(1, int(round((z1 - z0) / pitch)))
        n = turns * steps_per_turn
        total = turns * TAU
        rings = []
        for i in range(n + 1):
            a = total * i / n
            z = z0 + (z1 - z0) * i / n
            rings.append([t.verts.new((r * math.cos(a), r * math.sin(a), z + dz))
                          for (r, dz) in prof])
        for i in range(n):
            A, B = rings[i], rings[i + 1]
            for k in range(3):
                t.faces.new((A[k], A[k + 1], B[k + 1], B[k]))
        t.faces.new(list(reversed(rings[0])))
        t.faces.new(rings[-1])
        _xform(t, loc, rot)
        self._merge(t, mat, 0.0, 2, smooth=True)

    # -- finishing ---------------------------------------------------------
    def to_object(self, collection, mats, group, part, extra=None):
        bmesh.ops.recalc_face_normals(self.bm, faces=list(self.bm.faces))
        me = bpy.data.meshes.new(self.name)
        self.bm.to_mesh(me)
        self.bm.free()
        obj = bpy.data.objects.new(self.name, me)
        collection.objects.link(obj)
        for mn in self.slots:
            obj.data.materials.append(mats[mn])
        obj['dsh_group'] = group
        obj['dsh_part'] = part
        if extra:
            for k, v in extra.items():
                obj[k] = v
        return obj


# --------------------------------------------------------------------------
def boolean(target, cutter, operation='DIFFERENCE'):
    """Apply one boolean and throw the cutter away."""
    mod = target.modifiers.new('BOOL_TMP', 'BOOLEAN')
    mod.operation = operation
    mod.object = cutter
    try:
        mod.solver = 'EXACT'
    except Exception:
        pass
    # Every part is a concatenation of overlapping primitives (a shaft sunk
    # into its motor, a boss sunk into its block).  Without use_self the exact
    # solver mis-classifies those overlaps and silently deletes geometry.
    try:
        mod.use_self = True
    except Exception:
        pass
    try:
        mod.use_hole_tolerant = True
    except Exception:
        pass
    for o in bpy.context.view_layer.objects:
        o.select_set(False)
    target.select_set(True)
    bpy.context.view_layer.objects.active = target
    bpy.ops.object.modifier_apply(modifier=mod.name)
    bpy.data.objects.remove(cutter, do_unlink=True)


def make_cutter(name, build, collection):
    """build(part) fills a cutter Part; returns the linked cutter object."""
    part = Part(name)
    build(part)
    bmesh.ops.recalc_face_normals(part.bm, faces=list(part.bm.faces))
    me = bpy.data.meshes.new(name)
    part.bm.to_mesh(me)
    part.bm.free()
    obj = bpy.data.objects.new(name, me)
    collection.objects.link(obj)
    obj.hide_viewport = True
    obj.hide_render = True
    return obj


# --------------------------------------------------------------------------
def world_aabb(obj):
    """AABB from real mesh vertices in world space.

    obj.bound_box is deliberately NOT used: it is stale for objects that were
    created and moved inside the same script run, which is exactly the bug
    that made the previous project's clash gate useless.
    """
    mw = obj.matrix_world
    lo = [1e30, 1e30, 1e30]
    hi = [-1e30, -1e30, -1e30]
    for v in obj.data.vertices:
        p = mw @ v.co
        for i in range(3):
            if p[i] < lo[i]:
                lo[i] = p[i]
            if p[i] > hi[i]:
                hi[i] = p[i]
    return lo, hi


def world_aabb_multi(objs):
    lo = [1e30] * 3
    hi = [-1e30] * 3
    for o in objs:
        a, b = world_aabb(o)
        for i in range(3):
            lo[i] = min(lo[i], a[i])
            hi[i] = max(hi[i], b[i])
    return lo, hi


def bvh_of(obj):
    """World-space BVH tree of the object's evaluated (final) mesh."""
    from mathutils.bvhtree import BVHTree
    mw = obj.matrix_world
    verts = [mw @ v.co for v in obj.data.vertices]
    polys = [tuple(p.vertices) for p in obj.data.polygons]
    return BVHTree.FromPolygons(verts, polys, all_triangles=False, epsilon=0.0)


def segment(a, b):
    """Separation of two AABBs.

    positive = true Euclidean clearance,  negative = the boxes really overlap.
    Taking min() of the per-axis gaps is WRONG: two boxes that are 600 mm apart
    in X and overlap in Z are clear, not overlapping.
    """
    seps = [max(b[0][i] - a[1][i], a[0][i] - b[1][i]) for i in range(3)]
    if all(s <= 0.0 for s in seps):
        return max(seps)                      # negative = overlap depth
    return math.sqrt(sum(max(0.0, s) ** 2 for s in seps))
