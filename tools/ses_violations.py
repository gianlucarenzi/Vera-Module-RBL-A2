#!/usr/bin/env python3
"""Sort the clearance violations of a fastroute report and rip up the
movable copper involved, so that fastroute can route it again.

The report (--report) lists every clearance violation with its point, layer
and actual distance, but not the nets: they come from the copper found at
that point (traces and vias of the session, pads of the DSN). Each violation
falls into one of these groups:
  - same net: not a violation for KiCad's DRC, ignored;
  - rounding: fastroute sees pads and vias as octagons, so it measures a hair
    less than KiCad; within TOL_MM of the clearance it is ignored;
  - real, movable: at least one of the two pieces of copper is a trace or a
    via that is not fixed: the traces and vias of the session near the point,
    on the nets involved, are removed (fastroute reconnects them);
  - real, fixed: only pads and protected items (the plane vias): listed, to
    fix by hand.

usage: ses_violations.py design.dsn in.ses report.json out.ses [TOL_MM]
  prints the groups, then "RIPPED <n>" (n = items removed); out.ses is in.ses
  without them. TOL_MM defaults to 0.002.
"""
import collections
import json
import math
import re
import sys

dsn_path, ses_path, rep_path, out_path = sys.argv[1:5]
TOL = float(sys.argv[5]) if len(sys.argv) > 5 else 0.002
NEAR = 0.03       # mm: copper whose edge is this close to the reported gap


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
    for m in re.finditer(r'\(%s[\s)]' % re.escape(head), s):
        yield m.start(), scope_end(s, m.start())


NAME = r'("[^"]*"|[^\s()]+)'


def unq(n):
    return n[1:-1] if n.startswith('"') else n


def per_mm(unit, n=1.0):
    return n * {'um': 1000.0, 'mm': 1.0, 'mil': 1 / 0.0254, 'inch': 1 / 25.4}[unit]


def pt_seg(c, u, v):
    dx, dy = v[0] - u[0], v[1] - u[1]
    L = dx * dx + dy * dy
    t = 0 if L == 0 else max(0, min(1, ((c[0] - u[0]) * dx + (c[1] - u[1]) * dy) / L))
    return math.hypot(c[0] - u[0] - t * dx, c[1] - u[1] - t * dy)


# ------------------------------------------------------------- the design
D = open(dsn_path).read()
mu = re.search(r'\(unit (\w+)\)', D)
DK = per_mm(mu.group(1) if mu else re.search(r'\(resolution (\w+)', D).group(1))

pad_r = {}
for s, e in scopes(D, 'padstack'):
    body = D[s:e]
    name = unq(re.match(r'\(padstack\s+' + NAME, body).group(1))
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

pin_net = {}
nw = D[D.find('(network'):]
nw = nw[:scope_end(nw, 0)]
for s, e in scopes(nw, 'net'):
    body = nw[s:e]
    net = unq(re.match(r'\(net\s+' + NAME, body).group(1))
    pm = re.search(r'\(pins\s+([^)]*)\)', body)
    if pm:
        for p in re.findall(r'"[^"]*"|\S+', pm.group(1)):
            pin_net[unq(p)] = net

images = {}
lib = D[D.find('(library'):]
for s, e in scopes(lib, 'image'):
    body = lib[s:e]
    name = unq(re.match(r'\(image\s+' + NAME, body).group(1))
    images[name] = [(unq(m.group(1)), unq(m.group(2)), float(m.group(3)), float(m.group(4)))
                    for m in re.finditer(r'\(pin\s+' + NAME + r'(?:\s+\(rotate [-\d.]+\))?\s+' + NAME +
                                         r'\s+(-?[\d.]+)\s+(-?[\d.]+)\)', body)]

pads = []     # (x, y, {layer: r}, net, "REF-PIN")  mm, y up
pl = D[D.find('(placement'):]
pl = pl[:scope_end(pl, 0)]
for s, e in scopes(pl, 'component'):
    body = pl[s:e]
    img = unq(re.match(r'\(component\s+' + NAME, body).group(1))
    for m in re.finditer(r'\(place\s+' + NAME + r'\s+(-?[\d.]+)\s+(-?[\d.]+)\s+(front|back)\s+(-?[\d.]+)', body):
        ref = unq(m.group(1))
        px, py, back, rot = float(m.group(2)), float(m.group(3)), m.group(4) == 'back', math.radians(float(m.group(5)))
        for ps, pid, x, y in images.get(img, []):
            if back:
                x = -x
            xr, yr = x * math.cos(rot) - y * math.sin(rot), x * math.sin(rot) + y * math.cos(rot)
            rs = pad_r.get(ps, {})
            if back:
                rs = {{'F.Cu': 'B.Cu', 'B.Cu': 'F.Cu'}.get(k, k): v for k, v in rs.items()}
            pads.append(((px + xr) / DK, (py + yr) / DK, rs, pin_net.get('%s-%s' % (ref, pid)), '%s-%s' % (ref, pid)))

