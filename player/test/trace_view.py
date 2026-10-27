# SPDX-License-Identifier: GPL-2.0-or-later
# Implements: SPEC-003, SPEC-601 (reading and comparing omptrace logs)
#
# Normalises an omptrace log so that two sessions (original module vs OpenMerc's) can be compared:
#  - heap addresses become symbolic (inv1, inv2, ... per inventory; "ptr" elsewhere);
#  - precache chatter (Com_sprintf, SoundIndex, EffectIndex) and plain think calls are dropped unless asked for;
#  - player-state lines keep only the words the player module writes (view weapon 0x6C, weapon id 0x72,
#    clip/reserve 0x74, weapon-state sequence 0x78, fov 0x94) plus the weapon kick copy 0x40;
#  - object descriptions (OBJ) are kept once.
# usage: trace_view.py show <trace> [--precache] [--all-ps] [--client] [--server]
#        trace_view.py diff <traceA> <traceB> [--precache] [-n context]
#        trace_view.py story <trace> [--client|--server] [--fold-idle]
#        trace_view.py storydiff <traceA> <traceB> [--client|--server] [--fold-idle] [-n context]
# "story": a time-free list of what the module did (sequences, shots, sounds, effects, player-state counters, events,
# inventories), consecutive repeats collapsed. Multiplayer sessions run in real time, so their traces are compared
# as stories; --fold-idle maps the idle variations (chosen by the time-seeded random) to their base name.
import difflib
import re
import sys

PS_KEEP = {'40', '6c', '70', '74', '78', '94'}
HEX8 = re.compile(r'\b[0-9A-Fa-f]{8}\b')


QUIET = {21, 22, 23, 25, 26, 27, 28, 29, 30, 32, 33, 35, 37, 39, 41, 43, 45, 47, 49, 51, 53, 55, 60, 61, 68, 70,
         71, 72, 73, 74}


def normalise(path, precache=False, all_ps=False, side=None, nested=False):
    inv_names = {}
    out = []
    seen_obj = set()
    dropped = False
    counter = [0]
    for raw in open(path, encoding='latin-1'):
        line = raw.rstrip('\n')
        if line.startswith('==='):
            if 'GetPlayer' in line:
                out.append(re.sub(r'-> [0-9A-F]+', '->', line))
            continue
        if len(line) < 12:
            continue
        ctx, rest = line[0], line[1:]
        if side and ctx != side:
            continue
        m = re.match(r'\s*(-?[0-9.]+) (.*)', rest)
        if not m:
            continue
        t, body = m.group(1), m.group(2)
        if body.startswith('  -> '):
            if dropped:
                continue
        dropped = False
        if not precache and re.match(r'(Com_sprintf|sv\.SoundIndex|sv\.EffectIndex|RegisterGsq)', body):
            dropped = True
            continue
        if re.match(r'INV03 [0-9A-F]+$', body):
            continue
        mq = re.match(r'(?:  )?INV(\d\d)\b', body)
        if mq and int(mq.group(1)) in QUIET and not nested:
            # getters called from inside another inventory method (the module's internal structure)
            if body.startswith('  ') or ' <' in body:
                continue
        if body.startswith('DESC'):
            body = re.sub(r'model=[0-9a-f]+', 'model=?', body)

        def inv_sym(mm):
            v = mm.group(0).upper()
            if v not in inv_names:
                inv_names[v] = 'inv?%d' % (len(inv_names) + 1)
            return inv_names[v]

        # inventories are named by creation order (addresses are reused differently by the two modules)
        mnew = re.match(r'NEWINV ([0-9A-F]{8})', body)
        if mnew:
            counter[0] += 1
            inv_names[mnew.group(1)] = 'inv%d' % counter[0]

        if re.match(r'(NEWINV|DELINV|BLK|INST|FULL|INV\d\d|REBUILD|BTN) ', body):
            body = re.sub(r'(?<=^NEWINV )[0-9A-F]{8}|(?<=^DELINV )[0-9A-F]{8}|(?<=^BLK )[0-9A-F]{8}|'
                          r'(?<=^INST )[0-9A-F]{8}|(?<=^FULL )[0-9A-F]{8}|(?<=^REBUILD )[0-9A-F]{8}|'
                          r'(?<=^INV\d\d )[0-9A-F]{8}|(?<=^BTN )[0-9A-F]{8}', inv_sym, body)
        if body.startswith('INV61'):
            continue
        if re.match(r'INV\d\d ', body):
            # pointer arguments (heap/stack addresses) differ between runs
            body = re.sub(r'(?<= )[0-9a-f]{8}\b', lambda mm: 'ptr' if int(mm.group(0), 16) >= 0x01000000 else mm.group(0), body)
        if body.startswith('OBJ'):
            key = re.sub(r'OBJ [0-9A-F]+ ', '', body)
            if key in seen_obj:
                continue
            seen_obj.add(key)
            body = re.sub(r'OBJ [0-9A-F]+ (id=\d+ )?', 'OBJ ', body)
        if body.startswith('INST'):
            body = re.sub(r'id=\d+', 'id=#', body)
        if body.startswith('BLK'):
            body = re.sub(r' 130:ptr', '', body)
            if re.match(r'BLK \S+$', body):
                continue
        if body.startswith('PS'):
            words = re.findall(r'([0-9a-f]{2}):(\S+)', body)
            if not all_ps:
                words = [(o, v) for o, v in words if o in PS_KEEP]
            words = [(o, ('%s(id)' % v[:4]) if o == '70' else v) for o, v in words]
            # the view-weapon pointer itself only tells whether the allocator reused the address
            words = [(o, v) for o, v in words if not (o == '6c' and v == '@inst')]
            if not words:
                continue
            body = 'PS ' + ' '.join('%s:%s' % w for w in words)
        body = re.sub(r' \(eax [0-9a-f]+\)', '', body)
        if body.startswith('sv.ReliableByte'):
            mm = re.match(r'sv\.ReliableByte (\w+) (\w+)', body)
            if mm:
                body = 'sv.ReliableByte %02x %s' % (int(mm.group(1), 16) & 0xFF, mm.group(2))
        if body.startswith('sv.') or body.startswith('cl.'):
            # stack garbage in the extra discovery words and argument pointers
            body = re.sub(r' \?[0-9a-f]+', '', body)
            body = re.sub(r'\b[0-9a-f]{6,8}\b', 'ptr', body)
            body = re.sub(r'ReliableByte \w*([0-9a-f]{2}) ', lambda mm: 'ReliableByte %s ' % mm.group(1), body)
        if body.startswith('KICK') and 'e+' in body:
            continue
        out.append('%s %8s %s' % (ctx, t, body))
    return out


