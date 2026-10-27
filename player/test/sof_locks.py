# SPDX-License-Identifier: GPL-2.0-or-later
# Implements: SPEC-313 Appendix A (parental-lock store), for black-box test runs of the original executable.
#
# On this PC the stored lock words in HKCU\Software\Raven Software\SoF were made with another volume key, so the
# original engine treats them as invalid and forces every lock on: cl_violence 63, sv_violence 31, and every local
# or loopback game is refused with `rejected_violence`. A test run therefore needs valid words for this machine for
# the few seconds the game reads them at start-up. This tool:
#   backup  <file>   save every value of the key (name, type, data) to JSON (refuses to overwrite a backup)
#   unlock           write valid "off" words for all six locks and an empty password for this machine
#   restore <file>   make the key byte-identical to the backup (removes values the backup lacks)
#   verify  <file>   exit 0 when the key equals the backup
#   show             print the values and whether they decode as off/on for this machine
# Key material (Appendix A): seed = volume serial of the first fixed drive; g(): seed = seed*214013+2531011,
# return seed>>16; words w = g() + 65536*g(): pairs (off, on) for value numbers 0..5, then the password key K, then
# the pair for value number 6. (Appendix A says "fourteen" words; the layout reproduces the stored values of another
# key as words 2v of a 15-word sequence, see docs/impl/I7.md.)
import ctypes
import json
import os
import string
import sys
import winreg

KEY = r'Software\Raven Software\SoF'
NAMES = {0: 'Server', 1: 'Graphics', 2: 'Sound', 3: 'Input', 4: 'Networking', 5: 'Performance', 6: 'Optimizations'}


def volume_serial():
    k32 = ctypes.windll.kernel32
    for letter in string.ascii_uppercase:
        root = letter + ':\\'
        if k32.GetDriveTypeW(root) != 3:  # DRIVE_FIXED
            continue
        serial = ctypes.c_uint32(0)
        if k32.GetVolumeInformationW(root, None, 0, ctypes.byref(serial), None, None, None, 0):
            return serial.value
    return 0


def words(seed):
    out = []
    for _ in range(15):
        seed = (seed * 214013 + 2531011) & 0xFFFFFFFF
        a = seed >> 16
        seed = (seed * 214013 + 2531011) & 0xFFFFFFFF
        b = seed >> 16
        out.append((a + 65536 * b) & 0xFFFFFFFF)
    return out


def pair(w, value):
    i = 2 * value if value <= 5 else 13
    return w[i], w[i + 1]


def encode_password(pw, key):
    raw = pw.encode('latin-1') + b'\0'
    kb = key.to_bytes(4, 'little')
    stored = bytes((raw[i] + kb[i % 4]) & 0xFF for i in range(len(raw)))
    c = 0
    for b in stored:
        c = (c * 2 + b) & 0xFFFFFFFF
    return stored + (c ^ key).to_bytes(4, 'little')


def read_all():
    vals = {}
    try:
        with winreg.OpenKey(winreg.HKEY_CURRENT_USER, KEY) as k:
            i = 0
            while True:
                try:
                    name, data, typ = winreg.EnumValue(k, i)
                except OSError:
                    break
                vals[name] = (typ, data.hex() if isinstance(data, bytes) else data)
                i += 1
    except FileNotFoundError:
        pass
    return vals


def write_value(k, name, typ, data):
    winreg.SetValueEx(k, name, 0, typ, bytes.fromhex(data) if typ == winreg.REG_BINARY else data)


def main():
    cmd = sys.argv[1] if len(sys.argv) > 1 else 'show'
    if cmd == 'backup':
        path = sys.argv[2]
        if os.path.exists(path):
            sys.exit('backup exists, refusing to overwrite: ' + path)
        with open(path, 'w') as f:
            json.dump(read_all(), f, indent=1, sort_keys=True)
        print('saved', path)
    elif cmd == 'restore':
        want = json.load(open(sys.argv[2]))
        with winreg.CreateKeyEx(winreg.HKEY_CURRENT_USER, KEY, 0, winreg.KEY_ALL_ACCESS) as k:
            for name in list(read_all()):
                if name not in want:
                    winreg.DeleteValue(k, name)
            for name, (typ, data) in want.items():
                write_value(k, name, typ, data)
        same = {n: tuple(v) for n, v in want.items()} == read_all()
        print('restored, identical:', same)
        sys.exit(0 if same else 1)
    elif cmd == 'verify':
        want = json.load(open(sys.argv[2]))
        same = {n: tuple(v) for n, v in want.items()} == read_all()
        print('identical:', same)
        sys.exit(0 if same else 1)
    elif cmd == 'unlock':
        w = words(volume_serial())
        with winreg.CreateKeyEx(winreg.HKEY_CURRENT_USER, KEY, 0, winreg.KEY_ALL_ACCESS) as k:
            for v in range(1, 7):
                winreg.SetValueEx(k, NAMES[v], 0, winreg.REG_DWORD, pair(w, v)[0])
            winreg.SetValueEx(k, 'Server', 0, winreg.REG_BINARY, encode_password('', w[12]))
        print('unlocked for serial %08X' % volume_serial())
    else:
        w = words(volume_serial())
        print('serial %08X K %08X' % (volume_serial(), w[12]))
        for name, (typ, data) in sorted(read_all().items()):
            state = ''
            for v, n in NAMES.items():
                if n == name and v and typ == winreg.REG_DWORD:
                    off, on = pair(w, v)
                    state = 'off' if data == off else 'on' if data == on else 'INVALID here'
            print(name, typ, data, state)


if __name__ == '__main__':
    main()
