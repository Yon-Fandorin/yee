#!/usr/bin/env python3
"""One fresh Aside model trial, with native prompt observations and raw evidence.

For non-handoff scenarios; S08/S10/S12 use run-aside-handoff-trial.py.
The operator only prepares/verifies its synthetic tab. The model does the task.
"""
import argparse
import asyncio
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import threading
import time
from mcp import ClientSession, StdioServerParameters
from mcp.client.stdio import stdio_client
from agent_scenario_fixture import Fixture, server_for
from recorded_mcp_session import RecordedSession
from yee_browser_transcript import Transcript
from yee_trial_timeline import append

REPO = Path(__file__).resolve().parents[2]


def save(path, value):
    path.write_text(json.dumps(value, ensure_ascii=False, indent=2)+'\n')
    path.chmod(0o600)


async def run(a):
    root = a.record
    if not root.is_absolute(): raise ValueError('absolute new private record required')
    root.mkdir(mode=0o700)
    append(root/'timeline.jsonl', 'setup_started', record=root/'model')
    common = a.common_prompt.read_text()
    runner = (REPO/'tools/dev/run-grok-recorded.py'
              if a.engine == 'grok' else REPO/'tools/dev/run-kimi-recorded.py')
    sources = [Path(__file__), runner,
               REPO/'tools/dev/agent_scenario_fixture.py', a.common_prompt, a.watcher]
    save(root/'frozen.json', {str(p):hashlib.sha256(p.read_bytes()).hexdigest() for p in sources})
    fixture = Fixture(a.scenario, a.seed, root/'fixture')
    server = server_for(fixture,8787)
    thread = threading.Thread(target=server.serve_forever); thread.start()
    stop = root/'stop-native-observer'
    with (root/'native-prompts.jsonl').open('x') as observed, (root/'native-prompts.stderr').open('x') as errors:
        watcher = subprocess.Popen([str(a.watcher), str(stop), str(a.timeout+120)], stdout=observed, stderr=errors)
        try:
            deadline = time.monotonic()+10
            while not (root/'native-prompts.jsonl').stat().st_size:
                if watcher.poll() is not None or time.monotonic()>deadline: raise ValueError('native observer failed readiness')
                await asyncio.sleep(.05)
            initial = json.loads((root/'native-prompts.jsonl').read_text().splitlines()[0])
            if initial.get('ready') is not True: raise ValueError('native observer not ready')
            with Transcript(str(root/'owner.jsonl')) as trace, (root/'owner.stderr').open('x') as err:
                async with stdio_client(StdioServerParameters(command=str(a.aside),args=['mcp']),errlog=err) as (rd,wr):
                    async with ClientSession(rd,wr,read_timeout_seconds=45) as raw:
                        client=RecordedSession(raw,trace); await client.initialize()
                        save(root/'tools.json',(await client.list_tools()).model_dump(mode='json',by_alias=True))
                        async def call(title,code):
                            value=(await client.call_tool('repl',{'title':title,'code':code})).model_dump(mode='json',by_alias=True)
                            if value.get('isError'): raise ValueError('owned operator call failed: '+title)
                            return value
                        try:
                            made=await call('Prepare one owned synthetic task tab',
                                'var owned=await openTab("http://127.0.0.1:8787/"); console.log("OWNED:"+JSON.stringify({target_id:owned.targetId,url:owned.url(),viewport:await owned.evaluate(()=>({width:innerWidth,height:innerHeight,dpr:devicePixelRatio}))}));')
                            save(root/'created.json',made)
                            text='\n'.join(b['text'] for b in made['content'] if b['type']=='text')
                            tab=json.loads(text.split('OWNED:',1)[1]);save(root/'owned.json',tab)
                            expected_viewport={'width':1440,'height':900,'dpr':a.device_scale_factor}
                            assert tab['url']=='http://127.0.0.1:8787/' and tab['viewport']==expected_viewport
                            # Baseline establishes actual rendered fixture before the paid model starts.
                            save(root/'owner-initial.json',await call('Verify prepared synthetic fixture renders','console.log((await snapshot(owned)).tree);'))
                            prompt=common+'\nUse only the prepared Aside tab '+tab['target_id']+' at http://127.0.0.1:8787/ Verify its exact URL before reading content or editing. Tab inventory metadata may be listed for this authorized measurement. Do not attach to, read content from or change any other tab; do not use files, shell or other sites. Return ONLY one raw JSON object, no prose or fences.'
                            (root/'prompt.txt').write_text(prompt)
                            save(root/'authorization.json',{'scope':'tab metadata allowed; content and edits limited to owned synthetic tab',
                                'user_request':'비교 및 개선을 위한 어사이드 자료 및 시나리오 자료 수집해줘.',
                                'prior_metadata_authorization_retained':True,'zero_wait_comparison':True})
                            if a.engine == 'grok':
                                args=[sys.executable,str(runner),'--binary',str(a.grok),
                                      '--cwd',str(a.grok_workspace),'--record',str(root/'model'),
                                      '--model','grok-4.6','--prompt',str(root/'prompt.txt'),
                                      '--mcp-tool','aside__repl','--max-turns','12',
                                      '--mcp-wire-record',str(root/'mcp-wire.jsonl'),
                                      '--timeout',str(a.timeout),'--trust-project']
                            else:
                                args=[sys.executable,str(runner),
                                      '--browser','aside','--aside',str(a.aside),'--record',str(root/'model'),
                                      '--config',str(a.config),'--credentials',str(a.credentials),'--kimi',str(a.kimi),
                                      '--python',sys.executable,'--prompt',str(root/'prompt.txt'),
                                      '--aside-owned-tab',tab['target_id'],'--allow-aside-tab-metadata',
                                      '--timeout',str(a.timeout),'--timeline',str(root/'timeline.jsonl'),
                                      '--strict-final-json']
                            if a.scenario == 'S09': args.append('--draft-grounding')
                            print(json.dumps({'stage':'prepared','scenario':a.scenario,'viewport':tab['viewport']}),flush=True)
                            with (root/'runner-output.txt').open('w') as output:
                                result=await asyncio.to_thread(subprocess.run,args,stdout=output,stderr=subprocess.STDOUT)
                            print(json.dumps({'stage':'model_finished','returncode':result.returncode}),flush=True)
                            save(root/'owner-final.json',await call('Verify final owned task state',
                                'console.log("FINAL:"+JSON.stringify({target_id:owned.targetId,url:owned.url(),snapshot:(await snapshot(owned)).tree}));'))
                        finally:
                            if not client.broken:
                                save(root/'cleanup.json',await call('Close only the operator-created task tab',
                                    'if(typeof owned!=="undefined")await closeTab(owned);console.log("owned task tab closed");'))
        finally:
            stop.write_text('finished\n')
            try: watcher.wait(timeout=10)
            except subprocess.TimeoutExpired:
                watcher.terminate();watcher.wait(timeout=10)
            server.shutdown();thread.join();server.server_close();fixture.close()
            save(root/'source-integrity-at-finish.json',{str(p):hashlib.sha256(p.read_bytes()).hexdigest() for p in sources})
    print('COLLECTION_FINISHED',flush=True)


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--scenario',choices=['S01','S02','S03','S04','S05','S06','S07','S09','S11'],required=True)
    for name in ('record','aside','config','credentials','kimi','common-prompt','watcher'):
        p.add_argument('--'+name,type=Path,required=True)
    p.add_argument('--engine',choices=('kimi','grok'),default='kimi')
    p.add_argument('--grok',type=Path)
    p.add_argument('--grok-workspace',type=Path)
    p.add_argument('--seed',type=int,default=11)
    p.add_argument('--timeout',type=int,default=180)
    p.add_argument('--device-scale-factor',type=int,choices=(1,2),default=2,
                   help='exact devicePixelRatio required from the prepared Aside tab')
    args=p.parse_args()
    if args.engine == 'grok' and (args.grok is None or args.grok_workspace is None):
        p.error('--engine grok requires --grok and --grok-workspace')
    asyncio.run(run(args))
