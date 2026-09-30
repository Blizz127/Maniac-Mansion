#!/usr/bin/env python3
"""Local-only iNES identification, bank extraction and exact verification."""
import argparse
import hashlib
import json
from pathlib import Path
import sys
import zlib

ROOT = Path(__file__).resolve().parents[1]


def hashes(data):
    return {"size": len(data), "crc32": f"{zlib.crc32(data):08x}",
            "sha1": hashlib.sha1(data).hexdigest(),
            "sha256": hashlib.sha256(data).hexdigest()}


def parse(data):
    if len(data) < 16 or data[:4] != b"NES\x1a":
        raise ValueError("Expected an iNES ROM with a 16-byte header")
    h = data[:16]
    nes2 = h[7] & 12 == 8
    if h[7] & 3:
        raise ValueError("VS/PlayChoice ROMs are not supported")
    if h[6] & 4:
        raise ValueError("Trainer present; do not strip or normalize the owner ROM")
    if nes2 and ((h[9] & 15) == 15 or (h[9] >> 4) == 15):
        raise ValueError("Exponent/multiplier ROM sizes are outside the retail profile")
    prg_units = h[4] | ((h[9] & 15) << 8 if nes2 else 0)
    chr_units = h[5] | ((h[9] >> 4) << 8 if nes2 else 0)
    prg_size, chr_size = prg_units * 16384, chr_units * 8192
    if len(data) != 16 + prg_size + chr_size:
        raise ValueError("ROM length disagrees with header (truncated or trailing data)")
    mapper = (h[6] >> 4) | (h[7] & 0xf0)
    if nes2:
        mapper |= (h[8] & 15) << 8
    if mapper != 1 or prg_size != 262144 or chr_size != 0:
        raise ValueError("Expected western retail MMC1: 256 KiB PRG and CHR RAM")
    return {"format": "NES 2.0" if nes2 else "iNES", "mapper": mapper,
            "submapper": h[8] >> 4 if nes2 else None,
            "prg_size": prg_size, "chr_size": chr_size,
            "battery": bool(h[6] & 2), "header_hex": h.hex()}, data[16:]


def identify(path):
    data = path.read_bytes()
    header, prg = parse(data)
    full, payload = hashes(data), hashes(prg)
    catalog = json.loads((ROOT / "config/known-dumps.json").read_text())
    matches = [d for d in catalog["dumps"] if d["payload"]["sha1"] == payload["sha1"]
               and d["payload"]["crc32"] == payload["crc32"]]
    authoritative = bool(matches and matches[0].get("authoritative") and
                         matches[0]["file"]["sha256"] == full["sha256"] and
                         matches[0]["payload"]["sha256"] == payload["sha256"])
    return {"path": str(path.resolve()), "header": header, "file": full,
            "payload": payload, "region": matches[0]["region"] if matches else None,
            "reference": catalog["source"], "payload_matches_reference": bool(matches),
            "full_file_matches_reference": bool(matches and
                matches[0]["file"]["sha1"] == full["sha1"]),
            "authoritative_record": matches[0].get("authoritative") if matches else None,
            "clean_status": "clean retail payload and catalogued NES 2.0 header; No-Intro verified record match"
                if authoritative else "metadata-mirror match; authoritative confirmation pending"
                if matches else "unknown: not a known retail payload"}


def pinned(manifest):
    record = json.loads(manifest.read_text())
    data = Path(record["path"]).read_bytes()
    parse(data)
    if hashes(data) != record["file"]:
        raise ValueError("Owner ROM differs from pinned identity; refusing to rebaseline")
    if not record["payload_matches_reference"]:
        raise ValueError("Unknown payload; identification required before retail build")
    return record, data


def extract(manifest, output):
    record, data = pinned(manifest)
    output.mkdir(parents=True, exist_ok=True)
    (output / "header.bin").write_bytes(data[:16])
    for bank in range(16):
        start = 16 + bank * 16384
        (output / f"prg{bank:02x}.bin").write_bytes(data[start:start + 16384])
    # This profile has CHR RAM: graphics remain inside PRG, no CHR-ROM to extract.
    vectors = data[-6:]
    (output / "vectors.json").write_text(json.dumps({
        name: {"cpu_address": int.from_bytes(vectors[i:i+2], "little"),
               "mapping": "candidate bank 0f fixed at c000; validate mapper mode"}
        for i, name in zip((0, 2, 4), ("nmi", "reset", "irq"))}, indent=2) + "\n")
    return record


def verify(manifest, output):
    record, source = pinned(manifest)
    rebuilt = output.read_bytes()
    if rebuilt != source:
        first = next((i for i, (a, b) in enumerate(zip(source, rebuilt)) if a != b),
                     min(len(source), len(rebuilt)))
        raise ValueError(f"Mismatch at file offset 0x{first:x}; lengths {len(source)}/{len(rebuilt)}")
    if hashes(rebuilt) != record["file"]:
        raise ValueError("Rebuilt hash differs from pinned identity")
    return {"status": "byte-for-byte match", **hashes(rebuilt)}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest="command", required=True)
    p = sub.add_parser("identify")
    p.add_argument("rom", type=Path)
    p.add_argument("--manifest", type=Path)
    p = sub.add_parser("extract")
    p.add_argument("manifest", type=Path)
    p.add_argument("output", type=Path)
    p = sub.add_parser("verify")
    p.add_argument("manifest", type=Path)
    p.add_argument("output", type=Path)
    args = parser.parse_args()
    try:
        if args.command == "identify":
            result = identify(args.rom)
            if args.manifest:
                if not result["payload_matches_reference"]:
                    raise ValueError("Unknown retail payload; manifest was not written")
                if args.manifest.exists():
                    existing = json.loads(args.manifest.read_text())
                    if existing["file"] != result["file"]:
                        raise ValueError("Manifest already pins another ROM; preserve it")
                args.manifest.parent.mkdir(parents=True, exist_ok=True)
                args.manifest.write_text(json.dumps(result, indent=2) + "\n")
        elif args.command == "extract":
            record = extract(args.manifest, args.output)
            result = {"status": "pinned ROM extracted", "prg_banks": 16,
                      "sha1": record["file"]["sha1"]}
        else:
            result = verify(args.manifest, args.output)
        print(json.dumps(result, indent=2))
    except (OSError, ValueError, KeyError) as error:
        print(f"{args.command}: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