def story(lines, fold_idle=False):
    """time-free event list from normalised lines"""
    out = []
    ps_last = {}

    def emit(ctx, text):
        if not out or out[-1] != (ctx, text):
            out.append((ctx, text))

    for i, l in enumerate(lines):
        if l.startswith('==='):
            continue
        ctx, body = l[0], l[11:]
        if body.startswith('sv.GetFireTime'):
            # verbose traces log the result on the next line, plain traces only non-zero results inline
            res = body if '->' in body else (lines[i + 1][11:] if i + 1 < len(lines) else '')
            if '->' in res and '-> 0.0000' not in res:
                emit(ctx, 'firetime%s' % body[len('sv.GetFireTime')])
            continue
        m = re.search(r'seq="([^"]*)"', body)
        if body.startswith('BLK') and m and m.group(1):
            name = m.group(1)
            if fold_idle and name.lower().startswith('idle'):
                name = re.sub(r'(?i)^(idle)[^_]*', r'\1~', name)
            emit(ctx, 'seq ' + name)
            continue
        if body.startswith(('NEWINV', 'DELINV', 'FULL', 'REBUILD')):
            emit(ctx, re.sub(r' \(vt wrapped\)', '', body))
            continue
        if body.startswith('PS '):
            for o, v in re.findall(r'([0-9a-f]{2}):(\S+)', body):
                if o == '74':
                    val = int(v, 16)
                    text = 'clip %d reserve %d' % (val & 0xFFFF, val >> 16)
                elif o == '70':
                    text = 'viewweapon %d' % int(v[:4], 16)
                elif o == '78':
                    text = 'weaponseq %d' % (int(v, 16) & 0xFF)
                elif o == '94':
                    import struct
                    text = 'fov %g' % struct.unpack('<f', struct.pack('<I', int(v, 16)))[0]
                else:
                    continue
                if ps_last.get((ctx, o)) != text:
                    ps_last[(ctx, o)] = text
                    emit(ctx, text)
            continue
        mm = re.match(r'(sv\.FirePrimary|cl\.FirePrimaryFx)', body)
        if mm:
            emit(ctx, 'fire1')
            continue
        mm = re.match(r'(sv\.FireAlt|cl\.FireAltFx)', body)
        if mm:
            emit(ctx, 'fire2')
            continue
        mm = re.match(r'cl\.Note(Sound|Effect) "([^"]*)"', body)
        if mm:
            emit(ctx, '%s %s' % (mm.group(1).lower(), mm.group(2)))
            continue
        mm = re.match(r'sv\.(ReloadStarted|DropWeapon|DropItem|ItemUsed|PrintId|Cbuf_AddText|WriteChunk|ReadChunk|'
                      r'ReliableByte|ReliableData|Goggles|Use\w+|ClearFireTime\d|GetFireTime\d)(.*)', body)
        if mm:
            rest = re.sub(r'\bptr\b ?', '', mm.group(2)).strip()
            rest = re.sub(r' <\d\d$', '', rest)
            if mm.group(1).startswith('GetFireTime'):
                rest = '' if '-> 0.0000' in rest else 'set'
                if not rest:
                    continue
            emit(ctx, ('%s %s' % (mm.group(1), rest)).strip())
            continue
        if body.startswith('KICK'):
            v = body.split()[1:]
            if any(float(x) != 0 for x in v):
                emit(ctx, 'kick ' + ' '.join(v))
            continue
        if body.startswith('BTN'):
            mm = re.search(r'att=(\d+) alt=(\d+) x1=(\d+) x2=(\d+)', body)
            if mm:
                emit(ctx, 'buttons %s' % ' '.join(mm.groups()))
            continue
        mm = re.match(r'INV(69|70|77|78|04|09|10|11|06|07|08|36|34) (\S+)(.*)', body)
        if mm and not body.startswith('  '):
            rest = re.sub(r' <\d\d$', '', mm.group(3)).strip()
            if mm.group(1) == '04' and rest.startswith('""'):
                continue
            emit(ctx, ('INV%s %s %s' % (mm.group(1), mm.group(2), rest)).strip())
            continue
    return out


