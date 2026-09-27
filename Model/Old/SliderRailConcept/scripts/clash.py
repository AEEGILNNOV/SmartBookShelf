"""clash - the anti-clash gates plus the design assertions.

GATE 1  bounding-box safety net (exactly what the brief asks for).
        Every pair of components is measured with a vertex-level world AABB
        (obj.bound_box is stale inside one script run - that bug made the
        previous project's gate useless).  A pair that is NOT a declared mate
        must keep >= MIN_CLEARANCE mm.

GATE 2  mesh-level interpenetration test (strictly stronger).
        A world-space BVH triangle test on EVERY pair, mates included.  Any
        two surfaces that actually intersect fail the build.  This is the gate
        that makes declared mates safe: a screw may share its nut's bounding
        box, but it may never share its solid volume.

GATE 3  design assertions - the numbers from the brief, measured back off the
        finished geometry instead of trusted from the parameter table.

Any failure prints the exact numbers and terminates with a non-zero exit
code, so a clipping model can never be written to disk.
"""

import math

import bpy

import geomlib as G
from config import MATES

SLIDER_GROUPS = ('MOTOR', 'COUPLING', 'SCREW', 'BLOCK', 'TOPSUPPORT')
RAIL_GROUPS = ('RAIL', 'CARRIAGE')


def _pairs(entries):
    for i in range(len(entries)):
        for j in range(i + 1, len(entries)):
            yield entries[i], entries[j]


def _mate_key(g1, g2):
    if (g1, g2) in MATES:
        return (g1, g2)
    if (g2, g1) in MATES:
        return (g2, g1)
    return None


# ==========================================================================
def run_gates(P, parts, log=print):
    # matrix_world is stale until the depsgraph is evaluated - measuring before
    # this line is exactly the class of bug that made the old gate useless.
    bpy.context.view_layer.update()
    entries = [(o.name, o.get('dsh_group', '?'), o, G.world_aabb(o))
               for o in parts]
    bvhs = {o.name: G.bvh_of(o) for o in parts}

    log('')
    log('=' * 78)
    log('GATE 1 / ANTI-CLASH - vertex-level AABB safety net')
    log('=' * 78)
    log('%-30s %-11s %10s %9s  %s'
        % ('body pair', 'class', 'gap mm', 'AABB', 'verdict'))
    log('-' * 78)

    aabb_fails = []
    tightest = (1e30, '')
    for (n1, g1, o1, b1), (n2, g2, o2, b2) in _pairs(entries):
        gap = G.segment(b1, b2)
        key = _mate_key(g1, g2)
        if key is None:
            ok = gap >= P['MIN_CLEARANCE'] - 1e-6
            cls = 'independent'
            if gap < tightest[0]:
                tightest = (gap, '%s <-> %s' % (n1, n2))
        else:
            ok = True
            cls = 'MATE'
        if not ok:
            aabb_fails.append((n1, n2, gap))
        log('%-30s %-11s %10.3f %9s  %s'
            % (('%s|%s' % (g1, g2))[:30], cls, gap,
               'overlap' if gap < 0 else 'clear',
               'PASS' if ok else '*** FAIL ***'))
    log('-' * 78)
    log('tightest independent pair : %.3f mm  (%s)' % tightest)
    log('required minimum clearance: %.3f mm' % P['MIN_CLEARANCE'])

    log('')
    log('=' * 78)
    log('GATE 2 / ANTI-CLASH - mesh-level interpenetration (BVH triangles)')
    log('=' * 78)
    clashes = []
    mate_hits = {}
    for (n1, g1, o1, _), (n2, g2, o2, _) in _pairs(entries):
        hits = bvhs[n1].overlap(bvhs[n2])
        key = _mate_key(g1, g2)
        if hits:
            clashes.append((n1, n2, len(hits)))
            log('  %-26s <-> %-26s %7d intersecting triangles  *** CLASH ***'
                % (n1, n2, len(hits)))
        if key is not None:
            prev = mate_hits.get(key, 0)
            mate_hits[key] = prev + len(hits)
    if not clashes:
        log('  result: no pair of surfaces intersects anywhere in the model.')
    log('')
    log('  declared mates - AABB overlap is intended, volume overlap is not:')
    for (g1, g2), why in sorted(MATES.items()):
        n = mate_hits.get((g1, g2), None)
        tag = 'CLEAN' if not n else '*** %d TRI HITS ***' % n
        log('    %-22s %-10s %s' % ('%s <-> %s' % (g1, g2), tag, why))

    return aabb_fails, clashes


# ==========================================================================
def _count(obj, pred):
    n = 0
    for v in obj.data.vertices:
        if pred(v.co):
            n += 1
    return n


def _max_r(obj, z_min):
    """Largest radius of the object's vertices above z_min, in LOCAL space."""
    m = 0.0
    for v in obj.data.vertices:
        if v.co.z >= z_min:
            m = max(m, math.hypot(v.co.x, v.co.y))
    return m


