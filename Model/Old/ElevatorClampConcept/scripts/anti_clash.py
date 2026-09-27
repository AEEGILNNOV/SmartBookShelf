"""Anti-clash (穿模) validation.

Two independent gates:

GATE 1  interpenetration  - every pair of objects that belongs to different
        functional groups must have an empty volume intersection, unless the
        group pair is an explicitly declared bolted joint.

GATE 2  safety clearance  - the key parts named in the brief (push rod, push
        plate, platform, limit plate, book) must keep the hard margin
        P['MIN_CLEARANCE'] (2 mm) between them.

Any failure prints a WARNING and aborts the build so the parameters get
retuned instead of shipping an interpenetrating mechanism.
"""

import geom

EPS = 1e-6

# group pairs allowed to interpenetrate (bolted / sliding joints)
ALLOWED_OVERLAP = {
    ('platform', 'drive'), ('limit', 'drive'),
    ('book', 'platform'), ('push', 'book'),
    ('push', 'cabinet'), ('chute', 'cabinet'),
}

GROUP_LABEL = {
    'cabinet': 'cabinet + shell',
    'drive': 'screws / rails / bottom motors',
    'platform': 'moving platform',
    'limit': 'limit plate',
    'push': 'rear push unit',
    'book': 'book',
    'chute': 'take-off chute',
    'stage': 'render stage',
}


def aabb(obj):
    return geom.world_bbox(obj)


def axis_gaps(a, b):
    return tuple(max(a[i][0] - b[i][1], b[i][0] - a[i][1]) for i in range(3))


def separation(a, b):
    """Distance along the best separating axis; 0 or negative means overlap."""
    return max(axis_gaps(a, b))


def overlap_depths(a, b):
    return tuple(min(a[i][1], b[i][1]) - max(a[i][0], b[i][0])
                 for i in range(3))


def intersects(a, b, tol=EPS):
    return all(d > tol for d in overlap_depths(a, b))


class Report(object):
    def __init__(self):
        self.fails = []
        self.checks = []

    def ok(self, name, detail):
        self.checks.append(('PASS', name, detail))

    def fail(self, name, detail):
        self.checks.append(('FAIL', name, detail))
        self.fails.append((name, detail))

    def info(self, name, detail):
        self.checks.append(('INFO', name, detail))

    @property
    def passed(self):
        return not self.fails


def gate1_interpenetration(objs, report, tol=0.01):
    """No two different groups may share volume unless declared a joint."""
    boxes = [(o.name, o.get('grp', 'misc'), aabb(o)) for o in objs]
    hits = 0
    for i in range(len(boxes)):
        n1, g1, b1 = boxes[i]
        for j in range(i + 1, len(boxes)):
            n2, g2, b2 = boxes[j]
            if g1 == g2:
                continue
            if (g1, g2) in ALLOWED_OVERLAP or (g2, g1) in ALLOWED_OVERLAP:
                continue
            if intersects(b1, b2, tol):
                d = overlap_depths(b1, b2)
                report.fail('INTERPENETRATION',
                            '%s [%s] x %s [%s] overlap by (%.1f, %.1f, %.1f) mm'
                            % (n1, g1, n2, g2, d[0], d[1], d[2]))
                hits += 1
    if hits == 0:
        report.ok('GATE-1 interpenetration',
                  'no undeclared volume overlap across %d objects / %d groups'
                  % (len(boxes), len({g for _, g, _ in boxes})))
    return report


def _by_name(objs, name):
    for o in objs:
        if o.name == name:
            return o
    return None


def _group(objs, grp):
    return [o for o in objs if o.get('grp') == grp]


def _min_sep(list_a, list_b):
    best = None
    pair = None
    for a in list_a:
        ba = aabb(a)
        for b in list_b:
            s = separation(ba, aabb(b))
            if best is None or s < best:
                best = s
                pair = (a.name, b.name)
    return best, pair


def gate2_key_clearances(P, objs, report):
    """>= MIN_CLEARANCE between the key parts named in the brief."""
    need = P['MIN_CLEARANCE']
    plat = _group(objs, 'platform')
    lim = _group(objs, 'limit')
    push = _group(objs, 'push')
    book = _group(objs, 'book')

    def named(group, want):
        return [o for o in group if o.name in want]

    push_plate = named(push, {'Push_Plate'})
    push_rod = named(push, {'Push_Rod', 'Push_Rod_LeadScrew'})

    rules = [
        ('push plate  <-> platform   ', push_plate, plat, need),
        ('push plate  <-> limit plate', push_plate, lim, need),
        ('book        <-> limit plate', book, lim, need),
        ('push rod    <-> platform   ', push_rod, plat, need),
        ('push rod    <-> book       ', push_rod, book, need),
        ('platform    <-> limit plate', plat, lim, need),
        ('book        <-> cabinet    ', book, _group(objs, 'cabinet'), need),
        ('book        <-> drive      ', book, _group(objs, 'drive'), need),
    ]
    for label, la, lb, mn in rules:
        if not la or not lb:
            report.fail('GATE-2 ' + label, 'missing objects for the check')
            continue
        s, pair = _min_sep(la, lb)
        if s < mn:
            report.fail('GATE-2 ' + label,
                        '%.2f mm < %.2f mm  (%s / %s)' % (s, mn, pair[0],
                                                          pair[1]))
        else:
            report.ok('GATE-2 ' + label, '%.2f mm >= %.2f mm  (%s / %s)'
                      % (s, mn, pair[0], pair[1]))
    return report


