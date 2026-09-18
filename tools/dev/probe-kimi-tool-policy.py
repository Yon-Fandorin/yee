#!/usr/bin/env python3
"""Run installed Kimi against a localhost synthetic provider/MCP, never a real model.

Private KIMI_CODE_HOME; macOS child network sandbox permits loopback only.
Deliberately emits a forbidden Bash call writing only a disposable canary.
Provider usage is fabricated fixture data, NOT actual model usage or billing.
"""
import argparse
import hashlib
import base64
import importlib.util
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
import os
from pathlib import Path
import subprocess
import sys
import threading
import time
from types import SimpleNamespace


def private_json(path, value):
    fd=os.open(path,os.O_WRONLY|os.O_CREAT|os.O_EXCL,0o600)
    with os.fdopen(fd,'w') as stream:json.dump(value,stream,indent=2)


def tool_spec(profile):
    if profile=='yee':
        return 'yee_browser', {'commands':[['status']]}, {'type':'object','properties':{'commands':{'type':'array'}},'required':['commands']}
    if profile=='aside':
        return 'repl', {'title':'Synthetic check','code':'console.log("synthetic")'}, {
            'type':'object','properties':{'title':{'type':'string'},'code':{'type':'string'}},'required':['title','code']}
    raise ValueError('unsupported synthetic tool profile')


def mcp(log, two_blocks=False, tool_profile='yee'):
    tool,_,schema=tool_spec(tool_profile)
    for line in sys.stdin:
        request=json.loads(line);method=request.get('method')
        fd=os.open(log,os.O_WRONLY|os.O_CREAT|os.O_APPEND,0o600)
        with os.fdopen(fd,'a') as stream:stream.write(json.dumps({'method':method,'params':request.get('params')})+'\n')
        if 'id' not in request:continue
        if method=='initialize':
            result={'protocolVersion':request['params']['protocolVersion'],'capabilities':{'tools':{}},
                    'serverInfo':{'name':'synthetic-yee-policy-probe','version':'1'}}
        elif method=='tools/list':
            result={'tools':[{'name':tool,'description':'Synthetic tool only; no browser connection.',
                              'inputSchema':schema}]}
        elif method=='tools/call':
            result={'content':[{'type':'text','text':'SYNTHETIC_YEE_TOOL_EXECUTED'}]}
            if two_blocks:result['content'].append({'type':'text','text':'SECOND_BLOCK_ORDER_MARKER'})
        else:result={}
        print(json.dumps({'jsonrpc':'2.0','id':request['id'],'result':result}),flush=True)


def evaluate_policy(requests, mcp_events, stdout, exit_code, canary_created, two_blocks=False, tool_profile='yee'):
    tool,arguments,_=tool_spec(tool_profile)
    model_tool=f'mcp__{tool_profile}__{tool}'
    rows=[json.loads(line) for line in stdout.splitlines() if line.strip()]
    offered={tool['name'] for r in requests for tool in r.get('tools',[])}
    calls=[e for e in mcp_events if e['method']=='tools/call']
    tool_rows=[row for row in rows if row.get('role')=='tool']
    results={row.get('tool_call_id'):row.get('content') for row in tool_rows}
    requested=[call for row in rows if row.get('role')=='assistant' for call in row.get('tool_calls',[])]
    expected_calls={'allowed_call':model_tool,'forbidden_call':'Bash'}
    calls_match=(len(requested)==2 and len(tool_rows)==2 and len(results)==2
                 and {call.get('id'):call.get('function',{}).get('name') for call in requested}==expected_calls)
    expected_result=('SYNTHETIC_YEE_TOOL_EXECUTEDSECOND_BLOCK_ORDER_MARKER'
                     if two_blocks else 'SYNTHETIC_YEE_TOOL_EXECUTED')
    allowed=(len(calls)==1 and calls[0]['params']=={'name':tool,'arguments':arguments}
             and results.get('allowed_call')==expected_result)
    denied=results.get('forbidden_call')=='Tool "Bash" not found' and not canary_created
    return {'synthetic_backend':True,'actual_model_usage':'not measured; fake provider counters',
            'provider_requests':len(requests),'offered_tools':sorted(offered),'two_blocks':two_blocks,'tool_profile':tool_profile,
            'permitted_mcp_calls':len(calls),'forbidden_canary_created':canary_created,
            'process_exit_code':exit_code,'runtime_denial_verified':denied and calls_match,
            'allowed_tool_verified':allowed,
            'policy_probe_pass':(exit_code==0 and len(requests)==2 and calls_match and allowed and denied
                                 and offered=={model_tool})}


