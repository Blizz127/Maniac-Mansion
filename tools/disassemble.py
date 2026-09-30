#!/usr/bin/env python3
"""Disassemble reviewed code ranges locally; retain opaque gaps byte-for-byte."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess
from progress import measure

ROOT = Path(__file__).resolve().parents[1]


def generate(da65):
    coverage = json.loads((ROOT / "config/coverage.json").read_text())
    measure(coverage)  # Reject inflated/invalid coverage before touching outputs.
    prg = b''.join((ROOT / f'build/extracted/prg{i:02x}.bin').read_bytes() for i in range(16))
    if hashlib.sha1(prg).hexdigest() != coverage['payload_sha1']:
        raise ValueError('Reviewed code ranges target the USA payload only; no cross-region reuse')
    output = ROOT / "build/disasm"
    output.mkdir(parents=True, exist_ok=True)
    for bank in range(16):
        ranges = sorted((r for r in coverage["ranges"]
                         if r["start"] // 16384 == bank), key=lambda r: r["start"])
        bankpath = ROOT / f"build/extracted/prg{bank:02x}.bin"
        data = bankpath.read_bytes()
        base = 0xc000 if bank == 15 else 0x8000
        cursor = 0
        lines = [f'; Generated locally from pinned owner ROM; bank {bank:02x}.']
        for entry in ranges:
            start = entry["start"] % 16384
            end = entry["end"] - bank * 16384
            if end > 16384:
                raise ValueError("Code range crosses a physical bank")
            if start > cursor:
                lines.append(f'.incbin "build/extracted/prg{bank:02x}.bin", {cursor}, {start-cursor}')
            symbol = entry["symbol"]
            lines += [f'.export {symbol}', f'{symbol}:',
                      f'.assert * = ${base+start:04x}, error, "range start drift"']
            if entry['kind'] == 'code':
                slice_file = output / f'{symbol}.bin'
                slice_file.write_bytes(data[start:end])
                result = subprocess.run([da65, '-S', str(base + start), '--comments', '4',
                                         str(slice_file)], capture_output=True, text=True, check=True)
                # Omit timestamp/path banner, keep generated instructions local.
                assembly = result.stdout[result.stdout.index('        .setcpu'):]
                externals = dict((name, int(address, 16)) for name, address in
                                 re.findall(r'(L[0-9A-F]+)\s*:= \$([0-9A-F]+)', assembly))
                # ca65 cannot range-check absolute branch targets in a relocatable
                # segment. Express out-of-slice targets relative to this instruction.
                def branch(match):
                    prefix, target, suffix, address = match.groups()
                    if target not in externals:
                        return match.group(0)
                    delta = externals[target] - int(address, 16)
                    return f'{prefix}* {delta:+d}{suffix}{address}'
                assembly = re.sub(r'(\b(?:bcc|bcs|beq|bmi|bne|bpl|bvc|bvs)\s+)'
                                  r'(L[0-9A-F]+)(\s*; )([0-9A-F]{4})', branch, assembly)
                lines += [f'.scope scope_{symbol}', assembly, '.endscope']
            else:
                lines += [f'; Reviewed data: {entry["evidence"]}',
                          f'.incbin "build/extracted/prg{bank:02x}.bin", {start}, {end-start}']
            lines += [f'.assert * = ${base+end:04x}, error, "range length drift"']
            cursor = end
        if cursor < 16384:
            lines.append(f'.incbin "build/extracted/prg{bank:02x}.bin", {cursor}, {16384-cursor}')
        (output / f'prg{bank:02x}.inc').write_text('\n'.join(lines) + '\n')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--da65', default='da65')
    generate(parser.parse_args().da65)