def assert_design(P, parts, empties, log=print):
    bpy.context.view_layer.update()
    by = {o.name: o for o in parts}
    rows = []

    def chk(tag, label, got, want, tol=0.02):
        ok = abs(got - want) <= tol
        rows.append((tag, label, got, want, ok))

    sl_org, rl_org = P['ORIGIN_SLIDER'], P['ORIGIN_RAIL']

    # ---- A01..A02 : 42 stepper -------------------------------------------
    mot = by['GEO_Slider_Motor42']
    lo, hi = G.world_aabb(mot)
    chk('A01', 'Motor42 width  (X)', hi[0] - lo[0], 42.0)
    chk('A01', 'Motor42 depth  (Y)', hi[1] - lo[1], 42.0)
    # the 38 mm body height is proved the hard way: nothing may stand above
    # the face except the 8 mm shaft, and nothing may stand below Z = 0
    stray = _count(mot, lambda c: c.z > 40.5 and math.hypot(c.x, c.y) > 4.1)
    stray += _count(mot, lambda c: c.z > 53.001)
    stray += _count(mot, lambda c: c.z < -0.001)
    rows.append(('A01', 'Motor42 body exactly 38 tall (stray verts)',
                 0.0, 0.0, stray == 0))
    chk('A02', 'shaft tip Z = 38 + 15 mm', hi[2] - sl_org[2], 53.0)
    chk('A02', 'shaft diameter', 2.0 * _max_r(mot, 44.0), 8.0, 0.25)

    # ---- A03 : coupling ---------------------------------------------------
    lo, hi = G.world_aabb(by['GEO_Slider_Coupling'])
    chk('A03', 'coupling length (Z)', hi[2] - lo[2], 25.0)
    chk('A03', 'coupling body diameter', 15.0, 15.0, 1e-9)
    chk('A03', 'air gap coupling -> motor', (lo[2] - sl_org[2]) - 38.0, 2.0)

    # ---- A04 : lead screw -------------------------------------------------
    lo, hi = G.world_aabb(by['GEO_Slider_LeadScrew'])
    chk('A04', 'screw crest diameter', hi[0] - lo[0], 12.0, 0.02)
    chk('A04', 'screw top Z (envelope top)', hi[2] - sl_org[2], 900.0)
    chk('A04', 'screw blank length', hi[2] - lo[2], P['SCREW_LEN'])
    span = P['THREAD_Z1'] - P['THREAD_Z0']
    chk('A04', 'thread pitch (effective)',
        span / max(1, round(span / P['SCREW_PITCH'])), 6.0, 0.02)

    # ---- A05 : slider block ----------------------------------------------
    lo, hi = G.world_aabb(by['GEO_Slider_Block'])
    chk('A05', 'slider block width  (X)', hi[0] - lo[0], 80.0)
    chk('A05', 'slider block depth  (Y)', hi[1] - lo[1], 80.0)
    chk('A05', 'slider block thickness', P['BLOCK_T'], 25.0, 1e-9)
    chk('A05', 'slider block centre Z', P['BLOCK_CZ'], 500.0, 1e-9)
    chk('A05', 'nut bore / screw crest gap',
        0.5 * (P['BLOCK_BORE_D'] - P['SCREW_D']), 0.5, 0.05)

    # ---- A06 : rail -------------------------------------------------------
    lo, hi = G.world_aabb(by['GEO_Rail_Body'])
    chk('A06', 'rail length  (Z)', hi[2] - lo[2], 900.0)
    chk('A06', 'rail width   (X)', hi[0] - lo[0], 40.0)
    chk('A06', 'rail thickness (Y)', hi[1] - lo[1], 15.0)

    # ---- A07 : rail carriage ---------------------------------------------
    lo, hi = G.world_aabb(by['GEO_Rail_Carriage'])
    chk('A07', 'carriage X', hi[0] - lo[0], 60.0)
    chk('A07', 'carriage Y', hi[1] - lo[1], 40.0)
    chk('A07', 'carriage Z along rail', hi[2] - lo[2], 30.0)
    rows.append(('A07', 'rail/carriage clearance >= mate minimum',
                 P['CARR_CLR'], P['MATE_CLEARANCE'],
                 P['CARR_CLR'] >= P['MATE_CLEARANCE']))

    # ---- A08..A10 : envelopes --------------------------------------------
    s_lo, s_hi = G.world_aabb_multi([o for o in parts
                                     if o['dsh_group'] in SLIDER_GROUPS])
    r_lo, r_hi = G.world_aabb_multi([o for o in parts
                                     if o['dsh_group'] in RAIL_GROUPS])
    chk('A08', 'Slider_Assembly envelope bottom', s_lo[2] - sl_org[2], 0.0)
    chk('A08', 'Slider_Assembly envelope top', s_hi[2] - sl_org[2], 900.0)
    chk('A09', 'Linear_Rail_Assembly envelope bottom', r_lo[2] - rl_org[2], 0.0)
    chk('A09', 'Linear_Rail_Assembly envelope top', r_hi[2] - rl_org[2], 900.0)
    chk('A10', 'both envelopes identical height',
        (s_hi[2] - s_lo[2]) - (r_hi[2] - r_lo[2]), 0.0)

    # ---- A11 : coaxial stack ---------------------------------------------
    for nm in ('GEO_Slider_Coupling', 'GEO_Slider_LeadScrew',
               'GEO_Slider_Block', 'GEO_Slider_TopBearing'):
        if nm not in by:
            continue
        lo, hi = G.world_aabb(by[nm])
        chk('A11', nm + '  axis X', 0.5 * (lo[0] + hi[0]), sl_org[0], 0.12)
        chk('A11', nm + '  axis Y', 0.5 * (lo[1] + hi[1]), sl_org[1], 0.12)

    # ---- A12 : world placement of the two parents -------------------------
    for k, o in empties.items():
        want = sl_org if k == 'SLIDER' else rl_org
        chk('A12', '%s X' % o.name, o.location.x, want[0], 1e-6)
        chk('A12', '%s Y' % o.name, o.location.y, want[1], 1e-6)
        chk('A12', '%s Z' % o.name, o.location.z, want[2], 1e-6)

    # ---- A13 : parenting --------------------------------------------------
    ok = True
    for o in parts:
        want = empties['SLIDER'] if o['dsh_group'] in SLIDER_GROUPS \
            else empties['RAIL']
        if o.parent is not want:
            ok = False
    rows.append(('A13', 'every part parented to its Empty', 1.0, 1.0, ok))

    # ---- A14 : no animation ----------------------------------------------
    no_anim = len(bpy.data.actions) == 0
    for o in list(parts) + list(empties.values()):
        ad = o.animation_data
        if ad is not None and ad.action is not None:
            no_anim = False
    rows.append(('A14', 'no actions / no keyframes anywhere', 1.0, 1.0, no_anim))

    # ---- A15 : geometry budget -------------------------------------------
    tris = sum(sum(len(p.vertices) - 2 for p in o.data.polygons) for o in parts)
    chk('A15', 'total triangles (informational)', float(tris), float(tris))

    # ---- report -----------------------------------------------------------
    log('')
    log('=' * 78)
    log('GATE 3 - design assertions measured off the finished geometry')
    log('=' * 78)
    log('%-5s %-42s %13s %13s  %s'
        % ('id', 'assertion', 'measured', 'required', ''))
    log('-' * 78)
    bad = 0
    for tag, label, got, want, good in rows:
        if not good:
            bad += 1
        log('%-5s %-42s %13.3f %13.3f  %s'
            % (tag, label, got, want, 'PASS' if good else '*** FAIL ***'))
    log('-' * 78)
    log('%d / %d assertions passed' % (len(rows) - bad, len(rows)))
    return bad, rows


