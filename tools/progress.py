#!/usr/bin/env python3
"""Count reviewed ranges, never count opaque incbins as understood code."""
import json
from pathlib import Path


def measure(record):
    kinds = ("code", "data")
    totals = {kind: 0 for kind in kinds}
    occupied = set()
    symbols = set()
    for entry in record["ranges"]:
        start, end = entry["start"], entry["end"]
        if not 0 <= start < end <= record["prg_bytes"]:
            raise ValueError("Invalid PRG-relative coverage range")
        if not entry.get("evidence") or not entry.get("symbol"):
            raise ValueError("Coverage needs a symbol and evidence")
        if entry["kind"] not in kinds:
            raise ValueError("Coverage kind must be code or data")
        addresses = set(range(start, end))
        if occupied & addresses:
            raise ValueError("Overlapping coverage would inflate progress")
        occupied.update(addresses)
        totals[entry["kind"]] += end - start
        symbols.add(entry["symbol"])
    return {"understood_bytes": len(occupied), "prg_bytes": record["prg_bytes"],
            "percent": round(100 * len(occupied) / record["prg_bytes"], 4),
            "documented_symbols": len(symbols), **totals}


if __name__ == "__main__":
    root = Path(__file__).resolve().parents[1]
    print(json.dumps(measure(json.loads((root / "config/coverage.json").read_text())), indent=2))
