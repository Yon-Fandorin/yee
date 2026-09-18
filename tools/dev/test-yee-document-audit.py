import copy
import json
import unittest

from yee_document_audit import verify


class DocumentAuditTests(unittest.TestCase):
    def records(self):
        doc = '12345678-1234-1234-1234-123456789abc'
        alias = '~012345abcdef.1'
        native = [
            {'request': {'command': 'observe'}},
            {'response': {'id': '1', 'ok': True, 'document': doc, 'text': 'policy'}},
            {'request': {'command': 'read', 'ref': doc+'_1'}},
            {'response': {'id': '2', 'ok': True, 'text': 'complete policy'}}]
        calls = [
            {'arguments': {'commands': [['observe']]}, 'native_sequence_start': 0},
            {'native_sequence_end': 2, 'content': [json.dumps({
                'ok': True, 'document': alias, 'text': 'policy'})]},
            {'arguments': {'commands': [['--document', alias, 'read', '1']]}, 'native_sequence_start': 2},
            {'native_sequence_end': 4, 'content': [json.dumps({'ok': True, 'text': 'complete policy'})]}]
        return calls, native

    def test_verb_first_document_has_identical_audited_binding(self):
        calls,native=self.records()
        argv=calls[2]['arguments']['commands'][0]
        calls[2]['arguments']['commands'][0]=['read',*argv[:2],'1']
        self.assertEqual(verify(calls,native)['direct_commands_checked'],2)
        calls[2]['arguments']['commands'][0]=['--document','~012345abcdef.1','read','--document','other','1']
        with self.assertRaises(ValueError):verify(calls,native)

    def test_actual_presented_handle_expands_to_native_uuid(self):
        self.assertEqual(verify(*self.records())['direct_commands_checked'], 2)

    def test_content_and_native_ref_tampering_rejected(self):
        calls, native = self.records()
        changed = copy.deepcopy(native); changed[2]['request']['ref'] = 'other_1'
        with self.assertRaises(ValueError): verify(calls, changed)
        changed = copy.deepcopy(calls); changed[3]['content'] = ['{"ok":true,"text":"shortened"}']
        with self.assertRaises(ValueError): verify(changed, native)

    def test_unissued_or_reconnected_handle_rejected(self):
        for value in ('~012345abcdef.2', '~ffffffffffff.1'):
            calls, native = self.records()
            calls[2]['arguments']['commands'][0][1] = value
            with self.assertRaises(ValueError): verify(calls, native)

    def test_batch_cannot_use_handle_issued_later_in_same_batch(self):
        calls, native = self.records()
        combined = [dict(calls[0], arguments={'commands': [
            calls[0]['arguments']['commands'][0], calls[2]['arguments']['commands'][0]]}),
            {'native_sequence_end': 4, 'content': [json.dumps([
                json.loads(calls[1]['content'][0]), json.loads(calls[3]['content'][0])])]}]
        with self.assertRaises(ValueError): verify(combined, native)

    def test_named_expansion_does_not_claim_direct_audit(self):
        calls, native = self.records()
        calls[0]['arguments']['commands'] = [['read-named', 'Policy']]
        with self.assertRaises(ValueError): verify(calls, native)


if __name__ == '__main__': unittest.main()
