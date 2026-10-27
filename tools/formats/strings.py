#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Implements: SPEC-131
"""String packages (strip/*.sp): parser, validator, JSON dump.

usage:
  strings.py dump <file.sp> [codepage]        print JSON of one package
  strings.py validate [--base DIR] [--out DIR] [--order gs,pak3,...]
                                              parse every stock .sp in every pak,
                                              cross-check, write JSON dumps
  strings.py lookup <REF> [lang]              resolve ^REF^ through the default pak order
"""
import json
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from vfs import PakVFS, DEFAULT_BASE  # noqa: E402

LANGS = ["ENGLISH", "GERMAN", "FRENCH", "BRITISH"]   # every TEXT_* key seen in stock data
FLAG_NAMES = ["SP_FLAG_CAPTIONED", "SP_FLAG_CENTERED", "SP_FLAG_ALWAYS_PRINT",
              "SP_FLAG_CREDIT", "SP_FLAG_LAYOUT", "SP_FLAG_TYPEAMATIC"]
COLOR_CODES = ["$P_RED", "$P_GREEN", "$P_YELLOW", "$P_CYAN"]   # observed; full set UNKNOWN
FORMAT_RE = re.compile(rb"%(?:ps|hd|hu|[sdpn])")


class SPError(ValueError):
    pass


def _value(line, lineno):
    """Quoted value: from the first to the LAST double quote on the line.
    Stock data contains unescaped quotes inside values, so this is the only rule
    that parses all files."""
    a = line.find(b'"')
    b = line.rfind(b'"')
    if a < 0 or b <= a:
        raise SPError(f"line {lineno}: expected quoted value")
    return line[a + 1:b], line[b + 1:].strip()


def unescape(raw):
    """Only the two-byte sequence backslash-n occurs in stock data; it means newline."""
    return raw.replace(b"\\n", b"\n")


def parse_sp(data):
    """Parse one package from bytes. Returns a dict with raw byte values (undecoded)."""
    lines = data.replace(b"\r\n", b"\n").replace(b"\r", b"\n").split(b"\n")
    pkg = {"version": None, "id": None, "reference": None, "description": None,
           "count": None, "entries": []}
    cur = None
    state = "header"
    for no, raw in enumerate(lines, 1):
        line = raw.strip()
        if not line:
            continue
        key = line.split(None, 1)[0]
        rest = line[len(key):].strip()
        if state == "header":
            if key == b"VERSION":
                pkg["version"] = int(rest)
            elif key == b"ID":
                pkg["id"] = int(rest)
            elif key == b"REFERENCE":
                pkg["reference"] = rest.decode("ascii")
            elif key == b"DESCRIPTION":
                pkg["description"] = _value(line, no)[0]
            elif key == b"COUNT":
                pkg["count"] = int(rest)
            elif key == b"INDEX":
                state = "open"
                cur = {"index": int(rest), "line": no, "flags": [], "reference": None,
                       "notes": None, "text": {}}
            else:
                raise SPError(f"line {no}: unexpected header key {key!r}")
        elif state == "open":
            if line != b"{":
                raise SPError(f"line {no}: expected '{{'")
            state = "body"
        elif state == "body":
            if line == b"}":
                # REFERENCE is optional: 2 stock packages have an entry without one
                # (reachable by index only).
                pkg["entries"].append(cur)
                cur = None
                state = "between"
            elif key == b"FLAGS":
                for fl in rest.split():
                    name = fl.decode("ascii")
                    if name not in FLAG_NAMES:
                        raise SPError(f"line {no}: unknown flag {name}")
                    cur["flags"].append(name)
            elif key == b"REFERENCE":
                cur["reference"] = rest.decode("ascii")
            elif key == b"NOTES":
                cur["notes"] = _value(line, no)[0]
            elif key.startswith(b"TEXT_"):
                lang = key[5:].decode("ascii")
                if lang not in LANGS:
                    raise SPError(f"line {no}: unknown language {lang}")
                val, trail = _value(line, no)
                cur["text"][lang] = val
                if trail:
                    cur.setdefault("trailing", {})[lang] = trail
            else:
                raise SPError(f"line {no}: unexpected entry key {key!r}")
        elif state == "between":
            if key != b"INDEX":
                raise SPError(f"line {no}: expected INDEX")
            cur = {"index": int(rest), "line": no, "flags": [], "reference": None,
                   "notes": None, "text": {}}
            state = "open"
    if state not in ("between", "header") or cur is not None:
        raise SPError("unterminated entry at end of file")
    for k in ("version", "id", "reference", "count"):
        if pkg[k] is None:
            raise SPError(f"missing header {k.upper()}")
    return pkg


