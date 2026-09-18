import base64
import copy
import json
from pathlib import Path
import tempfile
import unittest

from grok_acp_correlation import correlate


def wire(path, content=None, error=False):
    content = content or [{'type': 'text', 'text': 'original feedback'}]
    frames = [('to_server', {'jsonrpc': '2.0', 'id': 1, 'method': 'tools/call',
                            'params': {'name': 'yee_browser', 'arguments': {'action': 'observe'}}}),
              ('to_client', {'jsonrpc': '2.0', 'id': 1, 'result': {'content': content, 'isError': error}})]
    rows = []
    for index, (direction, message) in enumerate(frames):
        identity = str(index)
        rows.append({'sequence': len(rows)+1, 'kind': 'wire_received', 'frame': identity,
                     'direction': direction, 'monotonic_ns': index*10,
                     'base64': base64.b64encode((json.dumps(message)+'\n').encode()).decode()})
        rows.append({'sequence': len(rows)+1, 'kind': 'wire_forwarded', 'frame': identity,
                     'direction': direction, 'monotonic_ns': index*10+1})
    path.write_text('\n'.join(json.dumps(row) for row in rows)+'\n')


def parsed(error=False):
    return {'calls': [{'id': 'call', 'name': 'use_tool',
                      'arguments': {'tool_name': 'yee__yee_browser', 'tool_input': {'action': 'observe'}},
                      'raw_output': {'type': 'MCP', 'server_name': 'yee', 'tool_name': 'yee_browser',
                                     'output': {'Error' if error else 'OkayOutput': 'original feedback'},
                                     'is_error': error},
                      'tool_error': error, 'text': 'original feedback'}]}


def exact(received, original, session):
    if received != original:
        raise ValueError('modified original text')


class CorrelationTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory(dir='/private/tmp')
        self.path = Path(self.directory.name)/'wire.jsonl'
        wire(self.path)

    def tearDown(self):
        self.directory.cleanup()

    def test_original_acp_output_and_wire_match_without_headless_conversion(self):
        self.assertTrue(correlate(parsed(), [self.path], exact, {})['verified'])

    def test_changed_server_tool_input_body_and_error_variant_are_rejected(self):
        mutations = [lambda c: c['raw_output'].update(server_name='foreign'),
                     lambda c: c['raw_output'].update(tool_name='foreign'),
                     lambda c: c['arguments'].update(tool_input={'action': 'click'}),
                     lambda c: c.update(text='different'),
                     lambda c: c['raw_output'].update(output={'OkayOutput': 'different'}),
                     lambda c: c['raw_output'].update(output={'Error': 'original feedback'}),
                     lambda c: c['raw_output'].update(is_error=True)]
        for mutation in mutations:
            data = copy.deepcopy(parsed())
            mutation(data['calls'][0])
            with self.assertRaises(ValueError):
                correlate(data, [self.path], exact, {})

    def test_actual_error_variant_is_retained(self):
        wire(self.path, error=True)
        result = correlate(parsed(True), [self.path], exact, {})
        self.assertEqual(result['mcp_errors'], 1)
        with self.assertRaises(ValueError):
            correlate(parsed(), [self.path], exact, {})

    def test_missing_call_and_unforwarded_original_wire_are_rejected(self):
        with self.assertRaises(ValueError):
            correlate({'calls': []}, [self.path], exact, {})
        rows = self.path.read_text().splitlines()
        self.path.write_text('\n'.join(rows[:-1])+'\n')
        with self.assertRaises(ValueError):
            correlate(parsed(), [self.path], exact, {})


if __name__ == '__main__':
    unittest.main()
