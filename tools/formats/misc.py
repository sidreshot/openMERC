#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Implements: SPEC-133
"""Small text formats: .cfg (console scripts), .lst (map lists), .bpf (bot
personalities), .txt (sky lists, driver tables, bot list), .gpl (player model lists).

usage:
  misc.py validate [--out DIR]     parse every stock file of these types, cross-check
                                   references, write JSON summaries
  misc.py cfg <file.cfg>           print the command list of one script
"""
import json
import os
import re
import sys
from collections import Counter, defaultdict

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from vfs import PakVFS  # noqa: E402


class FormatError(ValueError):
    pass


# ---------------------------------------------------------------------------
# .cfg -- console script, Quake II command-buffer rules (public GPL behaviour):
# commands end at a newline or at ';' outside double quotes; tokens are
# whitespace separated; "..." groups a token (no escapes); '//' starts a comment
# that runs to the end of the line. $name is expanded at execution time.
# ---------------------------------------------------------------------------
def cfg_commands(text):
    """Return [(line, [tokens])]. A '//' comment is recognised only where a token
    would start; a bare word runs to whitespace or ';' (so 'http://x' is one word)."""
    out = []
    line = 1
    i, n = 0, len(text)
    cur = []
    start_line = 1

    def flush():
        nonlocal cur
        if cur:
            out.append((start_line, cur))
        cur = []

    while i < n:
        c = text[i]
        if c == "\n":
            flush()
            line += 1
            i += 1
            continue
        if c == ";":
            flush()
            i += 1
            continue
        if c <= " ":
            i += 1
            continue
        if c == "/" and text[i:i + 2] == "//":
            while i < n and text[i] != "\n":
                i += 1
            continue
        if not cur:
            start_line = line
        if c == '"':
            j = i + 1
            while j < n and text[j] not in '"\n':
                j += 1
            cur.append(text[i + 1:j])
            i = j + 1 if j < n and text[j] == '"' else j
            continue
        j = i
        while j < n and text[j] > " " and text[j] != ";":
            j += 1
        cur.append(text[i:j])
        i = j
    flush()
    return out


# ---------------------------------------------------------------------------
# .lst -- map list: per line   <map path relative to maps/>  <"description">
# ---------------------------------------------------------------------------
_LST = re.compile(r'^\s*(\S+)\s+"([^"]*)"\s*$')


def parse_lst(text):
    out = []
    for no, ln in enumerate(text.splitlines(), 1):
        if not ln.strip():
            continue
        m = _LST.match(ln)
        if not m:
            raise FormatError(f"line {no}: expected  map \"description\"")
        out.append({"map": m.group(1), "description": m.group(2), "line": no})
    return out


# ---------------------------------------------------------------------------
# .bpf -- bot personality: '#' comment lines, key=value lines, chat sections
# [SECT=n] ... [SECTEND=n] (n = 1..6). userinfo is a backslash key\value list.
# ---------------------------------------------------------------------------
BPF_KEYS = {"userinfo": str, "accuracy": int, "fov": int, "turning_speed": int,
            "view_distance": int, "general_intelligence": int, "canchat": int, "chatfreq": int}
_SECT = re.compile(r"^\[SECT=(\d+)\]$")
_SECTEND = re.compile(r"^\[SECTEND=(\d+)\]$")


def parse_bpf(text):
    res = {"keys": {}, "sections": {}, "comments": 0}
    sect = None
    for no, raw in enumerate(text.splitlines(), 1):
        ln = raw.strip()
        if sect is not None:
            m = _SECTEND.match(ln)
            if m:
                if int(m.group(1)) != sect:
                    raise FormatError(f"line {no}: SECTEND {m.group(1)} closes SECT {sect}")
                sect = None
            elif ln:
                res["sections"][sect].append(ln)
            continue
        if not ln:
            continue
        if ln.startswith("#"):
            res["comments"] += 1
            continue
        m = _SECT.match(ln)
        if m:
            sect = int(m.group(1))
            if sect in res["sections"]:
                raise FormatError(f"line {no}: section {sect} repeated")
            res["sections"][sect] = []
            continue
        if "=" not in ln:
            raise FormatError(f"line {no}: expected key=value")
        k, v = ln.split("=", 1)
        k = k.strip().lower()
        if k not in BPF_KEYS:
            raise FormatError(f"line {no}: unknown key {k}")
        if k in res["keys"]:
            raise FormatError(f"line {no}: key {k} repeated")
        res["keys"][k] = BPF_KEYS[k](v.strip())
    if sect is not None:
        raise FormatError(f"section {sect} not closed")
    ui = res["keys"].get("userinfo")
    if ui is not None:
        parts = ui.split("\\")
        if parts and parts[0] == "":
            parts = parts[1:]
        if len(parts) % 2:
            raise FormatError("userinfo: odd number of key/value parts")
        res["userinfo"] = dict(zip(parts[0::2], parts[1::2]))
    return res


