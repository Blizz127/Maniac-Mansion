#!/usr/bin/env python3
"""Check exact Git index blobs against every 32-byte window of the local PRG.

Before commit: --manifest local/staged-guard.json
Before push: --recheck local/staged-guard.json (checks the same indexed blobs).
No ROM bytes or file contents are printed or written into the manifest.
"""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess

from rom import parse

BAD_EXT = {'.nes', '.fds', '.unf', '.prg', '.chr', '.bin', '.sav', '.srm',
           '.raw', '.ram', '.zip', '.7z', '.mmo', '.mst', '.mmst', '.fcs',
           '.cdl', '.png', '.bmp', '.ppm', '.wav'}
LOCAL_DIRS = {'rom', 'roms', 'build', 'local', '.tools'}


def git(*args):
    return subprocess.check_output(['git', *args])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--rom', default='rom/original.nes')
    group = parser.add_mutually_exclusive_group()
    group.add_argument('--manifest', type=Path)
    group.add_argument('--recheck', type=Path)
    args = parser.parse_args()
    _, prg = parse(Path(args.rom).read_bytes())
    windows = {prg[i:i + 32] for i in range(len(prg) - 31)}
    previous = json.loads(args.recheck.read_text()) if args.recheck else None
    if previous:
        if previous['prg_sha256'] != hashlib.sha256(prg).hexdigest():
            raise ValueError('Guard ROM identity changed')
        paths = list(previous['files'])
    else:
        paths = [p.decode() for p in git('diff', '--cached', '--name-only',
                 '--diff-filter=ACMR', '-z').split(b'\0') if p]
    if not paths:
        raise ValueError('No staged files to check')
    files, rejected = {}, []
    for name in paths:
        path = Path(name)
        if path.suffix.lower() in BAD_EXT or path.parts[0] in LOCAL_DIRS:
            rejected.append(f'{name}: local game-data path/type')
            continue
        if path.parts[0] == 'port':
            rejected.append(f'{name}: port changes are outside public decomp scope')
            continue
        data = git('show', ':' + name)
        digest = hashlib.sha256(data).hexdigest()
        files[name] = digest
        if previous and previous['files'][name] != digest:
            rejected.append(f'{name}: index changed since pre-commit guard')
        if data.startswith(b'NES\x1a'):
            rejected.append(f'{name}: ROM header')
        elif any(data[i:i + 32] in windows for i in range(max(0, len(data) - 31))):
            rejected.append(f'{name}: contains a 32-byte PRG window')
    for problem in rejected:
        print('guard-staged: ' + problem)
    print(f'guard-staged: {len(paths)} exact index blobs checked; '
          f'{len(rejected)} rejected (all PRG windows, no padding exclusions)')
    if rejected:
        return 1
    if args.manifest:
        args.manifest.write_text(json.dumps({
            'prg_sha256': hashlib.sha256(prg).hexdigest(), 'files': files
        }, indent=2) + '\n')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
