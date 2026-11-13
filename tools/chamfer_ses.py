#!/usr/bin/env python3
"""Chamfer the 90 degree bends of a Specctra session to 45 degrees.

fastroute routes at 45 degrees (directions 0/45/90...), but a horizontal
trace that turns vertical still makes a 90 degree bend on the same layer.
Every such corner is cut by a 45 degree segment of length d*sqrt(2), with d
along each leg:
  - only corners where exactly two segments of the same net and layer meet,
    with no via or pad there (a change of direction through a via, or at a
    pad, is allowed);
  - d is the largest of CHAMFER, 3/4, 1/2, 1/3, 1/4 of it that keeps the new
    segment at the clearance from the other nets (traces and vias of the
    session, pads of the DSN as their enclosing circle, so the check errs on
    the safe side), and at most 45 % of each leg;
  - a corner with no room is left as it is and listed (in mm, KiCad
    coordinates) to fix by hand.
The protected wiring (type protect: the plane vias' stubs) is not touched.
The other wires are written back as 2-point segments.

usage: chamfer_ses.py design.dsn in.ses out.ses [CHAMFER_MM]  (default 0.5)
"""
import collections
import math
import re
import sys

dsn_path, src, dst = sys.argv[1:4]
CH = float(sys.argv[4]) if len(sys.argv) > 4 else 0.5


def scope_end(s, i):
    d = 0
    for k in range(i, len(s)):
        c = s[k]
        if c == '(':
            d += 1
        elif c == ')':
            d -= 1
            if d == 0:
                return k + 1
    raise ValueError('unbalanced scope at %d' % i)


def scopes(s, head):
    """(start, end) of every scope starting with '(head ' or '(head\\n'."""
    for m in re.finditer(r'\(%s[\s)]' % re.escape(head), s):
        yield m.start(), scope_end(s, m.start())


def name_re():
    return r'("[^"]*"|[^\s()]+)'


def unq(n):
    return n[1:-1] if n.startswith('"') else n


def units_per_mm(t):
    m = re.search(r'\(resolution (\w+) (\d+)\)', t)
    unit, n = m.group(1), float(m.group(2))
    return n * {'um': 1000.0, 'mm': 1.0, 'mil': 1 / 0.0254, 'inch': 1 / 25.4}[unit]


# ---------------------------------------------------------------- the design
D = open(dsn_path).read()
# DSN coordinates are in (unit ...) (default: the resolution unit)
mu = re.search(r'\(unit (\w+)\)', D)
dsn_unit = mu.group(1) if mu else re.search(r'\(resolution (\w+)', D).group(1)
DK = {'um': 1000.0, 'mm': 1.0, 'mil': 1 / 0.0254, 'inch': 1 / 25.4}[dsn_unit]  # DSN units per mm

CLEAR = max(float(v) for v in re.findall(r'\(clearance ([\d.]+)\)', D)) / DK + 0.01

# padstack -> {layer: radius of the enclosing circle (mm)}, '*' = every layer
pad_r = {}
for s, e in scopes(D, 'padstack'):
    body = D[s:e]
    name = unq(re.match(r'\(padstack\s+' + name_re(), body).group(1))
    rs = {}
    for m in re.finditer(r'\(shape \((\w+) (\S+)\s+([-\d.\s]+)', body):
        kind, layer, nums = m.group(1), m.group(2), [float(v) for v in m.group(3).split()]
        if kind == 'circle':
            r = nums[0] / 2 + (math.hypot(nums[1], nums[2]) if len(nums) >= 3 else 0)
        elif kind == 'rect':
            r = max(math.hypot(x, y) for x in (nums[0], nums[2]) for y in (nums[1], nums[3]))
        elif kind in ('path', 'polygon'):
            w, pts = nums[0], nums[1:]
            r = max(math.hypot(pts[i], pts[i + 1]) for i in range(0, len(pts) - 1, 2)) + w / 2
        else:
            continue
        rs[layer] = max(rs.get(layer, 0), r / DK)
    pad_r[name] = rs

# image -> [(padstack, x, y)]
images = {}
lib = D[D.find('(library'):]
for s, e in scopes(lib, 'image'):
    body = lib[s:e]
    name = unq(re.match(r'\(image\s+' + name_re(), body).group(1))
    pins = []
    for m in re.finditer(r'\(pin\s+' + name_re() + r'(?:\s+\(rotate [-\d.]+\))?\s+' + name_re() +
                         r'\s+(-?[\d.]+)\s+(-?[\d.]+)\)', body):
        pins.append((unq(m.group(1)), unq(m.group(2)), float(m.group(3)), float(m.group(4))))
    images[name] = pins

