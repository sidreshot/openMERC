# SPDX-License-Identifier: GPL-2.0-or-later
# Implements: SPEC-003, SPEC-601 (acceptance comparison: the differences left after removing the explained ones)
#
# Reads a `trace_view.py diff --words` output and drops the differences explained in docs/impl/I7.md:
#  - BTN lines that differ only in the one-shot word (the server never sets it; the original passes stack garbage);
#  - block words the original leaves uninitialised or fills with garbage: 0x0F8 (tag padding), 0x190 (owner-context
#    pointer before the first think), 0x270 (idle-variation counter of a new client inventory).
# Prints the remaining differing lines and a count. usage: diff_summary.py <file.diff>
import re
import sys

EXPLAINED_WORDS = {'0f8', '190', '270'}


def main():
    lines = open(sys.argv[1], encoding='latin-1').read().splitlines()
    out = []
    i = 0
    while i < len(lines):
        l = lines[i]
        if l.startswith(('---', '+++', '@@', '#')) or not l[:1] in '-+~':
            i += 1
            continue
        if l.startswith('~'):
            words = re.findall(r' ([0-9a-f]{3}):', l.split('(orig/om)', 1)[1])
            if words and set(words) <= EXPLAINED_WORDS and 'seq:' not in l:
                i += 1
                continue
        if l.startswith('-') and ' BTN ' in l and i + 1 < len(lines) and lines[i + 1].startswith('+'):
            a = re.sub(r' one=\S+', '', l[1:])
            b = re.sub(r' one=\S+', '', lines[i + 1][1:])
            if a == b:
                i += 2
                continue
        if ' BTN ' in l:
            # unpaired BTN lines (diff grouped several of them): compare without the one-shot word
            out.append(re.sub(r' one=\S+', '', l))
        else:
            out.append(l)
        i += 1
    # unpaired BTN removals/additions that cancel out
    rem = [x[1:] for x in out if x.startswith('-') and ' BTN ' in x]
    add = [x[1:] for x in out if x.startswith('+') and ' BTN ' in x]
    cancel = set(rem) & set(add)
    out = [x for x in out if not (' BTN ' in x and x[1:] in cancel)]
    for x in out:
        print(x)
    print('# %d unexplained difference lines' % len(out))


if __name__ == '__main__':
    main()
