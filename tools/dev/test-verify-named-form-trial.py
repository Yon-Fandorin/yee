import copy
import importlib.util
import json
from pathlib import Path
import unittest


def module(name, filename):
    spec = importlib.util.spec_from_file_location(name, Path(__file__).with_name(filename))
    value = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(value)
    return value


gate = module('named_gate', 'verify-named-form-trial.py')
old = module('reference_tests', 'test-verify-direct-form-trial.py')
VIEWPORT = {'width': 1440, 'height': 900}


class GateTests(unittest.TestCase):
    def fixture(self):
        summary, evidence, rows = old.FormGateTests().fixture()
        summary['reported_cost_usd'] = 0.01
        evidence['total_native_user_wait_seconds'] = 1.0
        doc = rows[1]['response']['document']
        # Old synthetic fixture has compact lines; native evidence is expanded.
        for row in rows[1::2]:
            row['response']['snapshot'] = 'page @' + doc + ' rev=1\n' + row['response']['snapshot'].replace('@', '@' + doc + '_')
            row['response']['viewport'] = VIEWPORT
        extra = copy.deepcopy(rows[:2])
        extra[0]['request']['id'] = extra[1]['response']['id'] = 'n4'
        extra[1]['response']['snapshot'] = extra[1]['response']['snapshot'].replace('value=""', 'value="Cedar"')
        rows[4:4] = extra
        for i in (0, 4):
            rows[i]['request']['full'] = True
        for i in (2, 6):
            rows[i]['request']['baseline_document'] = doc
        evidence['calls'] = [evidence['calls'][0], {'id': 't2', 'tool': 'use_tool', 'input': {
            'tool_name': 'yee__yee_browser', 'tool_input': {'commands': [
                ['fill-named', 'Name', 'Cedar'], ['click-named', 'Save locally']]}}}]
        evidence['results'] = [evidence['results'][0], {'id': 't2', 'server': 'yee',
            'tool': 'yee_browser', 'output': '\n'.join(json.dumps(gate.compact_response(rows[i]['response']))
                                                       for i in (3, 7))}]
        return summary, evidence, rows

    def test_complete_evidence_passes(self):
        self.assertTrue(gate.verify_evidence(*self.fixture(), VIEWPORT)['success'])

    def test_missing_or_invalid_measurements_cannot_pass(self):
        for key in ('elapsed_seconds', 'reported_cost_usd'):
            for value in (None, True, -1, float('nan'), float('inf'), '5'):
                with self.subTest(key=key, value=value):
                    summary, evidence, rows = self.fixture()
                    summary[key] = value
                    result = gate.verify_evidence(summary, evidence, rows, VIEWPORT)
                    self.assertFalse(result['success'])
                    self.assertEqual(result['tokens'], 123)
        for value in (None, True, -1, 6, float('nan'), float('inf')):
            summary, evidence, rows = self.fixture()
            evidence['total_native_user_wait_seconds'] = value
            self.assertFalse(gate.verify_evidence(summary, evidence, rows, VIEWPORT)['success'])
        for value in (None, True, -1, 0, 1.5, '123'):
            summary, evidence, rows = self.fixture()
            summary['usage'] = evidence['provider_usage'] = {'total_tokens': value}
            self.assertFalse(gate.verify_evidence(summary, evidence, rows, VIEWPORT)['success'])

    def test_wrong_ref_value_viewport_and_initial_state_fail_with_usage_retained(self):
        for mutate in (lambda r: r[2]['request'].update(ref='wrong'),
                       lambda r: r[2]['request'].update(value='Birch'),
                       lambda r: r[5]['response'].update(viewport={'width': 1, 'height': 1}),
                       lambda r: r[1]['response'].update(snapshot='not empty'),
                       lambda r: r.pop()):
            summary, evidence, rows = self.fixture()
            mutate(rows)
            result = gate.verify_evidence(summary, evidence, rows, VIEWPORT)
            self.assertFalse(result['success'])
            self.assertEqual(result['tokens'], 123)

    def test_fake_output_unknown_error_extra_call_and_wrong_tool_fail(self):
        for mutate in (lambda e: e['results'][1].update(output='{"ok":true}'),
                       lambda e: e['results'][1].update(tool_error=True),
                       lambda e: e['calls'].append(e['calls'][0]),
                       lambda e: e['results'][1].update(server='other'),
                       lambda e: e.update(terminal_error=True),
                       lambda e: e.update(provider_usage=None)):
            summary, evidence, rows = self.fixture()
            mutate(evidence)
            self.assertFalse(gate.verify_evidence(summary, evidence, rows, VIEWPORT)['success'])


if __name__ == '__main__':
    unittest.main()
