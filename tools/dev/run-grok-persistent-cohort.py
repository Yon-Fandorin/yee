#!/usr/bin/env python3
"""Real browser/model/MCP continuity across the original twelve independent tasks.

One ACP session, one browser MCP connection and (for Yee) one exact launched
browser process last for this cohort. Browser/connection/preparation/verification
and final cleanup are included in monotonic cohort timing. No failed task retry.
"""
import argparse
import asyncio
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import subprocess
import sys
import threading
import time
import uuid

from mcp import ClientSession, StdioServerParameters
from mcp.client.stdio import stdio_client
from grok_acp_session import AcpSession
from grok_acp_usage import normalize
from grok_acp_events import inspect_prompt
from yee_browser_transcript import Transcript

DEV=Path(__file__).resolve().parent
spec=importlib.util.spec_from_file_location('original_cohort',DEV/'run-grok-scenario-cohort.py')
c=importlib.util.module_from_spec(spec);sys.modules[spec.name]=c;spec.loader.exec_module(c)
spec=importlib.util.spec_from_file_location('persistent_native_audit',DEV/'inspect-yee-mcp-calls.py')
native_audit=importlib.util.module_from_spec(spec);spec.loader.exec_module(native_audit)


def save(path,value):
    path=Path(path);temporary=path.with_name(path.name+'.'+uuid.uuid4().hex+'.tmp')
    fd=os.open(temporary,os.O_WRONLY|os.O_CREAT|os.O_EXCL,0o600)
    with os.fdopen(fd,'w') as f:json.dump(value,f,ensure_ascii=False,indent=2);f.write('\n')
    os.replace(temporary,path)


def new_file(path,mode='w'):
    return os.fdopen(os.open(path,os.O_WRONLY|os.O_CREAT|os.O_EXCL,0o600),mode)


def quoted_policy():
    return ' Reuse complete successful feedback instead of repeatedly extracting the whole page. '


def native_cleanup_pending(root):
    case=Path(root)/'aside/S10'
    attempts=list(case.glob('native-setup-attempt-*.json'))
    bound=list(case.glob('native-owned-*.json'))
    if not attempts and not (case/'owned.json').exists():return False
    if len(attempts)>len(bound):return True
    try:closed=json.loads((Path(root)/'owned-native-cleanup.json').read_text())
    except (FileNotFoundError,json.JSONDecodeError):return True
    return closed.get('verified') is not True


def servers(root,browser,bridge,pid):
    entry=str(DEV/'cohort-mcp-recording.py')
    prefix=[entry,'--root',str(root),'--browser',browser]
    backend=[str(sys.executable),*prefix,'--mode','native','--bridge',str(bridge),'--peer-pid',str(pid)] if browser=='yee' else [str(c.ASIDE),'mcp']
    result=[{'name':browser,'command':sys.executable,'args':[*prefix,'--mode','wire','--',*backend],'env':[]}]
    if browser=='aside':
        result.append({'name':'trial_host','command':sys.executable,
            'args':[*prefix,'--mode','wire','--',sys.executable,*prefix,'--mode','host'],'env':[]})
    return result


async def quit_exact(args,executable,root):
    result=await asyncio.to_thread(subprocess.run,[str(args.yee_shutdown_helper),str(executable)],capture_output=True,text=True)
    with (root/'shutdown.jsonl').open('a') as f:
        f.write(json.dumps({'time_ns':time.time_ns(),'returncode':result.returncode,'stdout':result.stdout,'stderr':result.stderr})+'\n')
    if result.returncode:raise RuntimeError('exact Yee graceful shutdown failed')
    prefix=str(executable.parent.parent)+'/'
    deadline=time.monotonic()+30
    import re
    pattern='|'.join(re.escape(p) for p in {prefix,prefix.replace('/private/tmp/','/tmp/')})
    while subprocess.run(['pgrep','-f',pattern],stdout=subprocess.DEVNULL).returncode!=1:
        if time.monotonic()>deadline:raise TimeoutError('Yee shutdown wait expired')
        await asyncio.sleep(.2)


