# SPDX-License-Identifier: GPL-2.0-or-later
# Implements: SPEC-721 (human gore-zone tables), SPEC-AMEND-005 G3.9
# Generates game/src/damage/g3_gore_tables.inc from the zone, sever-group and variant tables of
# docs/specs/SPEC-721-gore-and-dismemberment.md (male soldier, civilian/officer, female families).
#   python game/src/damage/tools/gen_gore_tables.py
# The variant tables are split into priority chains below (the spec: "within a chain (consecutive rows covering
# the same zones) the first shown mesh wins"); the chain split is G3's reading of the tables.
import os, re, sys

here = os.path.dirname(os.path.abspath(__file__))
repo = os.path.abspath(os.path.join(here, '..', '..', '..', '..'))
spec = os.path.join(repo, 'docs', 'specs', 'SPEC-721-gore-and-dismemberment.md')
out = os.path.join(here, '..', 'g3_gore_tables.inc')
L = open(spec, encoding='utf-8').read().splitlines()

ZONES = ['HEAD_FRONT', 'HEAD_BACK', 'NECK', 'SHLDR_RIGHT_FRONT', 'SHLDR_RIGHT_BACK', 'SHLDR_LEFT_FRONT',
         'SHLDR_LEFT_BACK', 'CHEST_FRONT', 'CHEST_BACK', 'ARM_UPPER_RIGHT', 'ARM_UPPER_LEFT', 'ARM_LOWER_RIGHT',
         'ARM_LOWER_LEFT', 'GUT_FRONT', 'GUT_BACK', 'GROIN', 'LEG_UPPER_RIGHT_FRONT', 'LEG_UPPER_RIGHT_BACK',
         'LEG_UPPER_LEFT_FRONT', 'LEG_UPPER_LEFT_BACK', 'LEG_LOWER_RIGHT', 'LEG_LOWER_LEFT', 'FOOT_RIGHT',
         'FOOT_LEFT', 'GUT_FRONT_EXTRA', 'GUT_BACK_EXTRA']
ZI = {z: i for i, z in enumerate(ZONES)}
SEG = {'end': 'SEG_END', 'torso': 'SEG_TORSO', 'neck': 'SEG_NECK', 'shoulder': 'SEG_SHOULDER', 'head': 'SEG_HEAD',
       'right upper arm': 'SEG_RUARM', 'left upper arm': 'SEG_LUARM', 'right forearm': 'SEG_RFARM',
       'left forearm': 'SEG_LFARM', 'right thigh': 'SEG_RTHIGH', 'left thigh': 'SEG_LTHIGH',
       'right calf': 'SEG_RCALF', 'left calf': 'SEG_LCALF', 'right foot': 'SEG_RFOOT', 'left foot': 'SEG_LFOOT',
       'groin': 'SEG_GROIN'}
FIELD = {'wound mesh': 'VF_WOUND', 'owner': 'VF_OWNER', 'body cap': 'VF_BODYCAP', 'piece cap': 'VF_PIECECAP',
         'body cap bolt': 'VF_BODYCAPBOLT', 'piece cap bolt': 'VF_PIECECAPBOLT', 'next zone of piece': 'VF_NEXT',
         'sever group': 'VF_SEVER'}

