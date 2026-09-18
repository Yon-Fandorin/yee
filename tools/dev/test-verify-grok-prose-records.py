import importlib.util
import json
from pathlib import Path
import tempfile
import unittest

spec = importlib.util.spec_from_file_location('audit', Path(__file__).with_name('verify-grok-prose-records.py'))
audit = importlib.util.module_from_spec(spec)
spec.loader.exec_module(audit)


class RecordTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.root = Path(self.tmp.name)
        for name, decision in (
            ('yee-read-call-1', {'command': ['--document', 'doc', 'read', '10']}),
            ('yee-read-call-2', {'answer': 'TAIL'}),
            ('yee-read-after-call-1', {'answer': 'TAIL'}),
        ):
            folder = self.root / name
            folder.mkdir()
            raw = {'text': json.dumps(decision), 'num_turns': 1, 'total_cost_usd': 0.01,
                   'modelUsage': {'grok-4.6-build': {'modelCalls': 1}},
                   'usage': {'input_tokens': 10, 'cache_read_input_tokens': 0,
                             'cache_creation_input_tokens': 0, 'output_tokens': 5,
                             'total_tokens': 15}}
            self.write(folder / 'stdout.json', raw)
            self.write(folder / 'summary.json', {'returncode': 0, 'timed_out': False,
                                                'usage': audit.recorder.normalize_usage(raw)})
        self.write(self.root / 'yee-read-call-1/browser-before.json',
                   {'ok': True, 'document': 'doc', 'snapshot': 'Article', 'truncated': False})
        self.write(self.root / 'yee-read-call-1/browser-after.json',
                   {'ok': True, 'text': 'body TAIL', 'field_truncated': False})
        self.write(self.root / 'yee-read-after-call-1/browser-before.json',
                   {'ok': True, 'snapshot': 'Article body TAIL', 'truncated': False})

    def tearDown(self):
        self.tmp.cleanup()

    def write(self, path, value):
        path.write_text(json.dumps(value))

    def test_valid_ablation_never_becomes_competitive_pass(self):
        result = audit.audit(self.root)
        self.assertEqual(result['before_total_tokens'], 30)
        self.assertEqual(result['after_total_tokens'], 15)
        self.assertEqual(result['competitive_gate'], 'not_evaluated')

    def test_wrong_answer_fails(self):
        path = self.root / 'yee-read-after-call-1/stdout.json'
        raw = json.loads(path.read_text())
        raw['text'] = '{"answer":"invented"}'
        self.write(path, raw)
        with self.assertRaises(ValueError):
            audit.audit(self.root)

    def test_missing_usage_fails(self):
        path = self.root / 'yee-read-call-2/stdout.json'
        raw = json.loads(path.read_text())
        raw.pop('usage')
        self.write(path, raw)
        with self.assertRaises(ValueError):
            audit.audit(self.root)

    def test_truncated_evidence_fails(self):
        self.write(self.root / 'yee-read-call-1/browser-after.json',
                   {'ok': True, 'text': 'body TAIL', 'field_truncated': True})
        with self.assertRaises(ValueError):
            audit.audit(self.root)


if __name__ == '__main__':
    unittest.main()
