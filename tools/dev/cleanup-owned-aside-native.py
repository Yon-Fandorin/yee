#!/usr/bin/env python3
"""Close recorded fixture tabs through their verified native context menus."""
import argparse
import asyncio
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import time
import uuid

from mcp import ClientSession,StdioServerParameters
from mcp.client.stdio import stdio_client
from cohort_recording import private_directory


def checked(command):
    result=subprocess.run(command,capture_output=True,text=True,timeout=15)
    if result.returncode:raise RuntimeError('Owned native operation failed: '+str(command[:1]))
    return json.loads(result.stdout)


async def wait_closed(inventory,target_id,protected,timeout=5,interval=.15):
    deadline=time.monotonic()+timeout
    while True:
        current={x['targetId'] for x in await inventory()}
        if not protected.issubset(current):raise RuntimeError('Protected tab inventory changed; cleanup stopped')
        if target_id not in current:return current
        if time.monotonic()>=deadline:raise RuntimeError('Owned tab close did not settle; input was not repeated')
        await asyncio.sleep(interval)


def request_close(title,url):
    result=subprocess.run(['/private/tmp/yee-close-owned-aside-tab-20260913-v8',title,url],capture_output=True,text=True,timeout=15)
    try:value=json.loads(result.stdout)
    except ValueError:raise RuntimeError('Owned close was not confirmed as requested')
    if value.get('close_requested') is not True or value.get('expected_url')!=url:
        raise RuntimeError('Owned close request identity not verified: '+json.dumps(value,sort_keys=True))
    return {'process_returncode':result.returncode,'request_result':value}


def load_owned(root,seed):
    case=Path(root)/'aside/S10'
    full=case/'owned.json'
    if full.exists():
        owned=json.loads(full.read_text())
        if len(owned)!=3:raise ValueError('Complete ownership record requires three tabs')
        ownership_sha=hashlib.sha256(full.read_bytes()).hexdigest()
    else:
        files=sorted(case.glob('native-owned-*.json'))
        if not 1<=len(files)<=3 or [p.name for p in files]!=[f'native-owned-{i:02}.json' for i in range(1,len(files)+1)]:
            raise ValueError('Contiguous independently bound native ownership records required')
        owned=[json.loads(p.read_text()) for p in files]
        ownership_sha=hashlib.sha256(b''.join(p.read_bytes() for p in files)).hexdigest()
    if len({t['target_id'] for t in owned})!=len(owned):raise ValueError('Duplicate native target ownership')
    for t in owned:
        if not re.fullmatch('[A-F0-9]{32}',t['target_id']) or not re.fullmatch(r'http://127\.0\.0\.1:8787/\?document=D-'+str(seed)+r'-[123]',t['url']):
            raise ValueError('Native ownership target or fixture URL mismatch')
    return owned,ownership_sha