# ------------------------------------------------------------- the session
T = open(ses_path).read()
rm = re.search(r'\(routes\s.*?\(resolution (\w+) (\d+)\)', T, re.S)
SK = per_mm(rm.group(1), float(rm.group(2)))
no = T.find('(network_out')
ne = scope_end(T, no)
items = []    # dict(kind, net, layers, geom, span, fixed)
for s, e in scopes(T[no:ne], 'net'):
    s += no
    e += no
    body = T[s:e]
    net = unq(re.match(r'\(net\s+' + NAME, body).group(1))
    for ws, we in scopes(body, 'wire'):
        w = body[ws:we]
        m = re.search(r'\(path (\S+) ([\d.]+)\s+([-\d.\s]+)\)', w)
        p = [float(v) / SK for v in m.group(3).split()]
        items.append(dict(kind='trace', net=net, layer=m.group(1), w=float(m.group(2)) / SK,
                          pts=[(p[i], p[i + 1]) for i in range(0, len(p), 2)],
                          span=(s + ws, s + we), fixed='(type protect)' in w))
    for vs, ve in scopes(body, 'via'):
        v = body[vs:ve]
        m = re.match(r'\(via\s+' + NAME + r'\s+(-?[\d.]+)\s+(-?[\d.]+)', v)
        rs = pad_r.get(unq(m.group(1)), {})
        items.append(dict(kind='via', net=net, r=max(rs.values()) if rs else 0.4,
                          xy=(float(m.group(2)) / SK, float(m.group(3)) / SK),
                          span=(s + vs, s + ve), fixed='(type protect)' in v))


def edge_dist(it, c, layer):
    """distance from point c to the copper edge of a session item (None: other layer)"""
    if it['kind'] == 'trace':
        if it['layer'] != layer:
            return None
        return min(pt_seg(c, a, b) for a, b in zip(it['pts'], it['pts'][1:])) - it['w'] / 2
    return math.hypot(c[0] - it['xy'][0], c[1] - it['xy'][1]) - it['r']


# ------------------------------------------------------------- sort them
rep = json.load(open(rep_path))
groups = collections.defaultdict(list)
rip = set()
seen = set()
for v in rep.get('clearance_violations', []):
    layer, (x, y) = v['layer'], v['xy']
    # a through via is reported once per layer: keep the first
    k = (round(x, 3), round(y, 3), round(v['actual_mm'], 4))
    if k in seen:
        continue
    seen.add(k)
    c = (x, -y)                      # report: KiCad orientation (y down)
    half = v['actual_mm'] / 2 + NEAR
    near = []                        # (net, movable item index or None, label)
    for i, it in enumerate(items):
        d = edge_dist(it, c, layer)
        if d is not None and d <= half:
            near.append((it['net'], None if it['fixed'] else i, it['kind'] + ('' if not it['fixed'] else ' (fixed)')))
    for px, py, rs, net, pin in pads:
        r = rs.get(layer)
        if r is not None and math.hypot(c[0] - px, c[1] - py) - r <= half + 0.3:
            # the enclosing circle overestimates a pad: accept it more loosely,
            # it only names the net of the pad
            near.append((net, None, 'pad ' + pin))
    nets = set(unq(n) if n else '?' for n, _, _ in near)
    where = '%s (%.3f, %.3f) mm, %.3f < %.3f mm' % (layer, x, y, v['actual_mm'], v['clearance_mm'])
    if len(nets) <= 1:
        groups['same net (ignored)'].append(where + '  ' + ','.join(nets))
    elif v['actual_mm'] >= v['clearance_mm'] - TOL:
        groups['rounding (ignored)'].append(where)
    else:
        movable = [i for _, i, _ in near if i is not None]
        if movable:
            rip.update(movable)
            groups['real, movable (ripped up)'].append(where + '  ' + ' / '.join(sorted(nets)))
        else:
            groups['real, fixed (by hand)'].append(where + '  ' + ' / '.join(sorted(set(l for _, _, l in near))) +
                                                   '  ' + ' / '.join(sorted(nets)))

out = T
for i in sorted(rip, key=lambda i: -items[i]['span'][0]):
    s, e = items[i]['span']
    out = out[:out.rfind('\n', 0, s)] + out[e:]
open(out_path, 'w').write(out)

for g in ('real, movable (ripped up)', 'real, fixed (by hand)', 'rounding (ignored)', 'same net (ignored)'):
    print('%-28s %d' % (g, len(groups[g])))
    if g.startswith('real'):
        for w in groups[g]:
            print('    ' + w)
print('RIPPED %d' % len(rip))