def run_probe(args):
    tool,tool_arguments,_=tool_spec(args.tool_profile)
    model_tool=f'mcp__{args.tool_profile}__{tool}'
    root=args.record
    if not root.is_absolute():raise ValueError('record directory must be absolute and new')
    root.mkdir(mode=0o700)
    for directory in ('kimi-home','workspace','skills','workspace/.git','workspace/.kimi-code'):
        (root/directory).mkdir(mode=0o700)
    # Keep ordinary HOME intact; Kimi's documented override isolates its state.
    env={key:value for key,value in os.environ.items() if key in ('PATH','HOME','USER','TMPDIR','SHELL','LANG')}
    env['KIMI_CODE_HOME']=str(root/'kimi-home')
    policy=root/'loopback.sb'
    policy.write_text('(version 1)\n(allow default)\n(deny network*)\n'
                      '(allow network-outbound (remote ip "localhost:*"))\n'
                      '(allow network-inbound (local ip "localhost:*"))\n'
                      '(allow network* (local unix-socket) (remote unix-socket))\n')
    policy.chmod(0o600)
    prefix=['/usr/bin/sandbox-exec','-f',str(policy)]
    requests=[]
    class Handler(BaseHTTPRequestHandler):
        def log_message(self,*_):pass
        def do_GET(self):
            self.send_response(200);self.end_headers();self.wfile.write(b'LOCAL_PROBE')
        def do_POST(self):
            size=int(self.headers.get('Content-Length','0'))
            if not 0<size<2*1024*1024:self.send_error(413);return
            body=json.loads(self.rfile.read(size));requests.append(body)
            # Persist only this synthetic isolated run. No user config is loaded.
            private_json(root/f'provider-request-{len(requests)}.json',body)
            if len(requests)==1:
                blocks=[{'type':'tool_use','id':'allowed_call','name':model_tool,
                         'input':tool_arguments},
                        {'type':'tool_use','id':'forbidden_call','name':'Bash',
                         'input':{'command':'printf forbidden > forbidden-canary'}}]
                stop='tool_use'
            else:blocks=[{'type':'text','text':'Synthetic policy probe finished.'}];stop='end_turn'
            message={'id':'msg_probe','type':'message','role':'assistant','model':'claude-sonnet-4-20250514',
                     'content':blocks,'stop_reason':stop,'stop_sequence':None,
                     'usage':{'input_tokens':1,'output_tokens':1}}
            self.send_response(200)
            if not body.get('stream'):
                self.send_header('Content-Type','application/json');self.end_headers()
                self.wfile.write(json.dumps(message).encode());return
            self.send_header('Content-Type','text/event-stream');self.end_headers()
            events=[{'type':'message_start','message':{**message,'content':[],'stop_reason':None}}]
            for index,block in enumerate(blocks):
                initial={**block,'input':{}} if block['type']=='tool_use' else {'type':'text','text':''}
                delta=({'type':'input_json_delta','partial_json':json.dumps(block['input'])}
                       if block['type']=='tool_use' else {'type':'text_delta','text':block['text']})
                events.extend([{'type':'content_block_start','index':index,'content_block':initial},
                               {'type':'content_block_delta','index':index,'delta':delta},
                               {'type':'content_block_stop','index':index}])
            events.extend([{'type':'message_delta','delta':{'stop_reason':stop,'stop_sequence':None},'usage':{'output_tokens':1}},
                           {'type':'message_stop'}])
            for event in events:self.wfile.write(('event: '+event['type']+'\ndata: '+json.dumps(event)+'\n\n').encode())
            self.wfile.flush()
    server=ThreadingHTTPServer(('127.0.0.1',0),Handler)
    thread=threading.Thread(target=server.serve_forever,daemon=True);thread.start()
    try:
        origin=f'http://127.0.0.1:{server.server_port}'
        # Verify the OS restriction, rather than assuming a profile parsed.
        netcode=('import socket,urllib.request,json\n'
                 f'assert urllib.request.urlopen({origin!r}).read()==b"LOCAL_PROBE"\n'
                 's=socket.socket();s.settimeout(1)\n'
                 'try:s.connect(("203.0.113.1",443))\n'
                 'except PermissionError:print("LOOPBACK_ONLY_VERIFIED")\n'
                 'else:raise RuntimeError("external network was not denied")\n')
        network=subprocess.run(prefix+[sys.executable,'-c',netcode],capture_output=True,text=True,env=env,timeout=10)
        private_json(root/'network-check.json',{'code':network.returncode,'stdout':network.stdout,'stderr':network.stderr})
        if network.returncode or 'LOOPBACK_ONLY_VERIFIED' not in network.stdout:
            raise RuntimeError('loopback-only OS restriction not established; Kimi was not launched')
        config=f'''default_model = "local-probe"
telemetry = false
[providers.probe]
type = "anthropic"
base_url = "{origin}"
api_key = "synthetic-no-credentials"
[models.local-probe]
provider = "probe"
model = "claude-sonnet-4-20250514"
max_context_size = 128000
max_output_size = 1024
capabilities = ["tool_use"]
[thinking]
enabled = false
[loop_control]
max_steps_per_turn = 4
max_attempts_per_step = 1
[model_catalog]
refresh_on_start = false
refresh_interval_ms = 0
[experimental]
tool-select = false
[[permission.rules]]
decision = "allow"
pattern = "{model_tool}"
'''
        (root/'kimi-home/config.toml').write_text(config);(root/'kimi-home/config.toml').chmod(0o600)
        profile=root/'browser-only.md'
        profile.write_text('---\nname: yee-policy-probe\ndescription: Synthetic local tool-policy test\n'
                           f'tools: [{model_tool}]\ndisallowedTools: [select_tools]\nsubagents: []\n---\n'
                           '${base_prompt}\nUse only the synthetic MCP tool. Never use files or shell.\n')
        profile.chmod(0o600)
        mcp_args=[str(Path(__file__).resolve()),'--mcp-log',str(root/'mcp.jsonl'),
                  '--tool-profile',args.tool_profile]
        mcp_args += ['--two-blocks'] if args.two_blocks else []
        relay=Path(__file__).with_name('record-mcp-stdio.py').resolve()
        if args.wire_relay:
            mcp_args=[str(relay),'--record',str(root/'mcp-wire.jsonl'),
                      '--stderr',str(root/'mcp-upstream.stderr'),'--timeout','60','--',
                      sys.executable,*mcp_args]
        private_json(root/'kimi-home/mcp.json',{'mcpServers':{args.tool_profile:{
            'command':sys.executable,'args':mcp_args}}})
        version=subprocess.run(prefix+[str(args.kimi),'--version'],capture_output=True,text=True,env=env,timeout=10)
        if version.returncode or version.stdout.strip()!='0.41.0':raise RuntimeError('expected installed Kimi0.41.0')
        with args.kimi.open('rb') as binary:binary_hash=hashlib.file_digest(binary,'sha256').hexdigest()
        private_json(root/'manifest.json',{'synthetic_backend':True,'kimi_version':version.stdout.strip(),
                     'kimi_sha256':binary_hash,'probe_sha256':hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
                     'profile_sha256':hashlib.sha256(profile.read_bytes()).hexdigest(),
                     'wire_relay':args.wire_relay,
                     'tool_profile':args.tool_profile,
                     'relay_sha256':hashlib.sha256(relay.read_bytes()).hexdigest() if args.wire_relay else None,
                     'source_semantics_commit':'95478e8c7ba248fd2470d5bb151555ec7fedd19d'})
        doctor=subprocess.run(prefix+[str(args.kimi),'doctor','config',str(root/'kimi-home/config.toml')],
                              capture_output=True,text=True,env=env,cwd=root/'workspace',timeout=20)
        private_json(root/'doctor.json',{'code':doctor.returncode,'stdout':doctor.stdout,'stderr':doctor.stderr})
        if doctor.returncode:raise RuntimeError('synthetic config rejected; no provider request attempted')
        command=prefix+[str(args.kimi),'--agent-file',str(profile),'--skills-dir',str(root/'skills'),
                        '-m','local-probe','--output-format','stream-json','-p','Call the synthetic MCP tool once.']
        started=time.monotonic()
        spec=importlib.util.spec_from_file_location('kimi_supervisor',Path(__file__).with_name('run-kimi-recorded.py'))
        supervisor=importlib.util.module_from_spec(spec);spec.loader.exec_module(supervisor)
        with (root/'stdout.jsonl').open('xb') as output,(root/'stderr.txt').open('xb') as errors:
            (root/'stdout.jsonl').chmod(0o600);(root/'stderr.txt').chmod(0o600)
            lifecycle=supervisor.execute(command,cwd=root/'workspace',env=env,timeout=60,
                                         stdout=output,stderr=errors)
        private_json(root/'lifecycle.json',lifecycle)
        process=SimpleNamespace(returncode=lifecycle['returncode'],
            stdout=(root/'stdout.jsonl').read_text(),stderr=(root/'stderr.txt').read_text())
        private_json(root/'process.json',{'code':process.returncode,'stdout':process.stdout,'stderr':process.stderr,
                                         'elapsed_ms':(time.monotonic()-started)*1000})
        mcp_events=[json.loads(s) for s in (root/'mcp.jsonl').read_text().splitlines()] if (root/'mcp.jsonl').exists() else []
        summary=evaluate_policy(requests,mcp_events,process.stdout,process.returncode,
                                (root/'workspace/forbidden-canary').exists(), args.two_blocks, args.tool_profile)
        summary['process_lifecycle']=lifecycle
        summary['policy_probe_pass'] &= not any(lifecycle[key] for key in
            ('timed_out','interrupted','descendants_after_normal_exit'))
        if args.wire_relay:
            rows=[json.loads(line) for line in (root/'mcp-wire.jsonl').read_text().splitlines()]
            summary['wire_relay']=evaluate_relay(rows,mcp_events)
            summary['policy_probe_pass'] &= summary['wire_relay']['verified']
        private_json(root/'result.json',summary)
        print(json.dumps(summary))
        if not summary['policy_probe_pass']:raise RuntimeError('Kimi tool policy probe did not pass')
    finally:server.shutdown();server.server_close();thread.join(timeout=2)


