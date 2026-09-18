#!/usr/bin/env python3
"""Join original Kimi, MCP, native and scenario evidence without exporting prose.

Record checks are not provider billing, whole-task timing or a competitive gate.
Failed checks retain any independently readable usage rather than zeroing costs.
"""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import yee_trial_timeline
from browser_trial_answer import POLICY, final_answer, unique_object


def module(name, filename):
    spec=importlib.util.spec_from_file_location(name,Path(__file__).with_name(filename))
    result=importlib.util.module_from_spec(spec);spec.loader.exec_module(result)
    return result


usage=module('kimi_usage','import-kimi-usage.py')
correlation=module('kimi_correlation','inspect-yee-mcp-calls.py')
oracle=module('scenario_oracle','verify-agent-scenario.py')


def inspect(record, fixture, review=None, tab_origin=None):
    root=Path(record); hashes={}; failures=[]
    def read(path, lines=False):
        with Path(path).open('rb') as stream:raw=stream.read(8*1024*1024+1)
        if len(raw)>8*1024*1024:raise ValueError('evidence exceeds limit')
        hashes[str(path)]=hashlib.sha256(raw).hexdigest()
        parse=lambda value:json.loads(value,object_pairs_hook=unique_object)
        value=[parse(line) for line in raw.splitlines() if line.strip()] if lines else parse(raw)
        if any(not isinstance(v,dict) for v in value) if lines else not isinstance(value,dict):
            raise ValueError('expected evidence objects')
        return value
    result={'schema':'yee.kimi-scenario-inspection.v1','record_checks_pass':False,
            'answer_policy':POLICY,
            'usage':None,'scenario':None,'correlation':None,'runner_elapsed_seconds':None,
            'answer_format_valid':None,'scenario_evidence_stage':None,
            'model_calls':None,'loop_calls':None,'compaction_calls':None,
            'raw_usage':None,'reported_cost_usd':None,'provider_retries':None,
            'whole_task_elapsed_seconds':None,'provider_billing_reconciled':False,
            'trial_boundary_verified':False,'model_autonomy_verified':False,'comparison_ready':False}
    # Read independent evidence separately so an invalid answer does not hide
    # successful or failed model call costs elsewhere in the same trial.
    wire=None; stdout=None; native=None; calls=None
    for name,filename in [('stdout','stdout.jsonl'),('native','native.jsonl'),('calls','mcp-calls.jsonl')]:
        try:
            value=read(root/filename,True)
            if name=='stdout':stdout=value
            elif name=='native':native=value
            else:calls=value
        except (OSError,ValueError,TypeError):failures.append(name+'_evidence_unreadable')
    try:
        summary=read(root/'summary.json')
        result['runner_elapsed_seconds']=summary.get('runner_elapsed_seconds')
        if (type(summary.get('returncode')) is not int or summary['returncode']!=0 or summary.get('timed_out') is not False
                or summary.get('interrupted') is not False
                or summary.get('descendants_after_normal_exit') is not False):
            failures.append('process_not_cleanly_completed')
    except (OSError,ValueError,TypeError):failures.append('summary_unreadable')
    short_documents = False
    manifest = {}
    try:
        manifest=read(root/'manifest.json')
        if manifest.get('schema')!='yee.kimi-trial.v1':raise ValueError('manifest schema')
        if manifest.get('browser','yee')!='yee':
            failures.append('non_yee_record_requires_comparator_inspector')
        short_documents=manifest.get('short_documents',False)
        if type(short_documents) is not bool:raise ValueError('invalid document variant')
        if 'mcp_config_sha256' in manifest or short_documents:
            mcp_raw=(root/'kimi-home/mcp.json').read_bytes()
            hashes[str(root/'kimi-home/mcp.json')]=hashlib.sha256(mcp_raw).hexdigest()
            if manifest.get('mcp_config_sha256')!=hashes[str(root/'kimi-home/mcp.json')]:
                raise ValueError('MCP setup changed')
            mcp_config=json.loads(mcp_raw)
            if ('--short-documents' in mcp_config['mcpServers']['yee']['args'])!=short_documents:
                raise ValueError('document variant mismatch')
        for key,path in [('profile_sha256','browser-only.md'),('prompt_sha256','prompt.txt'),
                         ('config_sha256','kimi-home/config.toml')]:
            raw=(root/path).read_bytes();digest=hashlib.sha256(raw).hexdigest()
            hashes[str(root/path)]=digest
            if manifest.get(key)!=digest:raise ValueError('setup changed')
    except (OSError,ValueError,TypeError,KeyError):failures.append('recorded_setup_hash_mismatch')
    try:
        paths=list((root/'kimi-home/sessions').glob('**/wire.jsonl'))
        if len(paths)!=1 or paths[0].parent.name!='main':raise ValueError('multiple or non-main wire')
        wire=read(paths[0],True)
        normalized=usage.normalize(wire,multi_call=True,v2_main=True)
        for key in ('usage','model_calls','loop_calls','compaction_calls','raw_usage','reported_cost_usd'):
            result[key]=normalized[key]
        requests=[e for e in wire if e.get('type')=='llm.request']
        result['requested_model_alias']=manifest.get('model_alias')
        result['observed_model_ids']=sorted({e['model'] for e in requests if isinstance(e.get('model'),str)})
        result['model_version_verified']=False
        if any(e.get('thinkingEffort')!='on' for e in requests):
            failures.append('declared_thinking_routing_unverified')
        snapshots=[e for e in wire if e.get('type')=='llm.tools_snapshot']
        if not snapshots or any(not isinstance(s.get('tools'),list)
                                or any(not isinstance(t,dict) for t in s['tools'])
                                or [t.get('name') for t in s['tools']]!=['mcp__yee__yee_browser']
                                for s in snapshots):failures.append('unexpected_offered_tool_set')
    except (OSError,ValueError,TypeError,KeyError):failures.append('wire_usage_or_scope_unverified')
    if calls is not None and native is not None:
        try:
            result['correlation']=correlation.inspect(calls,native)
            if stdout is not None:
                schemas={}
                for event in wire or []:
                    if event.get('type')!='llm.tools_snapshot':continue
                    for tool in event.get('tools',[]):
                        schema=tool.get('parameters');name=tool.get('name')
                        if not isinstance(schema,dict):continue
                        if name in schemas and schemas[name]!=schema:
                            raise ValueError('tool input schema changed during trial')
                        schemas[name]=schema
                rejections=[]
                result['correlation']['model_call_correlation_verified']=correlation.correlate_model(
                    stdout,calls,allow_parallel=True,input_schemas=schemas,local_rejections=rejections)
                result['correlation']['model_input_rejections']=rejections
                # Model calls may share a step; the MCP recorder still must
                # prove serialized native intervals and unique call signatures.
                result['correlation']['parallel_model_steps']=sum(
                    e.get('role')=='assistant' and len(e.get('tool_calls') or [])>1
                    for e in stdout)
            if short_documents:
                from yee_document_audit import verify as verify_documents
                result['correlation']['document_presentation']=verify_documents(calls,native)
            if not result['correlation']['native_settlement_verified']:failures.append('native_outcome_unsettled')
        except (ValueError,TypeError,KeyError,IndexError):failures.append('model_mcp_native_correlation_failed')
    if stdout is not None and native is not None:
        stage='final_answer'
        try:
            answer=final_answer(stdout)
            result['answer_format_valid']=True
            stage='fixture_evidence'
            reviewed=read(Path(review)) if review else None
            fixture_paths=[Path(fixture)/name for name in ('manifest.json','dataset.json','events.jsonl')]
            for path in fixture_paths:read(path,lines=path.name=='events.jsonl')
            stage='scenario_acceptance'
            result['scenario']=oracle.verify(fixture,answer,native,reviewed,tab_origin)
            stage='fixture_stability'
            if any(hashlib.sha256(path.read_bytes()).hexdigest()!=hashes[str(path)] for path in fixture_paths):
                raise ValueError('fixture evidence changed during verification')
            if result['scenario'].get('success') is not True:failures.append('scenario_acceptance_failed')
            result['scenario_evidence_stage']='complete'
        except (OSError,ValueError,TypeError,KeyError,IndexError):
            if stage=='final_answer':result['answer_format_valid']=False
            result['scenario_evidence_stage']=stage
            # Stable stages distinguish format, missing evidence, and rejected
            # task traces without exporting exception messages/page contents.
            failures.append('answer_or_scenario_evidence_invalid')
    result.update(record_checks_pass=not failures,failures=failures,input_sha256=hashes)
    return result


def main(argv=None):
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('record',type=Path);parser.add_argument('fixture',type=Path)
    parser.add_argument('--review',type=Path);parser.add_argument('--tab-origin')
    parser.add_argument('--timeline',type=Path,help='same trial timeline at execution_finished; completes verification timing')
    args=parser.parse_args(argv)
    result=inspect(args.record,args.fixture,args.review,args.tab_origin)
    if args.timeline:
        # Final review/reporting is outside this inspector. The operator closes
        # the timeline only after those steps, as for the container comparator.
        timing=yee_trial_timeline.append(args.timeline,'verification_finished',record=args.record)
        result['timing']=timing
        result['recorded_interval_seconds']=timing['recorded_interval_seconds']
        prefix=args.timeline.read_bytes()
        result['timeline_checkpoint']={'path':str(args.timeline), 'bytes':len(prefix),
                                       'sha256':hashlib.sha256(prefix).hexdigest()}
        # The append-only journal is still open; hash its verified prefix, not
        # a supposedly immutable complete file that reporting will extend.
    print(json.dumps(result,ensure_ascii=False,indent=2))
    return 0 if result['record_checks_pass'] else 1


if __name__=='__main__':
    raise SystemExit(main())
