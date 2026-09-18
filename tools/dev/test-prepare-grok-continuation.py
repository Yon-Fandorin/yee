import importlib.util
import json
from pathlib import Path
import tempfile
import unittest

spec = importlib.util.spec_from_file_location('prepare', Path(__file__).with_name('prepare-grok-continuation.py'))
prepare = importlib.util.module_from_spec(spec)
spec.loader.exec_module(prepare)


class ContinuationTests(unittest.TestCase):
    def test_only_task_decision_and_selected_output_are_replayed(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            (root / 'input.json').write_text(json.dumps({'prompt': 'Synthetic task'}))
            (root / 'stdout.json').write_text(json.dumps({
                'text': '{"commands":[]}', 'sessionId': 'DO_NOT_FORWARD_SESSION',
                'usage': {'private': 'DO_NOT_FORWARD_USAGE'},
                'modelUsage': {'private': 'DO_NOT_FORWARD_MODEL_USAGE'}}))
            result = root / 'result.txt'
            result.write_text('Saved: test')
            prompt = prepare.make_prompt(root, result)
            self.assertIn('Synthetic task', prompt)
            self.assertIn('{"commands":[]}', prompt)
            self.assertIn('Saved: test', prompt)
            self.assertNotIn('DO_NOT_FORWARD', prompt)

    def test_oversized_inputs_fail_closed(self):
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / 'large.txt'
            path.write_text('x' * 65537)
            with self.assertRaises(ValueError):
                prepare.bounded_text(path)


if __name__ == '__main__':
    unittest.main()
