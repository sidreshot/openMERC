#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Implements: SPEC-130
"""Menu files (menus/*.rmf): tokenizer, schema-checked parser, JSON dump,
tag/attribute inventory and asset-reference check.

usage:
  rmf.py dump <file.rmf>                      JSON tree of one file (from disk)
  rmf.py validate [--out DIR] [--order a,b]   parse every .rmf in every stock pak
                                              (strict schema), write JSON trees,
                                              inventory and reference report
"""
import json
import os
import re
import sys
from collections import Counter, defaultdict

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from vfs import PakVFS, DEFAULT_BASE  # noqa: E402


class RMFError(ValueError):
    pass


# ---------------------------------------------------------------------------
# Lexer
# ---------------------------------------------------------------------------
# A file is a stream of
#   <name tok tok ...>   tag; tokens are "quoted" (no escapes; may contain < > and
#                        newlines) or bare runs of non-space, non-quote characters
#   </name>              closing tag (only </stm> occurs)
#   "quoted text"        text run outside tags
#   bare words           text run outside tags (whitespace separated)
# There is no comment syntax and no escape character.

_BARE_IN_TAG = re.compile(r'[^\s">]+')
_BARE_TEXT = re.compile(r'[^\s<"]+')


def lex(text):
    i, n, line = 0, len(text), 1
    out = []
    while i < n:
        c = text[i]
        if c == "\n":
            line += 1
            i += 1
        elif c in " \t\r\f\v":
            i += 1
        elif c == "<":
            start_line = line
            j = i + 1
            toks = []
            closing = False
            if j < n and text[j] == "/":
                closing = True
                j += 1
            while True:
                if j >= n:
                    raise RMFError(f"line {start_line}: unterminated tag")
                ch = text[j]
                if ch == ">":
                    j += 1
                    break
                if ch == "\n":
                    line += 1
                    j += 1
                elif ch in " \t\r\f\v":
                    j += 1
                elif ch == '"':
                    k = text.find('"', j + 1)
                    if k < 0:
                        raise RMFError(f"line {line}: unterminated quote in tag")
                    s = text[j + 1:k]
                    line += s.count("\n")
                    toks.append((s, True))
                    j = k + 1
                else:
                    m = _BARE_IN_TAG.match(text, j)
                    toks.append((m.group(0), False))
                    j = m.end()
            if not toks:
                raise RMFError(f"line {start_line}: empty tag")
            out.append(("close" if closing else "tag", toks, start_line))
            i = j
        elif c == '"':
            k = text.find('"', i + 1)
            if k < 0:
                raise RMFError(f"line {line}: unterminated quoted text")
            s = text[i + 1:k]
            out.append(("text", (s, True), line))
            line += s.count("\n")
            i = k + 1
        else:
            m = _BARE_TEXT.match(text, i)
            out.append(("text", (m.group(0), False), line))
            i = m.end()
    return out


# ---------------------------------------------------------------------------
# Schema (inferred from the 492 stock files; every tag/option seen is listed)
# ---------------------------------------------------------------------------
# argument types:  num = number literal, int = integer literal, any other type
# name is a semantic label only (str, cvar, tint, key, cmd, menu, cfg, img, ...)
NUMERIC = {"num", "int"}

