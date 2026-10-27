# SPDX-License-Identifier: GPL-2.0-or-later
# Implements: SPEC-120, SPEC-121 (as a reader; test fixture generator for G3's unit tests)
# Writes mock-model descriptions for the G3 unit tests from the user's own game data (never committed):
#   python gen_mockmodels.py [ghoul_root] [out_dir]
# defaults: C:\openmerc-data\ghoul (extracted paks), C:\openmerc-data\g3\mock
# Each file lists the model's materials, meshes (with visibility after the set's _poff.gsq), null parts (bolts),
# sequences named by the set's .gsq and the notes of those sequences.
import glob, os, sys
here = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.abspath(os.path.join(here, '..', '..', '..', '..', 'tools', 'formats')))
import ghb  # noqa: E402

root = sys.argv[1] if len(sys.argv) > 1 else r'C:\openmerc-data\ghoul'
out = sys.argv[2] if len(sys.argv) > 2 else r'C:\openmerc-data\g3\mock'
os.makedirs(out, exist_ok=True)

def find(rel):
    rel = rel.replace(os.sep, '/').lower()
    for pak in sorted(os.listdir(root)):
        base = os.path.join(root, pak, 'ghoul')
        d, f = os.path.split(rel)
        dd = os.path.join(base, d)
        if os.path.isdir(dd):
            for n in os.listdir(dd):
                if n.lower() == f:
                    return os.path.join(dd, n)
    return None

def gsq_items(d, name, seen=None):
    seen = seen or set()
    p = find(d + '/' + name + '.gsq') or find('comskin/' + name + '.gsq')
    if not p or p in seen:
        return []
    seen.add(p)
    items = []
    for line in open(p, encoding='latin-1').read().splitlines():
        t = line.strip()
        if not t:
            continue
        if t.startswith('#'):
            items += gsq_items(d, t[1:], seen)
        else:
            items.append(t)
    return items

CASES = [('enemy/meso', 'irqsoldier1', 'MESO_IRQ1A'), ('enemy/meso', 'irqman2', 'MESO_IRQ1A'),
         ('enemy/meso', 'skinhead1', 'MESO_NYC1'), ('enemy/meso', 'sibguard', 'MESO_SIB1'),
         ('enemy/meso', 'skinboss', 'MESO_NYC3'), ('enemy/ecto', 'bum', 'ECTO'), ('enemy/ecto', 'commander', 'ECTO'),
         ('enemy/female', 'irqwoman1', 'FEM_NPC'), ('enemy/female', 'raider', 'FEMALE'),
         ('enemy/rottweiler', 'rottweiler', 'ROTTWEILER'), ('enemy/bull', 'bull', 'BULL')]
for d, setname, level in CASES:
    p = find(d + '/' + level + '.ghb')
    if not p:
        print('missing', d, level)
        continue
    m = ghb.parse(open(p, 'rb').read())
    off = set(x.lower() for x in gsq_items(d, setname + '_poff'))
    seqs = set(x.lower() for x in gsq_items(d, setname))
    lines = ['model %s %s %s' % (d, setname, level)]
    for mat in m['materials']:
        lines.append('mat %s' % mat['name'])
    for me in m['meshes']:
        vis = me['visible'].get('value', 1) if isinstance(me['visible'], dict) else 1
        if me['name'].lower() in off:
            vis = 0
        lines.append('mesh %s %d' % (me['name'], 1 if vis else 0))
    for n in m['nodes']:
        lines.append('node %s' % n['name'])
    notes = set()
    for s in m['sequences']:
        if s['name'].lower() in seqs or not seqs:
            lines.append('seq %s' % s['name'])
            for e in s.get('events', []):
                nm = e.get('name') if isinstance(e, dict) else None
                if nm:
                    notes.add(nm)
    for n in sorted(notes):
        lines.append('note %s' % n)
    fn = os.path.join(out, '%s_%s.txt' % (d.split('/')[1], setname))
    open(fn, 'w', newline='\n').write('\n'.join(lines) + '\n')
    print(fn, len(m['meshes']), 'meshes', len([l for l in lines if l.startswith('seq')]), 'seqs')

# bolt-on models (Enemy/bolt)
bd = None
for pak in os.listdir(root):
    c = os.path.join(root, pak, 'ghoul', 'enemy', 'bolt')
    if os.path.isdir(c):
        bd = c
lines = []
for f in sorted(glob.glob(os.path.join(root, '*', 'ghoul', 'enemy', 'bolt', '*.ghb'))):
    m = ghb.parse(open(f, 'rb').read())
    nm = os.path.splitext(os.path.basename(f))[0].lower()
    lines.append('model enemy/bolt %s' % nm)
    for mat in m['materials']:
        lines.append('mat %s' % mat['name'])
    for me in m['meshes']:
        lines.append('mesh %s 1' % me['name'])
    for n in m['nodes']:
        lines.append('node %s' % n['name'])
    for s in m['sequences']:
        lines.append('seq %s' % s['name'])
open(os.path.join(out, 'bolt_all.txt'), 'w', newline='\n').write('\n'.join(lines) + '\n')
print('bolt models written')