def main():
    args = sys.argv[1:]
    if not args:
        print(__doc__ if __doc__ else open(__file__).read().split('\n\n')[0])
        return
    precache = '--precache' in args
    all_ps = '--all-ps' in args
    side = 'C' if '--client' in args else 'S' if '--server' in args else None
    nested = '--nested' in args
    files = [a for a in args[1:] if not a.startswith('-') and not a.isdigit()]
    fold = '--fold-idle' in args
    if args[0] == 'story':
        for ctx, text in story(normalise(files[0], False, False, side, False), fold):
            print(ctx, text)
        return
    if args[0] == 'storydiff':
        n = int(args[args.index('-n') + 1]) if '-n' in args else 2
        a = ['%s %s' % x for x in story(normalise(files[0], False, False, side, False), fold)]
        b = ['%s %s' % x for x in story(normalise(files[1], False, False, side, False), fold)]
        if '--bag' in args:
            # order-free comparison: how often each story line occurs (real-time sessions reorder events that
            # happen in the same few frames)
            import collections
            ca, cb = collections.Counter(a), collections.Counter(b)
            keys = sorted(set(ca) | set(cb))
            nd = 0
            for k in keys:
                # buttons/kick: input and decay timing; firetime: how often the side-effect-free getter is asked
                # within one think is the module's own structure (the clears are compared)
                if ca[k] != cb[k] and not re.match(r'[CS] (buttons|kick) |[CS] firetime\d$', k):
                    print('%4d %4d  %s' % (ca[k], cb[k], k))
                    nd += 1
            print('# bag: %d story lines occur a different number of times (buttons/kick ignored)' % nd,
                  file=sys.stderr)
            return
        diff = list(difflib.unified_diff(a, b, files[0], files[1], n=n, lineterm=''))
        for l in diff:
            print(l)
        print('# story %d vs %d lines, %d diff lines' % (len(a), len(b), len(diff)), file=sys.stderr)
        return
    if args[0] == 'show':
        for l in normalise(files[0], precache, all_ps, side, nested):
            print(l)
    elif args[0] == 'diff':
        n = 3
        if '-n' in args:
            n = int(args[args.index('-n') + 1])
        a = normalise(files[0], precache, all_ps, side, nested)
        b = normalise(files[1], precache, all_ps, side, nested)
        diff = list(difflib.unified_diff(a, b, files[0], files[1], n=n, lineterm=''))
        words = '--words' in args
        i = 0
        while i < len(diff):
            l = diff[i]
            # a changed block line: show only the differing words
            if words and l.startswith('-') and ' BLK ' in l and i + 1 < len(diff) and diff[i + 1].startswith('+')                     and ' BLK ' in diff[i + 1]:
                wa = dict(re.findall(r' ([0-9a-f]{3}):(\S+)', l))
                wb = dict(re.findall(r' ([0-9a-f]{3}):(\S+)', diff[i + 1]))
                sa = re.search(r'seq="([^"]*)"', l)
                sb = re.search(r'seq="([^"]*)"', diff[i + 1])
                d = ['%s:%s/%s' % (k, wa.get(k, '-'), wb.get(k, '-')) for k in sorted(set(wa) | set(wb))
                     if wa.get(k) != wb.get(k)]
                if (sa and sa.group(1)) != (sb and sb.group(1)):
                    d.append('seq:%s/%s' % (sa and sa.group(1), sb and sb.group(1)))
                print('~' + l[1:l.index(' BLK ') + 5] + ' (orig/om) ' + ' '.join(d))
                i += 2
                continue
            print(l)
            i += 1
        print('# %d lines vs %d lines, %d diff lines' % (len(a), len(b), len(diff)), file=sys.stderr)


if __name__ == '__main__':
    main()