OPTS = {
    # interaction
    "key": ["key", "cmd"],                  # bind key/mouse button -> command
    "ckey": ["key", "cvar", "cmd", "cmd"],  # cvar != 0 ? cmd1 : cmd2 (hypothesis)
    "tip": ["str"],                         # tool-tip text
    "ifne": ["cvar", "num"],                # item shown only if cvar != value (hyp.)
    "ifgt": ["cvar", "num"],                # item shown only if cvar > value (hyp.)
    # binding to cvars
    "cvar": ["cvar"], "cvari": ["cvar"], "cvar2": ["cvar"], "setimage": ["cvar"],
    "hostname": ["cvar"], "address": ["cvar"],
    "match": ["str"], "bitmask": ["int"], "files": ["dir", "pattern", "ext"],
    "min": ["num"], "max": ["num"], "step": ["num"],
    "maxchars": ["int"],
    # geometry
    "width": ["num"], "height": ["num"], "xoff": ["num"], "yoff": ["num"],
    "scale": ["num"], "spacing": ["num"], "resize": ["num", "num"],
    "rotate": ["num", "num", "num"],
    "border": ["num", "num", "tint"],
    # colours / fonts
    "tint": ["tint"], "atint": ["tint"], "btint": ["tint"], "ctint": ["tint"],
    "dtint": ["tint"], "backfill": ["tint"], "bgcolor": ["tint"],
    "tiptint": ["tint"], "tipbtint": ["tint"], "tipfont": ["font"],
    "type": ["font"],
    # text
    "atext": ["str"], "title": ["str"], "headings": ["str"],
    "column": ["int", "str"], "conv": ["str", "str"], "convbits": ["str", "str", "str"],
    "sort": ["word"], "align": ["word"],
    # ghoul animation / 3D
    "bolt": ["bolt"], "bbolt": ["bolt"], "bgbolt": ["bolt", "seq"], "gbolt": ["bolt", "seq"],
    "time": ["num"], "end": ["cmd"], "speed": ["num"], "delay": ["num"],
    # frames / pages
    "page": ["menu"], "cpage": ["cvar"], "cut": ["frame"],
    "tile": ["img"], "stretch": ["img"],
    "command": ["cmd"],
}
FLAGS = {"oneshot", "control", "global", "hidden", "history", "invisible", "noshade",
         "regular", "transient", "noscale", "saving", "tab", "private", "weapons", "items",
         "center", "nopush", "waitanim", "fragile"}

# tag: (positional types, allowed options/flags, varargs-type or None)
T = {}


def tag(name, pos=(), opts="", var=None):
    T[name] = (list(pos), set(opts.split()), var)


# document structure
tag("stm", (), "global resize nopush command waitanim fragile")
tag("strings", ["pkg"])
tag("include", ["menu"])
tag("cinclude", ["cvar", "menu"])
tag("cninclude", ["cvar", "menu"])
tag("einclude", ["cvar", "str", "menu"])
tag("exinclude", ["cvar", "menu", "menu"])
tag("includecvar", ["cvar"])
tag("frame", ["frame", "num", "num"], "page cpage cut border backfill center")
tag("config", ["cfg"])
tag("exitcfg", ["cfg"])
tag("onexit", ["cvar", "menu"])
# state / definitions
tag("set", ["cvar", "str"])
tag("tint", ["name", "tint"])
tag("defaults", (), "tiptint tipbtint tipfont")
tag("alias", ["name", "cmd"])
tag("cbuf", ["cmd"])
tag("key", ["key", "cmd"])
tag("ikey", ["cmd", "cmd"])
tag("timeout", ["num", "cmd"])
tag("cursor", ["int"])
tag("autoscroll", ["num"])
# flow layout
for t in ("br", "hbr", "center", "left", "right", "normal"):
    tag(t)
tag("tab", (), "", var="num")
tag("font", (), "type tint atint")
tag("backdrop", (), "bgcolor tile stretch global")
tag("hr", (), "width tint")
tag("vbar", (), "tint")
# widgets
tag("text", ["str"], "key tip border tint atint bbolt bolt xoff yoff regular ifne")
tag("ctext", ["cvar"], "atext key ckey bbolt width yoff invisible noshade ifgt")
tag("image", ["img"], "tint atint bbolt bolt key tip border setimage transient width height "
                      "xoff yoff noscale")
tag("blank", ["num", "num"], "bbolt tip key ckey")
tag("list", ["str"], "cvar cvari match bitmask files atext key tip width ifne")
tag("slider", (), "cvar cvari min max step key")
tag("input", (), "cvar width border global maxchars hidden history bbolt xoff yoff tip btint")
tag("setkey", ["cmd"], "atext tab tip")
tag("hbar", ["img", "img"], "cvar invisible")
tag("ticker", ["str"], "bbolt speed width xoff yoff delay")
tag("ghoul", ["ghb"], "bgbolt gbolt time xoff yoff width height oneshot end control")
tag("bghoul", ["ghb"], "xoff yoff scale time oneshot control end width height")
tag("gpm", ["cvar", "cvar"], "cvari cvar2 bgbolt scale rotate")
tag("selection", ["img", "num", "num"], "tint ctint dtint bolt xoff yoff weapons items")
tag("filebox", ["dir", "pattern", "ext"], "sort align width tint btint atint cvar key")
tag("loadbox", (), "width cvar key saving spacing")
tag("listfile", ["cvar"], "align cvar")
tag("serverbox", (), "column cvar hostname address height")
tag("serverdetail", (), "cvar tab title")
tag("players", (), "cvar title")
tag("users", (), "cvar key btint")
tag("rooms", (), "cvar")
tag("chat", (), "cvar private")
tag("bot", ["cvar"], "tab headings align")
tag("botlist", (), "cvar")
tag("dmnames", ["str"])
tag("translate", ["int"], "conv convbits")
tag("emote", ["str", "str"])
tag("semote", ["str", "str"])
tag("demote", ["str"])

