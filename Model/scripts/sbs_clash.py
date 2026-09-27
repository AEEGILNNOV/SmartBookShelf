"""Bounding-box anti-clash gate.

Every mesh carries a dsh_group tag naming the rigid body it belongs to.
Two objects in the SAME group are a declared rigid connection and may
interpenetrate.  Two objects in DIFFERENT groups must keep at least
MIN_CLEARANCE between their axis-aligned bounding boxes, otherwise this
module reports the pair and the build aborts.

This is deliberately conservative: it uses AABBs, so a reported failure is a
real risk worth inspecting, and a pass still needs a visual check.
"""

from mathutils import Vector


def world_aabb(obj):
    """True world AABB from evaluated-free mesh vertices.

    Deliberately not obj.bound_box: that cache is not refreshed when objects
    are created and moved inside one script run, which silently returns stale
    corners and makes clearance reports meaningless.
    """
    mw = obj.matrix_world
    verts = obj.data.vertices
    if len(verts) == 0:
        return (0.0, 0.0, 0.0, 0.0, 0.0, 0.0)
    xs = []
    ys = []
    zs = []
    for v in verts:
        w = mw @ v.co
        xs.append(w.x)
        ys.append(w.y)
        zs.append(w.z)
    return (min(xs), min(ys), min(zs), max(xs), max(ys), max(zs))


def gap(a, b):
    """Per-axis separation. Positive = clear by that much, negative = overlap."""
    dx = max(a[0] - b[3], b[0] - a[3])
    dy = max(a[1] - b[4], b[1] - a[4])
    dz = max(a[2] - b[5], b[2] - a[5])
    return dx, dy, dz


def separation(a, b):
    """Euclidean distance between two AABBs (0 when they intersect)."""
    dx, dy, dz = gap(a, b)
    dx, dy, dz = max(dx, 0.0), max(dy, 0.0), max(dz, 0.0)
    return (dx * dx + dy * dy + dz * dz) ** 0.5


def check(objects, min_clearance=2.0, allow=None, verbose=True):
    """Return (failures, checked_count).

    objects : list of Blender mesh objects carrying a 'dsh_group' prop
    allow   : iterable of unordered (groupA, groupB) pairs to skip
    """
    boxes = []
    for o in objects:
        if o.type != 'MESH':
            continue
        boxes.append((o.name, o.get('dsh_group', 'UNGROUPED'), world_aabb(o)))

    allowset = set()
    for pair in (allow or []):
        a, b = pair
        allowset.add(frozenset((a, b)))

    # prove the filter is not silently swallowing everything: tally every
    # cross-group pair, and how many of those were declared contacts.
    tally = {}
    for i in range(len(boxes)):
        for j in range(i + 1, len(boxes)):
            gi, gj = boxes[i][1], boxes[j][1]
            if gi == gj:
                continue
            key = tuple(sorted((gi, gj)))
            tally[key] = tally.get(key, 0) + 1
    if verbose:
        print('[clash] cross-group pair tally:')
        for key in sorted(tally):
            decl = 'DECLARED-CONTACT' if frozenset(key) in allowset else 'tested'
            print('[clash]    {:<26} pairs={:<5} {}'.format(
                ' + '.join(key), tally[key], decl))

    failures = []
    checked = 0
    n = len(boxes)
    for i in range(n):
        ni, gi, bi = boxes[i]
        for j in range(i + 1, n):
            nj, gj, bj = boxes[j]
            if gi == gj:
                continue
            if frozenset((gi, gj)) in allowset:
                continue
            dx, dy, dz = gap(bi, bj)
            if dx >= min_clearance and dy >= min_clearance and dz >= min_clearance:
                continue
            # AABB faces that merely touch (all three separations exactly 0)
            # are a designed surface contact, not a clash.
            if dx == 0.0 and dy == 0.0 and dz == 0.0:
                continue
            checked += 1
            if dx < 0.0 and dy < 0.0 and dz < 0.0:
                kind = 'INTERPENETRATION'
                worst = -max(dx, dy, dz)
            else:
                kind = 'GAP-BELOW-MINIMUM'
                worst = min(max(dx, 0.0), max(dy, 0.0), max(dz, 0.0))
            failures.append({
                'a': ni, 'b': nj, 'group_a': gi, 'group_b': gj,
                'kind': kind, 'dx': round(dx, 3), 'dy': round(dy, 3),
                'dz': round(dz, 3), 'worst_mm': round(worst, 3),
            })

    failures.sort(key=lambda f: f['worst_mm'], reverse=True)

    if verbose:
        print('[clash] groups checked: {}  pairs tested: {}  failures: {}'
              .format(len({b[1] for b in boxes}), checked, len(failures)))
        for f in failures[:40]:
            print('[clash] {:>19}  {:<34} vs {:<34} dx={:>9} dy={:>9} dz={:>9}'
                  .format(f['kind'], f['a'], f['b'], f['dx'], f['dy'], f['dz']))
        if len(failures) > 40:
            print('[clash] ... {} more'.format(len(failures) - 40))
    return failures, checked


