#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# ENG2 - black-box check of the SPEC-AMEND-011 gates on a run of tests/ENG2/fire_gates.cfg
# (client.log with om_cpltrace 1, om_fxtrace 1, s_om_debug 4).
#   1. notes at exactly 0 ms are delivered: every shot of a weapon whose fire sequences carry their notes at 0 ms
#      (read from the in-view model in the game data) gets its `effect` note and its fire sound;
#   2. the local player's own entity is in the client frame with effects bit 0x4000 clear at every view note, and
#      no first-person muzzle effect is detached;
#   3. `sound` notes (reload, handling) play on the own entity, channel 1, volume 0.6, attenuation none.
# Fire-sound file, volume and attenuation are read from the user's own effect files (game data paks), not from a
# spec table.
# usage: fire_gates.py <client.log> [--base D:\Games\OpenMerc-eng-test\base]
# Implements: black-box test (no game code); data readers SPEC-120 (ghb), SPEC-142/SPEC-400 (eft sound primitive)
import argparse
import os
import re
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..', '..', 'tools', 'formats'))
import eft  # noqa: E402
import ghb  # noqa: E402
import vfs  # noqa: E402

WEAPONS = {8: ('shotgun', 'shotgun'), 3: ('pistol1', 'pistol1')}   # weapon id -> (in-view model, playermz effect)


def eft_sounds(data):
    """(file, volume range, attenuation range) of every sound primitive (SPEC-400 type 1: name +4, volume +0x44,
    attenuation +0x4C)."""
    out = []
    for p in eft.Effect(data).prims:
        if p.type != 1.0:
            continue
        raw = p.raw
        name = raw[4:68].split(b'\0')[0].decode('latin-1')
        vol = struct.unpack_from('<2f', raw, 0x44)
        att = struct.unpack_from('<2f', raw, 0x4C)
        out.append((name.lower().replace('\\', '/'), vol, att))
    return out


def fire_note_times(v, model):
    m = ghb.parse(v.read(f'ghoul/weapon/inview/{model}/{model}.ghb'))
    res = {}
    for s in m['sequences']:
        ts = [e['time_ms'] for e in s['events'] if e['type'].lower() == 'effect' and 'playermz' in e['arg']]
        if ts and s['name'].upper().startswith('FIRE'):
            res[s['name']] = ts
    return res


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('log')
    ap.add_argument('--base', default=r'D:\Games\OpenMerc-eng-test\base')
    a = ap.parse_args()
    v = vfs.PakVFS(a.base)
    lines = open(a.log, encoding='latin-1').read().splitlines()
    ok = True

    def report(cond, msg):
        nonlocal ok
        ok = ok and cond
        print(('PASS ' if cond else 'FAIL ') + msg)

    starts = []
    rx_start = re.compile(r'OMsnd: \[p=(\d+)[^\]]*\] start (\S+) ent (-?\d+) ch (\d+) vol (\d+) attn ([\d.]+)')
    rx_pent = re.compile(r'\(player ent (\d+)\)')
    for i, ln in enumerate(lines):
        m = rx_start.search(ln)
        if m:
            pe = rx_pent.search(ln)
            starts.append((i, m.group(2).lower(), int(m.group(3)), int(m.group(4)), int(m.group(5)),
                           float(m.group(6)), int(pe.group(1)) if pe else None))

    # gate 1: fire sounds, values from the user's effect files
    for wid, (model, effect) in WEAPONS.items():
        times = fire_note_times(v, model)
        snd = eft_sounds(v.read(f'effects/weapons/playermz/{effect}.eft'))
        print(f'data: {model} fire sequences/effect-note times (ms) {times}; playermz/{effect}.eft sound {snd}')
        if len(snd) != 1:
            report(False, f'{effect}.eft: expected one sound primitive, found {len(snd)}')
            continue
        f, vol, att = snd[0]
        shots = [i for i, ln in enumerate(lines) if re.search(rf'clplayer: \S+ fire weapon {wid}$', ln)]
        notes = [i for i, ln in enumerate(lines) if f'note effect "weapons/playermz/{effect} flash"' in ln]
        fires = [s for s in starts if s[1].endswith(f.lstrip('/'))]
        good = [s for s in fires if s[2] == -1 and s[3] == 0 and s[4] == int(vol[0] * 255) and abs(s[5] - att[0]) < 0.01]
        zero = any(0.0 in t for t in times.values())
        report(len(shots) >= 1 and len(notes) == len(shots) and len(good) == len(shots),
               f'gate 1 weapon {wid} ({model}, notes at 0 ms: {zero}): shots {len(shots)}, effect notes {len(notes)}, '
               f'{f} starts {len(fires)} of which ent -1 ch 0 vol {int(vol[0] * 255)} attn {att[0]:g}: {len(good)}')

    # gate 2: own entity in frame, bit 0x4000 clear, no muzzle effect detached
    vn = [ln for ln in lines if ln.startswith('fx: view note ')]
    bad = [ln for ln in vn if not re.search(r'in frame 1 effects 0x([0-9a-f]+)', ln)
           or int(re.search(r'effects 0x([0-9a-f]+)', ln).group(1), 16) & 0x4000]
    det = [ln for ln in lines if ln.startswith("fx: detach 'weapons/playermz")]
    report(len(vn) > 0 and not bad and not det,
           f'gate 2: view notes {len(vn)}, own entity missing or 0x4000 set: {len(bad)}, muzzle effects detached: {len(det)}')
    for ln in (bad + det)[:5]:
        print('   ', ln)

    # gate 3: sound notes -> own entity, channel 1, volume 0.6 (or the note's random volume), attenuation none
    rx_note = re.compile(r'clplayer: \S+ note sound "[^"]*" -> (\S+) vol ([\d.]+)')
    n_ok = n_all = 0
    reload_seen = []
    for i, ln in enumerate(lines):
        m = rx_note.search(ln)
        if not m:
            continue
        n_all += 1
        f = m.group(1).lower().lstrip('/')
        want = int(float(m.group(2)) * 255)
        hit = [s for s in starts if s[0] > i and s[0] < i + 40 and s[1].endswith(f)]
        if hit:
            s = hit[0]
            if s[2] == s[6] and s[3] == 1 and abs(s[4] - want) <= 1 and s[5] == 0.0:
                n_ok += 1
            else:
                print(f'    note {f}: ent {s[2]} (player {s[6]}) ch {s[3]} vol {s[4]} (want {want}) attn {s[5]}')
        else:
            print(f'    note {f}: no sound start found')
        if 'reload' in f or 'putin' in f or 'cock' in f:
            reload_seen.append(f)
    report(n_all > 0 and n_ok == n_all and len(reload_seen) > 0,
           f'gate 3: sound notes {n_all}, own entity/ch 1/vol 0.6/attn none: {n_ok}; reload notes {sorted(set(reload_seen))}')
    print('RESULT', 'PASS' if ok else 'FAIL')
    return 0 if ok else 1


if __name__ == '__main__':
    sys.exit(main())
