"""Run with the isolated mcp==2.1.1 Python environment."""
import argparse
import importlib.util
import io
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import threading
import time
import unittest
from unittest.mock import patch

from yee_browser_results import unpack_results
import anyio

spec = importlib.util.spec_from_file_location('adapter', Path(__file__).with_name('yee-browser-mcp.py'))
adapter = importlib.util.module_from_spec(spec)
spec.loader.exec_module(adapter)


class AdapterTests(unittest.TestCase):
    def link_page(self, document='doc', url='https://example.test/order'):
        page = self.small_page()
        page.update(document=document, tab='11111111-1111-4111-8111-111111111111', url=url, receipt_persisted=True,
                    snapshot=f'page @{document} rev=1 title="Page" origin="https://example.test"\n'
                    f'+@{document}_1 link "Policy" href="https://example.test/policy"\n'
                    f'+@{document}_2 link "Terms" href="https://example.test/terms"\n')
        return page

    def test_visit_links_preserves_viewports_and_native_exact_url_and_active_return(self):
        calls = []
        def native(command):
            calls.append((command.command, getattr(command, 'url', None)))
            if command.command == 'observe':return 0,self.link_page()
            if command.command == 'navigate':
                return 0,self.link_page('returned' if command.url.endswith('/order') else 'destination',command.url)
            return 0,{'ok':True,'execution_settled':True,'receipt_persisted':True,
                      'tab':self.link_page()['tab'],'tabs':[{'tab':self.link_page()['tab'],'active':True,'permission':'granted'}]}
        worker=adapter.Adapter(self.config(),native);worker.remember_observation(self.link_page())
        payload={'action':'visit-links','document':'doc','refs':['@1','2']}
        import jsonschema
        jsonschema.validate(payload,adapter.tool_schema())
        result=anyio.run(worker.call,payload);values=unpack_results(json.loads(result.content[0].text))
        self.assertFalse(result.is_error)
        self.assertEqual(calls,[('observe',None),('navigate','https://example.test/policy'),
                                ('navigate','https://example.test/terms'),('navigate','https://example.test/order'),('tabs',None)])
        self.assertEqual(len(values),5)
        self.assertTrue(values[-1]['visit']['return_verified'])
        self.assertEqual(values[-1]['link_visit'],{'visited_urls':['https://example.test/policy','https://example.test/terms'],
                                                 'return_url':'https://example.test/order','return_url_verified':True})
        self.assertEqual([v['url'] for v in values[1:4]],['https://example.test/policy','https://example.test/terms','https://example.test/order'])
        self.assertEqual(worker.links.plan('returned',['1'])[2],['https://example.test/policy'])

    def test_tab_inventory_keeps_observed_links_only_with_settled_same_active_tab(self):
        page=self.link_page();links=adapter.ObservedLinks()
        inventory={'ok':True,'execution_settled':True,'receipt_persisted':True,'tab':page['tab'],
                   'tabs':[{'tab':page['tab'],'active':True,'url':'https://example.test/cached-old-url'}]}
        links.remember(page);links.remember(inventory)
        self.assertEqual(links.plan('doc',['1'])[2],['https://example.test/policy'])
        invalid=[{'ok':False},{'execution_settled':False},{'receipt_persisted':False},
                 {'partial_effect_possible':True},{'tab':'other'},
                 {'tabs':[{'tab':'other','active':True}]},
                 {'tabs':[{'tab':page['tab'],'active':False}]},
                 {'tabs':[{'tab':page['tab'],'active':True},{'tab':'other','active':True}]}]
        for change in invalid:
            links.remember(page);links.remember({**inventory,**change})
            with self.assertRaises(ValueError):links.plan('doc',['1'])

    def test_visit_links_invalid_plans_never_dispatch_and_handles_expire(self):
        calls=[];config=self.config();config.short_documents=True
        worker=adapter.Adapter(config,lambda c:calls.append(c));worker.remember_observation(self.link_page())
        handle=worker.documents.present({'document':'doc'})['document']
        for document,refs in ((handle,[]),(handle,['1','@1']),(handle,['1']*5),(handle,['3']),
                              (handle,['2','doc_2']),(handle,[False]),('old',['1'])):
            result=anyio.run(worker.call,{'action':'visit-links','document':document,'refs':refs})
            self.assertTrue(result.is_error);self.assertEqual(calls,[])
        worker.documents.present({'document':'new'})
        self.assertTrue(anyio.run(worker.call,{'action':'visit-links','document':handle,'refs':['1']}).is_error)
        self.assertEqual(calls,[])

    def test_visit_links_href_change_during_preflight_never_navigates(self):
        calls=[]
        def native(command):
            calls.append(command.command);page=self.link_page()
            page['snapshot']=page['snapshot'].replace('/policy','/replacement')
            return 0,page
        worker=adapter.Adapter(self.config(),native);worker.remember_observation(self.link_page())
        result=anyio.run(worker.call,{'action':'visit-links','document':'doc','refs':['1']})
        self.assertTrue(result.is_error);self.assertEqual(calls,['observe'])
        self.assertNotIn('"return_verified":true',result.content[0].text)

    def test_visit_links_never_continues_after_failure_uncertainty_redirect_or_budget(self):
        for mode in ('failed','partial','unsettled','receipt','redirect','tab','truncated','budget'):
            calls=[]
            def native(command):
                calls.append(command.command)
                if command.command=='observe':return 0,self.link_page()
                page=self.link_page('destination',command.url)
                if mode=='failed':return 1,{'ok':False,'error':'user_takeover','execution_settled':True}
                if mode=='partial':page['partial_effect_possible']=True
                if mode=='unsettled':page['execution_settled']=False
                if mode=='receipt':page['receipt_persisted']=False
                if mode=='redirect':page['url']='https://example.test/redirected'
                if mode=='tab':page['tab']='22222222-2222-4222-8222-222222222222'
                if mode=='truncated':page['truncated']=True
                if mode=='budget':page['snapshot']+='x'*adapter.MAX_SCAN_OUTPUT_BYTES
                return 0,page
            worker=adapter.Adapter(self.config(),native);worker.remember_observation(self.link_page())
            result=anyio.run(worker.call,{'action':'visit-links','document':'doc','refs':['1','2']})
            self.assertTrue(result.is_error,mode);self.assertEqual(calls,['observe','navigate'],mode)
            self.assertNotIn('"return_verified":true',result.content[0].text)

    def test_visit_links_unverified_final_inventory_cannot_claim_return(self):
        calls=[]
        def native(command):
            calls.append(command.command)
            if command.command=='observe':return 0,self.link_page()
            if command.command=='navigate':return 0,self.link_page('new',command.url)
            return 0,{**self.link_page('new'),'tabs':[{'tab':'22222222-2222-4222-8222-222222222222','active':True}]}
        worker=adapter.Adapter(self.config(),native);worker.remember_observation(self.link_page())
        result=anyio.run(worker.call,{'action':'visit-links','document':'doc','refs':['1']})
        self.assertTrue(result.is_error);self.assertEqual(calls,['observe','navigate','navigate','tabs'])
        self.assertNotIn('"return_verified":true',result.content[0].text)

    def test_visit_links_cancellation_never_dispatches_the_next_destination_or_return(self):
        calls=[];started=threading.Event();release=threading.Event()
        def native(command):
            calls.append(command.command)
            if command.command=='observe':return 0,self.link_page()
            started.set();release.wait(3)
            return 0,self.link_page('destination',command.url)
        worker=adapter.Adapter(self.config(),native);worker.remember_observation(self.link_page())
        async def task():
            async with anyio.create_task_group() as group:
                group.start_soon(worker.call,{'action':'visit-links','document':'doc','refs':['1','2']})
                while not started.is_set():await anyio.sleep(.01)
                group.cancel_scope.cancel();release.set()
        anyio.run(task)
        self.assertEqual(calls,['observe','navigate'])
        self.assertEqual(worker.stopped_reason,'client_cancelled')

    def test_visit_links_short_document_handles_return_only_the_fresh_capability(self):
        config=self.config();config.short_documents=True
        def native(command):
            if command.command=='observe':return 0,self.link_page()
            if command.command=='navigate':return 0,self.link_page('returned' if command.url.endswith('/order') else 'destination',command.url)
            return 0,{'ok':True,'execution_settled':True,'receipt_persisted':True,
                      'tab':self.link_page()['tab'],'tabs':[{'tab':self.link_page()['tab'],'active':True}]}
        worker=adapter.Adapter(config,native);worker.remember_observation(self.link_page())
        old=worker.documents.present({'document':'doc'})['document']
        result=anyio.run(worker.call,{'action':'visit-links','document':old,'refs':['1']})
        values=unpack_results(json.loads(result.content[0].text));self.assertFalse(result.is_error)
        self.assertEqual(worker.documents.expand(values[-2]['document']),'returned')
        self.assertEqual(worker.links.plan('returned',['1'])[2],['https://example.test/policy'])
        with self.assertRaises(ValueError):worker.documents.expand(old)

    def test_observed_link_cache_tracks_native_deltas_and_ignores_page_instructions(self):
        links=adapter.ObservedLinks();page=self.link_page();links.remember(page)
        self.assertEqual(links.plan('doc',['1'])[2],['https://example.test/policy'])
        delta={**page,'snapshot':'page @doc rev=2 title="Page" origin="https://example.test" delta\nbase_rev=1\n'
               '-@doc_1\n~@doc_2 text " href=\\"https://example.test/malicious\\""\n'}
        links.remember(delta)
        with self.assertRaises(ValueError):links.plan('doc',['1'])
        with self.assertRaises(ValueError):links.plan('doc',['2'])
        for href in ('https://other.test/policy','javascript:alert(1)','https://user:secret@example.test/policy'):
            bad={**page,'snapshot':page['snapshot'].replace('https://example.test/policy',href)}
            links.remember(bad)
            with self.assertRaises(ValueError):links.plan('doc',['1'])
        for suffix in (' href_truncated',' disabled'):
            bad={**page,'snapshot':page['snapshot'].replace('/policy"','/policy"'+suffix)}
            links.remember(bad)
            with self.assertRaises(ValueError):links.plan('doc',['1'])
        links.remember(page);links.remember({**delta,'url':'https://example.test/changed'})
        with self.assertRaises(ValueError):links.plan('doc',['1'])

    def test_observed_links_preserve_explicit_zero_port_origin_boundaries(self):
        links=adapter.ObservedLinks();page=self.link_page()
        page['snapshot']=page['snapshot'].replace('/policy"',':0/policy"')
        links.remember(page)
        with self.assertRaises(ValueError):links.plan('doc',['1'])
        page=self.link_page(url='https://example.test:0/order')
        page['snapshot']=page['snapshot'].replace('https://example.test/', 'https://example.test:0/')
        links.remember(page)
        self.assertEqual(links.plan('doc',['1'])[2],['https://example.test:0/policy'])

    def test_readonly_inventory_preserves_file_viewport_and_scan_continuation(self):
        page=self.link_page(url='file:///tmp/owned-fixture.html')
        page['scroll']={'y':0,'max_y':2800,'can_scroll_down':True,'can_scroll_up':False}
        calls=[]
        inventory={'ok':True,'execution_settled':True,'receipt_persisted':True,'tab':page['tab'],
                   'tabs':[{'tab':page['tab'],'active':True,'url':'file:///tmp/old-consent.html'}]}
        def native(command):
            calls.append(command.command)
            if command.command=='observe':return 0,page
            if command.command=='tabs':return 0,inventory
            self.assertEqual(command.command,'scroll');self.assertEqual(command.document,'doc')
            return 0,{**page,'scroll':{**page['scroll'],'y':700}}
        worker=adapter.Adapter(self.config(),native)
        self.assertFalse(anyio.run(worker.call,{'action':'observe','full':True}).is_error)
        cursor=worker.issue_scan_cursor();position=worker.position;generation=worker.position_generation;state=worker.full_state
        self.assertFalse(anyio.run(worker.call,{'action':'tabs'}).is_error)
        self.assertEqual(worker.position,position);self.assertEqual(worker.position_generation,generation)
        self.assertEqual(worker.full_state,state)
        self.assertFalse(anyio.run(worker.call,{'action':'scan','cursor':cursor,'steps':1}).is_error)
        self.assertEqual(calls,['observe','tabs','scroll'])

    def test_changed_or_uncertain_inventory_invalidates_viewport_continuations(self):
        page=self.link_page();calls=[]
        inventory={'ok':True,'execution_settled':True,'receipt_persisted':True,'tab':page['tab'],
                   'tabs':[{'tab':page['tab'],'active':True}]}
        for change in ({'tabs':[{'tab':'other','active':True}]},{'execution_settled':False},
                       {'receipt_persisted':False},{'partial_effect_possible':True}):
            calls.clear()
            def native(command):
                calls.append(command.command)
                return 0,page if command.command=='observe' else {**inventory,**change}
            worker=adapter.Adapter(self.config(),native)
            anyio.run(worker.call,{'action':'observe','full':True});cursor=worker.issue_scan_cursor()
            anyio.run(worker.call,{'action':'tabs'})
            self.assertTrue(anyio.run(worker.call,{'action':'scan','cursor':cursor,'steps':1}).is_error)
            self.assertEqual(calls,['observe','tabs'])

    def test_batch_schema_rejects_ambiguous_or_missing_targets(self):
        import jsonschema
        for item in ({'action':'click', 'name':'Next', 'ref':'@1'}, {'action':'click'}):
            with self.assertRaises(jsonschema.ValidationError):
                jsonschema.validate({'action':'batch', 'batch':[item]}, adapter.tool_schema())
        for item in ({'action':'click', 'name':'Next'}, {'action':'click', 'ref':'@1'}):
            jsonschema.validate({'action':'batch', 'batch':[item]}, adapter.tool_schema())

    def test_batch_mixed_target_error_identifies_safe_repair_without_dispatch(self):
        for document in (None, 'observed-document'):
            calls=[]
            payload={'action':'batch', 'batch':[{'action':'fill', 'name':'Company',
                                               'ref':'@5', 'value':'Private supplied value'}]}
            if document is not None:
                payload['document']=document
            result=anyio.run(adapter.Adapter(self.config(), lambda c: calls.append(c)).call, payload)
            error=json.loads(result.content[0].text)
            self.assertTrue(result.is_error)
            self.assertFalse(error['native_dispatched'])
            self.assertEqual(calls, [])
            target='ref' if document is not None else 'name'
            self.assertEqual(error['target'], target)
            self.assertEqual(error['item_index'], 0)
            self.assertEqual(error['missing'], [])
            self.assertEqual(error['unexpected'], ['name' if target=='ref' else 'ref'])
            self.assertEqual(set(error['example']), {'action', target, 'value'})
            self.assertNotIn('Private supplied value', json.dumps(error))

    def test_cursor_schema_rejects_mixed_or_non_scan_capabilities(self):
        import jsonschema
        for payload in ({'action':'scan','document':'doc','cursor':'scan_owned'},
                        {'action':'observe','cursor':'scan_owned'}):
            with self.assertRaises(jsonschema.ValidationError):jsonschema.validate(payload,adapter.tool_schema())

    def test_model_schema_binds_document_to_actions_that_accept_it(self):
        import jsonschema
        schema = adapter.tool_schema()
        for action in adapter.DIRECT_FIELDS:
            example = adapter.action_example(action)
            jsonschema.validate(example, schema)
            required, optional = adapter.DIRECT_FIELDS[action]
            with_document = {**example, 'document': 'observed-document'}
            if 'document' in required | optional:
                jsonschema.validate(with_document, schema)
            else:
                with self.assertRaises(jsonschema.ValidationError):
                    jsonschema.validate(with_document, schema)
        for payload in (
            {'action': 'batch', 'document': 'doc', 'batch': [{'action': 'click', 'ref': '1'}]},
            {'action': 'batch', 'batch': [{'action': 'fill', 'name': 'Name', 'value': 'Literal'}]},
            {'action': 'scan', 'cursor': 'connection-local-cursor'},
        ):
            jsonschema.validate(payload, schema)

    def test_served_guidance_fits_host_card_with_all_stop_and_expiry_rules(self):
        for short_documents in (False, True):
            tool = adapter.served_tool(short_documents)
            self.assertLessEqual(len(tool.description.encode('utf-8')), 2033)
            for instruction in ('url_truncated/url_credentials_redacted',
                                'excluded from tabs', 'recover same request',
                                'complete surface', 'no JavaScript/shell runtime',
                                'call visit-links(document,refs) directly without tabs or return_to',
                                'prefer one batch',
                                'task-relevant questions',
                                'user_cancelled/user_takeover/session_stopped',
                                'native permission/target/settlement/expiry'):
                self.assertIn(instruction, tool.description)
            if short_documents:
                self.assertIn('MCP reconnection', tool.description)

    def test_current_browser_url_survives_compaction_and_unchanged_node_delta(self):
        worker = adapter.Adapter(self.config(), lambda command: (0, current))
        current = {**self.small_page(), 'url': 'https://example.test/order?view=1#policy'}
        first = anyio.run(worker.call, {'action': 'observe'})
        self.assertEqual(json.loads(first.content[0].text)['url'], current['url'])
        current = {**current, 'url': 'https://example.test/order?view=2#policy',
                   'snapshot': 'page @doc rev=2 title="Page" origin="https://example.test" delta\nbase_rev=1\n',
                   'url_truncated': True, 'url_credentials_redacted': True}
        result = anyio.run(worker.call, {'action': 'observe'})
        shown = json.loads(result.content[0].text)
        for key in ('url', 'url_truncated', 'url_credentials_redacted'):
            self.assertEqual(shown[key], current[key])

    def test_changed_location_invalidates_unchanged_content_comparison(self):
        for changed in ({'url': 'https://example.test/order?view=2'},
                        {'url_truncated': True}, {'url_credentials_redacted': True}):
            worker = adapter.Adapter(self.config(), lambda command: None)
            page = {**self.small_page(), 'url': 'https://example.test/order?view=1'}
            worker.remember_observation(page)
            self.assertIsNotNone(worker.full_state)
            delta = {**self.small_page(rev=2, delta=True), **page}
            delta['snapshot'] = self.small_page(rev=2, delta=True)['snapshot']
            self.assertTrue(worker.remember_observation(delta))
            self.assertIsNotNone(worker.full_state)
            worker.remember_observation({**delta, **changed})
            self.assertIsNone(worker.full_state)

    def test_successful_cancel_and_stopped_status_latch_before_following_read(self):
        for action,response in (('cancel',{'ok':True}),
                                ('status',{'ok':True,'session_stopped':True,'reason':'client_cancelled'})):
            calls=[]
            def native(command):calls.append(command.command);return 0,response
            worker=adapter.Adapter(self.config(),native)
            anyio.run(worker.call,{'action':action})
            result=anyio.run(worker.call,{'action':'observe'})
            self.assertTrue(result.is_error);self.assertEqual(calls,[action])

    def test_completed_receipt_reconnection_has_no_action_or_value_copy(self):
        with tempfile.TemporaryDirectory() as d:
            root=Path(d).resolve();config=self.config();config.bridge=str(root)
            first=adapter.Adapter(config)
            first.native_started({'id':'completed-original','command':'observe','value':'not copied'})
            reference=json.loads((root/'mcp-last-native.json').read_text())
            self.assertEqual(reference,{'id':'completed-original','command':'observe'})
            self.assertEqual((root/'mcp-last-native.json').stat().st_mode & 0o777,0o600)
            archive=root/('native-request-'+reference['id'].encode().hex().upper()+'.result')
            response=dict(id=reference['id'],ok=False,error='stale_document',
                          execution_settled=True,receipt_persisted=True)
            archive.write_text(json.dumps(response))
            result=anyio.run(adapter.Adapter(config).call,{'action':'recover'})
            value=json.loads(result.content[0].text)
            self.assertTrue(result.is_error);self.assertEqual(value['recovery'],{'action':'attach'})
            self.assertFalse((root/'request.json').exists())
            self.assertFalse((root/'client-pending.json').exists())

    def test_eof_cancellation_preserves_native_session_for_recovery(self):
        with tempfile.TemporaryDirectory() as d:
            config=self.config();config.bridge=str(Path(d).resolve())
            started=threading.Event();release=threading.Event();calls=[]
            def native(command,**kwargs):
                calls.append(command.command);command._native_request_started({'id':'original-request','command':command.command})
                started.set();release.wait(3)
                return 0,dict(ok=True,execution_settled=True)
            with patch.object(adapter.cli,'run',side_effect=native):
                worker=adapter.Adapter(config)
                async def task():
                    token=worker.explicit_cancel.set(anyio.Event())
                    try:
                        async with anyio.create_task_group() as group:
                            group.start_soon(worker.call,{'commands':[['status'],['status']]})
                            while not started.is_set():await anyio.sleep(.01)
                            group.cancel_scope.cancel();release.set()
                    finally:worker.explicit_cancel.reset(token)
                anyio.run(task)
            self.assertEqual(calls,['status']);self.assertIsNone(worker.stopped_reason)
            self.assertFalse((Path(d)/'client-stop.json').exists())

    def test_wire_cancel_intent_is_bound_only_to_active_request(self):
        async def task():
            event=anyio.Event();active={7:event}
            for value in (
                {'method':'notifications/cancelled','params':{'requestId':8}},
                {'method':'notifications/cancelled','params':{'requestId':True}},
                {'id':9,'method':'notifications/cancelled','params':{'requestId':7}},
                {'method':'ping','params':{'requestId':7}},
            ):
                await adapter.BoundedInput(io.BytesIO((json.dumps(value)+'\n').encode()),active).__anext__()
                self.assertFalse(event.is_set());self.assertEqual(set(active),{7})
            value={'method':'notifications/cancelled','params':{'requestId':7}}
            await adapter.BoundedInput(io.BytesIO((json.dumps(value)+'\n').encode()),active).__anext__()
            self.assertTrue(event.is_set())
        anyio.run(task)

    def test_recovered_auth_invalidation_suggests_consent_but_never_mutation(self):
        response=dict(error='stale_document',execution_settled=True)
        for original,partial in (('ask',False),('observe',False),('fill',False),('click',True)):
            command=argparse.Namespace(command='recover',_recovered_command=original)
            shown=adapter.Adapter.add_native_recovery(command,{**response,'partial_effect_possible':partial},response)
            self.assertEqual('recovery' in shown,original in ('ask','observe') and not partial)

    def test_transport_cancellation_publishes_native_stop_before_worker_settles(self):
        with tempfile.TemporaryDirectory() as d:
            root=Path(d).resolve();config=self.config();config.bridge=str(root)
            started=threading.Event();calls=[]
            def native(command,**kwargs):
                calls.append(command.command)
                command._native_request_started({'id':'owned-native-request','command':command.command})
                started.set();deadline=time.monotonic()+3
                while not (root/'client-stop.json').exists() and time.monotonic()<deadline:time.sleep(.01)
                self.assertTrue((root/'client-stop.json').exists())
                return 1,dict(ok=False,error='client_cancelled',execution_settled=True)
            with patch.object(adapter.cli,'run',side_effect=native):
                worker=adapter.Adapter(config)
                async def task():
                    async with anyio.create_task_group() as group:
                        group.start_soon(worker.call,{'commands':[['status'],['status']]})
                        while not started.is_set():await anyio.sleep(.01)
                        group.cancel_scope.cancel()
                anyio.run(task)
            self.assertEqual(calls,['status'])
            self.assertEqual(json.loads((root/'client-stop.json').read_text()),
                             {'id':'owned-native-request','reason':'client_cancelled'})
            self.assertEqual(worker.stopped_reason,'client_cancelled')

    def test_worker_failure_does_not_become_user_cancellation(self):
        with tempfile.TemporaryDirectory() as d:
            config=self.config();config.bridge=str(Path(d).resolve())
            with patch.object(adapter.cli,'run',side_effect=adapter.cli.BridgeError('controlled native failure')):
                worker=adapter.Adapter(config);result=anyio.run(worker.call,{'action':'status'})
            self.assertTrue(result.is_error);self.assertIsNone(worker.stopped_reason)
            self.assertFalse((Path(d)/'client-stop.json').exists())

    def test_model_schema_is_flat_bounded_and_omits_legacy_coordinates(self):
        schema=adapter.TOOL.input_schema
        encoded=json.dumps(schema,separators=(',',':'),ensure_ascii=False).encode('utf-8')
        # The bounded visit-links action and cursor preflight add catalog fields.
        # Keep a small explicit budget without weakening their constraints.
        self.assertLess(len(encoded),2000)
        self.assertEqual(schema['required'],['action'])
        self.assertFalse({'commands','from_y'} & set(schema['properties']))
        self.assertIn('cursor',schema['properties'])
        self.assertNotIn('oneOf',schema)
        self.assertIn('truncated=false',adapter.TOOL.description)
        self.assertNotIn('do not read those refs',adapter.TOOL.description)
        self.assertIn('read required values only when absent or truncated',adapter.TOOL.description.lower())
        self.assertIn('do not repeat tabs for that proof',adapter.TOOL.description)

    def test_compound_action_fields_are_bound_in_the_public_schema(self):
        import jsonschema
        schema=adapter.tool_schema()
        valid=(
            {'action':'visit-links','document':'doc','refs':['1']},
            {'action':'visit-tabs','tabs':['other'],'return_to':'original'},
            {'action':'batch','batch':[{'action':'click','name':'Save'}]},
        )
        for payload in valid:
            jsonschema.validate(payload,schema)
        invalid=(
            {'action':'visit-links','document':'doc','refs':['1'],'return_to':'original'},
            {'action':'visit-tabs','tabs':['other'],'return_to':'original','refs':['1']},
            {'action':'batch','batch':[{'action':'click','name':'Save'}],'tabs':['other']},
        )
        for payload in invalid:
            with self.assertRaises(jsonschema.ValidationError):
                jsonschema.validate(payload,schema)

    def test_every_compound_action_reports_missing_and_unexpected_fields_without_dispatch(self):
        for action in ('visit-tabs','visit-links','batch'):
            required,optional=adapter.ACTION_FIELDS[action]
            example=adapter.action_example(action)
            self.assertTrue(required <= set(example))
            self.assertTrue(set(example) <= required | optional)
            for payload in ({**example,'question':'unexpected'},
                            *[{k:v for k,v in example.items() if k != missing}
                              for missing in required if missing != 'action']):
                calls=[];worker=adapter.Adapter(self.config(),lambda c:calls.append(c))
                result=anyio.run(worker.call,payload)
                self.assertTrue(result.is_error);self.assertEqual(calls,[])
                error=json.loads(result.content[0].text)
                self.assertEqual(error['error_code'],'invalid_action_fields')
                self.assertEqual(error['example'],example)

    def test_every_direct_action_reports_missing_and_unexpected_fields_without_exception(self):
        for action,(required,optional) in adapter.DIRECT_FIELDS.items():
            example=adapter.action_example(action)
            self.assertTrue(required <= set(example))
            self.assertTrue(set(example) <= required | optional)
            for payload in ({**example,'unknown_option':True},
                            *[{k:v for k,v in example.items() if k != missing}
                              for missing in required if missing != 'action']):
                calls=[];worker=adapter.Adapter(self.config(),lambda c:calls.append(c))
                result=anyio.run(worker.call,payload)
                self.assertTrue(result.is_error);self.assertEqual(calls,[])
                error=json.loads(result.content[0].text)
                self.assertEqual(error['error_code'],'invalid_action_fields')
                self.assertFalse(error['native_dispatched'])
                self.assertEqual(error['example'],example)
        self.assertEqual(adapter.action_example('wait-change')['wait_ms'],1000)

    def test_readonly_full_option_reaches_native_with_scope_and_wait_mode_intact(self):
        for action,extra in (('scroll',{'direction':'down','pages':2}),
                             ('wait-change',{'wait_ms':30000,'content':True})):
            for full in (False,True):
                calls=[]
                def execute(command):
                    calls.append(command)
                    request=adapter.cli.request_for(command,{'document':'doc','revision':7})
                    self.assertEqual(request.get('full',False),full)
                    self.assertEqual(request['document'],'doc')
                    if action=='wait-change':self.assertEqual((request['wait_ms'],request['wait_mode']),(30000,'content'))
                    else:self.assertEqual((request['direction'],request['pages']),('down',2))
                    return 0,self.small_page()
                worker=adapter.Adapter(self.config(),execute)
                result=anyio.run(worker.call,{'action':action,'document':'doc','full':full,**extra})
                self.assertFalse(result.is_error);self.assertEqual(len(calls),1)

    def test_visit_tabs_returns_all_viewports_and_original_inventory_in_order(self):
        import jsonschema
        ids=['11111111-1111-4111-8111-111111111111','22222222-2222-4222-8222-222222222222','33333333-3333-4333-8333-333333333333']
        payload={'action':'visit-tabs','tabs':ids[1:],'return_to':ids[0],'content':True}
        jsonschema.validate(payload,adapter.TOOL.input_schema)
        calls=[];active=ids[0]
        def execute(command):
            nonlocal active
            calls.append(command.command)
            if command.command=='tabs':return 0,{'ok':True,'execution_settled':True,'receipt_persisted':True,'tabs':[{'tab':tab,'active':tab==active,'permission':'granted'} for tab in ids]}
            self.assertEqual(command.command,'select-tab');active=command.tab
            return 0,{**self.small_page(text=active),'tab':active,'execution_settled':True,'receipt_persisted':True}
        worker=adapter.Adapter(self.config(),execute);worker.observed=True
        result=anyio.run(worker.call,payload);values=unpack_results(json.loads(result.content[0].text))
        self.assertFalse(result.is_error);self.assertEqual(calls,['tabs','select-tab','select-tab','select-tab','tabs'])
        self.assertEqual([v['tab'] for v in values[1:-1]],[*ids[1:],ids[0]])
        self.assertEqual(active,ids[0]);self.assertEqual(values[-1]['tabs'][0]['active'],True)
        self.assertEqual(values[-1]['visit'],{'return_to':ids[0],'return_verified':True})
        wire=result.content[0].text
        self.assertLess(wire.index('"visit"'),wire.index('"results"'))

    def test_visit_tabs_rejects_false_content_without_native_dispatch(self):
        calls=[]
        result=anyio.run(adapter.Adapter(self.config(),lambda c:calls.append(c)).call,{
            'action':'visit-tabs',
            'tabs':['22222222-2222-4222-8222-222222222222'],
            'return_to':'11111111-1111-4111-8111-111111111111',
            'content':False,
        })
        self.assertTrue(result.is_error)
        self.assertEqual(calls,[])

    def test_visit_receipt_requires_every_native_result_to_be_settled_and_persisted(self):
        ids=['11111111-1111-4111-8111-111111111111','22222222-2222-4222-8222-222222222222']
        for missing in ('execution_settled','receipt_persisted'):
            calls=[];active=ids[0]
            def execute(command):
                nonlocal active
                calls.append(command.command)
                response={'ok':True,'execution_settled':True,'receipt_persisted':True}
                if command.command=='tabs':response['tabs']=[{'tab':t,'active':t==active,'permission':'granted'} for t in ids]
                else:active=command.tab;response.update(self.small_page(text=active),tab=active)
                if len(calls)==2:response[missing]=False
                return 0,response
            worker=adapter.Adapter(self.config(),execute);worker.observed=True
            result=anyio.run(worker.call,{'action':'visit-tabs','tabs':ids[1:],'return_to':ids[0]})
            self.assertFalse(result.is_error)
            self.assertFalse(any('visit' in v for v in unpack_results(json.loads(result.content[0].text))))

    def test_visit_tabs_fails_before_selection_for_unknown_ungranted_or_wrong_origin_tab(self):
        ids=['11111111-1111-4111-8111-111111111111','22222222-2222-4222-8222-222222222222']
        for failure in ('unknown','ungranted','active_changed','duplicate','malformed','not_ok'):
            calls=[]
            def execute(command):
                calls.append(command.command)
                tabs=[{'tab':t,'active':i==0,'permission':'granted'} for i,t in enumerate(ids)]
                if failure=='unknown':tabs.pop()
                if failure=='ungranted':tabs[-1]['permission']='denied'
                if failure=='active_changed':tabs[0]['active']=False;tabs[1]['active']=True
                if failure=='duplicate':tabs.append(dict(tabs[-1]))
                if failure=='malformed':tabs[-1]['tab']=[]
                return 0,{'ok':failure!='not_ok','tabs':tabs}
            worker=adapter.Adapter(self.config(),execute);worker.observed=True
            result=anyio.run(worker.call,{'action':'visit-tabs','tabs':ids[1:],'return_to':ids[0]})
            self.assertTrue(result.is_error);self.assertEqual(calls,['tabs'])

    def test_visit_tabs_stops_on_native_failure_and_checks_final_selection(self):
        ids=['11111111-1111-4111-8111-111111111111','22222222-2222-4222-8222-222222222222','33333333-3333-4333-8333-333333333333']
        for fail_at in (2,3,5):
            calls=[]
            def execute(command):
                calls.append(command.command)
                if len(calls)==fail_at and fail_at in (2,3):return 1,{'ok':False,'error':'permission_denied','execution_settled':True}
                if command.command=='tabs':return 0,{'ok':True,'tabs':[{'tab':t,'active':i==(1 if len(calls)==5 else 0),'permission':'granted'} for i,t in enumerate(ids)]}
                return 0,self.small_page(text=command.tab)
            worker=adapter.Adapter(self.config(),execute);worker.observed=True
            result=anyio.run(worker.call,{'action':'visit-tabs','tabs':ids[1:],'return_to':ids[0]})
            self.assertTrue(result.is_error);self.assertEqual(len(calls),fail_at)
            self.assertNotIn('"return_verified":true',result.content[0].text)

    def test_visit_tabs_invalid_plan_never_dispatches_and_verb_first_alias_expands(self):
        ids=['11111111-1111-4111-8111-111111111111','22222222-2222-4222-8222-222222222222']
        for tabs,original in (([],ids[0]),([ids[1]]*2,ids[0]),([ids[0]],ids[0]),([False],ids[0]),([ids[1]],'guessed')):
            calls=[]
            worker=adapter.Adapter(self.config(),lambda c:calls.append(c))
            self.assertTrue(anyio.run(worker.call,{'action':'visit-tabs','tabs':tabs,'return_to':original}).is_error)
            self.assertEqual(calls,[])
        config=self.config();config.short_documents=True;calls=[]
        worker=adapter.Adapter(config,lambda c:(calls.append(c) or (0,{'ok':True,'text':'literal'})))
        alias=worker.documents.present({'document':'doc'})['document']
        result=anyio.run(worker.call,{'commands':[['read','--document',alias,'4']]})
        self.assertFalse(result.is_error);self.assertEqual(calls[0].document,'doc')

    def test_checked_batch_boolean_schema_and_native_payload(self):
        import jsonschema
        for target in ('ref','name'):
            payload={'action':'batch','batch':[{'action':'fill',target:'1','value':'가'*1333+'x'},
                                               {'action':'check',target:'2','checked':False},
                                               {'action':'click',target:'3'}]}
            if target=='ref':payload['document']='doc'
            jsonschema.validate(payload,adapter.TOOL.input_schema)
            calls=[]
            def execute(command):
                calls.append(command)
                return 0,{'ok':True,'execution_settled':True}
            result=anyio.run(adapter.Adapter(self.config(),execute).call,payload)
            self.assertFalse(result.is_error)
            plan=json.loads(calls[0].actions_json if target=='ref' else calls[0].actions_json)
            self.assertEqual(plan[1],{'command':'check','ref':'2','checked':False} if target=='ref' else ['check','2',False])
            for invalid in (1,'true',None):
                payload['batch'][1]['checked']=invalid;calls.clear()
                self.assertTrue(anyio.run(adapter.Adapter(self.config(),execute).call,payload).is_error)
                self.assertEqual(calls,[])

    def test_scan_default_covers_long_document_and_validates_progress_before_dispatch(self):
        calls=[]
        def execute(command):
            calls.append(command.command)
            page=self.small_page(len(calls),f'Part {len(calls)}')
            page['scroll']={'y':len(calls)*100,'max_y':900,'can_scroll_down':len(calls)<9}
            return 0,page
        worker=adapter.Adapter(self.config(),execute)
        start=self.small_page();start['scroll']={'y':0}
        worker.remember_observation(start)
        for payload in ({'action':'scan','document':'wrong'},
                        {'action':'scan','document':'doc','cursor':'scan_bad'},
                        {'action':'scan'}):
            self.assertTrue(anyio.run(worker.call,payload).is_error)
            self.assertEqual(calls,[])
        result=anyio.run(worker.call,{'action':'scan','document':'doc'})
        self.assertFalse(result.is_error)
        values=unpack_results(json.loads(result.content[0].text))
        self.assertEqual(len(values),9)
        for i,value in enumerate(values,1): self.assertIn(f'Part {i}',value['snapshot'])
        self.assertEqual(values[-1]['scan'],{'stop':'bottom'})

    def test_lossless_dictionary_scan_uses_wire_budget_and_preserves_every_receipt(self):
        calls=[];native=[]
        phrase='Published handling conditions remain part of the complete displayed policy. '
        def execute(command):
            self.assertEqual(command.command,'scroll');calls.append(command)
            page=self.small_page(len(calls))
            page['receipt_persisted']=True
            page['scroll']={'y':len(calls)*100,'can_scroll_down':len(calls)<9}
            page['snapshot']=('page @doc rev='+str(len(calls))+'\n'+''.join(
                f'+@doc_{len(calls)*10+i} text "[/#part-{len(calls)}-{i}] {phrase*3}"\n'
                for i in range(10)))
            native.append(page)
            return 0,page
        worker=adapter.Adapter(self.config(),execute)
        start=self.small_page();start['scroll']={'y':0};worker.remember_observation(start)
        result=anyio.run(worker.call,{'action':'scan','document':'doc'})
        self.assertFalse(result.is_error);self.assertEqual(len(calls),9)
        packed=json.loads(result.content[0].text)
        self.assertEqual(packed['format'],'yee-shared-v3')
        self.assertLess(len(result.content[0].text.encode()),adapter.MAX_SCAN_OUTPUT_BYTES)
        values=unpack_results(packed);self.assertEqual(values[-1]['scan'],{'stop':'bottom'})
        for shown,original in zip(values,native):
            self.assertEqual(shown['snapshot'],adapter.cli.compact_response(original)['snapshot'])
            self.assertEqual(shown['scroll'],original['scroll'])
            self.assertIs(shown['receipt_persisted'],True)
            self.assertEqual(shown['revision'],original['revision'])

    def test_scan_stops_at_stall_and_output_budget_without_losing_last_result(self):
        for reason in ('stalled','output_limit'):
            calls=[]
            def execute(command):
                calls.append(command.command)
                page=self.small_page(len(calls),'Visible '+('x'*7000 if reason=='output_limit' else 'same'))
                page['scroll']={'y':len(calls)*100 if reason=='output_limit' else 0,'can_scroll_down':True}
                return 0,page
            worker=adapter.Adapter(self.config(),execute)
            start=self.small_page();start['scroll']={'y':0};worker.remember_observation(start)
            result=anyio.run(worker.call,{'action':'scan','document':'doc'})
            values=unpack_results(json.loads(result.content[0].text))
            if isinstance(values,dict):values=[values]
            self.assertEqual(len(calls),1 if reason=='stalled' else 2)
            self.assertEqual(values[-1]['scan']['stop'],reason)
            self.assertIn('Visible',values[-1]['snapshot'])
            if reason == 'output_limit':
                self.assertRegex(values[-1]['scan']['next_cursor'], r'^scan_[0-9a-f]{32}$')

    def test_scan_cursor_is_opaque_single_latest_continuation(self):
        calls=[]
        def execute(command):
            calls.append(command.command)
            page=self.small_page(len(calls),'Chunk '+str(len(calls))+' '+('x'*7000))
            page['scroll']={'y':len(calls)*100,'can_scroll_down':True}
            return 0,page
        worker=adapter.Adapter(self.config(),execute)
        start=self.small_page();start['scroll']={'y':0};worker.remember_observation(start)
        first=anyio.run(worker.call,{'action':'scan','document':'doc'})
        first_values=unpack_results(json.loads(first.content[0].text))
        cursor=first_values[-1]['scan']['next_cursor']
        self.assertLess(len(first.content[0].text.encode('utf-8')),20*1024)
        self.assertNotIn('next_from_y',first.content[0].text)
        second=anyio.run(worker.call,{'action':'scan','cursor':cursor})
        second_values=unpack_results(json.loads(second.content[0].text))
        latest=second_values[-1]['scan']['next_cursor']
        before=len(calls)
        expired=anyio.run(worker.call,{'action':'scan','cursor':cursor})
        self.assertTrue(expired.is_error)
        self.assertIn('expired scan cursor',expired.content[0].text)
        expired_payload=unpack_results(json.loads(expired.content[0].text))
        self.assertEqual(expired_payload['error_code'],'expired_scan_cursor')
        self.assertEqual(expired_payload['recovery'],{'action':'scan','document':'doc'})
        self.assertEqual(len(calls),before)
        same_position=self.small_page(99,'Unrelated fresh observation')
        same_position['scroll']={'y':before*100,'can_scroll_down':True}
        worker.remember_observation(same_position)
        expired=anyio.run(worker.call,{'action':'scan','cursor':latest})
        self.assertTrue(expired.is_error)
        self.assertEqual(len(calls),before)

    def test_invalid_direct_fields_explain_exact_repair_without_dispatch(self):
        calls=[]
        result=anyio.run(adapter.Adapter(self.config(),calls.append).call,
                         {'action':'fill','document':'doc','ref':'1','extra':'x'})
        self.assertTrue(result.is_error);self.assertEqual(calls,[])
        payload=unpack_results(json.loads(result.content[0].text))
        self.assertEqual(payload['error'],'invalid fields for action fill')
        self.assertEqual(payload['error_code'],'invalid_action_fields')
        self.assertIs(payload['native_dispatched'],False)
        self.assertEqual(payload['missing'],['value'])
        self.assertEqual(payload['unexpected'],['extra'])
        self.assertEqual(payload['optional'],['full'])
        self.assertEqual(payload['example'],{
            'action':'fill','document':'RETURNED_DOCUMENT',
            'ref':'RETURNED_REF','value':'VALUE'})

    def test_scan_selector_repair_preserves_only_live_cursor_without_dispatch(self):
        calls=[]
        worker=adapter.Adapter(self.config(),calls.append)
        page=self.small_page();page['scroll']={'y':0,'can_scroll_down':True}
        worker.remember_observation(page)
        cursor=worker.issue_scan_cursor()
        result=anyio.run(worker.call,{'action':'scan','document':'wrong','cursor':cursor})
        payload=unpack_results(json.loads(result.content[0].text))
        self.assertTrue(result.is_error);self.assertEqual(calls,[])
        self.assertIs(payload['native_dispatched'],False)
        self.assertEqual(payload['error_code'],'invalid_scan_selector')
        self.assertEqual(payload['recovery'],{'action':'scan','cursor':cursor})
        self.assertIn(cursor,worker.scan_cursors)
        for bad in ('expired', [], None):
            result=anyio.run(worker.call,{'action':'scan','document':'wrong','cursor':bad})
            payload=unpack_results(json.loads(result.content[0].text))
            self.assertEqual(payload['recovery'],{'action':'scan','document':'doc'})
            self.assertEqual(calls,[])

    def test_missing_scan_selector_recovers_without_inventing_document(self):
        calls=[]
        result=anyio.run(adapter.Adapter(self.config(),calls.append).call,{'action':'scan'})
        payload=unpack_results(json.loads(result.content[0].text))
        self.assertEqual(payload['error_code'],'invalid_scan_selector')
        self.assertEqual(payload['recovery'],{'action':'observe','full':True})
        self.assertIs(payload['native_dispatched'],False);self.assertEqual(calls,[])

    def test_unknown_read_reference_suggests_fresh_viewport_only_after_settlement(self):
        response={'ok':False,'error':'unknown_reference','execution_settled':True,
                  'receipt_persisted':True}
        for command in ('read','click','fill','recover'):
            shown=adapter.Adapter.add_native_recovery(argparse.Namespace(command=command),response,response)
            self.assertEqual('recovery' in shown,command=='read')
            self.assertEqual(shown['error'],'unknown_reference')
            if command=='read':
                self.assertEqual(shown['recovery'],{'action':'observe','full':True})
        for changes in ({'execution_settled':False},{'receipt_persisted':False},
                        {'partial_effect_possible':True}):
            unsafe={**response,**changes}
            shown=adapter.Adapter.add_native_recovery(argparse.Namespace(command='read'),unsafe,unsafe)
            self.assertNotIn('recovery',shown)

    def test_numeric_preflight_reports_bounds_without_clamping_or_dispatch(self):
        calls=[]
        worker=adapter.Adapter(self.config(),calls.append)
        for action,field,maximum in (('scroll','pages',3),('scan','steps',12),
                                     ('wait-change','wait_ms',30000)):
            for bad in (0,maximum+1,True,'2'):
                request={'action':action,'document':'doc',field:bad}
                if action=='scroll':request['direction']='down'
                result=anyio.run(worker.call,request)
                payload=unpack_results(json.loads(result.content[0].text))
                self.assertTrue(result.is_error);self.assertEqual(calls,[])
                self.assertIs(payload['native_dispatched'],False)
                self.assertEqual(payload['error_code'],'invalid_action_value')
                self.assertEqual(payload['field'],field)
                self.assertEqual(payload['constraints'],{'type':'integer','minimum':1,'maximum':maximum})
                self.assertNotIn('recovery',payload)

    def test_stale_scan_without_current_page_recovers_with_full_observe(self):
        calls=[]
        result=anyio.run(adapter.Adapter(self.config(),calls.append).call,
                         {'action':'scan','cursor':'scan_expired'})
        self.assertTrue(result.is_error);self.assertEqual(calls,[])
        payload=unpack_results(json.loads(result.content[0].text))
        self.assertIs(payload['native_dispatched'],False)
        self.assertEqual(payload['recovery'],{'action':'observe','full':True})

    def test_settled_auth_document_change_returns_attach_recovery_only(self):
        calls=[]
        def execute(command):
            calls.append(command.command)
            return 1,{'ok':False,'error':'stale_document',
                      'execution_settled':True,'status':'detached'}
        result=anyio.run(adapter.Adapter(self.config(),execute).call,
                         {'action':'ask','question':'Sign in, then continue.'})
        self.assertTrue(result.is_error);self.assertEqual(calls,['ask'])
        payload=unpack_results(json.loads(result.content[0].text))
        self.assertEqual(payload['recovery'],{'action':'attach'})

    def test_uncertain_auth_error_does_not_offer_recovery(self):
        def execute(command):
            return 1,{'ok':False,'error':'stale_document',
                      'execution_settled':False,'partial_effect_possible':True}
        result=anyio.run(adapter.Adapter(self.config(),execute).call,
                         {'action':'ask','question':'Continue?'})
        payload=unpack_results(json.loads(result.content[0].text))
        self.assertNotIn('recovery',payload)

    def test_batch_full_is_top_level_in_schema_and_reaches_native(self):
        import jsonschema
        for target in ('ref','name'):
            payload={'action':'batch','batch':[{'action':'fill',target:'1','value':'literal'},
                                               {'action':'click',target:'2'}], 'full':True}
            if target=='ref':payload['document']='doc'
            jsonschema.validate(payload,adapter.TOOL.input_schema)
            calls=[]
            def execute(command):
                calls.append(command)
                return 0,{'ok':True,'execution_settled':True}
            result=anyio.run(adapter.Adapter(self.config(),execute).call,payload)
            self.assertFalse(result.is_error);self.assertTrue(calls[0].full)
            invalid=dict(payload,batch=[dict(payload['batch'][0],full=True),payload['batch'][1]])
            with self.assertRaises(jsonschema.ValidationError):
                jsonschema.validate(invalid,adapter.TOOL.input_schema)

    def test_first_tabs_observes_only_active_grant_and_does_not_repeat_or_attach(self):
        for state in ('active','inactive','ungranted','mismatch','expired'):
            calls=[]
            def execute(command):
                calls.append(command.command)
                if command.command=='tabs':
                    return 0,{'ok':True,'execution_settled':True,'receipt_persisted':True,
                              'tab':'tab1','tabs':[
                        {'tab':'other' if state=='mismatch' else 'tab1','active':state!='inactive',
                         'permission':'denied' if state=='ungranted' else 'granted'}]}
                self.assertEqual(command.command,'observe');self.assertTrue(command.full)
                if state=='expired':return 1,{'ok':False,'error':'not_attached','execution_settled':True}
                page=self.small_page();page.update(tab='tab1',receipt_persisted=True,
                                                    scroll={'y':0,'can_scroll_down':False})
                return 0,page
            worker=adapter.Adapter(self.config(),execute)
            result=anyio.run(worker.call,{'action':'tabs'})
            self.assertEqual(calls,['tabs','observe'] if state in ('active','expired') else ['tabs'])
            self.assertEqual(result.is_error,state=='expired')
            if state=='active':
                anyio.run(worker.call,{'action':'tabs'})
                self.assertEqual(calls,['tabs','observe','tabs'])

    def test_reused_connection_tabs_observes_a_new_active_grant_once(self):
        calls=[];active=['tab1']
        def execute(command):
            calls.append(command.command)
            if command.command=='tabs':
                return 0,{'ok':True,'execution_settled':True,'receipt_persisted':True,
                          'tab':active[0],'tabs':[{'tab':active[0],'active':True,
                                                  'permission':'granted'}]}
            self.assertEqual(command.command,'observe');self.assertTrue(command.full)
            page=self.small_page();page.update(tab=active[0],receipt_persisted=True,
                                               scroll={'y':0,'can_scroll_down':False})
            return 0,page
        worker=adapter.Adapter(self.config(),execute)
        anyio.run(worker.call,{'action':'tabs'})
        anyio.run(worker.call,{'action':'tabs'})
        active[0]='tab2'
        changed=anyio.run(worker.call,{'action':'tabs'})
        anyio.run(worker.call,{'action':'tabs'})
        self.assertFalse(changed.is_error)
        self.assertEqual(calls,['tabs','observe','tabs','tabs','observe','tabs'])

    def test_successful_element_read_retains_full_comparison_and_scroll_progress(self):
        worker=adapter.Adapter(self.config(),lambda command:(0,{'ok':True,'execution_settled':True,'text':'Full field','field_truncated':False}))
        page=self.small_page();page['scroll']={'y':720}
        worker.remember_observation(page);baseline=worker.full_state
        result=anyio.run(worker.call,{'action':'read','document':'doc','ref':'1'})
        self.assertFalse(result.is_error);self.assertEqual(worker.full_state,baseline)
        self.assertEqual(worker.position,('doc',720))

    def test_click_after_scan_delta_waits_for_async_content_without_replay(self):
        calls=[]
        def execute(command):
            calls.append(command.command)
            if command.command=='click':return 0,self.small_page(3,'Expand final footnote')
            self.assertEqual(command.command,'wait-change')
            self.assertEqual((command.wait_ms,command.content),(250,True))
            return 0,self.small_page(4,'Verified manufacturing defects override both exceptions.')
        worker=adapter.Adapter(self.config(),execute)
        worker.remember_observation(self.small_page(1,'Initial viewport'))
        delta=self.small_page(2);delta['snapshot']='page @doc rev=2 delta\nbase_rev=1\n+@doc_93 button "Expand final footnote"\n'
        worker.remember_observation(delta);self.assertIsNone(worker.full_state)
        result=anyio.run(worker.call,{'action':'click','document':'doc','ref':'93','full':True})
        values=unpack_results(json.loads(result.content[0].text))
        self.assertFalse(result.is_error);self.assertEqual(calls,['click','wait-change'])
        self.assertIn('Expand final footnote',values[0]['snapshot'])
        self.assertIn('manufacturing defects',values[-1]['snapshot'])

    def test_batch_changed_fill_delta_gets_final_click_view_without_replaying(self):
        import jsonschema
        for target in ('name','ref'):
            payload={'action':'batch','batch':[{'action':'fill',target:'Name' if target=='name' else '1','value':'Cedar'},
                                               {'action':'click',target:'Next' if target=='name' else '2'}]}
            if target=='ref':payload['document']='doc'
            jsonschema.validate(payload,adapter.TOOL.input_schema)
            calls=[]
            def execute(command):
                calls.append(command.command)
                if command.command.startswith('batch-'):
                    return 0,{**self.small_page(2,'Filled old form'),'completed':2}
                self.assertEqual(command.command,'wait-change')
                self.assertEqual((command.document,command.wait_ms,command.content),('doc',250,True))
                return 0,self.small_page(3,'Step 2')
            result=anyio.run(adapter.Adapter(self.config(),execute).call,payload)
            self.assertFalse(result.is_error)
            self.assertEqual(calls,['batch-'+('named' if target=='name' else 'ref'),'wait-change'])
            values=unpack_results(json.loads(result.content[0].text))
            self.assertIn('Filled old form',values[0]['snapshot'])
            self.assertIn('Step 2',values[1]['snapshot'])

    def test_batch_wait_never_follows_incomplete_uncertain_or_failed_actions(self):
        for state in ('partial','unsettled','incomplete','error','truncated'):
            calls=[]
            def execute(command):
                calls.append(command.command)
                response={**self.small_page(2,'Changed'),'completed':1}
                if state=='partial':response['partial_effect_possible']=True
                if state=='unsettled':response['execution_settled']=False
                if state=='incomplete':response['completed']=0
                if state=='truncated':response['truncated']=True
                if state=='error':response.update(ok=False,error='tab_changed')
                return (1 if state=='error' else 0),response
            anyio.run(adapter.Adapter(self.config(),execute).call,{'batch':[{'action':'click','name':'Save'}]})
            self.assertEqual(calls,['batch-named'])

    def test_fill_only_batch_refreshes_identity_and_reference_schema_rejects_mixing(self):
        import jsonschema
        calls=[]
        def execute(command):
            calls.append(command.command)
            if command.command=='wait-change':
                self.assertEqual(command.wait_ms,100)
                self.assertFalse(command.content)
            return 0,{**self.small_page(2,'Fields'),'completed':1}
        payload={'action':'batch','document':'doc','batch':[{'action':'fill','ref':'1','value':'Cedar'}]}
        jsonschema.validate(payload,adapter.TOOL.input_schema)
        anyio.run(adapter.Adapter(self.config(),execute).call,payload)
        self.assertEqual(calls,['batch-ref','wait-change'])
        for bad in ({'action':'batch','document':'doc','batch':[{'action':'click','name':'Save'}]},
                    {'action':'batch','batch':[{'action':'click','ref':'1'}]},
                    {'action':'batch','document':'doc','batch':[{'action':'click','ref':'1','name':'Save'}]}):
            if {'ref', 'name'} <= set(bad['batch'][0]):
                with self.assertRaises(jsonschema.ValidationError):
                    jsonschema.validate(bad,adapter.TOOL.input_schema)
            else:
                jsonschema.validate(bad,adapter.TOOL.input_schema)
            calls.clear()
            result=anyio.run(adapter.Adapter(self.config(),execute).call,bad)
            self.assertTrue(result.is_error)
            self.assertEqual(calls,[])

    def test_scan_preserves_each_overlapping_viewport_and_stops_at_end_or_scope_limit(self):
        for stop in ('count','end','truncated','error'):
            calls=[]
            def execute(command):
                calls.append(command.command)
                self.assertEqual((command.document,command.direction,command.pages),('doc','down',1))
                if stop=='error' and len(calls)==2:
                    return 1, {'ok':False,'error':'stale_document','execution_settled':True}
                result=self.small_page(len(calls),f'Page {len(calls)}')
                result['scroll']={'y':len(calls)*720,'max_y':4000,'can_scroll_down':not (stop=='end' and len(calls)==2)}
                if stop=='truncated' and len(calls)==2:result['truncated']=True
                return 0,result
            worker=adapter.Adapter(self.config(),execute)
            start=self.small_page();start['scroll']={'y':0};worker.remember_observation(start)
            result=anyio.run(worker.call,{'action':'scan','document':'doc','steps':4})
            self.assertEqual(len(calls),4 if stop=='count' else 2)
            values=unpack_results(json.loads(result.content[0].text))
            self.assertEqual(len(values),len(calls))
            self.assertIn('Page 1',values[0]['snapshot'])
            self.assertEqual(result.is_error,stop=='error')

    def test_navigation_during_followup_wait_reads_once_without_replay_or_new_grant(self):
        for state in ('allowed', 'expired', 'uncertain', 'same_document', 'truncated', 'partial'):
            calls = []
            def execute(command):
                calls.append(command.command)
                if command.command == 'click': return 0, self.small_page(2, 'Same')
                if command.command == 'wait-change':
                    return 1, {'ok':False,'error':'stale_document','execution_settled':state!='uncertain'}
                self.assertEqual(command.command, 'observe')
                if state == 'expired': return 1, {'ok':False,'error':'not_attached','execution_settled':True}
                page=self.small_page(1,'New document')
                if state!='same_document':
                    page['document']='doc2';page['snapshot']=page['snapshot'].replace('doc','doc2')
                if state=='truncated':page['truncated']=True
                if state=='partial':page['partial_effect_possible']=True
                return 0,page
            worker = adapter.Adapter(self.config(), execute)
            worker.remember_observation(self.small_page(1, 'Same'))
            result = anyio.run(worker.call, {'action':'click','document':'doc','ref':'1'})
            self.assertEqual(calls, ['click','wait-change'] + ([] if state=='uncertain' else ['observe']))
            values = unpack_results(json.loads(result.content[0].text))
            self.assertEqual(result.is_error,state!='allowed')
            self.assertEqual(values[1]['error'], 'stale_document')
            if state == 'expired': self.assertEqual(values[-1]['error'], 'not_attached')

    def test_direct_navigation_handoff_and_scroll_use_existing_cli_contract(self):
        import jsonschema
        examples = [
            ({'action':'scroll','document':'doc','direction':'down'}, ['--document','doc','scroll','down','1']),
            ({'action':'scan','document':'doc'}, ['--document','doc','scroll','down','1']),
            ({'action':'wait-change','document':'doc','wait_ms':250,'content':True}, ['--document','doc','wait-change','250','--content']),
            ({'action':'ask','question':'Which slot?'}, ['ask','Which slot?']),
            ({'action':'navigate','url':'http://127.0.0.1:8787/'}, ['navigate','http://127.0.0.1:8787/']),
            ({'action':'select-tab','tab':'capability'}, ['select-tab','capability']),
            ({'action':'attach'}, ['attach']), ({'action':'tabs'}, ['tabs']),
        ]
        for payload, expected in examples:
            jsonschema.validate(payload, adapter.TOOL.input_schema)
            self.assertEqual(adapter.direct_command(payload), expected)
        for payload in ({'action':'scroll','document':'doc','direction':'down','pages':True},
                        {'action':'scroll','document':'doc','direction':'sideways'},
                        {'action':'wait-change','document':'doc','wait_ms':30001},
                        {'action':'attach','permissions':{'navigate':'allow'}},
                        {'action':'ask','question':''}):
            with self.assertRaises(jsonschema.ValidationError): jsonschema.validate(payload, adapter.TOOL.input_schema)
            with self.assertRaises(adapter.cli.BridgeError): adapter.direct_command(payload)

    def test_named_batch_full_reaches_native_as_full_without_changing_actions(self):
        import jsonschema
        payload={'action':'batch','batch':[{'action':'fill','name':'Name','value':'Cedar'}, {'action':'click','name':'Save'}], 'full':True}
        jsonschema.validate(payload, adapter.TOOL.input_schema)
        calls=[]
        def execute(command):
            calls.append(command)
            return 0, {'ok':True,'execution_settled':True}
        result=anyio.run(adapter.Adapter(self.config(),execute).call,payload)
        self.assertFalse(result.is_error)
        self.assertEqual(len(calls),1)
        self.assertEqual(calls[0].command,'batch-named')
        self.assertTrue(calls[0].full)

    def test_final_fill_refreshes_refs_once_without_content_filter_or_replay(self):
        for error in (False, True):
            calls = []
            def execute(command):
                calls.append(command.command)
                if command.command == 'fill':
                    return 0, self.small_page(2, 'Entered')
                self.assertEqual((command.document, command.wait_ms, command.content), ('doc', 100, False))
                if error:
                    return 1, {'ok': False, 'error': 'tab_changed', 'execution_settled': True}
                value = self.small_page(3, 'Entered')
                value['snapshot'] = value['snapshot'].replace('@doc_1', '@doc_22')
                value['wait'] = {'reason': 'changed'}
                return 0, value
            result = anyio.run(adapter.Adapter(self.config(), execute).call,
                              {'action': 'fill', 'document': 'doc', 'ref': '1', 'value': 'Entered'})
            self.assertEqual(calls, ['fill', 'wait-change'])
            values = unpack_results(json.loads(result.content[0].text))
            self.assertIn('@1 ', values[0]['snapshot'])
            self.assertEqual(result.is_error, error)
            if not error:
                self.assertIn('@22 ', values[-1]['snapshot'])
                self.assertEqual(values[-1]['wait']['reason'], 'changed')

    def test_fill_refresh_does_not_cross_uncertainty_or_preplanned_next_action(self):
        for kind in ('unsettled', 'partial', 'truncated', 'nonfinal'):
            calls = []
            def execute(command):
                calls.append(command.command)
                value = self.small_page()
                if kind == 'unsettled': value['execution_settled'] = False
                if kind == 'partial': value['partial_effect_possible'] = True
                if kind == 'truncated': value['truncated'] = True
                return 0, value
            commands = [['--document', 'doc', 'fill', '1', 'Entered']]
            if kind == 'nonfinal': commands.append(['status'])
            worker=adapter.Adapter(self.config(),execute)
            worker.remember_observation(self.small_page(1,'Before click'))
            anyio.run(worker.call, {'commands': commands})
            self.assertNotIn('wait-change', calls)

    def test_cancelled_fill_never_dispatches_followup_wait(self):
        started = threading.Event(); release = threading.Event(); calls = []
        def execute(command):
            calls.append(command.command); started.set(); release.wait(5)
            return 0, self.small_page(2, 'Entered')
        worker = adapter.Adapter(self.config(), execute)
        async def scenario():
            async with anyio.create_task_group() as group:
                group.start_soon(worker.call, {'action': 'fill', 'document': 'doc', 'ref': '1', 'value': 'Entered'})
                while not started.is_set(): await anyio.sleep(.01)
                group.cancel_scope.cancel(); release.set()
        anyio.run(scenario)
        self.assertEqual(calls, ['fill'])

    def test_direct_actions_preserve_document_literal_values_and_full(self):
        import jsonschema
        cases = [
            ({'action': 'observe'}, ['observe']),
            ({'action': 'observe', 'full': True}, ['observe', '--full']),
            ({'action': 'read', 'document': 'doc', 'ref': '13'}, ['--document', 'doc', 'read', '13']),
            ({'action': 'fill', 'document': 'doc', 'ref': '13', 'value': '--full'},
             ['--document', 'doc', 'fill', '13', '--full']),
            ({'action': 'fill', 'document': 'doc', 'ref': '13', 'value': '', 'full': True},
             ['--document', 'doc', 'fill', '13', '', '--full']),
            ({'action': 'click', 'document': 'doc', 'ref': '14', 'full': True},
             ['--document', 'doc', 'click', '14', '--full']),
        ]
        for arguments, legacy in cases:
            with self.subTest(arguments=arguments):
                jsonschema.validate(arguments, adapter.TOOL.input_schema)
                actual = []
                def execute(command):
                    actual.append(vars(command))
                    return 0, {'ok': True, 'execution_settled': True}
                result = anyio.run(adapter.Adapter(self.config(), execute).call, arguments)
                self.assertFalse(result.is_error)
                expected = vars(adapter.cli.session_command(legacy, self.config()))
                if expected['command'] == 'observe':
                    expected['full'] = True  # First observation is full on both paths.
                    expected['_expected_observation_baseline'] = None
                self.assertEqual(actual, [expected])

    def test_direct_invalid_or_mixed_payload_never_dispatches(self):
        import jsonschema
        cases = [
            {'action': 'fill', 'ref': '1', 'value': 'x'},
            {'action': 'click', 'document': '', 'ref': '1'},
            {'action': 'click', 'document': 'doc', 'ref': 1},
            {'action': 'fill', 'document': 'doc', 'ref': '1', 'value': False},
            {'action': 'observe', 'full': 'true'},
            {'action': 'observe', 'commands': [['detach']]},
            {'action': 'observe', 'batch': [{'action': 'click', 'name': 'Save'}]},
            {'action': 'observe', 'document': 'doc'},
            {'action': 'read', 'document': 'doc', 'ref': '1', 'full': True},
            {'action': ['observe']},
        ]
        schema_rejected = {1, 2, 3, 4, 5, 6, 7, 9}
        for arguments in cases:
            with self.subTest(arguments=arguments):
                if cases.index(arguments) in schema_rejected:
                    with self.assertRaises(jsonschema.ValidationError):
                        jsonschema.validate(arguments, adapter.TOOL.input_schema)
                else:
                    jsonschema.validate(arguments, adapter.TOOL.input_schema)
                calls = []
                result = anyio.run(adapter.Adapter(self.config(), calls.append).call, arguments)
                self.assertTrue(result.is_error)
                self.assertEqual(calls, [])

    def test_user_stop_cannot_be_undone_by_model_reattach_or_legacy_commands(self):
        for reason in ('user_takeover', 'user_cancelled'):
            calls=[]
            def execute(command):
                calls.append(command.command)
                if command.command=='ask':
                    return 1, {'ok':False,'error':reason,'execution_settled':True}
                return 0, {'ok':True,'execution_settled':True}
            worker=adapter.Adapter(self.config(),execute)
            first=anyio.run(worker.call,{'action':'ask','question':'Continue?'})
            self.assertTrue(first.is_error)
            for payload in ({'action':'attach'},{'action':'ask','question':'Again?'},
                            {'action':'observe'},{'commands':[['attach'],['observe']]},
                            {'document':'doc','batch':[{'action':'click','ref':'1'}]}):
                result=anyio.run(worker.call,payload)
                self.assertTrue(result.is_error)
                self.assertIn('session_stopped',result.content[0].text)
            self.assertEqual(calls,['ask'])
            for action in ('recover','detach','cancel'):
                self.assertFalse(anyio.run(worker.call,{'action':action}).is_error)
            self.assertTrue(anyio.run(worker.call,{'action':'attach'}).is_error)
            self.assertEqual(calls,['ask','recover','detach','cancel'])

    def test_user_stop_in_automatic_followup_also_latches(self):
        calls=[]
        def execute(command):
            calls.append(command.command)
            if command.command=='click':return 0,self.small_page()
            return 1,{'ok':False,'error':'user_takeover','execution_settled':True}
        worker=adapter.Adapter(self.config(),execute)
        self.assertTrue(anyio.run(worker.call,{'action':'click','document':'doc','ref':'1'}).is_error)
        self.assertEqual(calls,['click','wait-change'])
        self.assertTrue(anyio.run(worker.call,{'action':'attach'}).is_error)
        self.assertEqual(calls,['click','wait-change'])

    def test_direct_stale_click_keeps_failure_and_only_reads_recovery(self):
        calls = []
        def execute(command):
            calls.append(command.command)
            if command.command == 'click':
                return 1, {'ok': False, 'error': 'stale_target', 'execution_settled': True}
            return 0, self.small_page()
        result = anyio.run(adapter.Adapter(self.config(), execute).call,
                          {'action': 'click', 'document': 'doc', 'ref': '14'})
        self.assertTrue(result.is_error)
        self.assertEqual(calls, ['click', 'observe'])
        self.assertEqual(unpack_results(json.loads(result.content[0].text))[0]['error'], 'stale_target')

    def test_actual_luna_malformed_shapes_return_repair_example_without_dispatch(self):
        for arguments in ({'commands': ['observe']}, {'command': ['observe']},
                          {'commands': '["observe"]'},
                          {'commands': [['--document', 'doc', 'click', '1'], 'observe']}):
            with self.subTest(arguments=arguments):
                calls = []
                result = anyio.run(adapter.Adapter(self.config(), calls.append).call, arguments)
                self.assertTrue(result.is_error)
                self.assertEqual(calls, [])
                message = unpack_results(json.loads(result.content[0].text))['error']
                example = json.loads(message.split('example: ', 1)[1])
                self.assertEqual(example, {'action': 'observe'})

    def small_page(self, rev=1, text='Not saved', delta=False):
        return {'ok': True, 'execution_settled': True, 'document': 'doc', 'revision': rev,
                'truncated': False, 'viewport': {'width': 900, 'height': 700},
                'snapshot': (f'page @doc rev={rev} delta\nbase_rev={rev-1}\n' if delta else
                             f'page @doc rev={rev}\n+@doc_1 text "{text}"\n')}

    def test_unchanged_final_click_appends_bounded_wait_and_preserves_both_results(self):
        calls = []
        def execute(command):
            calls.append(command.command)
            if command.command == 'observe': return 0, self.small_page()
            if command.command == 'click': return 0, self.small_page(2)
            self.assertEqual((command.document, command.wait_ms, command.content), ('doc', 250, True))
            result = self.small_page(3, 'Saved')
            result['wait'] = {'reason': 'changed'}
            return 0, result
        result = anyio.run(adapter.Adapter(self.config(), execute).call,
                          {'commands': [['observe'], ['--document', 'doc', 'click', '2', '--full']]})
        self.assertEqual(calls, ['observe', 'click', 'wait-change'])
        values = unpack_results(json.loads(result.content[0].text))
        self.assertIn('Not saved', values[1]['snapshot'])
        self.assertIn('Saved', values[2]['snapshot'])
        self.assertEqual(values[2]['wait']['reason'], 'changed')
        self.assertFalse(result.is_error)

    def test_changed_nonfinal_or_unsettled_click_does_not_add_wait(self):
        for kind in ('changed', 'nonfinal', 'unsettled', 'partial'):
            calls = []
            def execute(command):
                calls.append(command.command)
                result = self.small_page(2, delta=kind != 'changed')
                if kind == 'unsettled': result['execution_settled'] = False
                if kind == 'partial': result['partial_effect_possible'] = True
                return 0, result
            commands = [['--document', 'doc', 'click', '2']]
            if kind == 'nonfinal': commands.append(['status'])
            worker=adapter.Adapter(self.config(),execute)
            worker.remember_observation(self.small_page(1,'Before click'))
            anyio.run(worker.call, {'commands': commands})
            self.assertNotIn('wait-change', calls)

    def test_settling_wait_limit_or_failure_does_not_invent_completion(self):
        for failure in (False, True):
            calls = []
            def execute(command):
                calls.append(command.command)
                if command.command == 'click': return 0, self.small_page(2, delta=True)
                if failure: return 1, {'ok': False, 'error': 'tab_changed', 'execution_settled': True}
                value = self.small_page(3)
                value['wait'] = {'reason': 'limit'}
                return 0, value
            result = anyio.run(adapter.Adapter(self.config(), execute).call,
                              {'commands': [['--document', 'doc', 'click', '2']]})
            self.assertEqual(calls, ['click', 'wait-change'])
            values = unpack_results(json.loads(result.content[0].text))
            self.assertTrue(values[0]['ok'])
            self.assertEqual(result.is_error, failure)
            if not failure: self.assertEqual(values[1]['wait']['reason'], 'limit')

    def test_cancellation_after_click_does_not_dispatch_settling_wait(self):
        started = threading.Event(); release = threading.Event(); calls = []
        def execute(command):
            calls.append(command.command); started.set(); release.wait(5)
            return 0, self.small_page(2, delta=True)
        worker = adapter.Adapter(self.config(), execute)
        async def scenario():
            async with anyio.create_task_group() as group:
                group.start_soon(worker.call, {'commands': [['--document', 'doc', 'click', '2']]})
                while not started.is_set(): await anyio.sleep(.01)
                group.cancel_scope.cancel(); release.set()
        anyio.run(scenario)
        self.assertEqual(calls, ['click'])

    def test_full_action_requests_existing_native_full_observation(self):
        config = self.config()
        command = adapter.cli.session_command(['--document', 'doc', 'click', '3', '--full'], config)
        request = adapter.cli.request_for(command, {'document':'doc','revision':1})
        self.assertTrue(request['full'])
        self.assertEqual(request['ref'], 'doc_3')
        literal = adapter.cli.session_command(['--document', 'doc', 'fill', '3', '--full'], config)
        self.assertEqual(literal.value, '--full')
        self.assertNotIn('full', adapter.cli.request_for(literal, {'document':'doc','revision':1}))
        flags = []
        def execute(command):
            flags.append(command.full)
            return 0, {'ok': True, 'snapshot': 'full verified page'}
        result = anyio.run(adapter.Adapter(config, execute).call,
                          {'commands': [['--document', 'doc', 'click', '3', '--full']]})
        self.assertEqual(flags, [True])
        self.assertEqual(unpack_results(json.loads(result.content[0].text))['snapshot'], 'full verified page')


    def test_attach_permissions_preserved_and_invalid_rules_rejected(self):
        config = self.config()
        command = adapter.cli.session_command(['attach', '--permissions', '{"fill":"allow","click":"deny"}'], config)
        self.assertEqual(adapter.cli.request_for(command)['permissions'], {'fill':'allow', 'click':'deny'})
        for invalid in ('[]', '{"other":"allow"}', '{"fill":"yes"}'):
            with self.assertRaises(adapter.cli.BridgeError):
                adapter.cli.session_command(['attach', '--permissions', invalid], config)


    def test_unicode_wire_preserves_page_text_and_invalid_surrogates(self):
        payload = {'snapshot': '이름 서울 배송 완료 😀 العربية',
                   'quoted': '"\\\n', 'unpaired': '\ud800'}
        result = adapter.Adapter.result([payload], False)
        wire = result.content[0].text
        self.assertEqual(json.loads(wire), payload)
        self.assertIn('서울', wire)
        wire.encode('utf-8', errors='strict')
        old = json.dumps(payload, ensure_ascii=True, separators=(',', ':'))
        self.assertLess(len(wire.encode('utf-8')), len(old.encode('utf-8')))

    def test_new_consumer_observes_full_after_external_baseline(self):
        flags=[]
        def execute(command):
            flags.append(command.full)
            return 0, {**self.small_page(len(flags), text='complete page', delta=not command.full),
                       'receipt_persisted':True}
        worker=adapter.Adapter(self.config(),execute)
        async def scenario():
            first=await worker.call({'commands':[['observe']]})
            self.assertEqual(unpack_results(json.loads(first.content[0].text))['snapshot'],
                             'page rev=1\n+@1 text "complete page"\n')
            await worker.call({'commands':[['observe'],['observe','--full']]})
            await adapter.Adapter(self.config(),execute).call({'commands':[['observe']]})
        anyio.run(scenario)
        self.assertEqual(flags,[True,False,True,True])

    def test_failed_first_observe_does_not_establish_consumer_baseline(self):
        flags=[]
        def execute(command):
            flags.append(command.full)
            return (1,{'ok':False,'error':'not attached'}) if len(flags)==1 else (0,{'ok':True})
        worker=adapter.Adapter(self.config(),execute)
        async def scenario():
            self.assertTrue((await worker.call({'commands':[['observe']]})).is_error)
            self.assertFalse((await worker.call({'commands':[['observe']]})).is_error)
        anyio.run(scenario)
        self.assertEqual(flags,[True,True])

    def test_typed_batch_reuses_one_existing_native_batch_command(self):
        calls=[]
        def execute(command):
            calls.append(command)
            return 0,{'ok':True,'completed':2}
        result=anyio.run(adapter.Adapter(self.config(),execute).call,{'batch':[
            {'action':'fill','name':'Name','value':'Cedar "서울"\nline'},
            {'action':'click','name':'Save locally'}]})
        self.assertFalse(result.is_error)
        self.assertEqual(len(calls),1)
        self.assertEqual(calls[0].command,'batch-named')
        self.assertEqual(json.loads(calls[0].actions_json),
            [['fill','Name','Cedar "서울"\nline'],['click','Save locally']])

    def test_invalid_typed_batch_never_dispatches(self):
        fill={'action':'fill','name':'Name','value':'Cedar'}
        bad=[{'commands':[['observe']],'batch':[fill]}, {'batch':[]},
             {'batch':[fill]*9}, {'batch':[{'action':'fill','name':'Name','value':True}]},
             {'batch':[dict(fill,approved=True)]},
             {'batch':[{'action':'click','name':'Next'},fill]},
             {'batch':[{'action':'submit','name':'Submit'}]}]
        for arguments in bad:
            calls=[]
            with self.subTest(arguments=arguments):
                result=anyio.run(adapter.Adapter(self.config(),lambda c:calls.append(c)).call,arguments)
                self.assertTrue(result.is_error)
                self.assertEqual(calls,[])

    def test_stale_action_appends_full_observation_without_retry_or_next_action(self):
        calls=[]
        def execute(command):
            calls.append(command.command)
            if command.command == 'click':
                return 1, {'ok':False,'error':'not_visible','execution_settled':True}
            self.assertTrue(command.full)
            return 0, {'ok':True,'document':'new-document','snapshot':'fresh targets'}
        result=anyio.run(adapter.Adapter(self.config(),execute).call,
                        {'commands':[['--document','doc','click','1'],['navigate','https://unused.invalid']]})
        self.assertTrue(result.is_error)
        self.assertEqual(calls,['click','observe'])
        payload=unpack_results(json.loads(result.content[0].text))
        self.assertEqual(payload[0]['error'],'not_visible')
        self.assertEqual(payload[1]['snapshot'],'fresh targets')

    def test_uncertain_or_unrelated_failure_never_refreshes(self):
        for error,settled,partial in [('not_visible',False,False),
                                      ('not_visible',True,True),
                                      ('request_expired',True,False),
                                      ('user_cancelled',True,False)]:
            calls=[]
            def execute(command):
                calls.append(command.command)
                return 1,{'ok':False,'error':error,'execution_settled':settled,
                          'partial_effect_possible':partial}
            result=anyio.run(adapter.Adapter(self.config(),execute).call,
                            {'commands':[['--document','doc','click','1']]})
            self.assertTrue(result.is_error)
            self.assertEqual(calls,['click'])

    def test_cancellation_after_stale_action_does_not_dispatch_refresh(self):
        started=threading.Event();release=threading.Event();calls=[]
        def execute(command):
            calls.append(command.command)
            started.set();release.wait(5)
            return 1,{'ok':False,'error':'not_visible','execution_settled':True}
        worker=adapter.Adapter(self.config(),execute)
        async def scenario():
            async with anyio.create_task_group() as group:
                group.start_soon(worker.call,{'commands':[['--document','doc','click','1']]})
                while not started.is_set():await anyio.sleep(.01)
                group.cancel_scope.cancel();release.set()
        anyio.run(scenario)
        self.assertEqual(calls,['click'])

    def test_failed_refresh_preserves_original_action_error(self):
        calls=[]
        def execute(command):
            calls.append(command.command)
            return 1,{'ok':False,'execution_settled':True,
                      'error':'stale_target' if command.command=='click' else 'tab_changed'}
        result=anyio.run(adapter.Adapter(self.config(),execute).call,
                        {'commands':[['--document','doc','click','1']]})
        self.assertTrue(result.is_error)
        self.assertEqual([x['error'] for x in unpack_results(json.loads(result.content[0].text))],
                         ['stale_target','tab_changed'])
        self.assertEqual(calls,['click','observe'])

    def config(self):
        return argparse.Namespace(bridge='/unused', timeout=1, request_timeout=1,
                                  compact=True, _transcript=None)

    def test_cancelled_audit_records_abort_without_response_or_following_action(self):
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory).resolve()
            with adapter.cli.Transcript(str(root/'native')) as native, adapter.cli.Transcript(str(root/'calls')) as audit:
                config=self.config();config._transcript=native
                started=threading.Event();release=threading.Event();executed=[]
                def execute(command):
                    executed.append(command.command)
                    started.set();release.wait(5)
                    return 0, {'ok':True}
                worker=adapter.AuditedCalls(adapter.Adapter(config,execute),audit)
                async def scenario():
                    async with anyio.create_task_group() as group:
                        group.start_soon(worker.call,'yee_browser',{'commands':[['status'],['status']]})
                        while not started.is_set():await anyio.sleep(.01)
                        group.cancel_scope.cancel();release.set()
                anyio.run(scenario)
            rows=[json.loads(line) for line in (root/'calls').read_text().splitlines()]
            self.assertEqual(len(executed),1)
            self.assertEqual([r['kind'] for r in rows],['mcp_request','mcp_aborted'])
            self.assertEqual(rows[0]['invocation'],rows[1]['invocation'])
            self.assertEqual(rows[1]['reason'],'cancelled')
            self.assertFalse(rows[1]['response_returned'])

    def test_audit_binds_invocation_to_native_sequence_interval(self):
        with tempfile.TemporaryDirectory() as root:
            root = Path(root).resolve()
            with adapter.cli.Transcript(str(root/'native')) as native, \
                    adapter.cli.Transcript(str(root/'calls')) as audit:
                config = self.config(); config._transcript = native
                def execute(command):
                    native.write({'kind': 'request', 'request': {'id': str(native.sequence_written)}})
                    native.write({'kind': 'response', 'response': {'ok': True}})
                    return 0, {'ok': True, 'status': 'detached'}
                worker = adapter.AuditedCalls(adapter.Adapter(config, execute), audit)
                first = anyio.run(worker.call, 'yee_browser', {'commands': [['status'], ['status']]})
                anyio.run(worker.call, 'yee_browser', {'commands': [['bad']]})
            rows = [json.loads(line) for line in (root/'calls').read_text().splitlines()]
            self.assertEqual([r['sequence'] for r in rows], [1, 2, 3, 4])
            self.assertEqual(rows[0]['native_sequence_start'], 0)
            self.assertEqual(rows[1]['native_sequence_end'], 4)
            self.assertEqual(rows[2]['native_sequence_start'], 4)
            self.assertEqual(rows[3]['native_sequence_end'], 4)
            self.assertEqual(rows[0]['invocation'], rows[1]['invocation'])
            self.assertNotEqual(rows[0]['invocation'], rows[2]['invocation'])
            self.assertEqual(rows[1]['content'], [item.text for item in first.content])
            self.assertTrue(rows[3]['is_error'])

    def test_short_document_lifecycle_and_content_preservation(self):
        handles = adapter.DocumentHandles()
        doc = '12345678-1234-1234-1234-123456789abc'
        source = {'document': doc, 'text': doc, 'snapshot': 'label ' + doc}
        shown = handles.present(source)
        handle = shown['document']
        self.assertLess(len(handle), len(doc))
        self.assertEqual(shown['text'], source['text'])
        self.assertEqual(shown['snapshot'], source['snapshot'])
        self.assertEqual(source['document'], doc)
        self.assertEqual(handles.present(source)['document'], handle)
        self.assertEqual(handles.expand(handle), doc)
        self.assertEqual(handles.expand(doc), doc)
        with self.assertRaises(ValueError):
            adapter.DocumentHandles().expand(handle)
        next_handle = handles.present({'document': 'another-document'})['document']
        with self.assertRaises(ValueError): handles.expand(handle)
        handles.present({'document': ''})
        with self.assertRaises(ValueError): handles.expand(next_handle)
        self.assertNotEqual(handles.present(source)['document'], handle)

    def test_short_document_expansion_is_structured_and_preflighted(self):
        config = self.config(); config.short_documents = True
        calls = []
        doc = '12345678-1234-1234-1234-123456789abc'
        def execute(command):
            calls.append(command)
            return 0, {'ok': True, 'document': doc}
        worker = adapter.Adapter(config, execute)
        first = anyio.run(worker.call, {'commands': [['observe']]})
        handle = unpack_results(json.loads(first.content[0].text))['document']
        result = anyio.run(worker.call, {'commands': [
            ['--document', handle, 'fill', '1', handle]]})
        self.assertFalse(result.is_error)
        self.assertEqual(calls[-1].document, doc)
        self.assertEqual(calls[-1].value, handle)
        before = len(calls)
        result = anyio.run(worker.call, {'commands': [
            ['status'], ['--document', '~expired', 'click', '1']]})
        self.assertTrue(result.is_error)
        self.assertEqual(len(calls), before)

    def test_short_documents_are_opt_in(self):
        worker = adapter.Adapter(self.config(), lambda c: (0, {'ok': True, 'document': 'uuid'}))
        result = anyio.run(worker.call, {'commands': [['observe']]})
        self.assertEqual(unpack_results(json.loads(result.content[0].text))['document'], 'uuid')
        self.assertIsNone(worker.documents)

    def test_audit_admission_failure_prevents_native_dispatch(self):
        class BrokenRecord:
            def write(self, event): raise ValueError('fixture audit disk failure')
        with tempfile.TemporaryDirectory() as root:
            with adapter.cli.Transcript(str(Path(root).resolve()/'native')) as native:
                calls = []
                config = self.config(); config._transcript = native
                worker = adapter.AuditedCalls(adapter.Adapter(config, lambda c: calls.append(c)), BrokenRecord())
                with self.assertRaisesRegex(ValueError, 'audit disk failure'):
                    anyio.run(worker.call, 'yee_browser', {'commands': [['status']]})
                self.assertEqual(calls, [])

    def test_invalid_later_command_never_dispatches(self):
        calls = []
        worker = adapter.Adapter(self.config(), lambda c: calls.append(c))
        for arguments in ({'commands': [['status'], ['bad']]},
                          {'commands': [['status'], ['--bridge', '/else', 'status']]},
                          {'commands': [['status']], 'bridge': '/else'},
                          {'commands': [['status']] * 17},
                          {'commands': [['ask', '\ud800']]},
                          {'commands': [['ask', 'a' * 65536]]}):
            self.assertTrue(anyio.run(worker.call, arguments).is_error)
        self.assertEqual(calls, [])

    def test_named_worker_cancel_checkpoint_stops_before_mutation(self):
        started, release = threading.Event(), threading.Event()
        mutations = []
        # Exercise the adapter's real default executor and AnyIO worker context.
        # The fake native run pauses at the same callback boundary as cli.run.
        def native(command, emit_output=True, _before_named_action=None):
            self.assertEqual(command.command, 'fill-named')
            started.set()
            if not release.wait(2):
                raise RuntimeError('worker not released')
            _before_named_action()
            mutations.append(command.name)
            return 0, {'ok': True}
        async def scenario():
            worker = adapter.Adapter(self.config())
            async with anyio.create_task_group() as group:
                group.start_soon(worker.call, {'commands': [['fill-named', 'Name', 'Cedar']]})
                while not started.is_set():
                    await anyio.sleep(0.001)
                group.cancel_scope.cancel()
                release.set()
        with patch.object(adapter.cli, 'run', native):
            anyio.run(scenario)
        self.assertEqual(mutations, [])

    def test_partial_failure_stops_and_keeps_results(self):
        calls = []
        def execute(command):
            calls.append(command.command)
            return (1, {'ok': False, 'error': 'denied'}) if len(calls) == 2 else (0, {'ok': True})
        result = anyio.run(adapter.Adapter(self.config(), execute).call,
                          {'commands': [['status'], ['status'], ['detach']]})
        self.assertTrue(result.is_error)
        self.assertEqual(calls, ['status', 'status'])
        self.assertEqual(len(result.content), 1)
        self.assertEqual([r['ok'] for r in unpack_results(json.loads(result.content[0].text))], [True, False])

    def test_success_strips_native_timing(self):
        result = anyio.run(adapter.Adapter(self.config(), lambda c: (0, {
            'ok': True, 'timing': {'user_wait_ms': 9}, 'status': 'idle'})).call,
            {'commands': [['status']]})
        self.assertFalse(result.is_error)
        self.assertNotIn('timing', result.content[0].text)

    def test_cancel_during_action_does_not_dispatch_next_action(self):
        started, release = threading.Event(), threading.Event()
        calls = []
        def execute(command):
            calls.append(command.command)
            started.set()
            if not release.wait(2):
                raise RuntimeError('test worker was not released')
            return 0, {'ok': True}
        async def scenario():
            worker = adapter.Adapter(self.config(), execute)
            async with anyio.create_task_group() as group:
                group.start_soon(worker.call, {'commands': [['status'], ['detach']]})
                while not started.is_set():
                    await anyio.sleep(0.001)
                group.cancel_scope.cancel()
                release.set()
        anyio.run(scenario)
        self.assertEqual(calls, ['status'])

    def test_bounded_input_rejects_oversize_before_parsing(self):
        async def scenario():
            stream = adapter.BoundedInput(io.BytesIO(b'a' * 65537 + b'\n'))
            with self.assertRaises(ValueError):
                await stream.__anext__()
        anyio.run(scenario)

    def test_stdio_handshake_list_invalid_call_and_eof(self):
        with tempfile.TemporaryDirectory() as directory:
            messages = [
                {'jsonrpc': '2.0', 'id': 1, 'method': 'initialize', 'params': {
                    'protocolVersion': '2025-11-25', 'capabilities': {},
                    'clientInfo': {'name': 'yee-test', 'version': '1'}}},
                {'jsonrpc': '2.0', 'method': 'notifications/initialized'},
                {'jsonrpc': '2.0', 'id': 2, 'method': 'tools/list'},
                {'jsonrpc': '2.0', 'id': 3, 'method': 'tools/call', 'params': {
                    'name': 'yee_browser', 'arguments': {'commands': [['bad']]}}},
            ]
            # Keep stdin live until replies arrive; EOF intentionally cancels
            # active requests in the SDK, so communicate(all_input) is not a
            # valid handshake client for this test.
            process = subprocess.Popen([sys.executable, str(Path(adapter.__file__)),
                                        '--bridge', directory, '--short-documents'], stdin=subprocess.PIPE,
                                       stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
            try:
                for message in messages:
                    process.stdin.write(json.dumps(message) + '\n')
                    process.stdin.flush()
                    if 'id' in message:
                        import select
                        self.assertTrue(select.select([process.stdout], [], [], 5)[0])
                        reply = json.loads(process.stdout.readline())
                        self.assertEqual(reply['id'], message['id'])
                        if message['id'] == 1:
                            self.assertEqual(reply['result']['protocolVersion'], '2025-11-25')
                            self.assertEqual(reply['result']['instructions'],adapter.SERVER_INSTRUCTIONS)
                        if message['id'] == 2:
                            self.assertEqual(reply['result']['tools'][0]['name'], 'yee_browser')
                            self.assertIn('Use returned document/ref exactly',
                                          reply['result']['tools'][0]['description'])
                            self.assertIn('Document handles expire on document change',
                                          reply['result']['tools'][0]['description'])
                        if message['id'] == 3:
                            self.assertTrue(reply['result']['isError'])
                process.stdin.close()
                self.assertEqual(process.wait(timeout=5), 0)
                self.assertFalse((Path(directory) / 'request.json').exists())
            finally:
                if process.poll() is None:
                    process.kill()
                    process.wait()
                process.stdout.close()
                process.stderr.close()

    def test_wire_cancel_and_disconnect_stop_batch_after_pending_action(self):
        import select
        for disconnect, named in ((False, False), (True, False), (False, True), (True, True),
                                  (False, 'batch'), (True, 'batch'), (False, 'wait'), (True, 'wait')):
            with self.subTest(disconnect=disconnect, named=named), tempfile.TemporaryDirectory() as directory:
                bridge = Path(directory)
                process = subprocess.Popen([sys.executable, str(Path(adapter.__file__)),
                                            '--bridge', directory], stdin=subprocess.PIPE,
                                           stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
                def send(message):
                    process.stdin.write(json.dumps(message) + '\n')
                    process.stdin.flush()
                def receive():
                    self.assertTrue(select.select([process.stdout], [], [], 5)[0])
                    return json.loads(process.stdout.readline())
                try:
                    send({'jsonrpc': '2.0', 'id': 1, 'method': 'initialize', 'params': {
                        'protocolVersion': '2025-11-25', 'capabilities': {},
                        'clientInfo': {'name': 'yee-test', 'version': '1'}}})
                    self.assertEqual(receive()['id'], 1)
                    send({'jsonrpc': '2.0', 'method': 'notifications/initialized'})
                    commands = ([['fill-named', 'Name', 'Cedar'], ['click-named', 'Save locally']]
                                if named else [['status'], ['detach']])
                    if named == 'batch':
                        commands = [['batch-named', '[["fill","Name","Cedar"]]']]
                    if named == 'wait':
                        (bridge/'client-state.json').write_text(json.dumps({'document':'doc-1','revision':1}))
                        commands = [['--document','doc-1','wait-change','30000'],['detach']]
                    send({'jsonrpc': '2.0', 'id': 2, 'method': 'tools/call', 'params': {
                        'name': 'yee_browser', 'arguments': {'commands': commands}}})
                    deadline = time.monotonic() + 5
                    while not (bridge / 'request.json').exists() and time.monotonic() < deadline:
                        time.sleep(0.01)
                    request = json.loads((bridge / 'request.json').read_text())
                    self.assertEqual(request['command'], 'wait-change' if named == 'wait' else 'observe' if named else 'status')
                    if disconnect:
                        process.stdin.close()
                    else:
                        send({'jsonrpc': '2.0', 'method': 'notifications/cancelled',
                              'params': {'requestId': 2, 'reason': 'test cancel'}})
                        # Ping proves the protocol loop processed input while
                        # the native command was pending, not just afterward.
                        send({'jsonrpc': '2.0', 'id': 3, 'method': 'ping'})
                        self.assertEqual(receive()['id'], 3)
                        deadline=time.monotonic()+3
                        while not (bridge/'client-stop.json').exists() and time.monotonic()<deadline:time.sleep(.01)
                        self.assertEqual(json.loads((bridge/'client-stop.json').read_text()),
                                         {'id':request['id'],'reason':'client_cancelled'})
                    (bridge / 'request.json').unlink()
                    response = {'id': request['id'], 'ok': True, 'status': 'idle',
                                'execution_settled':True,'receipt_persisted':True}
                    if named:
                        response.update(document='doc-1', revision=1, truncated=False,
                            snapshot='page @doc-1 rev=1\n+@doc-1_4 field "Name" value=""\n')
                    # Native writes the immutable receipt before its public
                    # response. Model that ordering rather than only setting
                    # receipt_persisted without an actual recoverable archive.
                    archive = bridge / ('native-request-' + request['id'].encode().hex().upper() + '.result')
                    adapter.cli.atomic_write(str(archive), json.dumps(response).encode())
                    adapter.cli.atomic_write(str(bridge / 'response.json'), json.dumps(response).encode())
                    if not disconnect:
                        process.stdin.close()
                    self.assertEqual(process.wait(timeout=5), 0)
                    self.assertFalse((bridge / 'request.json').exists(), 'later action dispatched')
                    self.assertEqual((bridge/'client-stop.json').exists(),not disconnect)
                    # EOF can be processed just after the settled receipt was
                    # acknowledged. Both that completed reference and an
                    # unacknowledged pending fence must recover this exact ID.
                    reference = (bridge/'client-pending.json' if
                                 (bridge/'client-pending.json').exists() else
                                 bridge/'mcp-last-native.json')
                    self.assertEqual(json.loads(reference.read_text())['id'],request['id'])
                    # A new client recovers the original settled receipt without
                    # posting another native request after transport cancellation.
                    config=self.config();config.bridge=str(bridge.resolve())
                    recovered=anyio.run(adapter.Adapter(config).call,{'action':'recover'})
                    self.assertFalse(recovered.is_error)
                    self.assertEqual(json.loads(recovered.content[0].text)['ok'],True)
                    self.assertFalse((bridge/'client-pending.json').exists())
                    self.assertFalse((bridge/'request.json').exists())
                finally:
                    if process.poll() is None:
                        process.kill()
                        process.wait()
                    if not process.stdin.closed:
                        process.stdin.close()
                    process.stdout.close()
                    process.stderr.close()

    def test_wire_document_handles_round_trip_and_reconnection(self):
        import select
        from yee_document_audit import verify
        with tempfile.TemporaryDirectory() as directory:
            bridge = Path(directory).resolve()
            doc = '12345678-1234-1234-1234-123456789abc'
            previous = None
            for connection in range(2):
                native_path, calls_path = bridge/f'native-{connection}', bridge/f'calls-{connection}'
                process = subprocess.Popen([sys.executable, str(Path(adapter.__file__)),
                    '--bridge', str(bridge), '--short-documents', '--record', str(native_path),
                    '--calls-record', str(calls_path)], stdin=subprocess.PIPE,
                    stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
                def send(message):
                    process.stdin.write(json.dumps(message)+'\n'); process.stdin.flush()
                def receive():
                    self.assertTrue(select.select([process.stdout], [], [], 5)[0], 'MCP reply timeout')
                    return json.loads(process.stdout.readline())
                def call(identifier, commands):
                    send({'jsonrpc': '2.0', 'id': identifier, 'method': 'tools/call',
                          'params': {'name': 'yee_browser', 'arguments': {'commands': commands}}})
                def reply_native(command, fields):
                    deadline = time.monotonic()+5
                    path = bridge/'request.json'
                    while not path.exists() and time.monotonic() < deadline: time.sleep(.01)
                    self.assertTrue(path.exists(), 'native request timeout')
                    request = json.loads(path.read_text())
                    self.assertEqual(request['command'], command)
                    path.unlink()
                    response = {'id': request['id'], 'ok': True, 'execution_settled': True, **fields}
                    adapter.cli.atomic_write(str(bridge/'response.json'), json.dumps(response).encode())
                    return request
                try:
                    send({'jsonrpc': '2.0', 'id': 1, 'method': 'initialize', 'params': {
                        'protocolVersion': '2025-11-25', 'capabilities': {},
                        'clientInfo': {'name': 'document-integration-test', 'version': '1'}}})
                    self.assertEqual(receive()['id'], 1)
                    send({'jsonrpc': '2.0', 'method': 'notifications/initialized'})
                    if previous:
                        # The CLI baseline survives, but the MCP handle must not.
                        self.assertEqual(json.loads((bridge/'client-state.json').read_text())['document'], doc)
                        call(2, [['status'], ['--document', previous, 'read', '1']])
                        rejected = receive()
                        self.assertTrue(rejected['result']['isError'])
                        self.assertFalse((bridge/'request.json').exists())
                    call(3, [['observe']])
                    request = reply_native('observe', {'document': doc, 'revision': 1, 'truncated': False,
                        'snapshot': f'page @{doc} rev=1\n@{doc}_1 text "Policy {doc}"\n@{doc}_2 button "Save"\n'})
                    self.assertTrue(request['full'], 'new MCP consumer must receive its own full baseline')
                    observed = receive()
                    self.assertFalse(observed['result'].get('isError', False))
                    shown = json.loads(observed['result']['content'][0]['text'])
                    handle = shown['document']
                    self.assertNotEqual(handle, previous)
                    self.assertEqual(shown['snapshot'], f'page rev=1\n@1 text "Policy {doc}"\n@2 button "Save"\n')
                    call(4, [['--document', handle, 'read', '1']])
                    request = reply_native('read', {'text': 'Complete policy: '+doc, 'field_truncated': False})
                    self.assertEqual(request['ref'], doc+'_1')
                    read_result = receive()
                    self.assertFalse(read_result['result'].get('isError', False))
                    self.assertEqual(json.loads(read_result['result']['content'][0]['text'])['text'],
                                     'Complete policy: '+doc)
                    call(5, [['--document', handle, 'click', '2', '--full']])
                    request = reply_native('click', {'document': doc, 'revision': 2, 'truncated': False,
                        'snapshot': f'page @{doc} rev=2\n@{doc}_1 text "Saved"\n@{doc}_2 button "Save"\n'})
                    self.assertTrue(request['full'])
                    self.assertEqual(request['ref'], doc+'_2')
                    result = receive()
                    self.assertFalse(result['result'].get('isError', False))
                    self.assertEqual(json.loads(result['result']['content'][0]['text'])['snapshot'],
                                     'page rev=2\n@1 text "Saved"\n@2 button "Save"\n')
                    previous = handle
                    process.stdin.close()
                    self.assertEqual(process.wait(timeout=5), 0)
                    native = [json.loads(line) for line in native_path.read_text().splitlines()]
                    calls = [json.loads(line) for line in calls_path.read_text().splitlines()]
                    # The failed preflight has no native interval; direct-result
                    # audit intentionally audits only the succeeding suffix.
                    if connection: calls = calls[2:]
                    self.assertEqual(verify(calls, native)['direct_commands_checked'], 3)
                finally:
                    if process.poll() is None: process.kill(); process.wait()
                    if not process.stdin.closed: process.stdin.close()
                    process.stdout.close(); process.stderr.close()


if __name__ == '__main__':
    unittest.main()