_NUM = re.compile(r"-?(\d+\.?\d*|\.\d+)$")
_INT = re.compile(r"-?\d+$")


def _check_type(ty, tok, line, what):
    s, _q = tok
    if ty == "num" and not _NUM.match(s):
        raise RMFError(f"line {line}: {what}: expected number, got {s!r}")
    if ty == "int" and not _INT.match(s):
        raise RMFError(f"line {line}: {what}: expected integer, got {s!r}")


def _conv(ty, tok):
    s, q = tok
    if ty == "int":
        return int(s)
    if ty == "num":
        return float(s) if ("." in s) else int(s)
    return s


def parse_tag(toks, line):
    name = toks[0][0].lower()
    if toks[0][1]:
        raise RMFError(f"line {line}: quoted tag name")
    if name not in T:
        raise RMFError(f"line {line}: unknown tag <{name}>")
    pos, allowed, var = T[name]
    rest = toks[1:]
    node = {"tag": name, "line": line}
    args = []
    if len(rest) < len(pos):
        raise RMFError(f"line {line}: <{name}> needs {len(pos)} arguments, got {len(rest)}")
    for ty, tok in zip(pos, rest):
        _check_type(ty, tok, line, f"<{name}> argument")
        args.append(_conv(ty, tok))
    i = len(pos)
    if var:
        while i < len(rest):
            _check_type(var, rest[i], line, f"<{name}> argument")
            args.append(_conv(var, rest[i]))
            i += 1
    if args:
        node["args"] = args
    opts = []
    while i < len(rest):
        s, q = rest[i]
        k = s.lower()
        if q or k not in allowed:
            raise RMFError(f"line {line}: <{name}>: unexpected {'quoted ' if q else ''}token {s!r}")
        if k in FLAGS and k not in OPTS:
            opts.append([k])
            i += 1
            continue
        types = OPTS[k]
        vals = rest[i + 1:i + 1 + len(types)]
        if len(vals) != len(types):
            raise RMFError(f"line {line}: <{name}> option {k} needs {len(types)} values")
        for ty, tok in zip(types, vals):
            _check_type(ty, tok, line, f"<{name}> {k}")
        opts.append([k] + [_conv(ty, tok) for ty, tok in zip(types, vals)])
        i += 1 + len(types)
    if opts:
        node["opts"] = opts
    return node


def parse(text):
    """Parse an .rmf text (str, latin-1 decoded) into a JSON-able dict.

    Result: {"document": bool, "stm": [opts] | None, "body": [...],
             "after_end": [...]}  -- after_end holds anything after </stm>.
    """
    items = lex(text)
    doc = {"document": False, "stm": None, "body": [], "after_end": []}
    target = doc["body"]
    seen_close = False
    for kind, val, line in items:
        if kind == "text":
            s, q = val
            target.append({"text": s, "quoted": q, "line": line})
        elif kind == "close":
            name = val[0][0].lower()
            if name != "stm" or len(val) != 1:
                raise RMFError(f"line {line}: unexpected </{name}>")
            if not doc["document"] or seen_close:
                raise RMFError(f"line {line}: </stm> without <stm>")
            seen_close = True
            target = doc["after_end"]
        else:
            node = parse_tag(val, line)
            if node["tag"] == "stm":
                if doc["document"] or doc["body"]:
                    raise RMFError(f"line {line}: <stm> not at start")
                doc["document"] = True
                doc["stm"] = node.get("opts", [])
                continue
            target.append(node)
    if doc["document"] and not seen_close:
        raise RMFError("missing </stm>")
    return doc


# ---------------------------------------------------------------------------
# References
# ---------------------------------------------------------------------------
CMD_SPLIT = re.compile(r";")


