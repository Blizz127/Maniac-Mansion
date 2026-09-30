#!/usr/bin/env python3
"""Hash raw reference frame dumps and compare frame-hash files.

  framehash.py raw DUMP.raw [--bpp 1|2] [--index-only] > hashes.txt
  framehash.py compare A.txt B.txt [--offset N]

mmfb1: 64-bit FNV-1a over the 256x240 pixels, one step per pixel, pixel =
palette index | emphasis << 6. With --index-only (mmfb1i) emphasis is
dropped, for references that do not report it (FCEUX).
"""
import argparse
import sys

W, H = 256, 240
MASK = (1 << 64) - 1


def fnv(pixels):
    h = 0xCBF29CE484222325
    for p in pixels:
        h = ((h ^ p) * 0x100000001B3) & MASK
    return h


def cmd_raw(a):
    n = W * H * a.bpp
    with open(a.dump, "rb") as f:
        i = 0
        print(f"# mmfb1{'i' if a.index_only else ''} raw={a.dump}")
        while True:
            b = f.read(n)
            if len(b) < n:
                break
            if a.bpp == 2:
                px = [b[k] | b[k + 1] << 8 for k in range(0, n, 2)]
            else:
                px = list(b)
            if a.index_only:
                px = [p & 0x3F for p in px]
            print(i, f"{fnv(px):016x}")
            i += 1


def load(path):
    out = {}
    for line in open(path):
        if line.startswith("#") or not line.strip():
            continue
        f, h = line.split()[:2]
        out[int(f)] = h
    return out


def cmd_compare(a):
    x, y = load(a.a), load(a.b)
    frames = sorted(f for f in x if f + a.offset in y)
    bad = [f for f in frames if x[f] != y[f + a.offset]]
    print(f"compared {len(frames)} frames (offset {a.offset}): {len(frames) - len(bad)} match, {len(bad)} differ")
    if bad:
        print(f"first difference at frame {bad[0]}: {x[bad[0]]} vs {y[bad[0] + a.offset]}")
    return 1 if bad or not frames else 0


def main():
    p = argparse.ArgumentParser()
    s = p.add_subparsers(dest="cmd", required=True)
    r = s.add_parser("raw")
    r.add_argument("dump")
    r.add_argument("--bpp", type=int, default=1)
    r.add_argument("--index-only", action="store_true")
    c = s.add_parser("compare")
    c.add_argument("a")
    c.add_argument("b")
    c.add_argument("--offset", type=int, default=0)
    a = p.parse_args()
    sys.exit(cmd_raw(a) if a.cmd == "raw" else cmd_compare(a))


if __name__ == "__main__":
    main()
