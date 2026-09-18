#!/usr/bin/env python3
"""Review a real rerun using original model, tool, host, fixture and AX evidence."""
import argparse
import base64
import hashlib
import importlib.util
import json
from pathlib import Path
from browser_trial_answer import final_answer
from mcp_wire_operations import derive


def module(name, file):
    s=importlib.util.spec_from_file_location(name,Path(__file__).with_name(file))
    m=importlib.util.module_from_spec(s);s.loader.exec_module(m);return m


cost=module('rerun_cost','collect-browser-comparison-evidence.py')
corr=module('rerun_correlation','inspect-yee-mcp-calls.py')
oracle=module('rerun_oracle','verify-agent-scenario.py')
handoff=module('rerun_handoff','inspect-aside-handoff-trial.py')
read=lambda p:json.loads(p.read_text())
lines=lambda p:[json.loads(s) for s in p.read_text().splitlines()]
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()


def exact_browser_join(events, wire):
    """Match text and unmodified image bytes, including original JPEG results."""
    ops,transport=derive(wire);calls=[];images=[]
    for start,end in zip(ops[::2],ops[1::2]):
        if start['operation']!='tools/call':continue
        assert start['params']['name']=='repl'
        texts=[];parts=[];has_image=False
        for b in end['result']['content']:
            if b['type']=='text':texts.append(b['text']);parts.append({'type':'text','text':b['text']})
            elif b['type']=='image':
                assert b['mimeType'] in ('image/png','image/jpeg','image/webp')
                raw=base64.b64decode(b['data'],validate=True)
                parts.append({'type':'image_url','imageUrl':{'url':'data:'+b['mimeType']+';base64,'+b['data']}})
                images.append({'mime':b['mimeType'],'bytes':len(raw),'sha256':hashlib.sha256(raw).hexdigest()})
                has_image=True
            else:raise ValueError('unhandled content; cannot prove original tool/model join')
        result={'content':texts}
        if has_image:result['content_parts']=parts
        calls += [{'name':'repl','arguments':start['params']['arguments']},result]
    corr.correlate_model(events,calls,model_tool='mcp__aside__repl',recorded_tool='repl',allow_parallel=True)
    return {'exact_model_tool_join':True,'images':images,'transport':transport}


def inspect(root):
    m=root/'model';scenario=read(root/'fixture/manifest.json')['scenario']
    summary=read(m/'summary.json');events=lines(m/'stdout.jsonl');wire=lines(m/'mcp-wire.jsonl')
    failures=[];review=read(root/'manual-review.json') if (root/'manual-review.json').exists() else {}
    review_keys=('owned_targets_only','task_mutations_only','no_credential_request_or_read','post_user_reply_read_order')
    if review.get('model_stdout_sha256')!=sha(m/'stdout.jsonl') or not all(review.get(k) is True for k in review_keys):
        failures.append('original_code_review_missing_or_failed')
    answer=None
    try:answer=final_answer(events)
    except (ValueError,TypeError):failures.append('terminal_json_invalid')
    if scenario in ('S08','S10','S12'):
        special=handoff.inspect(root)
        failures+=special['failures'];joined=special['browser_correlation']
        task=special['original_yee_oracle']
    else:
        joined=exact_browser_join(events,wire)
        task=oracle.verify(root/'fixture',answer,review=read(root/'review.json') if (root/'review.json').exists() else None)
        if not task['success']:failures.append('scenario_acceptance_failed')
        frozen=read(root/'frozen.json');finish=read(root/'source-integrity-at-finish.json')
        if frozen!=finish:failures.append('sources_changed_during_execution')
        setup=read(m/'manifest.json')
        for key,path in [('profile_sha256','browser-only.md'),('prompt_sha256','prompt.txt'),
                         ('config_sha256','kimi-home/config.toml'),('mcp_config_sha256','kimi-home/mcp.json')]:
            if sha(m/path)!=setup[key]:failures.append('setup_hash_mismatch:'+path)
        if (setup.get('aside_owned_tab_scope') or {}).get('tab_inventory_metadata_authorized') is not True:
            failures.append('metadata_permission_missing')
    if summary['returncode'] or summary['timed_out'] or summary['interrupted']:failures.append('runner_incomplete')
    if read(root/'cleanup.json')['isError']:failures.append('cleanup_error')
    host=lines(m/'host-calls.jsonl') if (m/'host-calls.jsonl').exists() else []
    requests=[r for r in host if r['kind']=='host_request']
    replies=[r for r in host if r['kind']=='host_response']
    assert len(requests)==len(replies)
    wait=sum(r['result']['user_wait_seconds'] for r in replies)
    approval=sum(r['arguments']['kind']=='document_permission' for r in requests)
    user_inputs=len(requests)-approval
    observer=root/'native-prompts.jsonl'
    if not observer.exists():observer=root.parent/(scenario+'-native-prompts.jsonl')
    observed=lines(observer);first,last=observed[0],observed[-1]
    if first.get('ready') is not True or last.get('finished') is not True or not last.get('stopped_by_marker'):
        failures.append('native_observer_incomplete')
    added=sum(r.get('added',0) for r in observed)
    if added!=last.get('new_native_permission_candidates'):failures.append('native_observer_count_mismatch')
    timeline=lines(root/'timeline.jsonl')
    start=next(r['wall_ns'] for r in timeline if r['phase']=='execution_started')
    end=next(r['wall_ns'] for r in timeline if r['phase']=='execution_finished')
    native_execution=sum(r.get('added',0) for r in observed if start<=r.get('time_ns',0)<=end)
    permission={'controlled_user_wait_seconds':0,'recorded_user_wait_seconds':wait,
                'host_approval_requests':approval,'host_input_or_auth_requests':user_inputs,
                'host_requests_total':len(requests),'native_prompt_candidates_all_phases':added,
                'native_prompt_candidates_execution':native_execution,
                'native_prompt_candidates_initial':first.get('initial_candidates'),
                'native_samples':last.get('samples'),'native_partial_samples':last.get('partial_samples'),
                'native_max_sample_gap_seconds':last.get('max_sample_gap_seconds'),
                'native_observer_ready_before_execution':first['time_ns']<=start,
                'native_complete_approval_count':None,
                'native_count_limit':last.get('coverage'),
                'native_raw_sha256':sha(observer)}
    if not permission['native_observer_ready_before_execution']:failures.append('native_observer_started_late')
    result=cost.extract(m,'aside',wait,'pass' if not failures else 'failed')
    result.update(schema='yee.aside-approval-rerun.v1',scenario=scenario,success=not failures,
                  failures=sorted(set(failures)),permission=permission,original_oracle=task,
                  correlation=joined,manual_review=review,
                  intermediate_prose_messages=sum(bool(e.get('content')) and not e.get('tool_calls') and e.get('role')=='assistant' for e in events)-1,
                  final_answer=answer)
    result['source_sha256'].update({str(p):sha(p) for p in (root/'fixture/events.jsonl',root/'owner-final.json',root/'manual-review.json',observer)})
    (root/'rerun-validation.json').write_text(json.dumps(result,ensure_ascii=False,indent=2)+'\n')
    return result


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('record',type=Path)
    a=p.parse_args();v=inspect(a.record)
    print(json.dumps({k:v[k] for k in ('scenario','success','failures','zero_wait_runner_seconds','usage','permission')},ensure_ascii=False,indent=2))