def check_sp(pkg, filename=None):
    """Structural checks; returns list of warning strings (empty = clean)."""
    w = []
    if pkg["version"] != 1:
        w.append(f"VERSION {pkg['version']}")
    if pkg["count"] != len(pkg["entries"]):
        w.append(f"COUNT {pkg['count']} != {len(pkg['entries'])} entries")
    prev = -1
    for e in pkg["entries"]:
        # INDEX values may have gaps (16 stock files) but always increase.
        if e["index"] <= prev:
            w.append(f"INDEX {e['index']} not increasing")
        if e["index"] > 255:
            w.append(f"INDEX {e['index']} > 255")
        prev = e["index"]
        if e["reference"] is None:
            w.append(f"INDEX {e['index']}: no REFERENCE (index-only entry)")
        if "ENGLISH" not in e["text"]:
            w.append(f"{e['reference']}: no TEXT_ENGLISH")
    refs = [e["reference"] for e in pkg["entries"] if e["reference"] is not None]
    if len(set(refs)) != len(refs):
        dup = sorted({r for r in refs if refs.count(r) > 1})
        w.append(f"duplicate REFERENCE {dup}")
    if filename:
        stem = os.path.splitext(os.path.basename(filename))[0]
        if stem.lower() != pkg["reference"].lower():
            w.append(f"file name {stem} != REFERENCE {pkg['reference']}")
    return w


def guess_codepage(raw_values):
    """cp1252 for stock Western data; cp1251 when the bytes look Cyrillic
    (the zmain.pak localisation stores Russian in the TEXT_ENGLISH slot)."""
    # Tool heuristic, not a file property: the files carry no code page marker.
    stripped = []
    for v in raw_values:
        for code in COLOR_CODES:
            v = v.replace(code.encode(), b"")
        stripped.append(v)
    raw_values = stripped
    hi = sum(1 for v in raw_values for c in v if c >= 0xC0)
    letters = sum(1 for v in raw_values for c in v if 0x61 <= (c | 0x20) <= 0x7A)
    return "cp1251" if hi and hi >= letters else "cp1252"


def to_json(pkg, codepage="cp1252", per_lang_codepage=None):
    per_lang_codepage = per_lang_codepage or {}

    def dec(b, lang=None):
        if b is None:
            return None
        cp = per_lang_codepage.get(lang, codepage)
        if cp == "auto":
            cp = guess_codepage([b])
        return unescape(b).decode(cp, errors="replace")

    return {
        "version": pkg["version"], "id": pkg["id"], "reference": pkg["reference"],
        "description": dec(pkg["description"]), "count": pkg["count"],
        "entries": [{
            "index": e["index"], "reference": e["reference"], "flags": e["flags"],
            **({"notes": dec(e["notes"])} if e["notes"] is not None else {}),
            "text": {k: dec(v, k) for k, v in e["text"].items()},
            **({"trailing_after_quote": {k: v.decode("latin-1") for k, v in e["trailing"].items()}}
               if "trailing" in e else {}),
        } for e in pkg["entries"]],
    }