async def owner_cli(case,bridge,pid,label,*parts):
    command=[sys.executable,str(DEV/'yee-browser.py'),'--bridge',str(bridge),
             '--peer-pid',str(pid),'--record',str(case/(label+'.jsonl')),*parts]
    result=await asyncio.to_thread(subprocess.run,command,capture_output=True,text=True)
    (case/(label+'.output')).write_text(result.stdout+result.stderr)
    if result.returncode:raise RuntimeError('Yee owner '+label+' failed')
    return json.loads(result.stdout)


async def release_previous_task(case,bridge,pid):
    """Explicit owner setup boundary; keep browser and model connections alive."""
    detached=await owner_cli(case,bridge,pid,'setup-release-previous-task','detach')
    if detached.get('ok') is not True or detached.get('execution_settled') is not True:
        raise RuntimeError('Previous task grant release did not settle')
    inventory=await owner_cli(case,bridge,pid,'setup-released-inventory','tabs')
    if (inventory.get('ok') is not True or inventory.get('execution_settled') is not True
            or inventory.get('tabs') != []):
        raise RuntimeError('Previous task capabilities remain; no new grants provisioned')


async def aside_setup(owner,root,case,fixture):
    async def call(title,code):
        result=(await owner.call_tool('repl',{'title':title,'code':code})).model_dump(mode='json',by_alias=True)
        if result.get('isError'):raise RuntimeError('Aside owner failed: '+title)
        return result
    urls=[c.ORIGIN+'/']
    native=fixture.scenario=='S10'
    if native:
        original=fixture.data['original_document_id'];docs=sorted(fixture.data['documents'],key=lambda d:d['id']==original)
        urls=[c.ORIGIN+d['source'] for d in docs]
        await call('Initialize only native task tab ownership','var owned=[];var ownership=[];')
        for i,url in enumerate(urls):
            save(case/f'native-setup-attempt-{i+1:02}.json',{'tab_index':i+1,'expected_url':url})
            await c.ui_step(root,case,'aside_open_native_tab',expected_url=url,tab_index=i+1)
            bound=await call('Bind observed native task tab','var p=await attachActiveBrowserTab();if(p.url()!=='+json.dumps(url)+')throw new Error("wrong URL");owned.push(p);console.log("NATIVE_OWNED:"+JSON.stringify({target_id:p.targetId,url:p.url()}));')
            text='\n'.join(x['text'] for x in bound['content'] if x['type']=='text')
            save(case/f'native-owned-{i+1:02}.json',json.loads(text.rsplit('NATIVE_OWNED:',1)[1]))
            if i==0:await c.ui_step(root,case,'aside_active_other',expected_url=url,not_url=urls[-1])
    else:
        await call('Open only independent task tab','var owned=[await openTab('+json.dumps(urls[0])+')];')
    result=await call('Measure only owned task viewports','var ownership=[];for(var p of owned){await p.bringToFront();ownership.push({target_id:p.targetId,url:p.url(),viewport:await p.evaluate(()=>({width:innerWidth,height:innerHeight,dpr:devicePixelRatio}))});}console.log("OWNED:"+JSON.stringify(ownership));')
    text='\n'.join(x['text'] for x in result['content'] if x['type']=='text')
    tabs=json.loads(text.split('OWNED:',1)[1]);save(case/'owned.json',[{k:t[k] for k in ('target_id','url')} for t in tabs])
    if any(t['viewport']!={'width':1440,'height':900,'dpr':1} for t in tabs):
        if not native:raise ValueError('Aside viewport differs')
        await c.ui_step(root,case,'aside_normalize_native_viewport',expected_url=urls[-1],observed=tabs)
        result=await call('Verify normalized owned viewports','var ownership=[];for(var p of owned){await p.bringToFront();ownership.push({target_id:p.targetId,url:p.url(),viewport:await p.evaluate(()=>({width:innerWidth,height:innerHeight,dpr:devicePixelRatio}))});}console.log("OWNED:"+JSON.stringify(ownership));')
        tabs=json.loads('\n'.join(x['text'] for x in result['content'] if x['type']=='text').split('OWNED:',1)[1])
        if any(t['viewport']!={'width':1440,'height':900,'dpr':1} for t in tabs):raise ValueError('Aside normalized viewport differs')
    save(case/'viewports.json',tabs)
    prompt=' Use original Aside APIs with only these prepared tabs: '+json.dumps([{k:t[k] for k in ('target_id','url')} for t in tabs])+'. Tab metadata may be listed; verify exact URLs before reading; never read/change unrelated tab contents. '
    if native:
        prompt+='Original tab is '+tabs[-1]['target_id']+'. Preserve tabs; read all three and return to original. '
        await c.ui_step(root,case,'aside_active_original_before',expected_url=urls[-1],not_url=urls[0])
    if fixture.scenario in ('S08','S12'):
        (case/'channel').mkdir(mode=0o700)
        save(case/'channel/config.json',{'schema':'yee.host-channel.v1','scenario':fixture.scenario,'run_id':str(uuid.uuid4()),'target_id':tabs[0]['target_id'],'origin':c.ORIGIN})
        prompt+='Use trial_host.request_user for the declared synthetic operator; pass kind, question, exact target_id and URL; wait, never poll or act before reply. Stop on cancellation/timeout. '
        if fixture.scenario=='S08':prompt+='Ask kind=choice before selecting a slot, then save once using returned choice. '
        else:prompt+='Ask kind=authentication at '+c.ORIGIN+'/; never request/read passwords or OTPs or authenticate yourself. After complete ask kind=document_permission for '+c.ORIGIN+'/?report=1 BEFORE reading. After allow verify URL and read; do not navigate or mutate. '
    return prompt,call,tabs


