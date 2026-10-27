#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Implements: SPEC-102
"""Reader for .ifl image file lists (SPEC-102).

An .ifl is plain text: one image file name per line, names relative to the
directory that holds the .ifl. Entry order is significant (entry k = frame/skin k).

usage:
  ifl.py show <file.ifl>...                 print the resolved entries
  ifl.py validate <dir> [pak ...]           parse every .ifl under <dir>; with paks, also
                                            check that each entry exists in them
"""
import os
import posixpath
import sys


class IFLError(ValueError):
    pass


def parse(data, strict=True):
    """Return the list of entry names.

    Lines end with CR LF (LF alone is accepted). Leading/trailing blanks are
    stripped. Blank lines at the end are ignored; a blank line followed by
    further entries does not occur in stock data and is rejected when strict
    (otherwise skipped).
    """
    if isinstance(data, bytes):
        if b"\0" in data:
            raise IFLError("NUL byte in text")
        data = data.decode("latin-1")
    lines = [ln.strip(" \t\r") for ln in data.split("\n")]
    while lines and not lines[-1]:
        lines.pop()
    entries = []
    for i, ln in enumerate(lines):
        if not ln:
            if strict:
                raise IFLError("blank line %d between entries" % (i + 1))
            continue
        if strict and len(ln.split()) != 1:
            raise IFLError("line %d has more than one token: %r" % (i + 1, ln))
        entries.append(ln)
    if strict and not entries:
        raise IFLError("no entries")
    return entries


def resolve(ifl_path, entry):
    """Game-relative path of an entry (forward slashes, case preserved)."""
    return posixpath.join(posixpath.dirname(ifl_path.replace("\\", "/")), entry.replace("\\", "/"))


def load(path, strict=True):
    with open(path, "rb") as f:
        return parse(f.read(), strict)


def _pak_names(paks):
    sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
    import pak
    names = set()
    for p in paks:
        for name, _, _ in pak.read_dir(p):
            names.add(name.lower())
    return names


def main(argv):
    if len(argv) < 2:
        print(__doc__)
        return 2
    if argv[0] == "show":
        for p in argv[1:]:
            print("== %s" % p)
            for k, e in enumerate(load(p, strict=False)):
                print("%3d %s" % (k, e))
        return 0
    if argv[0] == "validate":
        root = argv[1]
        names = _pak_names(argv[2:]) if len(argv) > 2 else None
        ok = bad = entries = missing = 0
        for dp, _, fn in os.walk(root):
            for f in sorted(fn):
                if not f.lower().endswith(".ifl"):
                    continue
                full = os.path.join(dp, f)
                try:
                    ents = load(full)
                except IFLError as e:
                    bad += 1
                    print("FAIL %s: %s" % (full, e))
                    continue
                ok += 1
                entries += len(ents)
                for e in ents:
                    if "/" in e or "\\" in e or not e.lower().endswith(".tga"):
                        print("note %s: unusual entry %r" % (full, e))
                if names is not None:
                    # game path = path below the pak folder: <root>/<pak>/<game path>
                    rel = os.path.relpath(full, root).replace(os.sep, "/")
                    game = rel.split("/", 1)[1] if rel.split("/", 1)[0].lower().startswith(("pak", "zmain", "zsond", "gs")) else rel
                    for e in ents:
                        if resolve(game, e).lower() not in names:
                            missing += 1
        print("parsed %d ok, %d failed; %d entries%s" % (
            ok, bad, entries, "" if names is None else ", %d entries not present in the given paks" % missing))
        return 0 if bad == 0 else 1
    print(__doc__)
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