async def run(root,restore):
    private_directory(root)
    seed=json.loads((root/'plan.json').read_text())['seed']
    owned,ownership_sha=load_owned(root,seed)
    attempt=uuid.uuid4().hex
    records=[]
    def record(value):
        value={**value,'attempt':attempt,'time_ns':time.time_ns(),'owned_file_sha256':ownership_sha}
        records.append(value)
        fd=os.open(root/'owned-native-cleanup-events.jsonl',os.O_CREAT|os.O_APPEND|os.O_WRONLY|os.O_NOFOLLOW,0o600)
        with os.fdopen(fd,'w') as f:f.write(json.dumps(value)+'\n')
    async with stdio_client(StdioServerParameters(command='/Users/yongjunkim/.local/bin/aside',args=['mcp'])) as (rd,wr):
      async with ClientSession(rd,wr,read_timeout_seconds=30) as client:
        await client.initialize()
        async def call(code):
            result=(await client.call_tool('repl',{'title':'Close independently owned native fixture tabs','code':code})).model_dump(mode='json',by_alias=True)
            if result.get('isError'):raise RuntimeError('Owned Aside API operation failed')
            record({'kind':'aside_api','result':result});return result
        async def inventory():
            value=await call('console.log("TABS:"+JSON.stringify(await listBrowserTabs()));')
            return json.loads('\n'.join(c['text'] for c in value['content'] if c['type']=='text').split('TABS:',1)[1])
        before=await inventory();ids={t['targetId'] for t in before};owned_ids={t['target_id'] for t in owned}
        protected=ids-owned_ids
        remaining=[t for t in owned if t['target_id'] in ids]
        if restore and not remaining:
            raise RuntimeError('No owned tab remains to verify a new layout restore')
        record({'kind':'begin','protected_tab_ids':sorted(protected),'initial_owned_ids_absent':sorted(owned_ids-ids)})
        for i,t in enumerate(remaining):
            await call('var cleanupOwned=await attachBrowserTab('+json.dumps(t['target_id'])+');if(cleanupOwned.url()!=='+json.dumps(t['url'])+')throw new Error("owned URL changed");await cleanupOwned.bringToFront();')
            title='Cedar delivery policy revision '+t['url'].split('-')[-1]+' — '+t['url'].split('document=')[1]
            record({'kind':'native_select','target_id':t['target_id'],'result':checked(['/private/tmp/yee-fourpoint-aside-tab',title,t['url']])})
            native=checked(['/private/tmp/yee-fourpoint-check-native-url',t['url'],'at.studio.AsideBrowser'])
            assert native['matches'] and native['app_frontmost'];pid=native['pid']
            if i==len(remaining)-1 and restore:
                record({'kind':'resize','result':checked(['/private/tmp/yee-resize-aside-owned-window',t['url'],'1424','926','absolute'])})
                record({'kind':'position','result':checked(['/private/tmp/yee-fourpoint-restore-aside-position',t['url'],'203','50'])})
                chrome=checked(['/private/tmp/yee-measure-owned-aside-chrome-20260913',t['url']]);delta=275-chrome['sidebar_handle_relative_x']
                assert abs(delta)<=100
                if abs(delta)>.5:record({'kind':'sidebar','result':checked(['/private/tmp/yee-fourpoint-aside-sidebar',t['url'],str(delta)])})
                chrome=checked(['/private/tmp/yee-measure-owned-aside-chrome-20260913',t['url']])
                assert chrome['pid']==pid and all(abs(chrome[k]-v)<=1 for k,v in [('window_x',203),('window_y',50),('window_width',1424),('window_height',926),('sidebar_handle_relative_x',275)])
                record({'kind':'restored_geometry','result':chrome})
            native=checked(['/private/tmp/yee-fourpoint-check-native-url',t['url'],'at.studio.AsideBrowser'])
            assert native['pid']==pid and native['matches'] and native['app_frontmost']
            record({'kind':'close_attempt','target_id':t['target_id'],'operation':'AXPress exact owned Native tab context Close'})
            try:close_result=request_close(title,t['url'])
            except Exception as error:
                record({'kind':'close_failure','target_id':t['target_id'],'error':str(error)})
                raise
            record({'kind':'close','target_id':t['target_id'],'result':close_result})
            await wait_closed(inventory,t['target_id'],protected)
            record({'kind':'closed_verified','target_id':t['target_id']})
        final_ids={x['targetId'] for x in await inventory()}
        assert not owned_ids.intersection(final_ids) and protected.issubset(final_ids)
        proof={'verified':True,'owned':owned,'initial_owned_ids_absent':sorted(owned_ids-ids),'protected_tab_ids_preserved':sorted(protected),'restored_layout':restore,'records':records,
               'owned_file_sha256':ownership_sha,'cleanup_helper_sha256':hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),'close_scope':'exact owned Native tab context Close after target ID/URL verification; no keyboard input or browser shutdown'}
        temporary=root/('owned-native-cleanup.'+attempt+'.tmp')
        fd=os.open(temporary,os.O_CREAT|os.O_EXCL|os.O_WRONLY,0o600)
        with os.fdopen(fd,'w') as f:f.write(json.dumps(proof,indent=2)+'\n')
        os.replace(temporary,root/'owned-native-cleanup.json')


if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('root',type=Path);p.add_argument('--restore-layout',action='store_true');args=p.parse_args();asyncio.run(run(args.root,args.restore_layout))
