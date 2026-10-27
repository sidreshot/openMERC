#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Gatekeeper: copy reviewed R drafts into docs/specs with an approved status line and APPROVALS rows.
Run only after spec_gate.py is clean and a manual review found no code, addresses or internal names.
usage: approve_spec.py <drafts-dir> <date> <draft-name>...   (names without .md; the drafts directory is the
       private R workspace, outside this repository)
"""
import sys
from pathlib import Path

drafts = Path(sys.argv[1])
specs = Path(__file__).resolve().parent.parent / "docs" / "specs"
date = sys.argv[2]
status = ("Status: approved (G, %s) — produced by the reverse team, reviewed against CLEANROOM.md rules; "
          "in-game test vectors not yet verified" % date)
rows = []
for name in sys.argv[3:]:
    text = (drafts / (name + ".md")).read_text(encoding="utf-8")
    lines = text.splitlines()
    for i, l in enumerate(lines[:6]):
        if l.startswith("Status:"):
            lines[i] = status
            break
    else:
        raise SystemExit("no Status line in " + name)
    (specs / (name + ".md")).write_text("\n".join(lines) + "\n", encoding="utf-8")
    num, title = name.split("-", 2)[1], name.split("-", 2)[2].replace("-", " ")
    rows.append("| SPEC-%s %s | R team (spec draft) | lint clean; manual sample review | %s |" % (num, title, date))
    print("approved", name)
ap = specs / "APPROVALS.md"
ap.write_text(ap.read_text(encoding="utf-8").rstrip("\n") + "\n" + "\n".join(rows) + "\n", encoding="utf-8")
