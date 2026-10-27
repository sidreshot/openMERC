#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""OpenMerc X1 - analyse om_autorecord sessions (demos/auto/<date>_<time>_<map>.jsonl, written by
engine/src/client/cl_omrec.c next to each automatic demo) and flag gameplay anomalies:

  ice      on the ground, no movement input, horizontal speed growing or staying high (sliding)
  stuck    movement input held, on the ground, but the player does not move
  shake    camera shake (fx_shake) or view kick that does not settle
  sound    voices stolen / no free voice, music stream ran dry, cut-offs other than a same-channel override
  hitch    a frame longer than the threshold (default 50 ms), outside level loads
  error    console errors (and warnings, reported but not failing)

  python tests/X1/demo_analyze.py <user>/demos/auto            # every session in the folder
  python tests/X1/demo_analyze.py session.jsonl --last 3 --json report.json
Exit code 0 = clean, 1 = anomalies, 2 = no input / unreadable.
Implements: black-box test aid (reads only the recorder's own log)
"""
import argparse
import glob
import json
import math
import os
import re
import sys

LOAD_GRACE_MS = 3000    # after a start / map / load / save command: loading frames are not hitches
ICE_SPEED = 40.0        # units/s of horizontal speed with no input on the ground
ICE_SAMPLES = 4         # consecutive samples (0.4 s at 10 Hz)
STUCK_SAMPLES = 20      # 2 s pushing without moving
SHAKE_MS = 6000         # shake / kick lasting longer than this is "endless"
LOAD_CMDS = {'map', 'gamemap', 'load', 'save', 'changelevel', 'demomap', 'restart', 'newgame', 'menu', 'killserver'}


def read_session(path):
    ev, bad = [], 0
    with open(path, encoding='utf-8', errors='replace') as f:
        for line in f:
            line = line.strip()
            if not line:
                continue
            try:
                ev.append(json.loads(line))
            except ValueError:
                bad += 1  # a line cut by a crash: everything before it is still valid
    return ev, bad


def hspeed(s):
    v = s.get('vel') or [0, 0, 0]
    return s['spd'] if 'spd' in s else math.hypot(v[0], v[1])


def analyse(path, hitch_ms):
    ev, bad = read_session(path)
    out = {'file': path, 'anomalies': [], 'warnings': [], 'stats': {}}
    A = lambda kind, t, msg: out['anomalies'].append({'kind': kind, 't': t, 'msg': msg})
    W = lambda kind, t, msg: out['warnings'].append({'kind': kind, 't': t, 'msg': msg})
    if not ev:
        A('error', 0, 'empty log')
        return out
    start = next((e for e in ev if e['k'] == 'start'), None)
    out['map'] = start.get('map') if start else '?'
    out['demo'] = start.get('demo') if start else '?'
    if bad:
        W('log', 0, '%d unreadable line(s) (log cut by a crash or kill?)' % bad)
    if not any(e['k'] == 'end' for e in ev):
        W('log', ev[-1]['t'], 'no end event: the session ended by a crash or a kill')

    # loading windows
    busy = [0]
    for e in ev:
        if e['k'] == 'cmd' and (e.get('cmd', '').split() or [''])[0].lower() in LOAD_CMDS:
            busy.append(e['t'])
        if e['k'] == 'print' and 'loading' in e.get('msg', '').lower():
            busy.append(e['t'])
    near_load = lambda t: any(0 <= t - b <= LOAD_GRACE_MS for b in busy)

    samples = [e for e in ev if e['k'] == 's']
    live = [s for s in samples if not s.get('paused') and s.get('moves', 0) > 0 and 'cmd' in s]
    out['stats'] = {'events': len(ev), 'samples': len(samples), 'live_samples': len(live),
                    'duration_s': round(ev[-1]['t'] / 1000.0, 1)}
    fps = [s['fps'] for s in samples if s.get('fps') and not s.get('paused')]
    if fps:
        fps_sorted = sorted(fps)
        out['stats']['fps_median'] = round(fps_sorted[len(fps) // 2], 1)
        out['stats']['fps_p5'] = round(fps_sorted[len(fps) // 20], 1)

    # ice: no input, on the ground, normal movement, speed high or growing
    run = []
    def flush_ice():
        if len(run) >= ICE_SAMPLES:
            sp = [hspeed(s) for s in run]
            grow = sp[-1] - sp[0]
            A('ice', run[0]['t'], 'no input on the ground for %.1f s but speed %.0f..%.0f u/s (%s) at %s' % (
                (run[-1]['t'] - run[0]['t']) / 1000.0, min(sp), max(sp), 'growing' if grow > 5 else 'sliding',
                run[0].get('pos')))
        run.clear()
    prev = None
    for s in live:
        idle = s['cmd'] == [0, 0, 0] and s.get('ong') and s.get('pmtype', 0) == 0
        fast = hspeed(s) > ICE_SPEED or (prev is not None and idle and hspeed(s) > hspeed(prev) + 5 and hspeed(s) > 10)
        if idle and fast and not near_load(s['t']):
            # the first 0.3 s after letting go is normal friction stopping
            if prev is not None and prev['cmd'] != [0, 0, 0]:
                pass
            else:
                run.append(s)
        else:
            flush_ice()
        prev = s
    flush_ice()

    # stuck: input held, on the ground, position unchanged
    run = []
    def flush_stuck():
        if len(run) >= STUCK_SAMPLES:
            A('stuck', run[0]['t'], 'movement input %s for %.1f s on the ground, moved %.1f units at %s' % (
                run[0]['cmd'], (run[-1]['t'] - run[0]['t']) / 1000.0, dist(run[0]['pos'], run[-1]['pos']),
                run[0].get('pos')))
        run.clear()
    dist = lambda a, b: math.sqrt(sum((x - y) ** 2 for x, y in zip(a, b)))
    for s in live:
        push = s['cmd'][0] != 0 or s['cmd'][1] != 0
        if push and s.get('ong') and (not run or dist(run[0]['pos'], s['pos']) < 4.0):
            run.append(s)
        else:
            flush_stuck()
            if push and s.get('ong'):
                run.append(s)
    flush_stuck()

    # endless shake
    t0 = None
    for s in samples:
        shaking = abs(s.get('shake', 0)) > 0.01 or any(abs(k) > 0.05 for k in s.get('kick', [0, 0, 0]))
        if shaking and t0 is None:
            t0 = s['t']
        if not shaking and t0 is not None:
            if s['t'] - t0 > SHAKE_MS:
                A('shake', t0, 'camera shake / kick for %.1f s' % ((s['t'] - t0) / 1000.0))
            t0 = None
    if t0 is not None and samples and samples[-1]['t'] - t0 > SHAKE_MS:
        A('shake', t0, 'camera shake / kick for %.1f s until the end of the session' % ((samples[-1]['t'] - t0) / 1000.0))

    # hitches
    hitches = [e for e in ev if e['k'] == 'hitch' and e.get('ms', 0) > hitch_ms]
    late = [h for h in hitches if not near_load(h['t'])]
    out['stats']['hitches'] = len(hitches)
    out['stats']['hitches_outside_loads'] = len(late)
    for h in late:
        A('hitch', h['t'], 'frame %d ms' % h['ms'])

    # console: errors, warnings, sound
    cuts = {}
    for e in ev:
        if e['k'] != 'print':
            continue
        msg, lvl = e.get('msg', '').rstrip(), e.get('lvl')
        if lvl == 'err':
            A('error', e['t'], msg[:200])
        elif lvl == 'warn':
            W('warning', e['t'], msg[:200])
        if msg.startswith('OMsnd:'):
            m = re.search(r'\] cut \((\w+)\) (\S+)', msg)
            if m:
                cuts[m.group(1)] = cuts.get(m.group(1), 0) + 1
                if m.group(1) in ('stolen', 'freed'):
                    A('sound', e['t'], msg[6:200].strip())
            elif ' ran dry' in msg or 'no voice for' in msg:
                A('sound', e['t'], msg[6:200].strip())
            else:
                m = re.search(r'music dry (\d+)', msg)
                if m and int(m.group(1)) > 0:
                    A('sound', e['t'], 'music stream ran dry %s time(s) in 1 s' % m.group(1))
    out['stats']['sound_cuts'] = cuts
    out['stats']['damage_events'] = sum(1 for e in ev if e['k'] == 'dmg')
    out['stats']['deaths'] = sum(1 for e in ev if e['k'] == 'death')
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('paths', nargs='+', help='.jsonl files or folders (demos/auto)')
    ap.add_argument('--last', type=int, default=0, help='only the newest N sessions')
    ap.add_argument('--hitch', type=int, default=50, help='hitch threshold, ms (default 50)')
    ap.add_argument('--json', help='write the full report here')
    ap.add_argument('--max', type=int, default=12, help='anomalies printed per kind and session')
    a = ap.parse_args()
    files = []
    for p in a.paths:
        files += sorted(glob.glob(os.path.join(p, '*.jsonl'))) if os.path.isdir(p) else [p]
    if a.last:
        files = files[-a.last:]
    if not files:
        print('no sessions found')
        return 2
    reports, total = [], 0
    for f in files:
        try:
            r = analyse(f, a.hitch)
        except OSError as e:
            print('%s: %s' % (f, e))
            return 2
        reports.append(r)
        total += len(r['anomalies'])
        st = r['stats']
        print('== %s  map %s  %.1f s  samples %d  fps med %s p5 %s  hitches %d (%d outside loads)  %s' % (
            os.path.basename(f), r.get('map'), st.get('duration_s', 0), st.get('samples', 0), st.get('fps_median', '-'),
            st.get('fps_p5', '-'), st.get('hitches', 0), st.get('hitches_outside_loads', 0),
            'CLEAN' if not r['anomalies'] else '%d ANOMALIES' % len(r['anomalies'])))
        shown = {}
        for x in r['anomalies'] + r['warnings']:
            key = x['kind']
            shown[key] = shown.get(key, 0) + 1
            if shown[key] <= a.max:
                print('  %-8s %8.1f s  %s' % (x['kind'], x['t'] / 1000.0, x['msg']))
        for k, n in shown.items():
            if n > a.max:
                print('  %-8s ... %d more' % (k, n - a.max))
    if a.json:
        with open(a.json, 'w', encoding='utf-8') as f:
            json.dump(reports, f, ensure_ascii=False, indent=1)
    print('%d session(s), %d anomaly(ies)' % (len(files), total))
    return 1 if total else 0


if __name__ == '__main__':
    sys.exit(main())
