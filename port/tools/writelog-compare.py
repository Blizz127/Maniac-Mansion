#!/usr/bin/env python3
"""Compare per-frame register-write logs: the port (mm-headless --hashes -
--watch 4000-4017) against a reference (ref-mesen.lua MM_WRITELOG).

  writelog-compare.py PORT.txt REF.txt

Lines are "<frame> <hash> watch: ..." (port) or "<frame> watch: ..." (ref).
Only writes (Wxxxx=yy) are compared, in order, frame by frame.
"""
import re
import sys

W = re.compile(r"W([0-9A-F]{4})=([0-9A-F]{2})")


def load(path):
    out = {}
    for line in open(path):
        if line.startswith("#") or "watch:" not in line:
            continue
        frame = int(line.split()[0])
        out[frame] = W.findall(line.split("watch:", 1)[1])
    return out


def main():
    a, b = load(sys.argv[1]), load(sys.argv[2])
    frames = sorted(set(a) & set(b))
    writes = sum(len(a[f]) for f in frames)
    bad = [f for f in frames if a[f] != b[f]]
    print(f"compared {len(frames)} frames, {writes} register writes: {len(frames) - len(bad)} frames identical, {len(bad)} differ")
    if bad:
        f = bad[0]
        print(f"first difference at frame {f}:\n  port {a[f][:12]}\n  ref  {b[f][:12]}")
    return 1 if bad or not frames else 0


if __name__ == "__main__":
    sys.exit(main())