def split_commands(cmd):
    out = []
    for part in CMD_SPLIT.split(cmd):
        w = part.strip().split()
        if w:
            out.append(w)
    return out


def iter_nodes(doc):
    for n in doc["body"]:
        yield n
    for n in doc["after_end"]:
        yield n


def references(doc):
    """Yield (kind, value, line) for every asset/menu/cvar/command reference."""
    for o in doc.get("stm") or []:
        if o[0] == "command":
            for w in split_commands(o[1]):
                yield ("command", w, 0)
    for n in iter_nodes(doc):
        if "tag" not in n:
            continue
        t, a, ln = n["tag"], n.get("args", []), n["line"]
        pos = T[t][0]
        for ty, v in zip(pos, a):
            if ty in ("menu", "pkg", "cfg", "img", "ghb", "cvar", "dir"):
                if t == "exitcfg":
                    yield ("cfg", "menus/" + v, ln)
                elif t == "config":
                    yield ("cfg", v, ln)
                elif ty == "img" and t in ("hbar", "selection", "image"):
                    yield ("img", v, ln)
                else:
                    yield (ty, v, ln)
            if ty == "cmd":
                for w in split_commands(str(v)):
                    yield ("command", w, ln)
        for o in n.get("opts", []):
            k = o[0]
            types = OPTS.get(k, [])
            for ty, v in zip(types, o[1:]):
                if ty == "cmd":
                    for w in split_commands(str(v)):
                        yield ("command", w, ln)
                elif ty == "cvar":
                    yield ("cvar", v, ln)
                elif ty == "menu":
                    yield ("menu", v, ln)
                elif ty == "img":
                    yield ("img", v, ln)
                elif ty in ("bolt", "seq"):
                    yield (ty, v, ln)
            if k == "files":
                yield ("dir", o[1], ln)


def layout_frames(doc, screen=(640, 480)):
    """Frame rectangles under the HYPOTHESIS of SPEC-130 (not verified in the game):
    each <frame name W H [cut parent]> takes a piece of the parent's free area
    (the screen, or the <stm resize W H> window, when no cut); W=0 -> full free width,
    strip from the top; H=0 -> full free height, strip from the left; 0 0 -> the rest;
    both non-zero -> from the left if H fills the free height, from the top if W
    fills the free width, otherwise 'ambiguous' (placed top-left, cut from the top).
    A frame's content area is its rectangle inset by the border padding."""
    for o in doc.get("stm") or []:
        if o[0] == "resize":
            screen = (o[1], o[2])
    free = {None: [0, 0, screen[0], screen[1]]}
    out = []
    for n in doc["body"]:
        if n.get("tag") != "frame":
            continue
        name, w, h = n["args"]
        o = {x[0]: x[1:] for x in n.get("opts", [])}
        parent = o.get("cut", [None])[0]
        issues = []
        if parent not in free:
            issues.append(f"unknown parent {parent}")
            free[parent] = [0, 0, 0, 0]
        x, y, fw, fh = free[parent]
        ww = fw if w == 0 else w
        hh = fh if h == 0 else h
        if ww > fw or hh > fh:
            issues.append(f"overflow: {w}x{h} requested, {fw}x{fh} free")
        ww, hh = min(ww, fw), min(hh, fh)
        if w == 0 and h == 0:
            free[parent] = [x, y, 0, 0]
        elif w == 0 or (h != 0 and ww == fw and hh != fh):
            free[parent] = [x, y + hh, fw, fh - hh]
        elif h == 0 or hh == fh:
            free[parent] = [x + ww, y, fw - ww, fh]
        else:
            issues.append(f"ambiguous: {w}x{h} inside free {fw}x{fh}")
            free[parent] = [x, y + hh, fw, fh - hh]
        pad = o.get("border", [0])[0]
        inner = [x + pad, y + pad, ww - 2 * pad, hh - 2 * pad]
        free[name] = inner[:]
        out.append({"name": name, "parent": parent, "rect": [x, y, ww, hh], "inner": inner,
                    "page": o.get("page", [None])[0], "cpage": o.get("cpage", [None])[0],
                    "issues": issues})
    return out


STRREF = re.compile(r"\^([A-Za-z0-9_]+)\^")


def string_refs(text):
    return STRREF.findall(text)


