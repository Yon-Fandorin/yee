#!/usr/bin/env python3
"""Independent original oracles, actual ACP wire and cold lifecycle audit."""
import argparse
import importlib.util
import json
from pathlib import Path

from grok_acp_usage import FIELDS
from mcp_wire_operations import derive

spec=importlib.util.spec_from_file_location('common_inspector',Path(__file__).with_name('inspect-grok-persistent-cohort.py'))
common=importlib.util.module_from_spec(spec)
spec.loader.exec_module(common)


def inspect(root,reviews):
    plan=common.read(root/'plan.json')
    browser=plan['browser']
    expected=[[browser,f'S{i:02}'] for i in range(1,13)]
    if plan['schema']!='yee.actual-fresh-acp-cohort.v1' or plan['cases']!=expected:
        raise ValueError('Fresh ACP audit requires all original twelve tasks')
    rows=[common.inspect_case(root/b/s,reviews) for b,s in expected]
    result={'schema':'yee.actual-fresh-acp-ledger.v1','root':str(root),'rows':rows,'accepted':False,'failures':[]}
    try:
        if plan['reasoning_effort']!='high' or plan['timeout_seconds']!=240 or not plan['initial_setup_included']:
            raise ValueError('Fresh/persistent runtime conditions differ')
        complete=common.read(root/'collection-complete.json')
        timing=common.read(root/'cohort-timing.json')
        if complete['cases']!=12 or not complete['source_integrity'] or timing['failed']:
            raise ValueError('Fresh collection lifecycle incomplete')
        identities=[]
        totals={key:0 for key in FIELDS}
        pids=[]
        for row in rows:
            case=Path(row['case'])
            record=common.read(case/'model/acp-prompt.json')
            summary=common.read(case/'model/summary.json')
            if summary['model_process_reused'] or summary['mcp_connection_reused']:
                raise ValueError('Fresh model/MCP identity was reused')
            identities.append({'session_id':record['session_id'],'agent_pid':record['agent_pid']})
            for key in FIELDS:totals[key]+=row['usage'][key]
            ops,_=derive(common.lines(case/'mcp-wire.jsonl'))
            if sum(op['kind']=='mcp_request' and op['operation']=='initialize' for op in ops)!=1:
                raise ValueError('Fresh case must have exactly one browser MCP initialization')
            if browser=='yee':
                pids.append(common.read(case/'launch.json')['peer_pid'])
                cleanup=common.read(case/'profile-cleanup.json')
                if not cleanup['removed'] or Path(cleanup.get('profile','')).exists():
                    raise ValueError('Fresh Yee profile cleanup incomplete')
        if len({x['session_id'] for x in identities})!=12 or len({x['agent_pid'] for x in identities})!=12:
            raise ValueError('Twelve independent fresh model sessions/processes required')
        if browser=='yee' and len(set(pids))!=12:
            raise ValueError('Fresh Yee process was reused')
        usage={'totals':totals,'task_identities':identities,'cached_input_included_in_input':True}
        if usage!=common.read(root/'usage.json'):
            raise ValueError('Actual usage aggregate mismatch')
        result['usage']=usage
        if browser=='aside' and not common.read(root/'owned-native-cleanup.json')['verified']:
            raise ValueError('Owned Native Aside cleanup incomplete')
        elapsed=(timing['finished_monotonic_ns']-timing['started_monotonic_ns'])/1e9
        if abs(elapsed-timing['elapsed_seconds'])>1e-6:
            raise ValueError('Inclusive cohort timing mismatch')
        result['cohort_timing']=timing
        if any(common.sha(Path(path))!=digest for path,digest in common.read(root/'full-freeze.json').items()):
            raise ValueError('Source/App/operator freeze changed')
    except (ValueError,KeyError,FileNotFoundError) as exc:
        result['failures'].append('fresh_lifecycle: '+str(exc))
    result['accepted']=not result['failures'] and all(row['accepted'] for row in rows)
    return result


if __name__=='__main__':
    parser=argparse.ArgumentParser()
    parser.add_argument('root',type=Path)
    parser.add_argument('--reviews',type=Path,required=True)
    parser.add_argument('--output',type=Path,required=True)
    args=parser.parse_args()
    value=inspect(args.root,args.reviews)
    args.output.write_text(json.dumps(value,indent=2)+'\n')
    print(json.dumps({'accepted':value['accepted'],'passed':sum(row['accepted'] for row in value['rows']),'failures':value['failures']}))