def evaluate_relay(rows, mcp_events):
    received={};forwarded=set();methods=[]
    for sequence,row in enumerate(rows,1):
        if type(row.get('sequence')) is not int or row['sequence']!=sequence or row.get('direction') not in ('to_server','to_client'):
            raise ValueError('invalid wire record ordering/direction')
        frame=row.get('frame')
        if row.get('kind')=='wire_received':
            if not isinstance(frame,str) or not frame or frame in received:raise ValueError('duplicate wire frame')
            message=json.loads(base64.b64decode(row['base64'],validate=True))
            received[frame]=row['direction']
            if row['direction']=='to_server':
                methods.append({'method':message.get('method'),'params':message.get('params')})
        elif row.get('kind')=='wire_forwarded':
            if frame not in received or frame in forwarded or received[frame]!=row['direction']:
                raise ValueError('unmatched wire forwarding')
            forwarded.add(frame)
        else:raise ValueError('unknown wire event')
    return {'verified':bool(received) and set(received)==forwarded and methods==mcp_events,
            'received_frames':len(received),'forwarded_frames':len(forwarded),
            'synthetic_server_requests_match':methods==mcp_events}


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--record',type=Path)
    parser.add_argument('--kimi',type=Path,default=Path('/Users/yongjunkim/.kimi-code/bin/kimi'))
    parser.add_argument('--mcp-log',type=Path)
    parser.add_argument('--two-blocks',action='store_true')
    parser.add_argument('--wire-relay',action='store_true')
    parser.add_argument('--tool-profile',choices=('yee','aside'),default='yee')
    args=parser.parse_args()
    if args.mcp_log:mcp(args.mcp_log,args.two_blocks,args.tool_profile)
    elif args.record:run_probe(args)
    else:parser.error('--record is required')