# pin "REF-PIN" -> net
pin_net = {}
nw = D[D.find('(network'):]
nw = nw[:scope_end(nw, 0)]
for s, e in scopes(nw, 'net'):
    body = nw[s:e]
    net = re.match(r'\(net\s+' + name_re(), body).group(1)
    pm = re.search(r'\(pins\s+([^)]*)\)', body)
    if pm:
        for p in re.findall(r'"[^"]*"|\S+', pm.group(1)):
            pin_net[unq(p)] = unq(net)

# pads: (x, y, {layer: r}, net) in mm, Specctra orientation (y up)
pads = []
pl = D[D.find('(placement'):]
pl = pl[:scope_end(pl, 0)]
for s, e in scopes(pl, 'component'):
    body = pl[s:e]
    img = unq(re.match(r'\(component\s+' + name_re(), body).group(1))
    for m in re.finditer(r'\(place\s+' + name_re() + r'\s+(-?[\d.]+)\s+(-?[\d.]+)\s+(front|back)\s+(-?[\d.]+)', body):
        px, py, back, rot = float(m.group(2)), float(m.group(3)), m.group(4) == 'back', math.radians(float(m.group(5)))
        ref = unq(m.group(1))
        for ps, pid, x, y in images.get(img, []):
            if back:
                x = -x
            xr = x * math.cos(rot) - y * math.sin(rot)
            yr = x * math.sin(rot) + y * math.cos(rot)
            rs = pad_r.get(ps, {})
            if back:
                rs = {{'F.Cu': 'B.Cu', 'B.Cu': 'F.Cu'}.get(k, k): v for k, v in rs.items()}
            pads.append(((px + xr) / DK, (py + yr) / DK, rs, pin_net.get('%s-%s' % (ref, pid))))

# ---------------------------------------------------------------- the session
T = open(src).read()
SK = units_per_mm(T[T.find('(routes'):])
no = T.find('(network_out')
ne = scope_end(T, no)

segs = []      # [net, layer, w, (ax, ay), (bx, by)]  mm, y up
vias = []      # (net, x, y, r)
keep = []      # protected wire texts, by net
nets_order = []
net_body_span = {}
for s, e in scopes(T[no:ne], 'net'):
    s += no
    e += no
    body = T[s:e]
    net = re.match(r'\(net\s+' + name_re(), body).group(1)
    nets_order.append(net)
    net_body_span[net] = (s, e)
    for ws, we in scopes(body, 'wire'):
        w = body[ws:we]
        if '(type protect)' in w:
            keep.append((net, w))
            continue
        m = re.search(r'\(path (\S+) ([\d.]+)\s+([-\d.\s]+)\)', w)
        layer, width = m.group(1), float(m.group(2)) / SK
        p = [float(v) / SK for v in m.group(3).split()]
        for i in range(0, len(p) - 2, 2):
            a, b = (p[i], p[i + 1]), (p[i + 2], p[i + 3])
            if a != b:
                segs.append([net, layer, width, a, b])
    for m in re.finditer(r'\(via\s+' + name_re() + r'\s+(-?[\d.]+)\s+(-?[\d.]+)', body):
        rs = pad_r.get(unq(m.group(1)), {})
        vias.append((net, float(m.group(2)) / SK, float(m.group(3)) / SK, max(rs.values()) if rs else 0.4))


def key(p):
    return (round(p[0], 4), round(p[1], 4))


def seg_dist(p, q, a, b):
    """distance between segments pq and ab"""
    def pt_seg(c, u, v):
        dx, dy = v[0] - u[0], v[1] - u[1]
        L = dx * dx + dy * dy
        t = 0 if L == 0 else max(0, min(1, ((c[0] - u[0]) * dx + (c[1] - u[1]) * dy) / L))
        return math.hypot(c[0] - u[0] - t * dx, c[1] - u[1] - t * dy)

    def cross(o, u, v):
        return (u[0] - o[0]) * (v[1] - o[1]) - (u[1] - o[1]) * (v[0] - o[0])
    d1, d2, d3, d4 = cross(p, q, a), cross(p, q, b), cross(a, b, p), cross(a, b, q)
    if ((d1 > 0) != (d2 > 0)) and ((d3 > 0) != (d4 > 0)):
        return 0.0
    return min(pt_seg(p, a, b), pt_seg(q, a, b), pt_seg(a, p, q), pt_seg(b, p, q))


# points where a direction change is allowed: vias and pads (any net)
fixed = set(key((x, y)) for _, x, y, _ in vias)
for x, y, _, _ in pads:
    fixed.add(key((x, y)))
# protected stubs end on plane vias: their points are fixed too
for net, w in keep:
    m = re.search(r'\(path (\S+) ([\d.]+)\s+([-\d.\s]+)\)', w)
    p = [float(v) / SK for v in m.group(3).split()]
    for i in range(0, len(p), 2):
        fixed.add(key((p[i], p[i + 1])))


