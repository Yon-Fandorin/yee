"""Safety and completion behavior of the adapter's read-only condition wait."""
import importlib.util
import json
from pathlib import Path
import threading
import time
import unittest

import anyio
from yee_browser_conditions import viewport_text, matches
from yee_browser_results import pack_results, unpack_results

spec = importlib.util.spec_from_file_location('mcp_tests', Path(__file__).with_name('test-yee-browser-mcp.py'))
t = importlib.util.module_from_spec(spec)
spec.loader.exec_module(t)
adapter = t.adapter


class ConditionTests(unittest.TestCase):
    def page(self, text='Pending', document='doc'):
        page = t.AdapterTests().link_page(document)
        page['scroll'] = {'y': 0, 'max_y': 0, 'can_scroll_down': False, 'can_scroll_up': False}
        page['snapshot'] = f'page @{document} rev=1 title="Page" origin="https://example.test"\n+@{document}_1 text {json.dumps(text)}\n'
        return page

    def worker(self, native, page=None):
        worker = adapter.Adapter(t.AdapterTests().config(), native)
        worker.remember_observation(page or self.page())
        return worker

    def test_intermediate_arrivals_do_not_complete_absence_wait(self):
        calls = []
        pages = iter([self.page('Pending: one arrived'), self.page('Pending: two arrived'), self.page('All arrived')])
        def native(command):
            calls.append(command.command)
            return 0, next(pages)
        worker = self.worker(native)
        result = anyio.run(worker.call, {'action': 'wait-until', 'document': 'doc', 'value': 'Pending', 'wait_ms': 1000, 'absent': True})
        value = json.loads(result.content[0].text)
        self.assertFalse(result.is_error)
        self.assertEqual(calls, ['observe', 'wait-change', 'wait-change'])
        self.assertEqual(value['wait_condition']['state'], 'matched')
        self.assertEqual(value['wait_condition']['probes'], 3)
        self.assertIn('All arrived', value['snapshot'])

    def test_literal_text_decodes_prose_but_excludes_metadata_urls_and_flags(self):
        lines = ['+@doc_1 text "Accessible name" text="Actual \\"quoted\\" body"',
                 '+@doc_2 field "Search" value="Korean 한국어" focused',
                 '+@doc_3 link "More" href="https://example.test/completed" href_truncated']
        texts = viewport_text(lines)
        self.assertTrue(matches(texts, 'Actual "quoted" body'))
        self.assertTrue(matches(texts, '한국어'))
        self.assertFalse(matches(texts, 'completed'))
        self.assertFalse(matches(texts, 'focused'))
        for invalid in ([], ['~@doc_1 text "Pending"'], ['+@doc_1 text "Pending" text_truncated'],
                        ['+@doc_1 field "Secret" value=<redacted>'], ['+@doc_1 heading ' + json.dumps('x' * 160)]):
            self.assertIsNone(viewport_text(invalid))

    def test_matching_after_deadline_is_timeout_not_completion(self):
        def native(command):
            time.sleep(.01)
            return 0, self.page('All arrived')
        result = anyio.run(self.worker(native).call, {'action': 'wait-until', 'document': 'doc', 'value': 'All arrived', 'wait_ms': 1})
        self.assertTrue(result.is_error)
        self.assertEqual(json.loads(result.content[0].text)['wait_condition']['state'], 'timeout')

    def test_invalidation_and_empty_rerender_never_claim_disappearance(self):
        cases = [{'document': 'other'}, {'tab': 'other'}, {'url': 'https://example.test/other'},
                 {'scroll': {'y': 1}}, {'viewport': {'width': 1}}, {'truncated': True},
                 {'execution_settled': False}, {'receipt_persisted': False}, {'partial_effect_possible': True},
                 {'snapshot': 'page @doc rev=2 title="Page" origin="https://example.test"\n'},
                 {'snapshot': 'page @doc rev=2 delta\nbase_rev=1\n'}]
        for changes in cases:
            calls = []
            def native(command):
                calls.append(command.command)
                return 0, {**self.page('All arrived'), **changes}
            result = anyio.run(self.worker(native).call, {'action': 'wait-until', 'document': 'doc', 'value': 'Pending', 'wait_ms': 1000, 'absent': True})
            self.assertTrue(result.is_error, changes)
            self.assertEqual(calls, ['observe'])
            self.assertEqual(json.loads(result.content[0].text)['wait_condition']['state'], 'invalidated')

    def test_bad_input_and_unobserved_absence_do_not_dispatch(self):
        payload = {'action': 'wait-until', 'document': 'doc', 'value': 'Pending', 'wait_ms': 1000}
        invalid = [dict(payload, document='old'), dict(payload, wait_ms=True), dict(payload, absent='yes'),
                   dict(payload, value=''), dict(payload, value='한' * 1334), dict(payload, unknown=True),
                   dict(payload, value='Never observed', absent=True)]
        for item in invalid:
            calls = []
            result = anyio.run(self.worker(lambda command: calls.append(command)).call, item)
            self.assertTrue(result.is_error)
            self.assertEqual(calls, [])

    def test_short_document_handles_expire_and_remain_presentation_only(self):
        calls = []
        worker = self.worker(lambda c: (calls.append(c.command) or 0, self.page('All arrived')))
        worker.documents = adapter.DocumentHandles()
        handle = worker.documents.present({'document': 'doc'})['document']
        result = anyio.run(worker.call, {'action': 'wait-until', 'document': handle, 'value': 'All arrived', 'wait_ms': 1000})
        self.assertFalse(result.is_error)
        self.assertEqual(json.loads(result.content[0].text)['document'], handle)
        worker.documents.present({'document': 'new'})
        result = anyio.run(worker.call, {'action': 'wait-until', 'document': handle, 'value': 'All arrived', 'wait_ms': 1000})
        self.assertTrue(result.is_error)
        self.assertEqual(calls, ['observe'])

    def test_cancellation_during_probe_dispatches_no_followup(self):
        started, release = threading.Event(), threading.Event()
        calls = []
        def native(command):
            calls.append(command.command)
            started.set()
            release.wait(3)
            return 0, self.page('Pending')
        worker = self.worker(native)
        async def run():
            async with anyio.create_task_group() as group:
                group.start_soon(worker.call, {'action': 'wait-until', 'document': 'doc', 'value': 'All arrived', 'wait_ms': 1000})
                while not started.is_set():
                    await anyio.sleep(.01)
                group.cancel_scope.cancel()
                release.set()
        anyio.run(run)
        self.assertEqual(calls, ['observe'])

    def test_visit_context_is_exact_and_losslessly_decoded(self):
        source, destination, returned = self.page(), self.page(document='destination'), self.page(document='returned')
        inventory = {'ok': True, 'execution_settled': True, 'receipt_persisted': True, 'tab': source['tab'],
                     'tabs': [{'tab': source['tab'], 'active': True}]}
        results = [source, destination, returned, inventory]
        packed = pack_results(results)
        self.assertEqual(packed['current'], {'document': 'returned', 'tab': source['tab'], 'revision': 1})
        self.assertEqual(unpack_results(packed), results)
        for changes in ({'document': 'doc'}, {'revision': True}, {'tab': 'other'}):
            bad = {**packed, 'current': {**packed['current'], **changes}}
            with self.assertRaises(ValueError):
                unpack_results(bad)
        invalid = [source, destination, returned, {**inventory, 'tab': 'other'}]
        self.assertNotIn('current', pack_results(invalid))

    def test_missing_batch_action_has_flat_repair_and_zero_native_calls(self):
        calls = []
        worker = self.worker(lambda command: calls.append(command))
        result = anyio.run(worker.call, {'action': 'batch', 'document': 'doc', 'batch': [{'ref': '1', 'fill': {'value': 'Exact literal'}}]})
        self.assertTrue(result.is_error)
        error = json.loads(result.content[0].text)
        self.assertFalse(error['native_dispatched'])
        self.assertEqual(error['missing'], ['action'])
        self.assertTrue(all('action' in example and 'ref' in example for example in error['examples']))
        self.assertEqual(calls, [])


if __name__ == '__main__':
    unittest.main()