async def run(args):
    started=time.monotonic_ns();root=args.record;root.mkdir(mode=0o700,parents=False,exist_ok=False)
    browser=args.persistent_browser;c.ASIDE=args.aside
    freeze=c.frozen_sources(args.yee_app);save(root/'frozen.json',freeze)
    full_freeze_path=DEV.parents[2]/'runtime-native-freeze.json'
    full_freeze=json.loads(full_freeze_path.read_text())
    def verify_full():
        if not all(hashlib.sha256(Path(p).read_bytes()).hexdigest()==v for p,v in full_freeze.items()):
            raise ValueError('full persistent candidate/native/operator freeze changed')
    verify_full();save(root/'full-freeze.json',full_freeze)
    save(root/'plan.json',{'schema':'yee.actual-persistent-cohort.v1','browser':browser,'seed':args.seed,'model':'grok-4.6','reasoning_effort':'high','cases':[[browser,f'S{i:02}'] for i in range(1,13)],'timeout_seconds':240,'requested_turn_cap':12,'turn_cap_enforcement':'usage audit; ACP provider cap not verified','approval_wait_comparison_seconds':0,'model_retries':0,'no_best_of':True,'initial_setup_included':True,'started_monotonic_ns':started,'yee_app':str(args.yee_app)})
    executable=c.yee_executable(args.yee_app);bridge=None;process=None;browser_log=None;actor=None;actor_events=None;actor_err=None
    prompts=[];failed=False
    owner_trace=Transcript(str(root/'aside-owner-global.jsonl')) if browser=='aside' else None
    owner_err=new_file(root/'aside-owner.stderr') if browser=='aside' else None
    from contextlib import AsyncExitStack
    async with AsyncExitStack() as stack:
      owner=None
      try:
        if browser=='aside':
            rd,wr=await stack.enter_async_context(stdio_client(StdioServerParameters(command=str(args.aside),args=['mcp']),errlog=owner_err))
            raw=await stack.enter_async_context(ClientSession(rd,wr,read_timeout_seconds=60));owner=c.RecordedSession(raw,owner_trace);await owner.initialize()
        else:
            await quit_exact(args,executable,root)
            bridge=Path('/private/tmp/yee-agent.'+uuid.uuid4().hex[:12]);bridge.mkdir(mode=0o700)
            subprocess.run([sys.executable,str(DEV/'prepare-agent-test-profile.py'),str(bridge),'206'],check=True)
        for i in range(1,13):
            verify_full()
            if c.frozen_sources(args.yee_app)!=freeze:raise ValueError('frozen source/app changed')
            case=root/browser/f'S{i:02}';case.mkdir(mode=0o700,parents=True);(case/'model').mkdir(mode=0o700)
            save(root/'current.json',{'browser':browser,'scenario':case.name,'case':str(case)})
            task_started=time.monotonic_ns();save(case/'timing-start.json',{'monotonic_ns':task_started})
            fixture=c.Fixture(case.name,args.seed,case/'fixture');server=c.server_for(fixture,8787);thread=threading.Thread(target=server.serve_forever);thread.start()
            host_task=None;tabs=None;call=None
            try:
                common=c.common_prompt(case.name,fixture.data)+quoted_policy();(case/'common-prompt.txt').write_text(common)
                prompt='New independent task. Bind only the current prepared tabs; old task facts, refs and tab objects do not authorize this task. '+common
                if browser=='yee':
                    prepared_multi_tab=False
                    save(case/'bridge.json',{'bridge':str(bridge)})
                    if process is None:
                        browser_log=new_file(root/'yee-browser.stderr','wb')
                        process=subprocess.Popen([str(executable),'--user-data-dir='+str(bridge/'profile'),'--yee-agent-bridge='+str(bridge),'--no-first-run','--no-default-browser-check','--force-device-scale-factor=1','--window-size=1658,954',c.ORIGIN+'/'],stdout=browser_log,stderr=subprocess.STDOUT)
                    else:
                        save(case/'launch.json',{'bridge':str(bridge),'peer_pid':process.pid,'executable':str(executable),'pid_source':'same_direct_child_for_entire_cohort'})
                        await c.ui_step(root,case,'yee_reload_task',expected_url=c.ORIGIN+'/')
                    if process.poll() is not None:raise RuntimeError('persistent Yee process ended')
                    save(case/'launch.json',{'bridge':str(bridge),'peer_pid':process.pid,'executable':str(executable),'pid_source':'same_direct_child_for_entire_cohort'})
                    deadline=time.monotonic()+30
                    while not any(json.loads(l)['kind']=='view' for l in (case/'fixture/events.jsonl').read_text().splitlines()):
                        if time.monotonic()>deadline:raise TimeoutError('render timeout')
                        await asyncio.sleep(.1)
                    if case.name=='S10':
                        prepared_multi_tab=True
                        await release_previous_task(case,bridge,process.pid)
                        urls=[c.ORIGIN+d['source'] for d in fixture.data['documents']]
                        for url in urls+[c.ORIGIN+'/?unrelated=1']:await c.ui_step(root,case,'yee_open_task_tab',expected_url=url)
                        grants=[]
                        for j,url in enumerate(urls,2):
                            await c.ui_step(root,case,'yee_select_setup_tab',tab_index=j,expected_url=url)
                            state=await owner_cli(case,bridge,process.pid,'setup-attach-'+str(j),'attach')
                            if state.get('viewport')!={'width':1440,'height':900,'deviceScaleFactor':1}:
                                raise ValueError('Yee owned tab viewport differs')
                            grants.append(state['tab'])
                        original=next(j for j,d in enumerate(fixture.data['documents']) if d['id']==fixture.data['original_document_id'])
                        await owner_cli(case,bridge,process.pid,'setup-original','select-tab',grants[original]);save(case/'owned-tabs.json',{'grants':grants,'urls':urls,'all_urls':urls+[c.ORIGIN+'/?unrelated=1']})
                        await c.ui_step(root,case,'yee_active_original_before',expected_url=urls[original],not_url=urls[(original+1)%3])
                        prompt+='Use tabs first/last; only these three preapproved IDs: '+json.dumps(grants)+'. Read all and return to original '+grants[original]+'. '
                    else:
                        parts=['attach']
                        if case.name!='S12':parts+=['--permissions','{"fill":"allow","click":"allow","navigate":"allow"}']
                        state=await owner_cli(case,bridge,process.pid,'setup-attach',*parts)
                    if state.get('viewport')!={'width':1440,'height':900,'deviceScaleFactor':1}:raise ValueError('Yee viewport differs')
                    if not prepared_multi_tab:
                        prompt+='Begin with observe on prepared tab; no redundant navigation. '
                    if case.name=='S08':prompt+='Use native ask and wait for declared operator choice before selection/save. '
                    if case.name=='S12':prompt+='Use native ask for separate-channel authentication; no passwords/OTPs/navigation. After expected stale_document transition immediately request fresh default attach consent before any other reads. '
                else:
                    extra,call,tabs=await aside_setup(owner,root,case,fixture);prompt+=extra
                    if case.name in ('S08','S12'):host_task=asyncio.create_task(c.synthetic_host(case,fixture))
                (case/'prompt.txt').write_text(prompt)
                if actor is None:
                    workspace=root/'workspace';workspace.mkdir(mode=0o700);(workspace/'.grok').mkdir(mode=0o700)
                    allow=[browser+'__'+('yee_browser' if browser=='yee' else 'repl')]+(['trial_host__request_user'] if browser=='aside' else [])
                    (workspace/'.grok/config.toml').write_text('[ui]\npermission_mode = "dontAsk"\n[permission]\nallow = '+json.dumps(['MCPTool('+n+')' for n in allow])+'\ndeny = ["Bash(*)", "Read(*)", "Edit(*)"]\n')
                    profile=root/'browser-only.md';profile.write_text('---\nname: browser-only\ndescription: Declared browser tasks only\ntools: [search_tool, use_tool]\nsubagents: []\n---\n${base_prompt}\n'+(DEV/'browser-agent-policy.md').read_text())
                    actor_events=new_file(root/'acp-events.jsonl');actor_err=new_file(root/'acp.stderr')
                    command=[str(args.grok),'agent','--model','grok-4.6','--reasoning-effort','high','--no-leader','--agent-profile',str(profile),'stdio']
                    actor=AcpSession(command,workspace,actor_events,actor_err,authorized_mcp_tools=allow)
                    setup=await asyncio.to_thread(actor.initialize,workspace,servers(root,browser,bridge,process.pid if process else None));save(root/'acp-setup.json',setup)
                before=actor_events.tell();out=await asyncio.to_thread(actor.prompt,prompt,240);after=actor_events.tell();prompts.append(out)
                prompt_record={**out,'session_id':actor.session_id,'agent_pid':actor.process.pid,'global_event_offsets':[before,after]}
                save(case/'model/acp-prompt.json',prompt_record)
                with (root/'acp-events.jsonl').open('rb') as f:
                    f.seek(before);events=[json.loads(l) for l in f.read(after-before).splitlines()]
                parsed=None
                try:
                    parsed=inspect_prompt(prompt_record,events)
                    save(case/'model/acp-inspection.json',parsed)
                except ValueError as exc:
                    save(case/'model/acp-inspection.json',{'failure':str(exc)})
                usage=normalize([out],actor.session_id)
                save(case/'model/summary.json',{'schema':'yee.actual-acp-prompt-summary.v1',
                    'session_id':actor.session_id,'agent_pid':actor.process.pid,
                    'elapsed_seconds':out['elapsed_seconds'],'usage':usage,
                    'model_process_reused':True,'mcp_connection_reused':True,
                    'original_event_offsets':[before,after]})
                if parsed is None:
                    save(case/'model/prompt-failure.json',{'stage':'model_output',
                        'stop_reason':out['response']['result'].get('stopReason'),
                        'usage_preserved':True,'original_evidence_missing_not_synthesized':True})
                    raise RuntimeError('No auditable browser output; original response and usage retained')
                if browser=='yee':
                    native_rows=[json.loads(l) for l in (case/'model/mcp-native.jsonl').read_text().splitlines()]
                    calls_rows=[json.loads(l) for l in (case/'model/mcp-calls.jsonl').read_text().splitlines()]
                    settled=native_audit.inspect(calls_rows,native_rows)
                    save(case/'model/settlement-before-next-task.json',settled)
                    if not settled['native_settlement_verified']:
                        raise RuntimeError('Unsettled native work: cohort stopped, no next task or replay')
                    if any(r['kind']=='response' and (r['response'].get('error') in
                           ('user_cancelled','user_takeover','client_cancelled','session_stopped') or
                           r['response'].get('session_stopped') is True) for r in native_rows):
                        raise RuntimeError('Native user stop: no subsequent task or replay')
                elif (case/'model/host-calls.jsonl').exists():
                    host_rows=[json.loads(l) for l in (case/'model/host-calls.jsonl').read_text().splitlines()]
                    if any(r['kind']=='host_response' and r['result'].get('status') in ('cancelled','timeout') for r in host_rows):
                        raise RuntimeError('Host user stop: no subsequent task or replay')
                save(case/'source-integrity-at-finish.json',c.frozen_sources(args.yee_app))
                verify_full()
                if browser=='yee':
                    await owner_cli(case,bridge,process.pid,'owner-final','observe','--full')
                    if case.name=='S10':
                        await c.ui_step(root,case,'yee_active_original_after',expected_url=urls[original],not_url=urls[(original+1)%3])
                        await owner_cli(case,bridge,process.pid,'owner-final-tabs','tabs')
                        for j,url in reversed(list(enumerate(urls+[c.ORIGIN+'/?unrelated=1'],2))):await c.ui_step(root,case,'yee_close_task_tab',expected_url=url,tab_index=j)
                        await c.ui_step(root,case,'yee_select_setup_tab',tab_index=1,expected_url=c.ORIGIN+'/')
                else:
                    if case.name=='S10':await c.ui_step(root,case,'aside_active_original_after',expected_url=tabs[-1]['url'],not_url=tabs[0]['url'])
                    save(case/'owner-final.json',await call('Verify only current owned task state','var final=[];for(var p of owned)final.push({target_id:p.targetId,url:p.url(),data:await p.evaluate(()=>document.body.innerText)});console.log(JSON.stringify(final));'))
            finally:
                try:
                    if host_task:
                        host_task.cancel()
                        try:await host_task
                        except asyncio.CancelledError:pass
                    if browser=='aside' and (call is not None or any(case.glob('native-owned-*.json'))):
                        if case.name=='S10':
                            result=await asyncio.to_thread(subprocess.run,[sys.executable,str(DEV/'cleanup-owned-aside-native.py'),str(root),'--restore-layout'],capture_output=True,text=True)
                            (case/'cleanup-native.output').write_text(result.stdout+result.stderr)
                            if result.returncode:raise RuntimeError('owned Aside native cleanup failed')
                        else:save(case/'cleanup.json',await call('Close only current owned task tabs','for(var p of owned)await closeTab(p);'))
                finally:
                    server.shutdown();thread.join();server.server_close();fixture.close()
                    task_finished=time.monotonic_ns()
                    save(case/'timing-end.json',{'monotonic_ns':task_finished,'task_elapsed_seconds':(task_finished-task_started)/1e9})
            print(json.dumps({'stage':'persistent_task_collected','browser':browser,'scenario':case.name}),flush=True)
        save(root/'usage.json',normalize(prompts,actor.session_id))
        save(root/'collection-complete.json',{'cases':12,'same_agent_pid':actor.process.pid,'same_session_id':actor.session_id,'same_yee_pid':process.pid if process else None,'source_integrity':c.frozen_sources(args.yee_app)==freeze})
      except Exception:
        failed=True
        import traceback
        (root/'failure.txt').write_text(traceback.format_exc())
        raise
      finally:
        if actor:actor.close()
        if actor_events:actor_events.close()
        if actor_err:actor_err.close()
        if browser=='yee':
            await quit_exact(args,executable,root)
            if process:await asyncio.to_thread(process.wait,timeout=5)
            if browser_log:browser_log.close()
            if bridge:save(root/'profile-cleanup.json',c.reclaim_test_profile(bridge))
        await stack.aclose()
        if owner_trace:owner_trace.close()
        if owner_err:owner_err.close()
        finished=time.monotonic_ns()
        save(root/'cohort-timing.json',{'started_monotonic_ns':started,'finished_monotonic_ns':finished,'elapsed_seconds':(finished-started)/1e9,'includes_initial_browser_agent_mcp_setup':True,'includes_task_preparation_and_verification':True,'aside_native_S10_cleanup_pending':browser=='aside' and native_cleanup_pending(root),'failed':failed or not (root/'collection-complete.json').exists()})


if __name__=='__main__':
    parser=argparse.ArgumentParser(add_help=False);parser.add_argument('--persistent-browser',choices=['yee','aside'],required=True);known,rest=parser.parse_known_args();args=c.parse_args(rest);args.persistent_browser=known.persistent_browser
    if args.cases:raise SystemExit('Persistent cohort is the predeclared full twelve tasks; no subset selection')
    asyncio.run(run(args))
