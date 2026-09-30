import json
from pathlib import Path
import sys
import tempfile
import unittest
import hashlib
import shutil
import subprocess
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from rom import hashes, parse, extract, verify
from progress import measure
import disassemble


def fixture(nes2=True):
    header = bytearray(b'NES\x1a' + bytes(12))
    header[4] = 16
    header[6] = 0x12
    header[7] = 8 if nes2 else 0
    return bytes(header) + bytes(range(256)) * 1024


class ToolsTest(unittest.TestCase):
    def test_generated_code_data_and_external_branch_round_trip(self):
        root = Path(__file__).resolve().parents[1]
        binaries = {name: shutil.which(name) or str(root / f'.tools/cc65/usr/bin/{name}')
                    for name in ('ca65', 'ld65', 'da65')}
        if not all(Path(p).is_file() for p in binaries.values()):
            self.skipTest('cc65 tools not installed')
        with tempfile.TemporaryDirectory() as directory:
            workspace = Path(directory)
            extracted = workspace / 'build/extracted'
            extracted.mkdir(parents=True)
            (workspace / 'config').mkdir()
            banks = [bytearray(b'\xea' * 16384) for _ in range(16)]
            banks[0][15:18] = b'\x60\xd0\xfd'  # RTS; BNE to preceding opaque byte
            for i, bank in enumerate(banks):
                (extracted / f'prg{i:02x}.bin').write_bytes(bank)
            coverage = {'prg_bytes': 262144,
                        'payload_sha1': hashlib.sha1(b''.join(banks)).hexdigest(),
                        'ranges': [{'symbol': 'branch_fixture', 'start': 16, 'end': 18,
                                    'kind': 'code', 'evidence': 'synthetic fixture'},
                                   {'symbol': 'table_fixture', 'start': 30, 'end': 32,
                                    'kind': 'data', 'evidence': 'synthetic fixture'}]}
            (workspace / 'config/coverage.json').write_text(json.dumps(coverage))
            with patch.object(disassemble, 'ROOT', workspace):
                disassemble.generate(binaries['da65'])
            objects = []
            for i in range(16):
                source = workspace / f'build/prg{i:02x}.s'
                source.write_text(f'.segment "PRG{i:02X}"\n.include "build/disasm/prg{i:02x}.inc"\n')
                obj = workspace / f'build/prg{i:02x}.o'
                subprocess.run([binaries['ca65'], '-I', '.', '-o', str(obj), str(source)],
                               cwd=workspace, check=True, capture_output=True)
                objects.append(str(obj))
            (extracted / 'header.bin').write_bytes(fixture()[:16])
            header = workspace / 'build/header.o'
            subprocess.run([binaries['ca65'], '-o', str(header), str(root / 'src/header.s')],
                           cwd=workspace, check=True, capture_output=True)
            output = workspace / 'build/fixture.nes'
            subprocess.run([binaries['ld65'], '-C', str(root / 'config/mmc1.cfg'), '-o',
                            str(output), str(header), *objects], cwd=workspace,
                           check=True, capture_output=True)
            self.assertEqual(output.read_bytes(), fixture()[:16] + b''.join(banks))

    def test_ines_and_nes2_preserve_header(self):
        for nes2 in (False, True):
            data = fixture(nes2)
            header, payload = parse(data)
            self.assertEqual(header['format'], 'NES 2.0' if nes2 else 'iNES')
            self.assertEqual(header['header_hex'], data[:16].hex())
            self.assertEqual(len(payload), 262144)
            self.assertEqual(header['mapper'], 1)

    def test_reject_bad_length_mapper_and_trainer(self):
        data = fixture()
        for changed in (data[:-1], data + b'\0', b'bad!' + data[4:]):
            with self.assertRaises(ValueError):
                parse(changed)
        for index, value in ((6, 0x22), (6, 0x16), (8, 1), (9, 1)):
            changed = bytearray(data)
            changed[index] = value
            with self.assertRaises(ValueError):
                parse(changed)

    def test_extract_round_trip_and_reject_mutation(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source, manifest = root / 'fixture.nes', root / 'fixture.json'
            data = fixture()
            source.write_bytes(data)
            # Synthetic fixture metadata exists only in this test's temp directory.
            manifest.write_text(json.dumps({'path': str(source), 'file': hashes(data),
                                           'payload_matches_reference': True}))
            output = root / 'extracted'
            extract(manifest, output)
            rebuilt = root / 'rebuilt.nes'
            rebuilt.write_bytes((output / 'header.bin').read_bytes() + b''.join(
                (output / f'prg{i:02x}.bin').read_bytes() for i in range(16)))
            self.assertEqual(verify(manifest, rebuilt)['sha1'], hashes(data)['sha1'])
            changed = bytearray(data)
            changed[1234] ^= 1
            rebuilt.write_bytes(changed)
            with self.assertRaisesRegex(ValueError, '0x4d2'):
                verify(manifest, rebuilt)
            source.write_bytes(changed)
            with self.assertRaisesRegex(ValueError, 'refusing to rebaseline'):
                extract(manifest, output)

    def test_progress_rejects_overlap_and_missing_evidence(self):
        entry = {'start': 10, 'end': 30, 'kind': 'code', 'symbol': 'example',
                 'evidence': 'synthetic test'}
        self.assertEqual(measure({'prg_bytes': 100, 'ranges': [entry]})['percent'], 20)
        for ranges in ([entry, entry], [{**entry, 'evidence': ''}],
                       [{**entry, 'end': 101}]):
            with self.assertRaises(ValueError):
                measure({'prg_bytes': 100, 'ranges': ranges})


if __name__ == '__main__':
    unittest.main()
