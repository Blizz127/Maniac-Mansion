"""Publication guard must inspect index bytes, including before push rechecks."""
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

from test_tools import fixture

GUARD = Path(__file__).resolve().parents[1] / 'tools/guard-staged.py'


class GuardStagedTest(unittest.TestCase):
    def test_index_content_and_pre_push_manifest(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            subprocess.run(['git', 'init', '-q'], cwd=root, check=True)
            (root / 'rom').mkdir()
            (root / 'local').mkdir()
            (root / 'rom/original.nes').write_bytes(fixture())
            note = root / 'note.txt'
            note.write_text('Independent documentation.\n')
            subprocess.run(['git', 'add', 'note.txt'], cwd=root, check=True)
            # A dirty worktree cannot substitute for the already staged blob.
            note.write_bytes(bytes(range(32, 64)))
            def guard(*args):
                return subprocess.run([sys.executable, str(GUARD), *args],
                                      cwd=root, capture_output=True, text=True)
            self.assertEqual(guard('--manifest', 'local/check.json').returncode, 0)
            self.assertEqual(guard('--recheck', 'local/check.json').returncode, 0)
            subprocess.run(['git', 'add', 'note.txt'], cwd=root, check=True)
            result = guard('--recheck', 'local/check.json')
            self.assertEqual(result.returncode, 1)
            self.assertIn('index changed', result.stdout)
            self.assertIn('32-byte PRG window', result.stdout)

    def test_game_data_names_and_port_changes_are_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            subprocess.run(['git', 'init', '-q'], cwd=root, check=True)
            (root / 'rom').mkdir()
            (root / 'rom/original.nes').write_bytes(fixture())
            (root / 'port').mkdir()
            (root / 'port/new.c').write_text('Independent code.\n')
            (root / 'game.sav').write_text('No ROM window here.\n')
            subprocess.run(['git', 'add', 'port/new.c', 'game.sav'], cwd=root, check=True)
            result = subprocess.run([sys.executable, str(GUARD)], cwd=root,
                                    capture_output=True, text=True)
            self.assertEqual(result.returncode, 1)
            self.assertIn('2 rejected', result.stdout)