def gate2_contacts(P, objs, report):
    """Intentional contacts must really be in contact (not gaps/hits)."""
    pairs = [
        ('book rests on platform  ', 'Book_Cover_Bottom',
         'Platform_Plate_Main', 2),
        ('push plate on the spine ', 'Push_Plate', 'Book_Spine', 1),
    ]
    for label, na, nb, axis in pairs:
        a = _by_name(objs, na)
        b = _by_name(objs, nb)
        if a is None or b is None:
            report.fail('GATE-2 ' + label, 'missing %s / %s' % (na, nb))
            continue
        g = axis_gaps(aabb(a), aabb(b))[axis]
        if abs(g) > 0.5:
            report.fail('GATE-2 ' + label,
                        'not in contact: gap %.2f mm' % g)
        else:
            report.ok('GATE-2 ' + label, 'contact, gap %.2f mm' % g)
    return report


def gate3_pose(P, objs, report):
    """Freeze-frame and travel assertions from the brief."""
    def chk(name, got, want, tol=0.01):
        if abs(got - want) <= tol:
            report.ok('GATE-3 ' + name, '%.2f mm' % got)
        else:
            report.fail('GATE-3 ' + name, 'got %.2f, expected %.2f'
                        % (got, want))

    def chk_ge(name, got, low):
        if got >= low - 1e-6:
            report.ok('GATE-3 ' + name, '%.2f mm >= %.2f mm' % (got, low))
        else:
            report.fail('GATE-3 ' + name, '%.2f mm < %.2f mm' % (got, low))

    plat = _by_name(objs, 'Platform_Plate_Main')
    pad = _by_name(objs, 'Limit_Pad_Black')
    plate = _by_name(objs, 'Push_Plate')
    rod = _by_name(objs, 'Push_Rod')
    cover_b = _by_name(objs, 'Book_Cover_Bottom')
    cover_t = _by_name(objs, 'Book_Cover_Top')
    spine = _by_name(objs, 'Book_Spine')

    chk('platform top face Z', aabb(plat)[2][1], 580.0)
    chk('book bottom Z', aabb(cover_b)[2][0], 580.0)
    chk('book top Z', aabb(cover_t)[2][1], 620.0)
    chk_ge('limit pad bottom Z', aabb(pad)[2][0], 640.0)
    chk('push plate face Y = spine Y', aabb(plate)[1][1], aabb(spine)[1][0])
    chk('push plate width X', aabb(plate)[0][1] - aabb(plate)[0][0],
        P['PUSH_PLATE_W'])
    chk('push plate height Z', aabb(plate)[2][1] - aabb(plate)[2][0],
        P['PUSH_PLATE_H'])
    chk('push plate thickness Y', aabb(plate)[1][1] - aabb(plate)[1][0],
        P['PUSH_PLATE_T'])
    chk('exposed push rod Y', aabb(rod)[1][1] - aabb(rod)[1][0],
        P['PUSH_STROKE'])
    chk('push rod axis height Z',
        (aabb(rod)[2][0] + aabb(rod)[2][1]) / 2.0, P['PUSH_CZ'])

    off = P['LIMIT_SLIDER_Z'] - P['PLATFORM_SLIDER_Z']
    chk_ge('left/right slide offset', off, P['SLIDER_MIN_OFFSET'])

    travel_low = P['PLAT_MIN_Z']
    clear = travel_low - P['MOTOR_TOP_Z']
    chk_ge('platform travel vs bottom motor', clear, P['MIN_CLEARANCE'])
    report.info('INFO travel limit',
                'platform assembly lowest Z = %.0f mm, bottom motor top = '
                '%.0f mm, clearance %.0f mm' % (travel_low, P['MOTOR_TOP_Z'],
                                                clear))
    report.info('INFO limit pad note',
                'pad raised to %.1f mm (brief says 640) to keep %.1f mm over '
                'the push plate' % (P['LIM_PAD_BOTTOM_Z'],
                                    P['MIN_CLEARANCE']))
    return report


def run(P, objs):
    report = Report()
    objs = [o for o in objs if o.get('grp') != 'stage']
    gate1_interpenetration(objs, report)
    gate2_key_clearances(P, objs, report)
    gate2_contacts(P, objs, report)
    gate3_pose(P, objs, report)
    return report


def print_report(report):
    width = 78
    print('=' * width)
    print('ANTI-CLASH / POSE VALIDATION')
    print('=' * width)
    for status, name, detail in report.checks:
        print('  [%s] %-34s %s' % (status, name.strip(), detail))
    print('-' * width)
    if report.passed:
        print('  RESULT: PASS - no interpenetration, all clearances >= 2 mm')
    else:
        print('  RESULT: FAIL - %d violation(s):' % len(report.fails))
        for name, detail in report.fails:
            print('    ! %s -> %s' % (name.strip(), detail))
        print('  WARNING: parameters must be adjusted before rendering.')
    print('=' * width)
    return report.passed
