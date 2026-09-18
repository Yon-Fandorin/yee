"""Continuous progress publication cannot overwrite immutable trial receipts."""
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest

spec = importlib.util.spec_from_file_location(
    'fresh_cohort', Path(__file__).with_name('run-grok-fresh-acp-cohort.py'))
runner = importlib.util.module_from_spec(spec)
spec.loader.exec_module(runner)


class ProgressTests(unittest.TestCase):
    def test_all_twelve_task_transitions_and_immutable_receipt(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            runner.c.save(root/'plan.json', {'original': True})
            for index in range(1, 13):
                case = root/'yee'/f'S{index:02}'
                runner.save_current(root, 'yee', case)
                self.assertEqual(json.loads((root/'current.json').read_text())['case'], str(case))
                self.assertEqual((root/'current.json').stat().st_mode & 0o777, 0o600)
            self.assertEqual(list(root.glob('.current-*')), [])
            with self.assertRaises(FileExistsError):
                runner.c.save(root/'plan.json', {'original': False})
            self.assertEqual(json.loads((root/'plan.json').read_text()), {'original': True})

    def test_foreign_or_undeclared_case_keeps_current_pointer(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            runner.save_current(root, 'aside', root/'aside/S01')
            before = (root/'current.json').read_bytes()
            for case in (root/'yee/S02', root/'aside/S13', root.parent/'aside/S02'):
                with self.assertRaises(ValueError):
                    runner.save_current(root, 'aside', case)
            self.assertEqual((root/'current.json').read_bytes(), before)


if __name__ == '__main__':
    unittest.main()
