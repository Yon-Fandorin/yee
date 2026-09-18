import importlib.util
import json
from pathlib import Path
import unittest

from mcp import types

spec = importlib.util.spec_from_file_location('scope_probe', Path(__file__).with_name('probe-aside-task-scope.py'))
probe = importlib.util.module_from_spec(spec)
spec.loader.exec_module(probe)


def response(text, error=False):
    return types.CallToolResult.model_validate({
        'content': [{'type': 'text', 'text': text}], 'isError': error})


class ScopeReceiptTest(unittest.TestCase):
    def test_current_sdk_aliases_and_open_notice(self):
        value = {'targetId': 'A'*32, 'url': 'data:text/html,synthetic'}
        result = response('Opened owned tab\n'+probe.MARKER+json.dumps(value))
        self.assertEqual(probe.target(probe.payload(result), value['url']), 'A'*32)

    def test_error_does_not_become_success_receipt(self):
        with self.assertRaises(ValueError):
            probe.payload(response(probe.MARKER+'{"cleanupReturned":true}', True))

    def test_missing_duplicate_or_nonobject_receipt_fails(self):
        for text in ('', probe.MARKER+'[]', probe.MARKER+'{}\n'+probe.MARKER+'{}'):
            with self.subTest(text=text), self.assertRaises(ValueError):
                probe.payload(response(text))

    def test_foreign_url_and_injectable_target_are_rejected(self):
        for value in ({'targetId':'A'*32,'url':'https://not-the-owned-tab.invalid'},
                      {'targetId':'A";doSomething()','url':'data:text/html,synthetic'},
                      {'targetId':32,'url':'data:text/html,synthetic'}):
            with self.subTest(value=value), self.assertRaises(ValueError):
                probe.target(value,'data:text/html,synthetic')


if __name__ == '__main__':
    unittest.main()