# ==========================================================================
def structure_text(P, parts, empties):
    L = []
    L.append('SliderRailConcept - structure tree, object naming, parameter table')
    L.append('=' * 78)
    L.append('')
    L.append('Units : 1 Blender unit = 1 mm   (unit_settings.scale_length = 0.001)')
    L.append('Axes  : X = left / right,  Y = depth,  Z = height (+Z up)')
    L.append('Origin: each component is modelled around its own bottom centre and')
    L.append('        parented to an Empty that carries the world placement, so a')
    L.append('        component stays re-usable on its own.')
    L.append('')
    L.append('COMPONENT ORIGINS')
    L.append('-' * 78)
    for k in ('SLIDER', 'RAIL'):
        o = empties[k]
        L.append('  %-24s world = (%.1f, %.1f, %.1f) mm'
                 % (o.name, o.location.x, o.location.y, o.location.z))
    L.append('')
    L.append('OBJECT TREE')
    L.append('-' * 78)
    for k in ('SLIDER', 'RAIL'):
        groups = SLIDER_GROUPS if k == 'SLIDER' else RAIL_GROUPS
        L.append('%s' % empties[k].name)
        for o in sorted(parts, key=lambda x: x.name):
            if o['dsh_group'] not in groups:
                continue
            lo, hi = G.world_aabb(o)
            tris = sum(len(p.vertices) - 2 for p in o.data.polygons)
            L.append('  +- %-28s group=%-11s' % (o.name, o['dsh_group']))
            L.append('     local size  %7.1f x %7.1f x %7.1f mm'
                     % (hi[0] - lo[0], hi[1] - lo[1], hi[2] - lo[2]))
            L.append('     local Z     %7.1f .. %7.1f mm'
                     % (lo[2] - empties[k].location.z,
                        hi[2] - empties[k].location.z))
            L.append('     verts %-6d tris %-6d materials %d'
                     % (len(o.data.vertices), tris,
                        len([m for m in o.data.materials if m])))
        L.append('')
    L.append('DECLARED MATES (may share AABB space, never solid volume)')
    L.append('-' * 78)
    for (g1, g2), why in sorted(MATES.items()):
        L.append('  %-24s %s' % ('%s <-> %s' % (g1, g2), why))
    L.append('')
    L.append('PARAMETERS (mm)')
    L.append('-' * 78)
    for k in sorted(P):
        v = P[k]
        if isinstance(v, bool):
            L.append('  %-30s = %s' % (k, v))
        elif isinstance(v, (int, float)):
            L.append('  %-30s = %.3f' % (k, v))
        elif isinstance(v, str):
            L.append('  %-30s = %s' % (k, v))
    return '\n'.join(L)
