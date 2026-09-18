#!/usr/bin/env python3
"""Original task oracles plus actual persistent ACP/MCP/browser evidence."""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path

from grok_acp_events import inspect_prompt
from grok_acp_usage import normalize
from grok_acp_correlation import correlate as correlate_acp
from mcp_wire_operations import derive

DEV=Path(__file__).resolve().parent
spec=importlib.util.spec_from_file_location('original_inspector',DEV/'inspect-grok-scenario-cohort.py')
old=importlib.util.module_from_spec(spec);spec.loader.exec_module(old)
read=lambda p:json.loads(p.read_text())
lines=lambda p:[json.loads(l) for l in p.read_text().splitlines() if l.strip()]
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()


def verify_index(root,case,filename,global_name):
    local=lines(case/'model'/filename);index=lines(root/global_name)
    selected=[r for r in index if r['case']==str(case)]
    if len(local)!=len(selected):raise ValueError('global/task journal coverage mismatch')
    for event,entry in zip(local,selected):
        if entry['task_sequence']!=event['sequence'] or entry['event']!={k:v for k,v in event.items() if k!='sequence'}:
            raise ValueError('global/task original journal mismatch')
    return {'task_sha256':sha(case/'model'/filename),'global_sha256':sha(root/global_name),
            'events':len(local),'original_events_written_directly':True}


def inspect_case(case,reviews):
    root=case.parent.parent;browser=case.parent.name
    fresh=read(root/'plan.json').get('schema')=='yee.actual-fresh-acp-cohort.v1'
    result={'case':str(case),'browser':browser,'scenario':case.name,'accepted':False,'failures':[]}
    failure=result['failures']
    if not (case/'model/acp-prompt.json').exists():
        failure.append('no_complete_prompt_record');return result
    record=read(case/'model/acp-prompt.json');start,end=record['global_event_offsets']
    with ((case/'model/acp-events.jsonl') if fresh else (root/'acp-events.jsonl')).open('rb') as f:
        f.seek(start);raw=f.read(end-start)
    events=[json.loads(l) for l in raw.splitlines()]
    result['original_acp_span_sha256']=hashlib.sha256(raw).hexdigest()
    parsed=None;native=None;wait=0
    try:
        parsed=inspect_prompt(record,events);result['actual_acp']=parsed
        if parsed['stop_reason']!='end_turn' or parsed['terminal_failure']:failure.append('terminal_incomplete_or_invalid')
        turns=parsed['provider_num_turns']
        if type(turns) is not int or not 1<=turns<=12:failure.append('provider_turn_cap_missing_or_exceeded')
        allowed={'yee__yee_browser'} if browser=='yee' else {'aside__repl','trial_host__request_user'}
        if any(call['name']=='use_tool' and call['arguments'].get('tool_name') not in allowed for call in parsed['calls']):
            failure.append('tool_scope_failed')
        session={'session_id':record['session_id'],'session_identity':{'cwd':str(case/'workspace' if fresh else root/'workspace')}}
        wire_root=case if fresh else case/'model'
        wires=[wire_root/'mcp-wire.jsonl']
        if (wire_root/'host-wire.jsonl').exists():wires.append(wire_root/'host-wire.jsonl')
        result['correlation']=correlate_acp(parsed,wires,old.received_text,session)
    except (ValueError,KeyError,FileNotFoundError) as exc:failure.append('actual_correlation: '+str(exc))
    try:
        result['usage']=normalize([record],record['session_id'])['totals']
        result['raw_model_seconds']=record['elapsed_seconds']
    except (ValueError,KeyError) as exc:failure.append('actual_usage: '+str(exc))
    review_path=reviews/root.name/(browser+'-'+case.name+'.json')
    review=read(review_path) if review_path.exists() else None
    if browser=='yee':
        try:
            native=lines(case/'model/mcp-native.jsonl')
            result['native']=old.native_audit.inspect(lines(case/'model/mcp-calls.jsonl'),native)
            if not result['native']['native_settlement_verified']:failure.append('native_settlement_incomplete')
            wait=result['native']['native_user_wait_seconds']
            if fresh:
                result['direct_native_journals']={name:sha(case/'model'/name) for name in ('mcp-native.jsonl','mcp-calls.jsonl')}
            else:
                result['native_index']=verify_index(root,case,'mcp-native.jsonl','native-global.jsonl')
                result['calls_index']=verify_index(root,case,'mcp-calls.jsonl','calls-global.jsonl')
        except (ValueError,KeyError,FileNotFoundError) as exc:failure.append('native: '+str(exc))
    else:
        scope=reviews/root.name/('aside-'+case.name+'-scope.json')
        if not scope.exists() or read(scope).get('original_acp_span_sha256')!=result['original_acp_span_sha256'] or read(scope).get('passed') is not True:
            failure.append('independent_aside_complete_input_review_required')
    try:
        events_fixture=lines(case/'fixture/events.jsonl');data=read(case/'fixture/dataset.json')
        result['oracle']=old.oracle.verify(case/'fixture',parsed['answer'] if parsed else None,native,review,'http://127.0.0.1:8787')
        success=result['oracle']['success']
        if browser=='aside' and case.name in ('S08','S12'):
            result['host']=old.host_audit(case,events_fixture,data,case/'mcp-wire.jsonl' if fresh else case/'model/mcp-wire.jsonl');wait=result['host']['synthetic_wait_seconds']
            success=(case.name=='S08' or result['oracle'].get('content_success')) and not result['oracle']['safety_violations']
        if browser=='aside' and case.name=='S10':success=result['oracle'].get('content_success',False)
        if not success:failure.append('original_scenario_oracle_failed')
    except (ValueError,KeyError,FileNotFoundError) as exc:failure.append('original_oracle: '+str(exc))
    if case.name=='S10':
        receipts=[]
        for path in (root/'control').glob('*.request.json'):
            q=read(path)
            if q['case']==str(case) and 'active_' in q['kind']:
                reply=path.with_name(path.name.replace('.request.','.reply.'))
                if not reply.exists() or read(reply).get('ok') is not True:failure.append('native_tab_restoration_unverified')
                else:receipts.append({'request':q,'reply':read(reply)})
        if len(receipts)<(2 if browser=='yee' else 3):failure.append('native_tab_calibration_missing')
        result['native_ui_receipts']=receipts
    if not (case/'source-integrity-at-finish.json').exists() or read(case/'source-integrity-at-finish.json')!=read(root/'frozen.json'):
        failure.append('source_integrity_missing')
    result['normalized_model_seconds']=record['elapsed_seconds']-wait if wait is not None else None
    result['recorded_user_wait_seconds']=wait
    result['task_pipeline_timing']=read(case/'timing-end.json') if (case/'timing-end.json').exists() else None
    result['accepted']=not failure
    return result