# priority chains (first shown mesh of a chain wins)
CHAINS = {
    'meso': [['_baldhead', '_crewhead', '_beardhead', '_hoodhead', '_bosshead', '_maskhead', '_helmethead'],
             ['_comface', '_beardface', '_helmetface', '_bossface'],
             ['_neck', '_bossneck'],
             ['_tightchest', '_bulkychest', '_chest_b_t', '_bosschest', '_armorchest', '_maskchest'],
             ['_maskneck', '_hoodneck'],
             ['_rbicep_b_b', '_rbicep_b_t', '_rbicep_t_b', '_rbicep_t_t', '_rbossbicep'],
             ['_lbicep_b_b', '_lbicep_b_t', '_lbicep_t_b', '_lbicep_t_t', '_lbossbicep'],
             ['_lforearm_b_b', '_lforearm_b_t', '_lforearm_t_t', '_lforearm_t_b', '_lforearm_t_c', '_lbossforearm'],
             ['_rforearm_b_b', '_rforearm_b_t', '_rforearm_t_t', '_rforearm_t_b', '_rforearm_t_c', '_rbossforearm'],
             ['_coathips', '_tighthips', '_bulkyhips', '_bosships'],
             ['_rtightthigh', '_rbulkythigh', '_rbossthigh'],
             ['_ltightthigh', '_lbulkythigh', '_lbossthigh'],
             ['_ltightcalf', '_lcalf_t_b', '_lbulkycalf', '_lbosscalf', '_lcalfcuff'],
             ['_rtightcalf', '_rcalf_t_b', '_rbulkycalf', '_rbosscalf', '_rcalfcuff'],
             ['_lbootfoot', '_lshoefoot'],
             ['_rbootfoot', '_rshoefoot']],
    'ecto': [['_headbald', '_headcrew', '_headbeard', '_headhelmet'],
             ['_chest', '_coatchest'],
             ['_rbicep'], ['_lbicep'],
             ['_lforearm', '_lforearmcuff'], ['_rforearm', '_rforearmcuff'],
             ['_coathips', '_hips'],
             ['_rthigh'], ['_lthigh'],
             ['_lcalf', '_lcalfcuff'], ['_rcalf', '_rcalfcuff']],
    'female': [['_crewhair', '_MEDMHAIRMHEAD'], ['_comface'], ['_neck'], ['_tightchest', '_armorchest'],
               ['_rtightbicep'], ['_ltightbicep'], ['_ltightforearm'], ['_rtightforearm'],
               ['_dresships', '_tighthips'], ['_rtightthigh'], ['_ltightthigh'], ['_ltightcalf'], ['_rtightcalf'],
               ['_lheels'], ['_rheels']],
}
# Sever groups the spec abbreviates as "`_rthigh`…" (variant rows of the ecto and female thighs): the complete groups
# of SPEC-AMEND-005 G3.9 (thigh mesh, thigh bolt, the lower-leg zone, then the calf group).
ABBREV = {
    ('ecto', '_rthigh'): ['_rthigh', 'b:abolt_thigh_r', '+zone:LEG_LOWER_RIGHT', '_rcalf', '_rcalfcuff',
                          '_cap_rcalfcuff_rfoot', 'b:abolt_calf_r', '_rfoot'],
    ('ecto', '_lthigh'): ['_lthigh', 'b:abolt_thigh_l', '+zone:LEG_LOWER_LEFT', '_lcalf', '_lcalfcuff',
                          '_cap_lcalfcuff_lfoot', 'b:abolt_calf_l', '_lfoot'],
    ('female', '_rtightthigh'): ['_rtightthigh', 'b:abolt_thigh_r', '+zone:LEG_LOWER_RIGHT', '_rtightcalf',
                                 'b:abolt_calf_r', '_rheels'],
    ('female', '_ltightthigh'): ['_ltightthigh', 'b:abolt_thigh_l', '+zone:LEG_LOWER_LEFT', '_ltightcalf',
                                 'b:abolt_calf_l', '_lheels'],
}


def cells(l):
    return [c.strip() for c in l.strip().strip('|').split('|')]


def tick(s):
    s = s.strip()
    if s in ('—', '-', ''):
        return None
    m = re.match(r'`([^`]*)`', s)
    return m.group(1) if m else s


def zone_of(s):
    s = s.strip()
    if s.startswith('GZ_'):
        s = s[3:]
    return ZI[s]


def section(start_line_prefix):
    return next(n for n, l in enumerate(L) if l.startswith(start_line_prefix))


def parse_family(name, head):
    i = section(head)
    # zone table
    j = next(n for n in range(i, len(L)) if L[n].startswith('| # | zone'))
    fixed = []
    n = j + 2
    while L[n].startswith('|'):
        c = cells(L[n])
        idx = int(c[0])
        assert ZONES[idx] == c[1][3:], (name, c)
        gore, pcb, bcb = tick(c[2]), tick(c[3]), tick(c[4])
        cm, cmb = None, None
        if tick(c[5]):
            m = re.match(r'`([^`]*)`\s*\(`([^`]*)`\)', c[5])
            cm, cmb = m.group(1), m.group(2)
        seg = SEG[c[6]]
        nxt = -1 if tick(c[7]) is None else zone_of(c[7])
        link = -1 if tick(c[8]) is None else zone_of(c[8])
        fixed.append((gore, pcb, bcb, cm, cmb, seg, nxt, link))
        n += 1
    assert len(fixed) == 26, name
    # sever groups
    k = next(x for x in range(n, len(L)) if L[x].startswith('Sever groups'))
    k = next(x for x in range(k, len(L)) if L[x].startswith('| zone(s)')) + 2
    sever = []
    while L[k].startswith('|'):
        c = cells(L[k])
        zs = [zone_of(z) for z in c[0].split(',')]
        seg = SEG[c[1]]
        items = []
        txt = c[2]
        if txt not in ('—', '(groin marker)'):
            for part in [p.strip() for p in txt.split(',')]:
                if part.startswith('bolt '):
                    items.append('b:' + tick(part[5:]))
                elif part == '+head & both upper arms':
                    items.append('+headarms')
                elif part.startswith('+') and 'lower leg zone' in part:
                    items.append('+zone:' + ('LEG_LOWER_RIGHT' if 'right' in part else 'LEG_LOWER_LEFT'))
                else:
                    items.append(tick(part))
        sever.append((zs, seg, items))
        k += 1
    # variants
    v = next(x for x in range(k, len(L)) if L[x].startswith('Variant table'))
    v = next(x for x in range(v, len(L)) if L[x].startswith('| visible mesh')) + 2
    rows = []
    while v < len(L) and L[v].startswith('|'):
        c = cells(L[v])
        mesh = tick(c[0])
        sets = []
        for a in [x.strip() for x in c[1].split(';')]:
            m = re.match(r'([A-Z_]+) (wound mesh|owner|body cap bolt|piece cap bolt|body cap|piece cap|next zone of piece|sever group) = (.*)$', a)
            assert m, (name, a)
            z, f, val = zone_of(m.group(1)), FIELD[m.group(2)], m.group(3).strip()
            if f == 'VF_NEXT':
                sets.append((z, f, None, zone_of(val)))
            elif f == 'VF_SEVER':
                sets.append((z, f, mesh, 0))   # abbreviated group, completed from ABBREV
            else:
                sets.append((z, f, tick(val), 0))
        rows.append((mesh, sets))
        v += 1
    return fixed, sever, rows