def room(net, layer, w, a, b, skip):
    """True if segment ab of width w keeps the clearance from other nets"""
    lo_x, hi_x = min(a[0], b[0]), max(a[0], b[0])
    lo_y, hi_y = min(a[1], b[1]), max(a[1], b[1])
    m = w / 2 + CLEAR + 3.0      # nothing wider than 6 mm comes closer than this

    def far(x1, y1, x2, y2):
        return x2 < lo_x - m or x1 > hi_x + m or y2 < lo_y - m or y1 > hi_y + m
    for i, (n2, l2, w2, c, d) in enumerate(segs):
        if n2 == net or l2 != layer or i in skip:
            continue
        if far(min(c[0], d[0]), min(c[1], d[1]), max(c[0], d[0]), max(c[1], d[1])):
            continue
        if seg_dist(a, b, c, d) < w / 2 + w2 / 2 + CLEAR:
            return False
    for n2, x, y, r in vias:
        if n2 == net or far(x, y, x, y):
            continue
        if seg_dist(a, b, (x, y), (x, y)) < w / 2 + r + CLEAR:
            return False
    for x, y, rs, pn in pads:
        if pn == unq(net) or far(x, y, x, y):
            continue
        r = rs.get(layer, rs.get('*'))
        if r is None:
            continue
        if seg_dist(a, b, (x, y), (x, y)) < w / 2 + r + CLEAR:
            return False
    return True


# corners: points with exactly two segments of the same net and layer
ends = collections.defaultdict(list)     # (net, layer, point) -> [(seg index, 0|1)]
for i, (net, layer, w, a, b) in enumerate(segs):
    ends[(net, layer, key(a))].append((i, 0))
    ends[(net, layer, key(b))].append((i, 1))

done, left = 0, []
new = []
for (net, layer, pk), lst in list(ends.items()):
    if len(lst) != 2 or pk in fixed:
        continue
    (i, ei), (j, ej) = lst
    si, sj = segs[i], segs[j]
    P = si[3] if ei == 0 else si[4]
    Ai = si[4] if ei == 0 else si[3]      # far end of leg i
    Aj = sj[4] if ej == 0 else sj[3]
    ui = (Ai[0] - P[0], Ai[1] - P[1])
    uj = (Aj[0] - P[0], Aj[1] - P[1])
    li, lj = math.hypot(*ui), math.hypot(*uj)
    if li == 0 or lj == 0:
        continue
    cosang = (ui[0] * uj[0] + ui[1] * uj[1]) / (li * lj)
    if abs(cosang) > 0.02:                  # not a 90 degree bend
        continue
    ui = (ui[0] / li, ui[1] / li)
    uj = (uj[0] / lj, uj[1] / lj)
    w = min(si[2], sj[2])
    dmax = 0.45 * min(li, lj)
    ok = None
    for f in (1, 0.75, 0.5, 1 / 3, 0.25):
        d = min(CH * f, dmax)
        A = (P[0] + ui[0] * d, P[1] + ui[1] * d)
        B = (P[0] + uj[0] * d, P[1] + uj[1] * d)
        if room(net, layer, w, A, B, {i, j}):
            ok = (A, B)
            break
        if d == dmax:
            break
    if ok is None:
        left.append((unq(net), layer, P[0], -P[1]))
        continue
    A, B = ok
    # shorten both legs to the chamfer points, add the 45 degree segment
    if ei == 0:
        si[3] = A
    else:
        si[4] = A
    if ej == 0:
        sj[3] = B
    else:
        sj[4] = B
    seg = [net, layer, w, A, B]
    segs.append(seg)
    done += 1

# ---------------------------------------------------------------- write back
by_net = collections.defaultdict(list)
for net, layer, w, a, b in segs:
    by_net[net].append('(wire (path %s %d  %d %d  %d %d))' % (
        layer, round(w * SK), round(a[0] * SK), round(a[1] * SK), round(b[0] * SK), round(b[1] * SK)))
out = []
last = 0
for net in nets_order:
    s, e = net_body_span[net]
    body = T[s:e]
    # drop the unprotected wires, then append the segments before the net's ')'
    spans = [(ws, we) for ws, we in scopes(body, 'wire') if '(type protect)' not in body[ws:we]]
    for ws, we in reversed(spans):
        body = body[:body.rfind('\n', 0, ws)] + body[we:]
    if by_net[net]:
        k = body.rstrip().rfind(')')
        body = body[:k].rstrip() + '\n        ' + '\n        '.join(by_net[net]) + '\n      ' + body[k:]
    out.append(T[last:s])
    out.append(body)
    last = e
out.append(T[last:])
open(dst, 'w').write(''.join(out))

print('%d bends of 90 degrees chamfered (up to %.2f mm per leg)' % (done, CH))
print('%d bends of 90 degrees left (no room): fix them by hand' % len(left))
for net, layer, x, y in sorted(left):
    print('  %-20s %-6s (%.3f, %.3f) mm' % (net, layer, x, y))
