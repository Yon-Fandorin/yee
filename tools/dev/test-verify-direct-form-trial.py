import copy
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest

spec = importlib.util.spec_from_file_location('gate', Path(__file__).with_name('verify-direct-form-trial.py'))
gate = importlib.util.module_from_spec(spec)
spec.loader.exec_module(gate)


class FormGateTests(unittest.TestCase):
    def fixture(self):
        doc = '807b65e7-6236-4807-af28-48b45cf5eb82'
        plan = [['--document', doc, 'fill', '@4', 'Cedar'], ['--document', doc, 'click', '@7']]
        observations = [{'ok': True, 'document': doc, 'truncated': False, 'revision': i + 2,
                         'snapshot': snapshot} for i, snapshot in enumerate([
            '+@4 field "Name" value=""\n+@7 button "Save locally"\n+@8 text "Waiting for input"\n',
            '~@4 field "Name" value="Cedar"\n', '~@8 text "Saved: Cedar"\n'])]
        requests = [{'id': 'n1', 'command': 'observe'},
                    {'id': 'n2', 'command': 'fill', 'ref': doc + '_4', 'value': 'Cedar'},
                    {'id': 'n3', 'command': 'click', 'ref': doc + '_7'}]
        rows = []
        for request, observation in zip(requests, observations):
            rows += [{'kind': 'request', 'request': request},
                     {'kind': 'response', 'response': {**observation, 'id': request['id']}}]
        visible = [gate.compact_response(r['response']) for r in rows[1::2]]
        calls = [{'id': 't1', 'tool': 'search_tool', 'input': {}},
                 {'id': 't2', 'tool': 'use_tool', 'input': {'tool_name': 'yee__yee_browser',
                  'tool_input': {'commands': [['observe', '--full']]}}},
                 {'id': 't3', 'tool': 'use_tool', 'input': {'tool_name': 'yee__yee_browser',
                  'tool_input': {'commands': plan}}}]
        results = [{'id': 't1', 'output': 'discovered'},
                   {'id': 't2', 'server': 'yee', 'tool': 'yee_browser', 'output': json.dumps(visible[0])},
                   {'id': 't3', 'server': 'yee', 'tool': 'yee_browser',
                    'output': '\n'.join(json.dumps(o) for o in visible[1:])}]
        usage = {'total_tokens': 123}
        summary = {'returncode': 0, 'timed_out': False, 'usage': usage,
                   'allowed_mcp_tool': 'yee__yee_browser', 'elapsed_seconds': 5}
        evidence = {'provider_usage': usage, 'calls': calls, 'results': results, 'answer': 'Saved: Cedar',
                    'terminal_error': False, 'terminal_subtype': 'success'}
        return summary, evidence, rows

    def test_valid_native_and_tool_evidence_pass(self):
        self.assertTrue(gate.verify_evidence(*self.fixture(), 'mcp')['success'])

    def test_viewport_gate_checks_every_response_and_preserves_failed_usage(self):
        viewport = {'width': 1440, 'height': 900}
        for broken_index in (None, 0, 1, 2):
            summary, evidence, rows = self.fixture()
            for index, row in enumerate(rows[1::2]):
                row['response']['viewport'] = viewport if index != broken_index else {
                    'width': 698, 'height': 900}
            visible = [gate.compact_response(row['response']) for row in rows[1::2]]
            evidence['results'][1]['output'] = json.dumps(visible[0])
            evidence['results'][2]['output'] = '\n'.join(json.dumps(o) for o in visible[1:])
            result = gate.verify_evidence(summary, evidence, rows, 'mcp', viewport)
            self.assertEqual(result['success'], broken_index is None)
            self.assertEqual(result['tokens'], 123)
        self.assertFalse(gate.verify_evidence(*self.fixture(), 'mcp', viewport)['success'])

    def test_claimed_success_does_not_hide_tool_error(self):
        summary, evidence, rows = self.fixture()
        evidence['results'][-1]['tool_error'] = True
        result = gate.verify_evidence(summary, evidence, rows, 'mcp')
        self.assertFalse(result['success'])
        self.assertEqual(result['tokens'], 123)

    def test_missing_unknown_extra_and_mismatched_results_fail(self):
        for mutate in (
            lambda e: e['results'].pop(),
            lambda e: e['results'][-1].update(unrecognized_result=True),
            lambda e: e['calls'].append(copy.deepcopy(e['calls'][-1])),
            lambda e: e['results'][-1].update(id='unrelated'),
        ):
            summary, evidence, rows = self.fixture()
            mutate(evidence)
            self.assertFalse(gate.verify_evidence(summary, evidence, rows, 'mcp')['success'])

    def test_wrong_grounded_ref_and_wrong_native_value_fail(self):
        summary, evidence, rows = self.fixture()
        evidence['calls'][-1]['input']['tool_input']['commands'][0][3] = '@99'
        self.assertFalse(gate.verify_evidence(summary, evidence, rows, 'mcp')['success'])
        summary, evidence, rows = self.fixture()
        rows[2]['request']['value'] = 'different'
        self.assertFalse(gate.verify_evidence(summary, evidence, rows, 'mcp')['success'])

    def test_fake_saved_output_cannot_override_native_response(self):
        summary, evidence, rows = self.fixture()
        rows[-1]['response']['snapshot'] = 'Waiting for input'
        self.assertFalse(gate.verify_evidence(summary, evidence, rows, 'mcp')['success'])

    def test_unknown_usage_and_timeout_are_not_success(self):
        summary, evidence, rows = self.fixture()
        evidence['provider_usage'] = None
        result = gate.verify_evidence(summary, evidence, rows, 'mcp')
        self.assertFalse(result['success'])
        self.assertIsNone(result['tokens'])
        summary, evidence, rows = self.fixture()
        summary['timed_out'] = True
        self.assertFalse(gate.verify_evidence(summary, evidence, rows, 'mcp')['success'])

    def test_missing_record_is_failed_not_zero_tokens(self):
        with tempfile.TemporaryDirectory() as directory:
            result = gate.verify(Path(directory), [], 'mcp')
        self.assertFalse(result['success'])
        self.assertIsNone(result['tokens'])

    def test_provider_error_even_with_saved_answer_fails(self):
        summary, evidence, rows = self.fixture()
        evidence['terminal_error'] = True
        self.assertFalse(gate.verify_evidence(summary, evidence, rows, 'mcp')['success'])


if __name__ == '__main__':
    unittest.main()
