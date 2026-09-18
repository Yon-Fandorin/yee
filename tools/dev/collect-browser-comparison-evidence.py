#!/usr/bin/env python3
"""Extract per-model and per-tool costs from existing, hash-bound trial records.

No model calls, browser access, token estimation or rewriting of trial evidence.
"""
import argparse
import base64
from collections import Counter
import hashlib
import json
from pathlib import Path
import re
from mcp_wire_operations import derive


def load(path):
    return json.loads(path.read_text())


def lines(path):
    return [json.loads(s) for s in path.read_text().splitlines()]


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def verify_hashes(root, hashes):
    if not hashes: raise ValueError('missing prior evidence hashes')
    for name, expected in hashes.items():
        if digest(root/Path(name)) != expected:
            raise ValueError('prior reviewed evidence changed: '+name)


def models(root):
    paths=list((root/'kimi-home/sessions').glob('**/wire.jsonl'))
    assert len(paths)==1
    rows=lines(paths[0]); pending=None; steps=[]
    for row in rows:
        if row['type']=='llm.request':
            assert pending is None
            pending=row
        if row['type']=='usage.record':
            assert pending is not None
            usage=row['usage']
            inputs=sum(usage.get(k,0) for k in ('inputOther','inputCacheRead','inputCacheCreation'))
            elapsed=(row['time']-pending['time'])/1000
            assert elapsed>=0
            steps.append({'index':len(steps)+1,'request_to_usage_seconds':elapsed,
                          'input_tokens':inputs,'cached_input_tokens':usage.get('inputCacheRead',0),
                          'output_tokens':usage.get('output',0),
                          'total_tokens':inputs+usage.get('output',0),
                          'model':pending['model'],'thinking':pending.get('thinkingEffort'),
                          'message_count':pending.get('messageCount')})
            pending=None
    assert pending is None and steps
    return steps,paths[0],Counter(r['type'] for r in rows if 'permission' in r['type'])


def aside_calls(root):
    path=root/'mcp-wire.jsonl';rows=lines(path)
    operations,wire=derive(rows); calls=[];notifications=Counter(); reverse=Counter()
    for row in rows:
        if row['kind']!='wire_received':continue
        msg=json.loads(base64.b64decode(row['base64']))
        if 'method' in msg:
            if 'id' not in msg:notifications[row['direction']+':'+msg['method']]+=1
            elif row['direction']=='to_client':reverse[msg['method']]+=1
    for i in range(0,len(operations),2):
        begin,end=operations[i:i+2]
        if begin['operation']!='tools/call':continue
        code=begin['params']['arguments'].get('code','')
        text='\n'.join(b['text'] for b in end['result']['content'] if b['type']=='text')
        syntax={name:len(re.findall(pattern,code)) for name,pattern in {
            'snapshot':r'\bsnapshot\s*\(', 'evaluate':r'\.evaluate\s*\(',
            'fill':r'\.fill\s*\(', 'click':r'\.click\s*\(',
            'navigate':r'\.(?:goto|navigate)\s*\(', 'inventory':r'\blistBrowserTabs\s*\(',
            'attach':r'\battachBrowserTab\s*\(', 'fixed_wait_api':r'\b(?:sleep|waitForTimeout)\s*\('
        }.items()}
        codes=[]
        for label,pattern in [('stale_reference',r'stale|not found|does not exist'),
                              ('unsupported_api',r'not a function|is not defined'),
                              ('syntax_error',r'SyntaxError'),('timeout',r'\bTimeout(?:Error)?\b|timed out')]:
            if end['result'].get('isError') and re.search(pattern,text,re.I):codes.append(label)
        calls.append({'index':len(calls)+1,'seconds':end['elapsed_ns']/1e9,
                      'response_utf8_bytes':sum(len(b['text'].encode()) for b in end['result']['content'] if b['type']=='text'),'is_error':end['result']['isError'],
                      'error_categories':codes,'code_sha256':hashlib.sha256(code.encode()).hexdigest(),
                      'syntactic_api_occurrences':syntax,
                      'has_image':any(b['type']=='image' for b in end['result']['content'])})
    return calls,path,{'notifications':dict(notifications),'server_requests':dict(reverse),
                       'wire_audit':wire,'native_approval_count':None}


def yee_calls(root):
    path=root/'mcp-calls.jsonl'; rows=lines(path);calls=[]
    for i in range(0,len(rows),2):
        begin,end=rows[i:i+2]
        assert begin['invocation']==end['invocation'] and end['kind']=='mcp_response'
        text='\n'.join(end['content'])
        calls.append({'index':len(calls)+1,'seconds':(end['time_ns']-begin['time_ns'])/1e9,
                      'response_utf8_bytes':len(text.encode()),'is_error':end['is_error'],
                      'action':begin['arguments'].get('action','legacy_commands')})
    native_path=root/'native.jsonl';native=lines(native_path)
    commands=Counter(r['request']['command'] for r in native if r['kind']=='request')
    errors=Counter(r['response'].get('error','unknown') for r in native
                   if r['kind']=='response' and not r['response']['ok'])
    return calls,path,{'native_commands':dict(commands),'native_errors':dict(errors),
                       'native_sha256':digest(native_path)}


