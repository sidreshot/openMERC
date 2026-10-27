#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Gatekeeper lint for spec drafts (see CLEANROOM.md, "What may cross from R to C").

Flags text that suggests decompiler output, code, internal names or binary addresses leaked into a
spec. A clean result does not approve a spec; flagged lines must be reviewed and fixed by the author.

usage: spec_gate.py <spec.md>...    exit status 1 if anything was flagged
"""
import re
import sys

RULES = [
    ("decompiler auto-name", re.compile(
        r"\b(FUN|DAT|LAB|PTR|SUB|UNK|thunk_FUN|switchD|caseD)_[0-9a-fA-F]{4,}\b"
        r"|\b(param|local|unaff|extraout)_[A-Za-z0-9]+\b|\bin_[A-Z]{2,}\b"
        r"|\b[iuscbpf]{1,3}Var\d+\b|\bpu[a-z]*Var\d*\b|\bundefined[1248]\b"
        r"|\b(CONCAT|SUB|ZEXT|SEXT)\d{2}\b")),
    ("image address", re.compile(r"\b0x(?:[1-7][0-9a-fA-F]{7})\b")),
    ("code block", re.compile(r"^\s*```")),
    ("C statement", re.compile(
        r"^\s*(if|while|for|switch|return|do)\s*\(.*[;{]\s*$|^\s*[A-Za-z_][\w\.\->\[\]]*\s*[+\-*/|&]?=\s*[^=].*;\s*$|^\s*\w+\s*\(.*\)\s*;\s*$")),
    ("pointer arrow", re.compile(r"[A-Za-z_]\w*->\w")),
    ("goto/label", re.compile(r"\bgoto\b|^\s*\w+:\s*$")),
]


def lint(path):
    hits = []
    in_table = False
    with open(path, encoding="utf-8", errors="replace") as f:
        for n, line in enumerate(f, 1):
            for name, rx in RULES:
                if rx.search(line):
                    hits.append((n, name, line.rstrip()[:160]))
    return hits


def main(argv):
    bad = 0
    for p in argv:
        hits = lint(p)
        print(f"== {p}: {len(hits)} flagged")
        for n, name, text in hits:
            print(f"  {n:5} [{name}] {text}")
        bad += len(hits)
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