def cstr(s):
    return 'NULL' if s is None else '"%s"' % s


def emit(name, fixed, sever, rows):
    o = []
    o.append('static const G3ZoneFixed g3_%sFixed[26] = {' % name)
    for i, (g, p, b, cm, cmb, seg, nx, lk) in enumerate(fixed):
        o.append('\t{%s, %s, %s, %s, %s, %s, %d, %d}, // %d %s' % (cstr(g), cstr(p), cstr(b), cstr(cm), cstr(cmb),
                                                                    seg, nx, lk, i, ZONES[i]))
    o.append('};')
    for gi_, (zs, seg, items) in enumerate(sever):
        o.append('static const char *const g3_%sSeverItems%d[] = {%s};' % (
            name, gi_, ', '.join([cstr(x) for x in items] + ['NULL'])))
    for (fam, mesh), items in ABBREV.items():
        if fam == name:
            o.append('static const char *const g3_%sAbbrev_%s[] = {%s};' % (
                name, mesh.strip('_'), ', '.join([cstr(x) for x in items] + ['NULL'])))
    o.append('static const G3SeverDef g3_%sSever[] = {' % name)
    for gi_, (zs, seg, items) in enumerate(sever):
        zz = (zs + [-1] * 12)[:12]
        o.append('\t{%s, {%s}, g3_%sSeverItems%d},' % (seg, ', '.join(map(str, zz)), name, gi_))
    o.append('\t{SEG_END, {-1}, NULL}};')
    chains = CHAINS[name]
    chain_of = {}
    for ci, ch in enumerate(chains):
        for m in ch:
            chain_of[m] = ci
    for mesh, sets in rows:
        if mesh not in chain_of:
            sys.exit('%s: mesh %s has no chain' % (name, mesh))
    o.append('static const G3VarRow g3_%sVariants[] = {' % name)
    for mesh, sets in rows:
        ss = []
        for (z, f, val, iv) in sets:
            if f == 'VF_SEVER':
                ss.append('{%d, %s, NULL, 0, g3_%sAbbrev_%s}' % (z, f, name, mesh.strip('_')))
            else:
                ss.append('{%d, %s, %s, %d, NULL}' % (z, f, cstr(val), iv))
        ss.append('{-1, 0, NULL, 0, NULL}')
        o.append('\t{"%s", %d, {%s}},' % (mesh, chain_of[mesh], ', '.join(ss)))
    o.append('\t{NULL, -1, {{-1, 0, NULL, 0, NULL}}}};')
    return '\n'.join(o)


fams = [('meso', '#### Male soldier family'), ('ecto', '#### Civilian/officer family'),
        ('female', '#### Female family')]
text = ['// SPDX-License-Identifier: GPL-2.0-or-later',
        '// Generated by tools/gen_gore_tables.py from SPEC-721 (do not edit).',
        '// Implements: SPEC-721 (human gore-zone, sever-group and variant tables)']
maxsets = 0
for name, head in fams:
    fixed, sever, rows = parse_family(name, head)
    maxsets = max([maxsets] + [len(s) for _, s in rows])
    text.append(emit(name, fixed, sever, rows))
    print(name, len(fixed), 'zones', len(sever), 'sever groups', len(rows), 'variant rows')
text.append('// longest variant row: %d settings' % maxsets)
open(out, 'w', encoding='utf-8', newline='\n').write('\n'.join(text) + '\n')
print('wrote', out, 'max sets', maxsets)