def extract(root,engine,wait,status):
    summary=load(root/'summary.json'); steps,wire,permissions=models(root)
    assert sum(s['total_tokens'] for s in steps)==summary['usage']['total_tokens']
    calls,path,extra=(aside_calls if engine=='aside' else yee_calls)(root)
    runner=summary['runner_elapsed_seconds'];tool_seconds=sum(c['seconds'] for c in calls)
    assert 0<=wait<=runner
    result={'source':str(root),'status':status,'runner_seconds_raw':runner,
            'zero_wait_runner_seconds':runner-wait,'recorded_wait_seconds':wait,
            'controlled_wait_seconds':0,'usage':summary['usage'],
            'model_calls':len(steps),'browser_mcp_calls':len(calls),
            'model_request_to_usage_seconds':sum(s['request_to_usage_seconds'] for s in steps),
            'browser_tool_seconds':tool_seconds,'response_utf8_bytes':sum(c['response_utf8_bytes'] for c in calls),
            'mcp_errors':sum(c['is_error'] for c in calls),
            'per_model_call':steps,'per_browser_call':calls,
            'client_permission_event_types':dict(permissions),**extra,
            'source_sha256':{str(p):digest(p) for p in (root/'summary.json',root/'stdout.jsonl',wire,path)}}
    if engine=='aside':
        result['syntactic_api_occurrences']=dict(sum((Counter(c['syntactic_api_occurrences']) for c in calls),Counter()))
    return result


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--normalized',type=Path,required=True)
    p.add_argument('--yee-root',type=Path,required=True)
    p.add_argument('--s09-supplement',type=Path)
    p.add_argument('--output',type=Path,required=True)
    a=p.parse_args();baseline=load(a.normalized)
    docs=a.normalized.parent
    suite=load(docs/'aside-scenario-suite-20260909.json')
    approved=load(docs/'aside-approved-three-20260910.json')
    replacements={c['scenario']:Path(c['source_root']) for c in approved['cases']}
    prior_aside={c['scenario']:c for c in suite['cases']}
    prior_approved={c['scenario']:c for c in approved['cases']}
    yee={c['scenario']:c for c in load(a.yee_root/'after-summary.json')}
    catalog=load(Path(__file__).resolve().parents[2]/'tests/fixtures/scenarios/catalog.json')
    tasks={c['id']:c for c in catalog['scenarios']}
    cases=[]
    for old in baseline['cases']:
        sid=old['scenario'];task=tasks[sid]
        ar=replacements.get(sid,Path(suite['source_root'])/sid)
        verify_hashes(ar,prior_approved[sid]['record_sha256'] if sid in replacements else
                      prior_aside[sid]['scope_review']['sha256'])
        yr=a.yee_root/yee[sid]['attempt']
        verify_hashes(yr,load(yr/'inspection.json')['input_sha256'])
        entry={'scenario':sid,'task':task['task'],'capabilities':task['capabilities'],
               'acceptance':task['oracle'],'forbidden_effect':task['safety_failure'],
               'prior_reviewed_evidence_hashes_verified':True}
        entry['aside']=extract(ar/'model','aside',old['aside']['recorded_user_wait_seconds'],old['aside']['status'])
        entry['yee']=extract(a.yee_root/yee[sid]['attempt']/'model','yee',old['yee']['recorded_user_wait_seconds'],old['yee']['status'])
        cases.append(entry)
    extra=None
    if a.s09_supplement:
        review=load(a.s09_supplement/'validation.json')
        assert review['model_stdout_sha256']==digest(a.s09_supplement/'model/stdout.jsonl')
        verify_hashes(a.s09_supplement,review['source_sha256'])
        extra={'scenario':'S09','validation':review,
               'aside':extract(a.s09_supplement/'model','aside',0,review['status'])}
    result={'schema':'yee.browser-comparison-evidence.v1','date':'2026-09-10',
            'cases':cases,'new_s09_supplement':extra,
            'limits':['Historical baseline preserved; supplement is not silently substituted.',
                      'Request-to-usage includes provider/network/client time; not pure inference.',
                      'API occurrences are syntax counts, not executed actions; fixture events verify effects.',
                      'Response bytes are not tokens. Cached input remains included in actual total tokens.',
                      'Transport or client permission events do not measure all native Aside UI approvals.',
                      'S08/S12 use a separate host; S10 native viewport differs from historical Yee.'],
            'inputs_sha256':{str(x):digest(x) for x in (a.normalized,docs/'aside-scenario-suite-20260909.json',docs/'aside-approved-three-20260910.json')}}
    a.output.write_text(json.dumps(result,ensure_ascii=False,indent=2)+'\n')
    for c in cases:
        print(c['scenario'],*[f"{e}: {c[e]['model_calls']} model / {c[e]['browser_mcp_calls']} MCP / {c[e]['zero_wait_runner_seconds']:.3f}s / {c[e]['usage']['total_tokens']} tokens" for e in ('aside','yee')])


if __name__=='__main__':main()