# ---------------------------------------------------------------------------
# Whole-corpus validation
# ---------------------------------------------------------------------------
def _resolve_img(vfs, name):
    """Menu pictures: pics/menus/<name>.m32 (55/55 stock references resolve there)."""
    cand = "pics/menus/" + name.replace("\\", "/") + ".m32"
    return cand if vfs.exists(cand) else None


def validate(out=None, order=None, base=DEFAULT_BASE):
    vfs = PakVFS(base, order)
    rep = {"files": 0, "parsed": 0, "errors": [], "per_pak": Counter(), "documents": 0,
           "fragments": 0, "after_end": [],
           "frame_files": 0, "frame_issues": {}}
    inv_tag = Counter()
    inv_files = defaultdict(set)
    inv_opt = defaultdict(Counter)
    inv_ex = defaultdict(list)
    refs = defaultdict(lambda: defaultdict(set))   # kind -> value -> {where}
    commands = Counter()
    cvars_sub = Counter()
    cvars_hash = Counter()
    strrefs = Counter()
    stm_opts = Counter()
    tint_defs = {}
    tint_uses = Counter()
    for pak in vfs.order:
        for name in sorted(vfs.names("*.rmf", pak=pak)):
            rep["files"] += 1
            rep["per_pak"][pak] += 1
            raw = vfs.read(name, pak)
            text = raw.decode("latin-1")
            where = f"{pak}/{name}"
            try:
                doc = parse(text)
            except RMFError as e:
                rep["errors"].append(f"{where}: {e}")
                continue
            rep["parsed"] += 1
            rep["documents" if doc["document"] else "fragments"] += 1
            frames = layout_frames(doc)
            if frames:
                rep["frame_files"] += 1
                iss = [f"{fr['name']}: {i}" for fr in frames for i in fr["issues"]]
                if iss:
                    rep["frame_issues"][where] = iss
            if doc["after_end"]:
                rep["after_end"].append(where)
            for o in doc["stm"] or []:
                stm_opts[o[0]] += 1
            for n in iter_nodes(doc):
                if "tag" in n:
                    inv_tag[n["tag"]] += 1
                    inv_files[n["tag"]].add(where)
                    if len(inv_ex[n["tag"]]) < 3:
                        inv_ex[n["tag"]].append(where)
                    for o in n.get("opts", []):
                        inv_opt[n["tag"]][o[0]] += 1
                else:
                    inv_tag["#text"] += 1
            for n in iter_nodes(doc):
                if n.get("tag") == "tint":
                    tint_defs[n["args"][0].lower()] = n["args"][1]
                for o in n.get("opts", []):
                    for ty, val in zip(OPTS.get(o[0], []), o[1:]):
                        if ty == "tint":
                            tint_uses[str(val).lower()] += 1
            for kind, v, ln in references(doc):
                if kind == "command":
                    commands[v[0].lower()] += 1
                    head = v[0].lower()
                    if head in ("menu", "popmenu") and len(v) > 1:
                        refs["menu"][v[1]].add(where)
                    elif head == "exec" and len(v) > 1:
                        refs["cfg"][v[1]].add(where)
                    elif head == "play" and len(v) > 1:
                        refs["sound"][v[1]].add(where)
                else:
                    refs[kind][str(v)].add(where)
            for m in re.findall(r"\$([A-Za-z_][A-Za-z0-9_]*)", text):
                cvars_sub[m] += 1
            for m in re.findall(r"#([A-Za-z_][A-Za-z0-9_]*)", text):
                cvars_hash[m] += 1
            for s in string_refs(text):
                strrefs[s] += 1
            if out:
                dst = os.path.join(out, "json", pak, name.replace("/", os.sep) + ".json")
                os.makedirs(os.path.dirname(dst), exist_ok=True)
                with open(dst, "w", encoding="utf-8") as f:
                    json.dump({"source": where, **doc}, f, ensure_ascii=False, indent=1)

    # reference existence
    check = {}

    def chk(kind, val, path_fn):
        ok, bad = [], []
        skipped = []
        for v, wh in sorted(refs[kind].items()):
            if "$" in v or "#" in v:
                skipped.append(v)
                continue
            p = path_fn(v)
            (ok if p else bad).append((v, p or sorted(wh)[:3]))
        check[kind] = {"total": len(refs[kind]), "found": len(ok), "missing": bad,
                       "runtime": skipped}

    chk("menu", None, lambda v: (f"menus/{v}.rmf" if vfs.exists(f"menus/{v}.rmf") else None))
    chk("pkg", None, lambda v: (f"strip/{v}.sp" if vfs.exists(f"strip/{v}.sp") else None))
    chk("cfg", None, lambda v: next((c for c in (v, v + ".cfg") if vfs.exists(c)), None))
    chk("img", None, lambda v: _resolve_img(vfs, v))
    chk("ghb", None, lambda v: (f"ghoul/menus/{v}.ghb" if vfs.exists(f"ghoul/menus/{v}.ghb") else None))
    chk("sound", None, lambda v: (f"sound/{v}" if vfs.exists(f"sound/{v}") else None))
    chk("dir", None, lambda v: (v if vfs.dir_exists(v) else None))

    undefined = sorted(t for t in tint_uses if t not in tint_defs and not t.startswith("0x"))
    names = [t for t in tint_uses if not t.startswith("0x")]
    check["tint"] = {"total": len(names), "found": len(names) - len(undefined),
                     "missing": [(t, "no <tint> definition") for t in undefined],
                     "literals": sorted(t for t in tint_uses if t.startswith("0x"))}
    rep["per_pak"] = dict(rep["per_pak"])
    inventory = {
        t: {"uses": inv_tag[t], "files": len(inv_files[t]),
            "positional": T[t][0] if t in T else None,
            "varargs": T[t][2] if t in T else None,
            "options": dict(inv_opt[t].most_common()),
            "examples": inv_ex[t]}
        for t in sorted(inv_tag) if t != "#text"
    }
    result = {"report": rep, "text_runs": inv_tag["#text"], "stm_options": dict(stm_opts),
              "inventory": inventory, "commands": dict(commands.most_common()),
              "cvar_dollar": dict(cvars_sub.most_common()),
              "cvar_hash": dict(cvars_hash.most_common()),
              "string_refs": len(strrefs), "string_ref_uses": sum(strrefs.values()),
              "reference_check": check,
              "bolts": sorted(refs["bolt"]), "sequences": sorted(refs["seq"]),
              "cvars_bound": sorted(refs["cvar"])}
    if out:
        with open(os.path.join(out, "rmf_inventory.json"), "w", encoding="utf-8") as f:
            json.dump(result, f, indent=1, default=list)
    return result


