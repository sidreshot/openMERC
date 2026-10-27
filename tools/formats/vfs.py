# SPDX-License-Identifier: GPL-2.0-or-later
# Implements: SPEC-130, SPEC-131, SPEC-132, SPEC-133 (shared helper)
"""Read-only union view over the stock PACK archives.

Only the public Quake II PACK layout is used (see tools/pak.py). Lookups are
case-insensitive because the stock data mixes case freely ("Maps/DM" vs
"maps/dm", "B_WEB_HOME.ghb" vs "b_web_home").

The search order of the original engine over pak0..pak3, zmain, zsond, gs is
UNKNOWN (see SPEC-130/131 open questions). DEFAULT_ORDER is a hypothesis derived
from content: gs.pak replaces pak0/pak2 menus with newer GameSpy versions, pak3
(Platinum) replaces pak2 (Gold), pak2 replaces pak0, zmain/zsond replace pak0
with a Russian localisation. zmain is placed below pak2/pak3 because that is the
only placement under which every ^REF^ that exists in some package resolves
(zmain's packages are 1.0-era and lack the Gold-edition entries). Highest
priority first. Pass another order to PakVFS to test alternatives.
"""
import fnmatch
import os
import struct

DEFAULT_BASE = r"D:\Games\Soldier of Fortune\base"
DEFAULT_ORDER = ["gs", "pak3", "pak2", "zmain", "zsond", "pak1", "pak0"]
ALT_ORDER_Z_FIRST = ["gs", "zsond", "zmain", "pak3", "pak2", "pak1", "pak0"]


def read_pack_dir(path):
    with open(path, "rb") as f:
        magic, dofs, dlen = struct.unpack("<4sii", f.read(12))
        if magic != b"PACK":
            raise ValueError(f"{path}: not a PACK file")
        f.seek(dofs)
        raw = f.read(dlen)
    out = []
    for i in range(0, dlen, 64):
        name, ofs, ln = struct.unpack_from("<56sii", raw, i)
        out.append((name.split(b"\0", 1)[0].decode("latin-1"), ofs, ln))
    return out


class PakVFS:
    def __init__(self, base=DEFAULT_BASE, order=None):
        self.base = base
        self.order = list(order or DEFAULT_ORDER)
        self.paks = {}     # pak -> {lowername: (name, ofs, len)}
        for p in self.order:
            fn = os.path.join(base, p + ".pak")
            if not os.path.exists(fn):
                continue
            self.paks[p] = {n.lower().replace("\\", "/"): (n, o, l) for n, o, l in read_pack_dir(fn)}

    def _norm(self, path):
        return path.replace("\\", "/").lstrip("/").lower()

    def where(self, path):
        """All paks (priority order) that contain path."""
        k = self._norm(path)
        return [p for p in self.order if p in self.paks and k in self.paks[p]]

    def exists(self, path):
        return bool(self.where(path))

    def read(self, path, pak=None):
        k = self._norm(path)
        for p in ([pak] if pak else self.order):
            ent = self.paks.get(p, {}).get(k)
            if ent:
                with open(os.path.join(self.base, p + ".pak"), "rb") as f:
                    f.seek(ent[1])
                    return f.read(ent[2])
        raise FileNotFoundError(path)

    def names(self, pattern="*", pak=None):
        """Union of entry names (lower-case) matching a glob, highest priority first."""
        seen = {}
        for p in ([pak] if pak else self.order):
            for k in self.paks.get(p, {}):
                if k not in seen and fnmatch.fnmatchcase(k, pattern.lower()):
                    seen[k] = p
        return seen

    def dir_exists(self, prefix):
        pre = self._norm(prefix).rstrip("/") + "/"
        return any(k.startswith(pre) for d in self.paks.values() for k in d)