class StringTable:
    """All packages visible through a PakVFS (highest-priority copy wins)."""

    def __init__(self, vfs):
        self.vfs = vfs
        self.pkgs = {}      # REFERENCE -> (pak, pkg)
        for name, pak in vfs.names("strip/*.sp").items():
            pkg = parse_sp(vfs.read(name, pak))
            self.pkgs[pkg["reference"].upper()] = (pak, pkg)
        self._names = sorted(self.pkgs, key=len, reverse=True)

    def split(self, ref):
        """'MENU_GENERIC_YES' -> ('MENU_GENERIC', 'YES') by longest package prefix
        that also has the entry."""
        ref = ref.upper()
        for p in self._names:
            if ref.startswith(p + "_"):
                ent = ref[len(p) + 1:]
                if any((e["reference"] or "").upper() == ent for e in self.pkgs[p][1]["entries"]):
                    return p, ent
        return None, None

    def lookup(self, ref, lang="ENGLISH"):
        p, ent = self.split(ref)
        if p is None:
            return None
        for e in self.pkgs[p][1]["entries"]:
            if (e["reference"] or "").upper() == ent:
                v = e["text"].get(lang, e["text"].get("ENGLISH"))
                return unescape(v)
        return None


REF_RE = re.compile(rb"\^([A-Za-z0-9_]+)\^")


def validate(base=DEFAULT_BASE, out=None, order=None):
    vfs = PakVFS(base, order)
    report = {"files": 0, "ok": 0, "errors": [], "warnings": [], "per_pak": {}}
    ids = {}
    flags_count = {}
    langs_count = {}
    fmt_count = {}
    color_count = {}
    for pak in vfs.order:
        for name in sorted(vfs.names("*.sp", pak=pak)):
            report["files"] += 1
            report["per_pak"][pak] = report["per_pak"].get(pak, 0) + 1
            data = vfs.read(name, pak)
            try:
                pkg = parse_sp(data)
            except SPError as e:
                report["errors"].append(f"{pak}/{name}: {e}")
                continue
            report["ok"] += 1
            for wmsg in check_sp(pkg, name):
                report["warnings"].append(f"{pak}/{name}: {wmsg}")
            idx = [e["index"] for e in pkg["entries"]]
            if idx and idx != list(range(len(idx))):
                report.setdefault("sparse_index", []).append(f"{pak}/{name}")
            ids.setdefault((pak, pkg["id"]), []).append(pkg["reference"])
            for e in pkg["entries"]:
                for fl in e["flags"]:
                    flags_count[fl] = flags_count.get(fl, 0) + 1
                for lang, v in e["text"].items():
                    langs_count[lang] = langs_count.get(lang, 0) + 1
                    for m in FORMAT_RE.findall(v):
                        fmt_count[m.decode()] = fmt_count.get(m.decode(), 0) + 1
                    for c in COLOR_CODES:
                        n = v.count(c.encode())
                        if n:
                            color_count[c] = color_count.get(c, 0) + n
            if out:
                # zmain.pak (Russian) stores cp1251 text in the TEXT_ENGLISH slot;
                # decide per value there, cp1252 everywhere else.
                j = to_json(pkg, "cp1252", {"ENGLISH": "auto"} if pak == "zmain" else {})
                j["source"] = f"{pak}/{name}"
                j["codepage_english"] = "cp1251/cp1252 per value" if pak == "zmain" else "cp1252"
                dst = os.path.join(out, pak, name.replace("/", os.sep) + ".json")
                os.makedirs(os.path.dirname(dst), exist_ok=True)
                with open(dst, "w", encoding="utf-8") as f:
                    json.dump(j, f, ensure_ascii=False, indent=1)
    # IDs: duplicates inside one pak
    for (pak, i), refs in ids.items():
        if len(refs) > 1:
            report["warnings"].append(f"{pak}: ID {i} shared by {refs}")
    report["flags"] = flags_count
    report["languages"] = langs_count
    report["format_specifiers"] = fmt_count
    report["color_codes"] = color_count
    return vfs, report