def main(argv):
    if not argv:
        print(__doc__)
        return 2
    if argv[0] == "dump":
        text = open(argv[1], "rb").read().decode("latin-1")
        print(json.dumps(parse(text), indent=1, ensure_ascii=False))
        return 0
    if argv[0] == "validate":
        out = argv[argv.index("--out") + 1] if "--out" in argv else None
        order = argv[argv.index("--order") + 1].split(",") if "--order" in argv else None
        r = validate(out, order)
        rep = r["report"]
        print(f"rmf files: {rep['files']}  parsed: {rep['parsed']}  errors: {len(rep['errors'])}  "
              f"per pak: {rep['per_pak']}")
        print(f"documents (<stm>..</stm>): {rep['documents']}  fragments (include-only): "
              f"{rep['fragments']}  content after </stm>: {rep['after_end']}")
        for e in rep["errors"]:
            print("  ERROR", e)
        print(f"tags: {len(r['inventory'])}  text runs: {r['text_runs']}")
        for kind, c in r["reference_check"].items():
            print(f"refs {kind:6}: {c['total']:4} distinct, {c['found']:4} found, "
                  f"{len(c['missing'])} missing, {len(c.get('runtime', []))} built from cvars "
                  f"at run time {c.get('runtime', [])}")
            for v, wh in c["missing"]:
                print(f"     MISSING {kind} {v!r} used in {wh}")
        print("console commands used:", len(r["commands"]))
        print(f"bolt names: {len(r['bolts'])}, animation names: {len(r['sequences'])} (not checked)")
        print(f"frame layouts (SPEC-130 hypothesis): {rep['frame_files']} files, "
              f"{len(rep['frame_issues'])} with issues")
        for k, v in rep["frame_issues"].items():
            print("  FRAMES", k, v)
        return 1 if rep["errors"] else 0
    print(__doc__)
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
