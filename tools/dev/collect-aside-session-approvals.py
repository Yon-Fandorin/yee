#!/usr/bin/env python3
"""User-authorized Aside session-record inspection; no settings or page changes."""
import argparse
import asyncio
import hashlib
import json
from pathlib import Path
from mcp import ClientSession, StdioServerParameters
from mcp.client.stdio import stdio_client
from recorded_mcp_session import RecordedSession
from yee_browser_transcript import Transcript


async def run(root, historical_ids):
    root.mkdir(mode=0o700)
    authorization={'user_message':'승인할테니 세션기럭 조회를 진행해줘.',
                   'scope':'Identify comparison sessions and inspect session records for permission events.',
                   'settings_changed':False,'page_content_read':False,'model_calls':0}
    (root/'authorization.json').write_text(json.dumps(authorization,ensure_ascii=False,indent=2)+'\n')
    aside=Path('/Users/yongjunkim/.local/bin/aside')
    with Transcript(str(root/'calls.jsonl')) as trace,(root/'stderr.log').open('x') as err:
        async with stdio_client(StdioServerParameters(command=str(aside),args=['mcp']),errlog=err) as (rd,wr):
            async with ClientSession(rd,wr,read_timeout_seconds=45) as raw:
                client=RecordedSession(raw,trace);await client.initialize()
                async def call(name,code):
                    result=(await client.call_tool('repl',{'title':name,'code':code})).model_dump(mode='json',by_alias=True)
                    (root/(name+'.json')).write_text(json.dumps(result,ensure_ascii=False,indent=2)+'\n')
                    print(json.dumps({'step':name,'result':result},ensure_ascii=False),flush=True)
                await call('current-session-records', '''var permissionAuditCurrent = aside.sessions.current();
var permissionAuditRows = permissionAuditCurrent ? await aside.sessions.messageRows(permissionAuditCurrent.id) : null;
console.log(JSON.stringify({session:permissionAuditCurrent, rows:permissionAuditRows}));''')
                await call('session-inventory-metadata', '''var permissionAuditInventory = aside.sessions.list({limit:200,order:'desc'});
console.log(JSON.stringify({count:permissionAuditInventory.length,sessions:permissionAuditInventory.map(s=>({
id:s.id,keys:Object.keys(s),createdAt:s.createdAt,updatedAt:s.updatedAt,
activeTabTargetId:s.activeTabTargetId,ephemeral:s.ephemeral,incognito:s.incognito,
triggerSource:s.triggerSource,permissionMode:s.permissionMode,
benchmarkTitle: /yee|synthetic|benchmark|S0[1-9]|S1[012]/i.test(s.title||'') ? s.title : null
}))}));''')
                # Re-read after a real REPL operation to test whether external calls
                # are persisted as message records in this same live session.
                await call('current-session-after', '''console.log(JSON.stringify({
rows:await aside.sessions.messageRows(permissionAuditCurrent.id),
messages:await aside.sessions.messages(permissionAuditCurrent.id,{limit:20,order:'asc'})}));''')
                if historical_ids:
                    ids=json.loads(historical_ids.read_text())
                    if not isinstance(ids,list) or not all(isinstance(x,str) for x in ids):
                        raise ValueError('historical session ID list required')
                    await call('known-test-session-records','''var historicalPermissionRecords=[];
for(var testSessionId of '''+json.dumps(ids)+''') {
 try {
  var s=await aside.sessions.get(testSessionId);
  var rs=await aside.sessions.messageRows(testSessionId);
  var ms=await aside.sessions.messages(testSessionId,{limit:20,order:'asc'});
  historicalPermissionRecords.push({id:testSessionId,session:s,rows:rs,messages:ms});
 } catch(e) { historicalPermissionRecords.push({id:testSessionId,error:String(e)}); }
}
console.log(JSON.stringify(historicalPermissionRecords));''')
    (root/'manifest.json').write_text(json.dumps({'source_sha256':hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
        'aside_sha256':hashlib.sha256(aside.read_bytes()).hexdigest()},indent=2)+'\n')


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--record',type=Path,required=True)
    p.add_argument('--historical-ids',type=Path)
    a=p.parse_args();asyncio.run(run(a.record,a.historical_ids))