def inspect(root,reviews):
    plan=read(root/'plan.json');rows=[inspect_case(root/b/s,reviews) for b,s in plan['cases']]
    result={'schema':'yee.actual-persistent-ledger.v1','root':str(root),'rows':rows,'accepted':False,'failures':[]}
    try:
        complete=read(root/'collection-complete.json');timing=read(root/'cohort-timing.json')
        records=[read(Path(r['case'])/'model/acp-prompt.json') for r in rows]
        if len(records)!=12 or len({r['session_id'] for r in records})!=1 or len({r['agent_pid'] for r in records})!=1:
            raise ValueError('twelve tasks did not share actual agent/session')
        if complete['same_agent_pid']!=records[0]['agent_pid'] or complete['same_session_id']!=records[0]['session_id']:
            raise ValueError('continuous lifecycle identity mismatch')
        if plan['browser']=='yee' and len({read(Path(r['case'])/'launch.json')['peer_pid'] for r in rows})!=1:
            raise ValueError('Yee browser process was replaced')
        usage=normalize(records,records[0]['session_id']);result['usage']=usage
        if usage!=read(root/'usage.json'):raise ValueError('whole-prompt usage aggregate mismatch')
        if timing['failed'] or timing['aside_native_S10_cleanup_pending']:
            raise ValueError('cohort lifecycle or cleanup incomplete')
        elapsed=(timing['finished_monotonic_ns']-timing['started_monotonic_ns'])/1e9
        if abs(elapsed-timing['elapsed_seconds'])>1e-6:raise ValueError('cohort timing mismatch')
        result['cohort_timing']=timing
        index=lines(root/'mcp-wire.jsonl.global')
        operations,_=derive([{'sequence':r['sequence'],**r['event']} for r in index])
        if sum(r['kind']=='mcp_request' and r['operation']=='initialize' for r in operations)!=1:
            raise ValueError('browser MCP connection was not continuous')
        result['browser_mcp_initializations']=1
        marker=read(root/('connection-native-yee.json' if plan['browser']=='yee' else 'connection-wire-aside.json'))
        result['browser_mcp_server_pid']=marker['pid']
        if plan['browser']=='yee' and marker['peer_pid']!=complete['same_yee_pid']:
            raise ValueError('MCP targeted a different Yee process')
        if any(sha(Path(p))!=v for p,v in read(root/'full-freeze.json').items()):raise ValueError('full candidate freeze changed')
    except (ValueError,KeyError,FileNotFoundError) as exc:result['failures'].append('continuity: '+str(exc))
    result['accepted']=not result['failures'] and len(rows)==12 and all(r['accepted'] for r in rows)
    return result


if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('root',type=Path);p.add_argument('--reviews',type=Path,required=True);p.add_argument('--output',type=Path,required=True);args=p.parse_args();value=inspect(args.root,args.reviews);args.output.write_text(json.dumps(value,indent=2)+'\n');print(json.dumps({'accepted':value['accepted'],'passed':sum(r['accepted'] for r in value['rows']),'failures':value['failures']}))