# ---------------------------------------------------------------------------
# .gpl -- player model list: one model name per line, blank lines separate groups
# ---------------------------------------------------------------------------
def parse_gpl(text):
    groups, cur = [], []
    for ln in text.splitlines():
        s = ln.strip()
        if not s:
            if cur:
                groups.append(cur)
                cur = []
            continue
        if not re.fullmatch(r"[A-Za-z0-9_]+", s):
            raise FormatError(f"bad model name {s!r}")
        cur.append(s)
    if cur:
        groups.append(cur)
    return groups


# ---------------------------------------------------------------------------
# .txt variants
# ---------------------------------------------------------------------------
def parse_sky(text):
    lines = [ln.strip() for ln in text.splitlines() if ln.strip()]
    if len(lines) != 6:
        raise FormatError(f"sky list has {len(lines)} entries, expected 6")
    return lines


def parse_cards(text):
    out = []
    for no, ln in enumerate(text.splitlines(), 1):
        if not ln.strip():
            continue
        w = ln.split()
        if len(w) != 2:
            raise FormatError(f"line {no}: expected  <cfg> <substr,substr...>")
        out.append({"cfg": w[0], "match": w[1].split(","), "line": no})
    return out


def parse_wordlist(text):
    return [ln.strip() for ln in text.splitlines() if ln.strip()]


