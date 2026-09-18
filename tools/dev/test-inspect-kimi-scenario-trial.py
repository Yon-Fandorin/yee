import hashlib
import io
from contextlib import redirect_stdout
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch
from agent_scenario_fixture import Fixture

spec=importlib.util.spec_from_file_location('inspector',Path(__file__).with_name('inspect-kimi-scenario-trial.py'))
inspector=importlib.util.module_from_spec(spec);spec.loader.exec_module(inspector)


class InspectorTests(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory();self.root=Path(self.temp.name).resolve()
        self.record=self.root/'record';self.record.mkdir()
        self.f=Fixture('S02',11,str(self.root/'fixture'))
        v=self.f.view({'query':'SSD','minimum':'512','stock':'true','sort':'total'})
        answer={'items':[{'sku':p['sku'],'total_krw':p['price_krw']+p['shipping_krw']} for p in v['products']]}
        self.stdout=[{'role':'assistant','tool_calls':[{'id':'call1','function':{
            'name':'mcp__yee__yee_browser','arguments':'{"commands":[["status"]]}'}}]},
            {'role':'tool','tool_call_id':'call1','content':'{"ok":true}'},
            {'role':'assistant','content':json.dumps(answer)}]
        self.write('stdout.jsonl',self.stdout,True)
        self.write('summary.json',{'returncode':0,'timed_out':False,'interrupted':False,
                   'descendants_after_normal_exit':False,'runner_elapsed_seconds':3})
        manifest={'schema':'yee.kimi-trial.v1'}
        for key,name in [('profile_sha256','browser-only.md'),('prompt_sha256','prompt.txt'),
                         ('config_sha256','kimi-home/config.toml')]:
            p=self.record/name;p.parent.mkdir(parents=True,exist_ok=True);p.write_text('SYNTHETIC')
            manifest[key]=hashlib.sha256(p.read_bytes()).hexdigest()
        self.write('manifest.json',manifest)
        self.wire=[{'type':'llm.tools_snapshot','tools':[{'name':'mcp__yee__yee_browser'}]},
            {'type':'llm.request','agentId':'main','kind':'loop','turnStep':'0.1','thinkingEffort':'on',
             'model':'kimi-for-coding','modelAlias':'kimi-code/kimi-for-coding'},
            {'type':'usage.record','agentId':'main','model':'kimi-code/kimi-for-coding','usageScope':'turn',
             'usage':{'inputOther':10,'inputCacheRead':4,'inputCacheCreation':0,'output':6}}]
        self.write('kimi-home/sessions/session/agents/main/wire.jsonl',self.wire,True)
        self.write('native.jsonl',[
            {'sequence':1,'kind':'request','request':{'id':'native1','command':'status'}},
            {'sequence':2,'kind':'response','response_source':'mailbox',
             'response':{'id':'native1','ok':True,'execution_settled':True,
                         'timing':{'native_elapsed_ms':1,'user_wait_ms':0}}}],True)
        self.write('mcp-calls.jsonl',[
            {'sequence':1,'kind':'mcp_request','invocation':'local1','name':'yee_browser',
             'arguments':{'commands':[['status']]},'native_sequence_start':0},
            {'sequence':2,'kind':'mcp_response','invocation':'local1','native_sequence_end':2,
             'is_error':False,'content':['{"ok":true}']}],True)

    def write(self,name,value,lines=False):
        path=self.record/name;path.parent.mkdir(parents=True,exist_ok=True)
        path.write_text('\n'.join(json.dumps(v) for v in value)+'\n' if lines else json.dumps(value))

    def tearDown(self):self.f.close();self.temp.cleanup()

    def test_synthetic_join_pass_does_not_claim_comparison_or_boundaries(self):
        result=inspector.inspect(self.record,self.f.directory)
        self.assertTrue(result['record_checks_pass'],result['failures'])
        self.assertTrue(result['answer_format_valid'])
        self.assertEqual(result['scenario_evidence_stage'],'complete')
        self.assertEqual(result['usage']['total_tokens'],20)
        self.assertFalse(result['comparison_ready']);self.assertFalse(result['trial_boundary_verified'])
        self.assertIsNone(result['whole_task_elapsed_seconds'])
        self.assertIn(str(self.f.directory/'events.jsonl'),result['input_sha256'])

    def test_inspection_leaves_final_report_time_open(self):
        timeline=inspector.yee_trial_timeline
        path=self.root/'timeline.jsonl'
        for i,phase in enumerate(timeline.PHASES[:3]):
            timeline.append(path,phase,record=self.record,clock=lambda i=i:i*1_000_000_000,
                            wall=lambda:100,boot=lambda:'test-boot')
        original_append=timeline.append
        def finish_verification(path,phase,**kwargs):
            return original_append(path,phase,**kwargs,clock=lambda:3_000_000_000,
                                   wall=lambda:100,boot=lambda:'test-boot')
        output=io.StringIO()
        with patch.object(timeline,'append',side_effect=finish_verification), redirect_stdout(output):
            code=inspector.main([str(self.record),str(self.f.directory),'--timeline',str(path)])
        self.assertEqual(code,0)
        report=json.loads(output.getvalue())
        self.assertFalse(report['timing']['complete'])
        self.assertIsNone(report['recorded_interval_seconds'])
        rows=[json.loads(s) for s in path.read_text().splitlines()]
        self.assertEqual(rows[-1]['phase'],'verification_finished')
        closed=original_append(path,'finished',record=self.record,clock=lambda:8_000_000_000,
                               wall=lambda:100,boot=lambda:'test-boot')
        self.assertEqual(closed['phase_seconds']['verification_finished'],5)
        self.assertEqual(closed['recorded_interval_seconds'],8)
        checkpoint=report['timeline_checkpoint']
        self.assertEqual(hashlib.sha256(path.read_bytes()[:checkpoint['bytes']]).hexdigest(),
                         checkpoint['sha256'])
        self.assertNotIn(str(path),report['input_sha256'])
        self.assertFalse(closed['trial_boundary_verified'])
        self.assertIsNone(closed['whole_task_elapsed_seconds'])

    def test_bad_final_answer_preserves_usage(self):
        self.stdout[-1]['content']='I completed it.';self.write('stdout.jsonl',self.stdout,True)
        result=inspector.inspect(self.record,self.f.directory)
        self.assertFalse(result['record_checks_pass']);self.assertEqual(result['usage']['total_tokens'],20)
        self.assertFalse(result['answer_format_valid'])
        self.assertEqual(result['scenario_evidence_stage'],'final_answer')
        self.assertEqual(result['model_calls'],1)
        self.assertEqual(result['loop_calls'],1)
        self.assertEqual(result['compaction_calls'],0)
        self.assertEqual(result['raw_usage'],{'inputOther':10,'inputCacheRead':4,'inputCacheCreation':0,'output':6})
        self.assertIsNone(result['reported_cost_usd'])
        self.assertIsNone(result['provider_retries'])

    def test_parallel_model_calls_require_unique_serialized_evidence(self):
        calls=[json.loads(line) for line in (self.record/'mcp-calls.jsonl').read_text().splitlines()]
        native=[json.loads(line) for line in (self.record/'native.jsonl').read_text().splitlines()]
        calls.extend([
            {'sequence':3,'kind':'mcp_request','invocation':'local2','name':'yee_browser',
             'arguments':{'commands':[['tabs']]},'native_sequence_start':2},
            {'sequence':4,'kind':'mcp_response','invocation':'local2','native_sequence_end':4,
             'is_error':False,'content':['{"ok":true}']}])
        native.extend([
            {'sequence':3,'kind':'request','request':{'id':'native2','command':'tabs'}},
            {'sequence':4,'kind':'response','response_source':'mailbox',
             'response':{'id':'native2','ok':True,'execution_settled':True,
                         'timing':{'native_elapsed_ms':1,'user_wait_ms':0}}}])
        self.stdout[0]['tool_calls'].append({'id':'call2','function':{
            'name':'mcp__yee__yee_browser','arguments':'{"commands":[["tabs"]]}'}})
        # Provider response order may differ; identities must still match.
        self.stdout.insert(1,{'role':'tool','tool_call_id':'call2','content':'{"ok":true}'})
        self.write('stdout.jsonl',self.stdout,True)
        self.write('mcp-calls.jsonl',calls,True);self.write('native.jsonl',native,True)
        result=inspector.inspect(self.record,self.f.directory)
        self.assertTrue(result['record_checks_pass'],result['failures'])
        self.assertEqual(result['correlation']['parallel_model_steps'],1)
        # Identical concurrent signatures cannot be attributed by order alone.
        calls[2]['arguments']=calls[0]['arguments']
        self.stdout[0]['tool_calls'][1]['function']['arguments']='{"commands":[["status"]]}'
        self.write('stdout.jsonl',self.stdout,True);self.write('mcp-calls.jsonl',calls,True)
        result=inspector.inspect(self.record,self.f.directory)
        self.assertIn('model_mcp_native_correlation_failed',result['failures'])

    def test_comparator_manifest_cannot_pass_yee_gate(self):
        manifest=json.loads((self.record/'manifest.json').read_text())
        manifest['browser']='aside';self.write('manifest.json',manifest)
        result=inspector.inspect(self.record,self.f.directory)
        self.assertFalse(result['record_checks_pass'])
        self.assertIn('non_yee_record_requires_comparator_inspector',result['failures'])
        self.assertEqual(result['usage']['total_tokens'],20)

    def test_trace_failure_preserves_valid_format_without_exception_contents(self):
        with patch.object(inspector.oracle,'verify',side_effect=ValueError('PRIVATE PAGE DETAIL')):
            result=inspector.inspect(self.record,self.f.directory)
        self.assertFalse(result['record_checks_pass'])
        self.assertTrue(result['answer_format_valid'])
        self.assertEqual(result['scenario_evidence_stage'],'scenario_acceptance')
        self.assertEqual(result['usage']['total_tokens'],20)
        self.assertNotIn('PRIVATE PAGE DETAIL',json.dumps(result))

    def test_missing_fixture_is_distinct_from_answer_or_trace_failure(self):
        (self.f.directory/'dataset.json').unlink()
        result=inspector.inspect(self.record,self.f.directory)
        self.assertFalse(result['record_checks_pass'])
        self.assertTrue(result['answer_format_valid'])
        self.assertEqual(result['scenario_evidence_stage'],'fixture_evidence')
        self.assertEqual(result['usage']['total_tokens'],20)

    def test_missing_model_stream_retains_native_and_usage_costs(self):
        (self.record/'stdout.jsonl').unlink()
        result=inspector.inspect(self.record,self.f.directory)
        self.assertFalse(result['record_checks_pass'])
        self.assertEqual(result['usage']['total_tokens'],20)
        self.assertEqual(result['model_calls'],1)
        self.assertEqual(result['correlation']['mcp_calls'],1)
        self.assertEqual(result['correlation']['native_requests'],1)
        self.assertEqual(result['correlation']['native_user_wait_seconds'],0)
        self.assertFalse(result['correlation']['model_call_correlation_verified'])

    def test_opt_in_audit_and_mcp_configuration_binding(self):
        manifest=json.loads((self.record/'manifest.json').read_text())
        self.write('kimi-home/mcp.json',{'mcpServers':{'yee':{'args':['--short-documents']}}})
        manifest.update(short_documents=True,mcp_config_sha256=hashlib.sha256(
            (self.record/'kimi-home/mcp.json').read_bytes()).hexdigest())
        self.write('manifest.json',manifest)
        # Preserve the native settlement field in the actual model-visible
        # compact response, unlike the deliberately minimal historical fixture.
        content='{"ok":true,"execution_settled":true}'
        calls=[json.loads(line) for line in (self.record/'mcp-calls.jsonl').read_text().splitlines()]
        calls[1]['content']=[content];self.write('mcp-calls.jsonl',calls,True)
        self.stdout[1]['content']=content;self.write('stdout.jsonl',self.stdout,True)
        result=inspector.inspect(self.record,self.f.directory)
        self.assertTrue(result['record_checks_pass'],result['failures'])
        self.assertTrue(result['correlation']['document_presentation']['verified'])
        self.write('kimi-home/mcp.json',{'mcpServers':{'yee':{'args':[]}}})
        result=inspector.inspect(self.record,self.f.directory)
        self.assertIn('recorded_setup_hash_mismatch',result['failures'])
        self.assertEqual(result['usage']['total_tokens'],20)

    def test_missing_native_changed_setup_and_thinking_off_fail(self):
        (self.record/'native.jsonl').unlink();(self.record/'prompt.txt').write_text('changed')
        self.wire[1]['thinkingEffort']='off';self.write('kimi-home/sessions/session/agents/main/wire.jsonl',self.wire,True)
        result=inspector.inspect(self.record,self.f.directory)
        for reason in ('native_evidence_unreadable','recorded_setup_hash_mismatch','declared_thinking_routing_unverified'):
            self.assertIn(reason,result['failures'])
        self.assertEqual(result['usage']['total_tokens'],20)

    def test_truncated_usage_is_unmeasured(self):
        self.write('kimi-home/sessions/session/agents/main/wire.jsonl',self.wire[:-1],True)
        result=inspector.inspect(self.record,self.f.directory)
        self.assertFalse(result['record_checks_pass']);self.assertIsNone(result['usage'])

    def test_final_answer_rejects_duplicate_keys_and_json_fence(self):
        with self.assertRaises(ValueError):inspector.final_answer([{'role':'assistant','content':'{"x":1,"x":2}'}])
        with self.assertRaises(ValueError):inspector.final_answer([{'role':'assistant','content':'```json\n{"x":1}\n```'}])

    def test_malformed_records_fail_without_hiding_usage(self):
        self.write('stdout.jsonl',[None],True)
        self.write('summary.json',[])
        result=inspector.inspect(self.record,self.f.directory)
        self.assertIn('stdout_evidence_unreadable',result['failures'])
        self.assertIn('summary_unreadable',result['failures'])
        self.assertEqual(result['usage']['total_tokens'],20)


if __name__=='__main__':unittest.main()
