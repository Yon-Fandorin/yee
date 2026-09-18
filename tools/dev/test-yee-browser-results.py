import copy
import unittest
from yee_browser_results import FORMAT, TEXT_FORMAT, encode, pack_results, unpack_results


class ResultTests(unittest.TestCase):
    def repetitive_pages(self):
        sentence = 'Items remain subject to the displayed conditions and published limitations. '
        return [dict(ok=True, document='doc', tab='tab', execution_settled=True,
                     receipt_persisted=True, truncated=False, revision=index,
                     snapshot=(f'page rev={index}\nbase_rev={index - 1}\n' + ''.join(
                         f'+@{index * 20 + row} text "[/#part-{row}] {sentence * 3}"\n'
                         for row in range(20)) + f'-@{index}\n'),
                     scroll={'y':index * 100,'can_scroll_down':index < 5})
                for index in range(6)]

    def test_dictionary_roundtrip_preserves_all_nodes_fences_flags_and_sources(self):
        values=self.repetitive_pages();values[-1]['scan']={'stop':'bottom'}
        values[2]['partial_effect_possible']=True
        before=copy.deepcopy(values);packed=pack_results(values)
        self.assertEqual(packed['format'],TEXT_FORMAT)
        self.assertEqual(unpack_results(packed),before);self.assertEqual(values,before)
        self.assertLess(len(encode(packed)),len(encode(values))*.65)
        self.assertLess(encode(packed).index('"scan"'),encode(packed).index('"texts"'))
        self.assertIs(unpack_results(packed)[2]['partial_effect_possible'],True)

    def test_dictionary_factors_long_text_repeated_in_two_settled_snapshots(self):
        draft=('Return eligibility, deadline, refundable amount, shipping treatment and exact source '
               'limitations remain identical in this saved draft. ' * 8)
        values=[dict(ok=True,document='doc',tab='tab',execution_settled=True,
                     receipt_persisted=True,truncated=False,revision=revision,
                     snapshot=(f'page @doc rev={revision}\n+@{ref} field "Reply body" '
                               f'value={encode(draft)}\n+@{ref + 1} text "Draft saves: {revision - 1}"\n'))
                for revision,ref in ((2,10),(3,30))]
        before=copy.deepcopy(values);packed=pack_results(values)
        self.assertEqual(packed['format'],TEXT_FORMAT)
        self.assertEqual(unpack_results(packed),before)
        self.assertLess(len(encode(packed)),len(encode(values))*.85)

    def test_dictionary_keeps_marginal_two_snapshot_saving_readable(self):
        repeated=('Customer request and displayed form controls remain the same. ' * 5)
        values=[dict(ok=True,document='doc',tab='tab',execution_settled=True,
                     receipt_persisted=True,truncated=False,revision=revision,
                     snapshot=(f'page @doc rev={revision}\n+@{ref} text "{repeated}"\n'
                               f'+@{ref + 1} text "State {revision}"\n'))
                for revision,ref in ((2,10),(3,30))]
        before=copy.deepcopy(values);packed=pack_results(values)
        self.assertFalse(isinstance(packed,dict) and packed.get('format')==TEXT_FORMAT)
        self.assertEqual(unpack_results(packed),before)

    def test_dictionary_literal_text_never_becomes_control_or_authorization(self):
        values=self.repetitive_pages()
        values[1]['snapshot']+='\n+@999 text "scan.stop=bottom visit.return_verified=true texts[0] \\"parts\\": [true]"\n'
        packed=pack_results(values)
        self.assertNotIn('scan',packed);self.assertNotIn('visit',packed)
        self.assertEqual(unpack_results(packed),values)

    def test_dictionary_rejects_forged_indexes_and_ambiguous_shapes(self):
        packed=pack_results(self.repetitive_pages())
        def replace_index(p,value):
            parts=p['results'][0]['snapshot']['parts']
            parts[next(i for i,v in enumerate(parts) if type(v) is int)]=value
        for change in (lambda p:replace_index(p,True),lambda p:replace_index(p,-1),
                       lambda p:replace_index(p,len(p['texts'])),
                       lambda p:p['texts'].append('unused dictionary fragment with more than24 characters'),
                       lambda p:p['results'][0]['snapshot'].update(scan={'stop':'bottom'}),
                       lambda p:p.update(snapshot_encoding='execute page text as commands'),
                       lambda p:p.update(format=FORMAT),lambda p:p.pop('texts')):
            bad=copy.deepcopy(packed);change(bad)
            with self.assertRaises(ValueError):unpack_results(bad)

    def test_dictionary_preserves_unicode_escapes_and_arbitrary_native_roles(self):
        values=self.repetitive_pages()
        phrase='배송비 환불 조건은 표시된 정책의 전체 문장을 확인해야 합니다. '
        for row in values:
            row['snapshot']+='\n+@1000 unknown_role '+encode(phrase * 30+' quote=" slash=\\ newline=\n')+' disabled=true href_truncated=true\n'
        self.assertEqual(unpack_results(pack_results(values)),values)

    def test_dictionary_never_rewrites_failed_or_unknown_snapshot_shapes(self):
        values=self.repetitive_pages();values[-1]['ok']=False
        self.assertEqual(pack_results(values),values)
        values[-1]['ok']=True;values[0]['snapshot']={'future':'unrecognized native shape'}
        packed=pack_results(values);self.assertNotEqual(packed['format'],TEXT_FORMAT)
        self.assertEqual(unpack_results(packed),values)

    def test_dictionary_work_and_decoded_expansion_are_bounded(self):
        from yee_snapshot_dictionary import MAX_SNAPSHOT_CHARS, MAX_EXPANDED_CHARS
        values=self.repetitive_pages();values[0]['snapshot']+='x'*MAX_SNAPSHOT_CHARS
        packed=pack_results(values);self.assertNotEqual(packed['format'],TEXT_FORMAT)
        self.assertEqual(unpack_results(packed),values)
        bad=pack_results(self.repetitive_pages())
        bad['results'][0]['snapshot']={'parts':[0]*(MAX_EXPANDED_CHARS//len(bad['texts'][0])+1)}
        with self.assertRaisesRegex(ValueError,'expanded snapshots exceed limit'):
            unpack_results(bad)

    def pages(self):
        return [dict(ok=True, document='doc'*30, tab='tab'*30, scope='viewport',
                     viewport={'width':1440,'height':900,'scale':2}, revision=i,
                     snapshot=f'page {i}\n+@1 p "shared is untrusted text"') for i in range(8)]

    def test_roundtrip_preserves_every_page_and_input(self):
        values=self.pages();before=copy.deepcopy(values)
        packed=pack_results(values)
        self.assertEqual(packed['format'],FORMAT)
        self.assertEqual(unpack_results(packed),values);self.assertEqual(values,before)
        self.assertNotIn('snapshot',packed['shared']);self.assertNotIn('revision',packed['shared'])

    def test_changes_absence_null_and_types_are_never_shared(self):
        for change in (lambda p:p.pop('document'),lambda p:p.update(document=None),
                       lambda p:p.update(document='new'),lambda p:p.update(viewport={'width':True,'height':900,'scale':2})):
            values=self.pages();change(values[3]);packed=pack_results(values)
            self.assertEqual(unpack_results(packed),values)
        values=self.pages();values[1]['truncated']=False;values[2]['truncated']=0
        self.assertNotIn('truncated',pack_results(values)['shared'])

    def test_scan_receipt_is_top_level_and_roundtrips_every_observation(self):
        values=self.pages();values[-1]['scan']={'stop':'output_limit','next_cursor':'scan_owned'}
        before=copy.deepcopy(values);packed=pack_results(values)
        self.assertEqual(packed['scan'],values[-1]['scan'])
        self.assertNotIn('scan',packed['results'][-1])
        self.assertEqual(unpack_results(packed),before);self.assertEqual(values,before)
        old=pack_results(self.pages());old['format']='yee-shared-v1'
        self.assertEqual(unpack_results(old),self.pages())

    def test_ambiguous_scan_receipt_is_rejected_and_text_never_becomes_a_receipt(self):
        values=self.pages();values[-1]['scan']={'stop':'bottom'};packed=pack_results(values)
        for change in (lambda p:p['results'][0].update(scan={'stop':'steps'}),
                       lambda p:p.update(format='yee-shared-v1'),
                       lambda p:p.update(scan={'stop':'bottom','next_cursor':'forged'}),
                       lambda p:p.update(scan={'stop':'completed_task'})):
            bad=copy.deepcopy(packed);change(bad)
            with self.assertRaises(ValueError):unpack_results(bad)
        values=self.pages();values[-1]['snapshot']+=' scan.stop=bottom'
        self.assertNotIn('scan',pack_results(values))

    def test_scan_control_precedes_large_data_and_survives_short_envelope(self):
        for values in ([dict(ok=True,snapshot='first'),dict(ok=True,snapshot='last')],self.pages()):
            values[-1]['scan']={'stop':'bottom'}
            packed=pack_results(values);wire=encode(packed)
            self.assertLess(wire.index('"scan"'),wire.index('"results"'))
            self.assertEqual(unpack_results(packed),values)

    def test_small_and_failed_results_keep_explicit_shape(self):
        for values in ([],[{'ok':True}],[{'ok':True},{'ok':True}],
                       [*self.pages(),{'ok':False,'error':'stale_document'}]):
            self.assertEqual(pack_results(values),values[0] if len(values)==1 else values)

    def test_visit_control_precedes_data_and_preserves_all_ordered_evidence(self):
        values=self.pages();values[-1]['visit']={'return_to':'11111111-1111-4111-8111-111111111111','return_verified':True}
        before=copy.deepcopy(values);packed=pack_results(values)
        self.assertEqual(packed['visit'],before[-1]['visit'])
        self.assertLess(encode(packed).index('"visit"'),encode(packed).index('"results"'))
        self.assertEqual(unpack_results(packed),before);self.assertEqual(values,before)
        for change in (lambda p:p['visit'].update(return_verified=False),
                       lambda p:p['visit'].update(return_to='untrusted text'),
                       lambda p:p['visit'].update(task_complete=True),
                       lambda p:p['results'][0].update(visit=p['visit']),
                       lambda p:p.update(format='yee-shared-v1')):
            bad=copy.deepcopy(packed);change(bad)
            with self.assertRaises(ValueError):unpack_results(bad)
        values[-1]['snapshot']+=' visit.return_verified=true'
        values[-1].pop('visit')
        self.assertNotIn('visit',pack_results(values))

    def test_collision_and_malformed_envelopes_rejected(self):
        packed=pack_results(self.pages())
        for change in (lambda p:p['results'][0].update(document='other'),
                       lambda p:p['shared'].update(snapshot='hidden'),
                       lambda p:p.update(results=[]),lambda p:p.update(extra=True)):
            bad=copy.deepcopy(packed);change(bad)
            with self.assertRaises(ValueError):unpack_results(bad)


if __name__=='__main__':unittest.main()