# ---------------------------------------------------------------------------
def validate(out=None):
    vfs = PakVFS()
    rep = {"counts": Counter(), "parsed": Counter(), "errors": [], "missing": [], "notes": []}
    cfg_inv = {"commands": Counter(), "set": defaultdict(dict), "bind": {}, "exec": Counter()}
    dumps = {}

    def dump(kind, where, obj):
        dumps.setdefault(kind, {})[where] = obj

    for pak in vfs.order:
        names = vfs.names("*", pak=pak)
        for name in sorted(names):
            ext = os.path.splitext(name)[1]
            if ext not in (".cfg", ".lst", ".bpf", ".txt", ".gpl"):
                continue
            where = f"{pak}/{name}"
            rep["counts"][ext] += 1
            text = vfs.read(name, pak).decode("latin-1")
            try:
                if ext == ".cfg":
                    cmds = cfg_commands(text)
                    for ln, tk in cmds:
                        head = tk[0].lower()
                        cfg_inv["commands"][head] += 1
                        if head in ("set", "add", "seta", "sset") and len(tk) >= 3:
                            cfg_inv["set"][tk[1].lower()][where] = [head] + tk[2:]
                        elif head == "bind" and len(tk) >= 3:
                            cfg_inv["bind"].setdefault(where, {})[tk[1].lower()] = tk[2]
                        elif head == "exec" and len(tk) >= 2:
                            cfg_inv["exec"][tk[1]] += 1
                            if not vfs.exists(tk[1]):
                                rep["missing"].append(f"{where}:{ln}: exec {tk[1]}")
                        elif head in ("map", "gamemap") and len(tk) >= 2 and "$" not in tk[1]:
                            if not vfs.exists(f"maps/{tk[1]}.bsp"):
                                rep["missing"].append(f"{where}:{ln}: {head} {tk[1]}")
                    dump("cfg", where, [[ln] + tk for ln, tk in cmds])
                elif ext == ".lst":
                    ents = parse_lst(text)
                    for e in ents:
                        if not vfs.exists(f"maps/{e['map']}.bsp"):
                            rep["missing"].append(f"{where}: map {e['map']}")
                    dump("lst", where, ents)
                elif ext == ".bpf":
                    b = parse_bpf(text)
                    k = b["keys"]
                    for req in ("userinfo", "accuracy", "fov", "turning_speed", "view_distance",
                                "general_intelligence", "canchat"):
                        if req not in k:
                            rep["errors"].append(f"{where}: missing {req}")
                    if not 1 <= k.get("accuracy", 1) <= 180:
                        rep["errors"].append(f"{where}: accuracy {k['accuracy']} outside 1..180")
                    if "chatfreq" in k and not 1 <= k["chatfreq"] <= 100:
                        rep["errors"].append(f"{where}: chatfreq outside 1..100")
                    skin = b.get("userinfo", {}).get("skin")
                    if skin and not vfs.exists(f"ghoul/pmodels/{skin}.gpm"):
                        rep["missing"].append(f"{where}: skin {skin} (ghoul/pmodels/{skin}.gpm)")
                    if sorted(b["sections"]) not in ([], [1, 2, 3, 4, 5, 6]):
                        rep["notes"].append(f"{where}: sections {sorted(b['sections'])}")
                    dump("bpf", where, b)
                elif ext == ".gpl":
                    groups = parse_gpl(text)
                    for g in groups:
                        for m in g:
                            if not vfs.exists(f"ghoul/pmodels/{m}.gpm"):
                                rep["missing"].append(f"{where}: model {m}")
                    dump("gpl", where, groups)
                elif ext == ".txt":
                    if name.startswith("pics/sky/"):
                        faces = parse_sky(text)
                        for f in faces:
                            if f != "none" and not vfs.exists(f + ".m32"):
                                rep["missing"].append(f"{where}: sky face {f}")
                        dump("sky", where, faces)
                    elif name.endswith("drivers/cards.txt"):
                        cards = parse_cards(text)
                        for c in cards:
                            if not vfs.exists(f"drivers/{c['cfg']}.cfg"):
                                rep["missing"].append(f"{where}: drivers/{c['cfg']}.cfg")
                        dump("cards", where, cards)
                    elif name.endswith("drivers/drivers.txt"):
                        dump("drivers", where, parse_wordlist(text))
                    elif name.endswith("bot/botlist.txt"):
                        bots = parse_wordlist(text)
                        for b in bots:
                            if not vfs.exists(f"bot/bots/{b}.bpf"):
                                rep["missing"].append(f"{where}: bot/bots/{b}.bpf")
                        dump("botlist", where, bots)
                    else:
                        rep["notes"].append(f"{where}: free text ({len(text)} bytes), not parsed")
                rep["parsed"][ext] += 1
            except FormatError as e:
                rep["errors"].append(f"{where}: {e}")
    rep["counts"] = dict(rep["counts"])
    rep["parsed"] = dict(rep["parsed"])
    inv = {"commands": dict(cfg_inv["commands"].most_common()),
           "cvars_set": {k: v for k, v in sorted(cfg_inv["set"].items())},
           "binds": cfg_inv["bind"], "exec": dict(cfg_inv["exec"])}
    if out:
        os.makedirs(out, exist_ok=True)
        with open(os.path.join(out, "misc_validation.json"), "w", encoding="utf-8") as f:
            json.dump(rep, f, indent=1)
        with open(os.path.join(out, "cfg_inventory.json"), "w", encoding="utf-8") as f:
            json.dump(inv, f, indent=1)
        with open(os.path.join(out, "misc_dump.json"), "w", encoding="utf-8") as f:
            json.dump(dumps, f, indent=1)
    return rep, inv, dumps


def main(argv):
    if not argv:
        print(__doc__)
        return 2
    if argv[0] == "validate":
        out = argv[argv.index("--out") + 1] if "--out" in argv else None
        rep, inv, _ = validate(out)
        print("files:", rep["counts"], " parsed:", rep["parsed"], " errors:", len(rep["errors"]))
        for e in rep["errors"]:
            print("  ERROR  ", e)
        for m in rep["missing"]:
            print("  MISSING", m)
        for n in rep["notes"]:
            print("  NOTE   ", n)
        print("cfg commands:", inv["commands"])
        print("distinct cvars set in cfg:", len(inv["cvars_set"]))
        return 1 if rep["errors"] else 0
    if argv[0] == "cfg":
        for ln, tk in cfg_commands(open(argv[1], "rb").read().decode("latin-1")):
            print(ln, tk)
        return 0
    print(__doc__)
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
