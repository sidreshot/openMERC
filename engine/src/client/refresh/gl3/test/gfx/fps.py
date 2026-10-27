# SPDX-License-Identifier: GPL-2.0-or-later
# Reads the "OMFPS <name>" ... "OMFPS_END" sections of a run_gfx.ps1 log (cl_showfps 1 prints the rate of
# every 10 frames) and prints name, mean fps (frames / time), the slowest 10-frame window and samples.
# usage: python fps.py <log> [<log>...]
# Implements: GFX-modern.md measurement (test harness, no game code)
import re
import sys


def main():
    for path in sys.argv[1:]:
        name, vals, gpu = None, [], []
        for line in open(path, encoding="latin-1"):
            line = line.strip()
            if line.startswith("OMFPS_END"):
                if name and vals:
                    v = vals[1:] if len(vals) > 2 else vals  # the first window spans the settle step
                    mean = len(v) / sum(1.0 / x for x in v)
                    g = sum(gpu) / len(gpu) if gpu else 0.0
                    print("%-36s %8.1f fps  min %8.1f  gpu %6.3f ms  (%d windows)" % (name, mean, min(v), g, len(v)))
                name, vals, gpu = None, [], []
            elif line.startswith("OMFPS "):
                name, vals, gpu = line.split()[1], [], []
            elif name and line.startswith("OMGPU "):
                gpu.append(float(line.split()[1]))
            elif name:
                m = re.match(r"Frames Per Second = ([0-9.]+)", line)
                if m and float(m.group(1)) > 0:
                    vals.append(float(m.group(1)))


if __name__ == "__main__":
    main()