#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# OpenMerc - legal scan of a source tree before publication (docs/LEGAL_NOTES.md).
#
# Finds:
#   1. large constant arrays of numbers (hex bytes/words, floats) in source files;
#   2. long runs of hex-word / raw-bytes rows and long numbered string lists in documents (table dumps);
#   3. forbidden markers: references to the reverse team's workspace or tools output, binary addresses in specs;
#   4. paths that must not be published (.publish-exclude) when scanning an export (--export).
# Third-party GPL/BSD/zlib code listed in THIRD_PARTY.md (Quake II, Yamagi, stb, miniz, ...) is reported
# separately as "third-party" and does not fail the scan.
#
# usage: legal_scan.py [root] [--export] [--min-array N] [--json out.json]
# exit status: 0 clean, 1 findings
import argparse
import json
import fnmatch
import os
import re
import subprocess
import sys
from pathlib import Path

SRC_EXT = {".c", ".h", ".cpp", ".hpp", ".cc", ".inc", ".py", ".cs", ".java", ".kt"}
DOC_EXT = {".md", ".txt", ".json", ".cfg", ".csv"}
# third-party code that carries its own tables (listed in THIRD_PARTY.md); reported, not failed
THIRD_PARTY = [
    "engine/src/client/refresh/files/stb_image.h", "engine/src/client/sound/header/stb_vorbis.h",
    "engine/src/client/vid/header/stb_image_write.h", "engine/src/common/unzip/*", "engine/src/common/md4.c",
    "engine/src/client/refresh/constants/*", "engine/src/client/refresh/*/header/*", "engine/src/common/shared/*",
    "engine/src/client/refresh/gl3/glad/*", "engine/src/client/refresh/gles3/glad*/*", "engine/src/client/refresh/files/stb_*",
    "engine/src/client/refresh/files/pcx.c", "engine/src/client/refresh/files/*", "engine/src/backends/*",
    "engine/src/client/cl_cin.c", "engine/src/common/crc.c", "engine/src/common/szone.c",
    "engine/src/common/header/crc.h", "engine/src/client/input/*", "engine/src/client/curl/*",
    "engine/stuff/*", "engine/src/common/movemsg.c", "engine/src/client/vid/icon/*",
]
FORBIDDEN = [
    (re.compile(r"sof-re(?:\b(?!gistry)|[\\\\/])", re.I), "reverse-team workspace path"),
    (re.compile(r"\b(ghidra|reva|rea-\w+|decompil\w*)\b", re.I), "reverse-engineering tool output reference"),
    (re.compile(r"\bFUN_[0-9a-f]{6,8}\b|\bDAT_[0-9a-f]{6,8}\b|\bLAB_[0-9a-f]{6,8}\b"), "decompiler symbol name"),
    (re.compile(r"\b0x(?:10|20)0(?!00000\b)[0-9a-f]{5}\b", re.I), "possible address inside an original DLL"),
]
# documents that legitimately talk about the process (CLEANROOM etc.): markers there are expected
MARKER_OK = ["CLEANROOM.md", "docs/PLAN.md", "docs/STATUS.md", "docs/process/*", "docs/LEGAL_NOTES.md", "docs/DEVIATIONS.md", "tools/legal_scan.py",
             "docs/impl/LEGAL-tables.md", "docs/private/*", "README.md", "THIRD_PARTY.md", "tools/spec_gate.py",
             "tools/approve_spec.py"]

NUM = r"(?:0x[0-9a-fA-F]+[uUlL]*|-?\d+\.\d*(?:[eE][-+]?\d+)?f?|-?\d+[uUlL]*)"
NUMROW = re.compile(r"^\s*(?:\{\s*)?" + NUM + r"(?:\s*,\s*" + NUM + r"){3,}\s*,?\s*(?:\}\s*,?)?\s*(?://.*|/\*.*\*/)?$")
HEXROW = re.compile(r"(?:\b[0-9a-f]{8}\b[\s\t]+){2,}[0-9a-f]{8}\b")
NUMBERED = re.compile(r"^\d+\t")
# a row written as a macro call: G3A("name", MOVE, 1.0f, ...)  (optionally continued with a backslash)
MACROROW = re.compile(r'^\s*[A-Z][A-Z0-9_]*\("[^"]*".*\)\s*,?\s*\\?\s*(//.*)?$')
# a row of a struct table: {"name", 1, 2.0f, ...},  (strings mixed with numbers)
STRUCTROW = re.compile(r"^\s*\{\s*(?:\"[^\"]*\"|nullptr|NULL|" + NUM + r"|[A-Z_][A-Z0-9_]*)(?:\s*,\s*(?:\"[^\"]*\"|nullptr|NULL|true|false|" + NUM + r"|[A-Za-z_][A-Za-z0-9_:]*))+\s*\}\s*,?\s*(?://.*|/\*.*\*/)?$")


def matches(path, pats):
    return any(fnmatch.fnmatch(path, p) for p in pats)


def tracked(root):
    try:
        out = subprocess.run(["git", "-C", str(root), "ls-files"], capture_output=True, text=True, check=True).stdout
        return [l for l in out.split("\n") if l]
    except Exception:
        return [str(p.relative_to(root)).replace("\\", "/") for p in root.rglob("*") if p.is_file()]


