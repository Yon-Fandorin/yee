import copy
import base64
import importlib.util
import json
from pathlib import Path
import unittest

spec = importlib.util.spec_from_file_location('audit', Path(__file__).with_name('inspect-comparator-mcp.py'))
audit = importlib.util.module_from_spec(spec); spec.loader.exec_module(audit)


class Tests(unittest.TestCase):
    def records(self):
        arguments = {'title': 'Synthetic comparison', 'code': 'literal code'}
        records = [
            {'sequence': 1, 'kind': 'mcp_request', 'invocation': 'a', 'operation': 'tools/call',
             'params': {'name': 'repl', 'arguments': arguments}},
            {'sequence': 2, 'kind': 'mcp_response', 'invocation': 'a', 'operation': 'tools/call',
             'elapsed_ns': 250000000, 'result': {'isError': True, 'content': [
                 {'type': 'text', 'text': 'first'}, {'type': 'text', 'text': 'second'}]}}]
        events = [
            {'role': 'assistant', 'tool_calls': [{'id': 'model-a', 'function': {
                'name': 'mcp__aside__repl', 'arguments': json.dumps(arguments)}}]},
            {'role': 'tool', 'tool_call_id': 'model-a', 'content': 'firstsecond'}]
        return events, records

    def check(self, events, records):
        return audit.inspect(events, records, model_tool='mcp__aside__repl', recorded_tool='repl')

    def test_exact_error_result_preserves_count_and_cost_without_success_claim(self):
        result = self.check(*self.records())
        self.assertEqual(result['mcp_calls'], 1)
        self.assertEqual(result['mcp_errors'], 1)
        self.assertEqual(result['recorded_operation_seconds'], .25)
        self.assertFalse(result['comparison_ready'])
        self.assertIsNone(result['scenario_success'])
        self.assertFalse(result['native_settlement_verified'])
        self.assertIsNone(result['tool_errors'])

    def test_multiple_tools_require_complete_ordered_identity_and_results(self):
        events, records = self.records()
        second_events, second_records = self.records()
        second_events[0]['tool_calls'][0]['id']='model-b'
        second_events[0]['tool_calls'][0]['function']['name']='mcp__aside__other'
        second_events[1]['tool_call_id']='model-b'
        for i,row in enumerate(second_records,3): row.update(sequence=i,invocation='b')
        second_records[0]['params']['name']='other'
        events+=second_events; records+=second_records
        mapping={'repl':'mcp__aside__repl','other':'mcp__aside__other'}
        self.assertEqual(audit.inspect(events,records,tool_mapping=mapping)['mcp_calls'],2)
        grouped=[{'role':'assistant','tool_calls':events[2]['tool_calls']+events[0]['tool_calls']},
                 events[3],events[1]]
        self.assertEqual(audit.inspect(grouped,records,tool_mapping=mapping)['mcp_calls'],2)
        changed=copy.deepcopy(grouped);changed[1]['content']='wrong result'
        with self.assertRaises(ValueError):audit.inspect(changed,records,tool_mapping=mapping)
        for ev,rec in ((events,records[:2]),(events[:2],records),
                       (second_events+events[:2],records)):
            with self.assertRaises(ValueError):audit.inspect(ev,rec,tool_mapping=mapping)
        with self.assertRaises(ValueError):audit.inspect(events,records,tool_mapping={'repl':mapping['repl']})

    def test_identical_parallel_requests_cannot_be_guessed_by_order(self):
        events,records=self.records()
        other_events,other_records=self.records()
        other_events[0]['tool_calls'][0]['id']='other-id'
        other_events[1]['tool_call_id']='other-id'
        for i,row in enumerate(other_records,3):row.update(sequence=i,invocation='other')
        grouped=[{'role':'assistant','tool_calls':events[0]['tool_calls']+other_events[0]['tool_calls']},
                 events[1],other_events[1]]
        with self.assertRaisesRegex(ValueError,'ambiguous'):
            self.check(grouped,records+other_records)

    def test_screenshot_bytes_match_kimi_image_parts_without_silent_conversion(self):
        events,records=self.records()
        name='browser_screenshot';model='mcp__browser_harness__'+name
        records[0]['params']['name']=name
        events[0]['tool_calls'][0]['function']['name']=model
        data=base64.b64encode(b'\x89PNG\r\n\x1a\nsynthetic-test-payload').decode()
        records[1]['result']={'isError':False,'content':[
            {'type':'text','text':'{"path":"/tmp/shot.png"}'},
            {'type':'image','mimeType':'image/png','data':data}]}
        parts=[{'type':'text','text':'{"path":"/tmp/shot.png"}'},
               {'type':'image_url','imageUrl':{'url':'data:image/png;base64,'+data}}]
        events[1]['content']=json.dumps(parts)
        result=audit.inspect(events,records,model_tool=model,recorded_tool=name,
                             outcome_profile='browser-harness-0.1.13')
        self.assertEqual(result['image_blocks'],1)
        parts[1]['imageUrl']['url']+='changed'
        events[1]['content']=json.dumps(parts)
        with self.assertRaises(ValueError):
            audit.inspect(events,records,model_tool=model,recorded_tool=name,
                          outcome_profile='browser-harness-0.1.13')

    def test_aside_media_preserves_exact_image_text_order_and_error_semantics(self):
        for mime, raw in [('image/jpeg', b'\xff\xd8\xffsynthetic'),
                          ('image/png', b'\x89PNG\r\n\x1a\nsynthetic')]:
            events, records = self.records()
            data = base64.b64encode(raw).decode()
            records[1]['result']['content'].insert(1, {
                'type': 'image', 'mimeType': mime, 'data': data})
            parts = [{'type': 'text', 'text': 'first'},
                     {'type': 'image_url', 'imageUrl': {'url': 'data:'+mime+';base64,'+data}},
                     {'type': 'text', 'text': 'second'}]
            events[1]['content'] = json.dumps(parts)
            kwargs = dict(model_tool='mcp__aside__repl', recorded_tool='repl',
                          outcome_profile='aside-repl-media-v1')
            result = audit.inspect(events, records, **kwargs)
            self.assertEqual(result['image_bytes'], len(raw))
            self.assertEqual(result['image_blocks'], 1)
            self.assertEqual(result['mcp_errors'], 1)
            self.assertIsNone(result['tool_errors'])
            self.assertFalse(result['comparison_ready'])
            with self.assertRaises(ValueError): self.check(events, records)
            changed = copy.deepcopy(parts)
            changed[1]['imageUrl']['url'] = 'data:'+mime+';base64,'+base64.b64encode(raw+b'changed').decode()
            for bad in [changed, parts[::-1], parts[:2],
                        [{'type': 'text', 'text': 'changed'}]+parts[1:]]:
                events[1]['content'] = json.dumps(bad)
                with self.assertRaises(ValueError): audit.inspect(events, records, **kwargs)

    def test_aside_media_rejects_invalid_payload_and_wrong_tool_identity(self):
        for mime, data in [('image/jpeg', '%%%'), ('image/jpeg', 'YWJj'),
                           ('image/gif', 'R0lGODlh')]:
            events, records = self.records()
            records[1]['result']['content'] = [{'type': 'image', 'mimeType': mime, 'data': data}]
            with self.assertRaises(ValueError):
                audit.inspect(events, records, model_tool='mcp__aside__repl',
                              recorded_tool='repl', outcome_profile='aside-repl-media-v1')
        events, records = self.records()
        with self.assertRaisesRegex(ValueError, 'exact Aside repl'):
            audit.inspect(events, records, tool_mapping={'repl': 'mcp__other__repl'},
                          outcome_profile='aside-repl-media-v1')

    def test_harness_json_error_is_counted_without_double_counting(self):
        for protocol_error in (False, True):
            events, records = self.records()
            records[0]['params']['name'] = 'browser_current_tab'
            events[0]['tool_calls'][0]['function']['name'] = 'mcp__browser_harness__browser_current_tab'
            body = '{"error":"lost reply"}'
            records[1]['result'] = {'isError': protocol_error,
                                   'content': [{'type': 'text', 'text': body}]}
            events[1]['content'] = body
            result = audit.inspect(events, records,
                                   model_tool='mcp__browser_harness__browser_current_tab',
                                   recorded_tool='browser_current_tab', outcome_profile='browser-harness-0.1.13')
            self.assertEqual(result['mcp_errors'], int(protocol_error))
            self.assertEqual(result['tool_errors'], 1)
            self.assertEqual(result['recorded_operation_seconds'], .25)
            self.assertIsNone(result['scenario_success'])

    def test_harness_profile_rejects_uninterpretable_output(self):
        for texts in ([], ['not json'], ['{}', '{}']):
            with self.subTest(texts=texts), self.assertRaises(ValueError):
                audit.browser_harness_error(texts)
        self.assertFalse(audit.browser_harness_error(['{"result":{"error":"page data"}}']))

    def test_changed_arguments_output_or_model_identity_fail(self):
        for mode in ('arguments', 'output', 'name', 'id'):
            events, records = self.records()
            if mode == 'arguments': events[0]['tool_calls'][0]['function']['arguments'] = '{}'
            elif mode == 'output': events[1]['content'] = 'first\nsecond'
            elif mode == 'name': events[0]['tool_calls'][0]['function']['name'] = 'mcp__yee__yee_browser'
            else: events[1]['tool_call_id'] = 'other'
            with self.subTest(mode=mode), self.assertRaises(ValueError): self.check(events, records)

    def test_incomplete_replayed_or_parallel_calls_fail(self):
        events, records = self.records()
        for ev, rec in ((events, records[:1]), (events+events, records), (events[:1], records)):
            with self.assertRaises(ValueError): self.check(ev, rec)
        events[0]['tool_calls'] *= 2
        with self.assertRaises(ValueError): self.check(events, records)

    def test_nontext_or_invalid_timing_is_not_silently_omitted(self):
        for key, value in (('elapsed_ns', True), ('elapsed_ns', -1), ('invocation', 'other')):
            events, records = self.records(); records[1][key] = value
            with self.assertRaises(ValueError): self.check(events, records)
        events, records = self.records()
        records[1]['result']['content'].append({'type': 'image', 'data': 'synthetic'})
        with self.assertRaises(ValueError): self.check(events, records)

    def test_metadata_is_counted_but_cannot_substitute_for_model_use(self):
        events, records = self.records()
        metadata = copy.deepcopy(records)
        for row in metadata: row.update(invocation='metadata', operation='tools/list')
        with self.assertRaises(ValueError): self.check([], metadata)
        joined = metadata+records
        for i, row in enumerate(joined, 1): row['sequence'] = i
        result = self.check(events, joined)
        self.assertEqual(result['metadata_operations'], 1)
        self.assertEqual(result['recorded_operation_seconds'], .5)


if __name__ == '__main__': unittest.main()