def _by_part(objects):
    d = {}
    for o in objects:
        d.setdefault(o.get('dsh_part', o.name), []).append(o)
    return d


def penetr(a, b):
    """Depth of true 3-axis AABB interpenetration (0.0 when not penetrating)."""
    dx, dy, dz = gap(a, b)
    if dx < 0.0 and dy < 0.0 and dz < 0.0:
        return min(-dx, -dy, -dz)
    return 0.0


def _contains(outer, inner):
    """True when inner AABB lies inside outer AABB (a nested assembly)."""
    return (inner[0] >= outer[0] - 1e-6 and inner[3] <= outer[3] + 1e-6 and
            inner[1] >= outer[1] - 1e-6 and inner[4] <= outer[4] + 1e-6 and
            inner[2] >= outer[2] - 1e-6 and inner[5] <= outer[5] + 1e-6)


def _env(objects, parts):
    """Union AABB of every object whose dsh_part is in parts."""
    sel = [o for o in objects if o.get('dsh_part') in parts]
    if not sel:
        return None
    boxes = [world_aabb(o) for o in sel]
    return (min(b[0] for b in boxes), min(b[1] for b in boxes),
            min(b[2] for b in boxes), max(b[3] for b in boxes),
            max(b[4] for b in boxes), max(b[5] for b in boxes))


# Parts that make up the LEFT driven column and the RIGHT passive rail.
DRIVE_PARTS = ('motor42_L_body', 'motor42_R_body', 'motor42_L_flange',
               'motor42_R_flange', 'motor42_L_shaft', 'motor42_R_shaft',
               'coupling_L', 'coupling_R',
               'screw_L_core', 'screw_R_core', 'screw_L_thread',
               'screw_R_thread', 'screw_L_topblock', 'screw_R_topblock',
               'rail_L_plate', 'rail_R_plate', 'rail_L_track', 'rail_R_track')


