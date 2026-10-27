# SPDX-License-Identifier: GPL-2.0-or-later
# Implements: SPEC-302 ("Complete cvar list", "Complete command list")
"""Generates engine/src/common/sof_cvartable.h from SPEC-302.

Rows of the cvar table registered by the engine become entries
{name, default, flags, client_only}. Flags come from the spec's flag column
(the reference cvarlist); a row is client-only when the spec says so or when
an optional dedicated-server cvarlist capture (black-box output of the
original, see docs/impl/I1a.md) lacks it.

Also emits the commands of the command table that the client registers
without a handler ("client, forwarded to server").

usage: gen_sof_cvartable.py <SPEC-302.md> <out.h> [dedicated_cvarlist.log]
"""
import re
import sys

FLAGS = {'*': 0x1, 'U': 0x2, 'S': 0x4, '-': 0x8, 'L': 0x10, 'I': 0x20, 'P': 0x40}

# cvars that other engine code registers itself (start sequence, filesystem,
# string packages, parental locks, cpuid) or that are created on demand
SKIP = {
    'basedir', 'cddir', 'game', 'user', 'gamedir', 'fs_createpak',
    'developer', 'timescale', 'fixedtime', 'logfile', 'showtrace', 'dedicated',
    'version', 'host_speeds', 'log_stats', 'cl_maxfps', 'timedemo',
    'sp_language', 'sv_show_strip', 'sv_violence', 'cl_violence',
    'lock_password', 'lock_deaths', 'lock_blood', 'lock_gorezones', 'lock_sever',
    'lock_textures', 'lock_pure', 'non_multilingual',
    'cpu_speed', 'cpu_speed_level', 'cpu_memory', 'cpu_memory_level',
    'qport',
    # created on demand in the original (not in the reference cvarlist)
    'mapname', 'map_noareas', 'sv_showdamage', 'hostport', 'ip_hostport',
    'ipx_hostport', 'clientport', 'ip_clientport', 'ipx_clientport', 'port',
    'menu_mouse_x', 'menu_mouse_y', 'menu_gamma', 'menu_contrast',
    'menu_brightness', 'menu_modulate',
}

# rows whose default is not a plain literal: value used by OpenMerc
DEFAULTS = {
    'menu_animated': '0',
    's3tc_avail': '0',
    'menu_notruecolour': '0',
    'menu_nofullscreen': '0',
    'protocol': '33',
    'predicting': '1',
    'gamespyport': '28911',
    'maxclients': '1',
    'deathmatch': '0',
    'hostname': 'noname',
    'allow_download': '0',
    # V1 (docs/DEVIATIONS.md): borderless fullscreen is the desktop default (original: 1)
    'vid_fullscreen': '2',
    'g_movescale': '1',
}

# server/common cvars (present on a dedicated server) when no capture is given
COMMON_PREFIXES = ('sv_', 'net_', 'gsc_', 'bot_name', 'ctf_', 'control_', 'ai_')


def expand(names):
    out = []
    for part in [p.strip() for p in names.split(',')]:
        m = re.match(r'^(\w*?)(\d+) \.\. (\w*?)(\d+)$', part)
        if m:
            base, a, b = m.group(1), int(m.group(2)), int(m.group(4))
            out += ['%s%d' % (base, i) for i in range(a, b + 1)]
        elif re.match(r'^[\w+-]+$', part):
            out.append(part)
    return out


def parse_default(text):
    m = re.match(r'^"([^"]*)"', text.strip())
    return m.group(1) if m else None


def main():
    spec, out = sys.argv[1], sys.argv[2]
    dedicated = None
    if len(sys.argv) > 3:
        # name -> True when the dedicated server shows it with flags; a cvar
        # shown without flags there but with flags in the spec was created
        # by a .cfg file only, so the engine does not register it there
        dedicated = {}
        dedflags = {}
        for line in open(sys.argv[3], encoding='latin-1'):
            m = re.match(r'^(.{13}) (\S+) "', line.rstrip('\r\n'))
            if m:
                dedicated[m.group(2)] = m.group(1).strip() != ''
                cols = m.group(1)
                b = 0
                for i, ch in enumerate(cols[:6]):
                    if ch != ' ':
                        b |= (0x1, 0x2, 0x4, 0x8, 0x10, 0x20)[i]
                dedflags[m.group(2)] = b

    rows = []
    commands = []
    section = None
    for line in open(spec, encoding='utf-8'):
        if line.startswith('## Complete cvar list'):
            section = 'cvar'
            continue
        if line.startswith('## Complete command list'):
            section = 'cmd'
            continue
        if line.startswith('## ') and section:
            section = None
        if not line.startswith('| ') or line.startswith('| Name') or line.startswith('| Command') \
                or line.startswith('|---'):
            continue
        cells = [c.strip() for c in line.strip().strip('|').split('|')]
        if section == 'cvar' and len(cells) >= 6:
            names, flags, default, regby = cells[0], cells[1], cells[2], cells[3]
            if 'engine' not in regby or '(hidden' in flags:
                continue
            value = parse_default(default)
            bits = 0
            for ch in flags:
                bits |= FLAGS.get(ch, 0)
            for n in expand(names):
                if n in SKIP:
                    continue
                v = DEFAULTS.get(n, value)
                if v is None:
                    continue
                dbits = bits
                if dedicated is not None:
                    client = (n not in dedicated) or (bits and not dedicated[n])
                    if not client:
                        # e.g. team_red_blue: archive only on a dedicated
                        # server (the client adds userinfo)
                        dbits = dedflags[n]
                else:
                    client = not n.startswith(COMMON_PREFIXES)
                rows.append((n, v, bits, client, dbits))
        elif section == 'cmd' and len(cells) >= 2:
            if cells[1].startswith('client, forwarded to server'):
                commands.append(cells[0])

    with open(out, 'w', encoding='utf-8', newline='\n') as f:
        f.write('/*\n * SPDX-License-Identifier: GPL-2.0-or-later\n')
        f.write(' * Generated by tools/build/gen_sof_cvartable.py from SPEC-302; do not edit.\n')
        f.write(' * The names are the console interface (used by the configs and menus in the game data and by players); defaults\n * and flags are those printed by cvarlist in a black-box run of the game.\n')
        f.write(' * Implements: SPEC-302 ("Complete cvar list", "Complete command list")\n */\n\n')
        f.write('/* name, registered default, flags, client only, flags on a dedicated server */\n')
        f.write('static const sofcvardef_t sof_engine_cvars[] = {\n')
        for n, v, bits, client, dbits in rows:
            vv = v.replace('\\', '\\\\').replace('"', '\\"')
            f.write('\t{"%s", "%s", 0x%04x, %d, 0x%04x},\n' % (n, vv, bits, 1 if client else 0, dbits))
        f.write('};\n\n')
        f.write('/* client commands without a handler: forwarded to the server */\n')
        f.write('static const char *sof_forward_commands[] = {\n')
        for c in commands:
            f.write('\t"%s",\n' % c)
        f.write('};\n')
    print('%d cvars, %d forwarded commands' % (len(rows), len(commands)))


if __name__ == '__main__':
    main()