def all_names(vfs):
    """Union over every copy of every package in every pak: {'PKG_ENTRY': [pak, ...]}."""
    out = {}
    for pak in vfs.order:
        for name in vfs.names("strip/*.sp", pak=pak):
            pkg = parse_sp(vfs.read(name, pak))
            for e in pkg["entries"]:
                if e["reference"]:
                    out.setdefault(f"{pkg['reference']}_{e['reference']}".upper(), []).append(pak)
    return out


def validate_refs(vfs, table):
    """Every ^REF^ in menus (.rmf) and configs (.cfg) of every pak is checked
    (a) through the effective (priority-ordered) string table and
    (b) against the union of every package copy in every pak."""
    union = all_names(vfs)
    res = {"refs": 0, "resolved": 0, "shadowed": [], "missing": []}
    for pak in vfs.order:
        for name in vfs.names("*", pak=pak):
            if not (name.endswith(".rmf") or name.endswith(".cfg")):
                continue
            data = vfs.read(name, pak)
            for m in REF_RE.finditer(data):
                ref = m.group(1).decode()
                res["refs"] += 1
                if table.split(ref)[0]:
                    res["resolved"] += 1
                elif ref.upper() in union:
                    res["shadowed"].append(f"{pak}/{name}: ^{ref}^ (only in {union[ref.upper()]})")
                else:
                    res["missing"].append(f"{pak}/{name}: ^{ref}^")
    return res


def main(argv):
    if not argv:
        print(__doc__)
        return 2
    if argv[0] == "dump":
        pkg = parse_sp(open(argv[1], "rb").read())
        cp = argv[2] if len(argv) > 2 else "cp1252"
        sys.stdout.reconfigure(encoding="utf-8")
        print(json.dumps(to_json(pkg, cp), ensure_ascii=False, indent=1))
        return 0
    if argv[0] == "validate":
        base = DEFAULT_BASE
        out = None
        if "--base" in argv:
            base = argv[argv.index("--base") + 1]
        if "--out" in argv:
            out = argv[argv.index("--out") + 1]
        order = None
        if "--order" in argv:
            order = argv[argv.index("--order") + 1].split(",")
        vfs, rep = validate(base, out, order)
        table = StringTable(vfs)
        refs = validate_refs(vfs, table)
        print(f"sp files: {rep['files']}  parsed: {rep['ok']}  errors: {len(rep['errors'])}  "
              f"warnings: {len(rep['warnings'])}  per pak: {rep['per_pak']}")
        for e in rep["errors"]:
            print("  ERROR", e)
        for wmsg in rep["warnings"]:
            print("  WARN ", wmsg)
        print(f"packages with INDEX gaps (allowed): {len(rep.get('sparse_index', []))}")
        print("flags:", rep["flags"])
        print("languages:", rep["languages"])
        print("format specifiers:", rep["format_specifiers"])
        print("colour codes:", rep["color_codes"])
        print(f"^REF^ in rmf/cfg: {refs['refs']}  resolved (pak order {vfs.order}): "
              f"{refs['resolved']}  shadowed by a higher-priority copy: {len(refs['shadowed'])}  "
              f"missing everywhere: {len(refs['missing'])}")
        for u in refs["shadowed"]:
            print("  SHADOWED", u)
        for u in refs["missing"]:
            print("  MISSING ", u)
        if out:
            with open(os.path.join(out, "validation.json"), "w", encoding="utf-8") as f:
                json.dump({"sp": rep, "refs": refs}, f, indent=1)
        return 1 if rep["errors"] else 0
    if argv[0] == "lookup":
        table = StringTable(PakVFS())
        lang = argv[2] if len(argv) > 2 else "ENGLISH"
        v = table.lookup(argv[1], lang)
        sys.stdout.reconfigure(encoding="utf-8")
        print(table.split(argv[1]),
              repr(v.decode(guess_codepage([v])) if v is not None else None))
        return 0
    print(__doc__)
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