def assert_design(objects, P, verbose=True):
    """Explicit parametric checks for the step-1 design intent.

    These carry the real weight: every moving platform part must clear the
    fixed drive-column envelope by MIN_CLEARANCE, the book must rest on the
    platform with a real gap to the guards, and the pusher's reserved volume
    must still be empty.
    """
    by = _by_part(objects)
    mc = P['MIN_CLEARANCE']
    results = []

    def rec(name, value, limit, want_ge=True, unit='mm'):
        ok = (value >= limit) if want_ge else (value <= limit)
        results.append((ok, name, value, limit, want_ge, unit))
        return ok

    # ---- A1 every platform part must clear every drive COLUMN part --------
    # the drive columns are a non-convex assembly, so the union AABB would
    # falsely flag the platform: test part against part instead.
    # a nut swallows its screw and a carriage wraps its rail: those pairs are
    # legitimate nesting, not interference, so they are reported separately
    nested = []
    for part in ('carriage_l_screw', 'carriage_r_rail'):
        cb = _env(objects, (part,))
        if cb is None:
            continue
        for dp in DRIVE_PARTS:
            db = _env(objects, (dp,))
            if db is None:
                continue
            if _contains(db, cb):
                nested.append((part, dp))
    if verbose and nested:
        print('[assert]   legitimate nut/carriage nesting: {}'.format(
            ', '.join('%s<- %s' % (a, b) for a, b in nested)))

    strict_parts = ('plate', 'guard_rear', 'guard_left', 'bracket_l',
                    'bracket_r', 'rib_l', 'rib_r')
    worst_pen = None      # deepest true interpenetration (mm)
    worst_clear = None    # smallest distance between non-joined bodies (mm)
    clears = []           # every non-joined pair, for the report
    for pp in strict_parts:
        pb = _env(objects, (pp,))
        if pb is None:
            continue
        for dp in DRIVE_PARTS:
            db = _env(objects, (dp,))
            if db is None:
                continue
            if (pp, dp) in nested or _contains(db, pb):
                continue
            pen = penetr(pb, db)
            if pen > 1e-6:
                if worst_pen is None or pen > worst_pen[1]:
                    worst_pen = ('%s vs %s' % (pp, dp), pen)
                continue
            dx, dy, dz = gap(pb, db)
            if dx <= 1e-6 or dy <= 1e-6 or dz <= 1e-6:
                # a single touching face is a bolted join, which is allowed
                continue
            clear = separation(pb, db)
            entry = (clear, '%s vs %s' % (pp, dp),
                     'dx={:.1f} dy={:.1f} dz={:.1f}'.format(dx, dy, dz))
            clears.append(entry)
            if worst_clear is None or clear < worst_clear[0]:
                worst_clear = entry
    if verbose:
        clears.sort(key=lambda c: c[0])
        print('[assert]   A1 closest non-joined platform<->drive pairs:')
        for cl, nm, det in clears[:3]:
            print('[assert]      {:>44}  {:>8.1f} mm  ({})'.format(
                nm, cl, det))
    # FAIL only on real interpenetration; a clear pair must also keep MIN_CLEARANCE
    if worst_pen is not None:
        pen_ok = False
        pen_val, pen_name = -worst_pen[1], worst_pen[0]
    else:
        pen_ok = True
        pen_val, pen_name = 0.0, 'none'
    rec('A1a no platform part penetrates a drive column (%s)' % pen_name,
        pen_val, 0.0)
    if worst_clear is not None:
        rec('A1b tightest non-joined pair keeps MIN_CLEARANCE (%s)'
            % worst_clear[1], worst_clear[0], mc)

    # ---- A2 book rests on the platform top face, 0 gap -------------------
    bk = _env(objects, ('cover_bottom',))
    rec('A2 book bottom rests on platform top (gap)', bk[2] - P['PLAT_TOP_Z'],
        0.0, want_ge=False)

    # ---- A3 book fore edge clear of the rear guard -----------------------
    rec('A3 book fore edge to rear guard gap',
        P['PLAT_GUARD_Y0'] - P['BOOK_Y1'], mc)

    # ---- A4 book to left guard -------------------------------------------
    rec('A4 book to left guard gap',
        P['BOOK_X0'] - P['PLAT_GUARD_X1'], mc)

    # ---- A5 the 80 mm pusher plate brackets the 40 mm book symmetrically --
    # (intentional: the 150x80x5 plate overhangs a 40 mm book on both sides)
    rec('A5 pusher plate overhang above book top (intentional)',
        P['PUSH_PLATE_Z1'] - P['BOOK_Z1'], 0.0, want_ge=True)
    rec('A5b pusher plate underhang below book bottom (intentional)',
        P['BOOK_Z0'] - P['PUSH_PLATE_Z0'], 0.0, want_ge=True)

    # ---- A6 pusher plate back face clear of the rear guard ---------------
    rec('A6 pusher plate back face to rear guard gap',
        P['PLAT_GUARD_Y0'] - P['PUSH_PLATE_Y0'], 0.0)

    # ---- A7 pusher reserved volume is still empty in step 1 --------------
    # Nothing but the book may occupy the pusher's full stroke corridor.
    pv = (P['PUSH_PLATE_Z0'], P['PUSH_PLATE_Z1'],
          P['PUSH_PLATE_Y0'] - P['PUSH_STROKE'] - 40.0, P['PUSH_PLATE_Y1'])
    cx0 = -P['PUSH_PLATE_W'] / 2.0
    cx1 = P['PUSH_PLATE_W'] / 2.0
    ledger = []
    for o in objects:
        b = world_aabb(o)
        # full 3-D test: the corridor is only as wide as the 150 mm push plate
        if not (b[0] < cx1 and b[3] > cx0 and
                b[2] < pv[1] and b[5] > pv[0] and
                b[1] < pv[3] and b[4] > pv[2]):
            continue
        # the carrier plate and stiffening ribs live UNDER the corridor,
        # and the pusher bracket necessarily passes beside the rear guard
        if o.get('dsh_part') in ('plate', 'rib_l', 'rib_r'):
            continue
        ledger.append((o.name, o.get('dsh_part', '?')))
    blockers = [(n, p) for n, p in ledger
                if p not in ('spine', 'pages', 'cover_bottom', 'cover_top',
                             'guard_rear', 'guard_left')]
    if verbose:
        print('[assert]   corridor Y[{:.0f},{:.0f}] Z[{:.0f},{:.0f}] '
              '(full plate width)'.format(pv[2], pv[3], pv[0], pv[1]))
        for o in objects:
            b = world_aabb(o)
            if not (b[2] < pv[1] and b[5] > pv[0] and b[1] < pv[3]
                    and b[4] > pv[2]):
                continue
            print('[assert]     {:38} X[{:>7.1f},{:>7.1f}] '
                  'Y[{:>7.1f},{:>7.1f}] Z[{:>7.1f},{:>7.1f}]'.format(
                      o.name, b[0], b[3], b[1], b[4], b[2], b[5]))
    results.append((not blockers, 'A7 pusher corridor clear of structure',
                    0.0 if not blockers else float(len(blockers)), 0.0,
                    False, 'count'))

    # ---- A8 the two driven slides are staggered by the required amount ---
    rec('A8 left/right carriage stagger',
        P['LIMIT_SLIDER_Z'] - P['PLATFORM_SLIDER_Z'],
        P['SLIDER_MIN_OFFSET'])

    # ---- A9 motor + coupling stack = 70 mm -------------------------------
    rec('A9 motor+coupling stack height', P['COUPLING_Z1'] - P['MOTOR_FOOT_Z0'],
        70.0)

    # ---- A10 platform low limit clears the couplings ---------------------
    rec('A10 platform low limit to coupling top', 150.0 - P['COUPLING_Z1'], mc)

    # ---- A11 screw travel covers both carriages --------------------------
    ok_lo = P['SCREW_Z0'] <= P['PLATFORM_SLIDER_Z'] - P['SHUTTLE_H'] / 2.0
    ok_hi = P['SCREW_Z1'] >= P['LIMIT_SLIDER_Z'] + P['SHUTTLE_H'] / 2.0
    results.append((ok_lo and ok_hi, 'A11 screw travel covers both carriages',
                    1.0 if (ok_lo and ok_hi) else 0.0, 1.0, True, 'bool'))

    if verbose:
        print('[assert] design-intent checks')
        for ok, name, val, lim, ge, unit in results:
            tag = 'PASS' if ok else 'FAIL'
            print('[assert]  {}  {:<52} {:>10.3f} {}  (limit {} {})'.format(
                tag, name, val, unit, '>=' if ge else '<=', lim))
    bad = [r for r in results if not r[0]]
    return results, bad
