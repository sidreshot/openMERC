#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Implements: SPEC-121
"""Parsers for the small text files that accompany Ghoul models
(.gsq sequence/part lists, .gpm player models, .gbm bolt-ons, .gpl player
lists, .ifl image lists).  Derived from the data files only; see
docs/specs/SPEC-121-ghoul-text-formats.md.

usage:
  ghoul_text.py validate <ghoul-dir>       parse every text file below the dir
  ghoul_text.py show <file>                print the parsed structure
"""
import json
import os
import re
import sys

_TOKEN = re.compile(r'"[^"]*"|\{|\}|[^\s{}"]+')


def _read(path):
    return open(path, 'rb').read().decode('latin-1')


def tokens(text):
    """Whitespace tokenizer: "quoted strings" (quotes removed later), { and } as
    their own tokens, everything else split on whitespace.  // starts a comment
    that runs to the end of the line."""
    text = re.sub(r'//[^\n]*', '', text)
    return _TOKEN.findall(text)


def _unq(t):
    return t[1:-1] if len(t) >= 2 and t[0] == '"' and t[-1] == '"' else t


def _value(tok, it):
    """A header value: a word, a quoted string, or an empty block '{}' (= empty)."""
    if tok == '{':
        inner = []
        for t in it:
            if t == '}':
                break
            inner.append(t)
        return ' '.join(_unq(x) for x in inner)
    return _unq(tok)


def _block(it):
    """Read tokens up to the matching '}' (the '{' is already consumed)."""
    out = []
    for t in it:
        if t == '}':
            return out
        out.append(_unq(t))
    raise ValueError('unterminated block')


def _groups(items, n, what):
    if len(items) % n:
        raise ValueError('%s block: %d tokens is not a multiple of %d' % (what, len(items), n))
    return [items[i:i + n] for i in range(0, len(items), n)]


# ---------------------------------------------------------------------- .gpm
def parse_gpm(text):
    it = iter(tokens(text))
    head = [_value(next(it), it) for _ in range(7)]
    d = dict(zip(('dir', 'menu_model', 'model', 'set', 'team', 'sound_dir', 'face_prefix'), head))
    rest = list(it)
    blocks = []
    trailer = []
    i = 0
    while i < len(rest):
        if rest[i] == '{':
            j = rest.index('}', i)
            blocks.append([_unq(x) for x in rest[i + 1:j]])
            i = j + 1
        else:
            trailer.append(_unq(rest[i]))
            i += 1
    if len(blocks) != 3:
        raise ValueError('expected 3 blocks, got %d' % len(blocks))
    d['skins'] = [{'material': a, 'skin': b, 'u_int': int(c)} for a, b, c in _groups(blocks[0], 3, 'skin')]
    d['boltons'] = [{'gbm': a, 'bolt': b, 'to_bolt': c, 'scale': float(s)}
                    for a, b, c, s in _groups(blocks[1], 4, 'bolt-on')]
    d['parts'] = [{'part': a, 'on': int(b)} for a, b in _groups(blocks[2], 2, 'part')]
    d['trailer'] = trailer
    return d


# ---------------------------------------------------------------------- .gbm
def parse_gbm(text):
    it = iter(tokens(text))
    head = [_value(next(it), it) for _ in range(7)]
    d = dict(zip(('dir', 'menu_model', 'model', 'set', 'u_5', 'u_6', 'u_7'), head))
    rest = []
    for t in it:
        if t == '{':
            rest.extend(_block(it))
        else:
            rest.append(_unq(t))
    d['skins'] = [{'material': a, 'skin': b, 'u_int': int(c)} for a, b, c in _groups(rest, 3, 'skin')]
    return d


# ---------------------------------------------------------------------- .gpl
def parse_gpl(text):
    """Blank-line separated groups of player model names."""
    groups, cur = [], []
    for line in text.replace('\r', '').split('\n'):
        s = line.strip()
        if s:
            cur.append(s)
        elif cur:
            groups.append(cur)
            cur = []
    if cur:
        groups.append(cur)
    return groups


# ---------------------------------------------------------------------- .ifl / .gsq
def parse_lines(text):
    return [s.strip() for s in text.replace('\r', '').split('\n') if s.strip()]


def resolve_gsq(path, search_dirs=(), _seen=None):
    """Expand a .gsq list; '#name' includes name.gsq from the same directory or
    from search_dirs (the stock data uses ghoul/comskin)."""
    _seen = _seen or set()
    key = os.path.normcase(os.path.abspath(path))
    if key in _seen:
        return []
    _seen.add(key)
    out = []
    for item in parse_lines(_read(path)):
        if item.startswith('#'):
            inc = _find(item[1:].strip() + '.gsq', [os.path.dirname(path)] + list(search_dirs))
            if inc is None:
                raise FileNotFoundError(item)
            out.extend(resolve_gsq(inc, search_dirs, _seen))
        else:
            out.append(item)
    return out


def _find(name, dirs):
    for d in dirs:
        try:
            for f in os.listdir(d):
                if f.lower() == name.lower():
                    return os.path.join(d, f)
        except OSError:
            pass
    return None


PARSERS = {'.gpm': parse_gpm, '.gbm': parse_gbm, '.gpl': parse_gpl, '.ifl': parse_lines, '.gsq': parse_lines}


def main(argv):
    if len(argv) < 2:
        print(__doc__)
        return 2
    if argv[0] == 'show':
        p = argv[1]
        ext = os.path.splitext(p)[1].lower()
        print(json.dumps(PARSERS[ext](_read(p)), indent=1))
        return 0
    if argv[0] == 'validate':
        root = argv[1]
        comskin = []
        for d, _, fs in os.walk(root):
            if os.path.basename(d).lower() == 'comskin':
                comskin.append(d)
        counts, bad = {}, 0
        for d, _, fs in os.walk(root):
            for n in fs:
                ext = os.path.splitext(n)[1].lower()
                if ext not in PARSERS:
                    continue
                p = os.path.join(d, n)
                try:
                    PARSERS[ext](_read(p))
                    if ext == '.gsq':
                        resolve_gsq(p, comskin)
                    counts[ext] = counts.get(ext, 0) + 1
                except Exception as e:
                    bad += 1
                    print('FAIL %s: %s' % (p, e))
        print('parsed %s, failed %d' % (counts, bad))
        return 1 if bad else 0
    print(__doc__)
    return 2


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
