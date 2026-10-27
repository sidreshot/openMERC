# SPDX-License-Identifier: GPL-2.0-or-later
# Implements: SPEC-410 test harness (comparison restricted to the moving pixels of a view)
#
# usage: movecmp.py [-t threshold] <dir> <origprefix> <omprefix> <name> [<name>...]
# For a view captured at two paused game moments (<prefix>_<name>_0 and _1), the mask is the set of pixels
# that change between the two original captures by more than the threshold (default 8) in any channel.
# Prints, inside the mask: the original's own change between the moments (the size of one time step), the
# difference OpenMerc - original at moment 0 and at moment 1, and OpenMerc at 0 against the original at 1
# (must be larger than the same-moment differences if the comparison sees the motion).
import os
import sys

import numpy as np
from PIL import Image


def load(d, p, n, k):
    return np.asarray(Image.open(os.path.join(d, '%s_%s_%d.png' % (p, n, k))).convert('RGB')).astype(np.int32)


def masked_mean(a, b, mask):
    if not mask.any():
        return 0.0
    return float(np.abs(a - b)[mask].mean())


def main():
    args = sys.argv[1:]
    thr = 8
    if args[0] == '-t':
        thr = int(args[1])
        args = args[2:]
    d, po, pm = args[0:3]
    for n in args[3:]:
        g0, g1 = load(d, po, n, 0), load(d, po, n, 1)
        m0, m1 = load(d, pm, n, 0), load(d, pm, n, 1)
        mask = np.abs(g0 - g1).max(axis=2) > thr
        print('%-14s mask %5.1f%%  step %5.1f  om0-orig0 %5.1f  om1-orig1 %5.1f  om0-orig1 %5.1f' % (
            n, 100.0 * mask.mean(), masked_mean(g0, g1, mask), masked_mean(m0, g0, mask),
            masked_mean(m1, g1, mask), masked_mean(m0, g1, mask)))


if __name__ == '__main__':
    main()