def scan_file(root, rel, min_array, findings):
    p = root / rel
    ext = p.suffix.lower()
    if ext not in SRC_EXT | DOC_EXT:
        return
    try:
        lines = p.read_text(encoding="utf-8", errors="replace").split("\n")
    except OSError:
        return
    third = matches(rel, THIRD_PARTY)
    kind = "third-party" if third else "finding"
    # 1/2: runs of numeric rows (source) or hex rows / numbered rows (documents)
    run, start, nums = 0, 0, 0
    for i, l in enumerate(lines + [""]):
        isrow = False
        if ext in SRC_EXT and NUMROW.match(l):
            isrow, n = True, len(re.findall(NUM, l))
        elif ext in DOC_EXT and (HEXROW.search(l) or NUMBERED.match(l)):
            isrow, n = True, 1
        if isrow:
            if not run:
                start, nums = i, 0
            run += 1
            nums += n
        else:
            if (ext in SRC_EXT and nums >= min_array) or (ext in DOC_EXT and run >= 64):
                findings.append({"kind": kind, "file": rel, "line": start + 1, "rows": run, "values": nums,
                                 "what": "constant table" if ext in SRC_EXT else "table dump in a document"})
            run = 0
    # 1b: long struct tables (behaviour data from the specs): listed for review, not failed
    if ext in SRC_EXT and not third:
        run = 0
        for i, l in enumerate(lines + [""]):
            if STRUCTROW.match(l) or MACROROW.match(l) or (run and re.match(r"^\s*\{.*\},?\s*(//.*)?$", l)):
                if not run:
                    start = i
                run += 1
            else:
                if run >= 100:
                    findings.append({"kind": "review", "file": rel, "line": start + 1, "rows": run, "values": 0,
                                     "what": "struct table (spec-derived values; review)"})
                run = 0
    # 1c: long Markdown tables in specs (data tables measured from the original): listed for review
    if ext == ".md" and rel.startswith("docs/"):
        run = 0
        for i, l in enumerate(lines + [""]):
            if l.startswith("|") and not l.startswith("|-") and not l.startswith("| -"):
                if not run:
                    start = i
                run += 1
            else:
                if run >= 150:
                    findings.append({"kind": "review", "file": rel, "line": start + 1, "rows": run, "values": 0,
                                     "what": "long data table in a spec (review)"})
                run = 0
    # 3: markers
    if not matches(rel, MARKER_OK) and not third:
        for i, l in enumerate(lines):
            for rx, what in FORBIDDEN:
                m = rx.search(l)
                if m:
                    if what.startswith("possible address") and not rel.startswith("docs/specs/"):
                        continue   # numbers in code are reviewed by the table check
                    findings.append({"kind": "marker", "file": rel, "line": i + 1, "what": what,
                                     "text": l.strip()[:120]})


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("root", nargs="?", default=str(Path(__file__).resolve().parents[1]))
    ap.add_argument("--export", action="store_true", help="the tree is a public export: excluded paths are errors")
    ap.add_argument("--min-array", type=int, default=256, help="numbers in one constant run that count as a table")
    ap.add_argument("--json")
    ap.add_argument("-v", "--verbose", action="store_true", help="also list third-party tables")
    a = ap.parse_args()
    root = Path(a.root).resolve()
    files = tracked(root)
    findings = []
    excl = []
    pe = root / ".publish-exclude"
    if pe.exists():
        excl = [l.strip() for l in pe.read_text().split("\n") if l.strip() and not l.startswith("#")]
    for rel in files:
        if matches(rel, excl) or any(rel.startswith(e.rstrip("*").rstrip("/") + "/") for e in excl):
            if a.export:
                findings.append({"kind": "finding", "file": rel, "what": "path excluded from publication"})
            continue
        scan_file(root, rel, a.min_array, findings)
    bad = [f for f in findings if f["kind"] not in ("third-party", "review")]
    groups = {}
    for f in findings:
        groups.setdefault((f["kind"], f["file"], f["what"]), []).append(f)
    for (kind, file, what), fs in sorted(groups.items()):
        if kind == "third-party" and not a.verbose:
            continue
        rows = sum(f.get("rows", 0) for f in fs)
        vals = sum(f.get("values", 0) for f in fs)
        extra = " (%d rows, %d values)" % (rows, vals) if rows else ""
        print("%-11s %s:%s %s x%d%s %s" % (kind, file, fs[0].get("line", ""), what, len(fs), extra,
                                         fs[0].get("text", "")))
    tp = sorted({f["file"] for f in findings if f["kind"] == "third-party"})
    print("third-party files with tables (THIRD_PARTY.md, not failed): %d" % len(tp))
    print("%d files scanned; %d findings in %d groups (excluded from scan: %s)" %
          (len(files), len(bad), len([g for g in groups if g[0] not in ("third-party", "review")]), ", ".join(excl) or "none"))
    if a.json:
        Path(a.json).write_text(json.dumps(findings, indent=1), encoding="utf-8")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())