"""One real stdio connection, two journal scopes, fake upstream, no model/browser."""
import json
from pathlib import Path
import sys
import tempfile
import unittest

import anyio
from mcp import ClientSession,StdioServerParameters
from mcp.client.stdio import stdio_client
from mcp_wire_operations import derive


FAKE='''import json,sys
for line in sys.stdin:
 q=json.loads(line)
 if 'id' not in q:continue
 method=q['method']
 if method=='initialize':result={'protocolVersion':q['params']['protocolVersion'],'capabilities':{'tools':{}},'serverInfo':{'name':'fake','version':'1'}}
 elif method=='tools/list':result={'tools':[{'name':'yee_browser','inputSchema':{'type':'object'}}]}
 else:result={'content':[{'type':'text','text':json.dumps(q['params']['arguments'])}]}
 print(json.dumps({'jsonrpc':'2.0','id':q['id'],'result':result}),flush=True)
'''


class WireTests(unittest.TestCase):
    def test_one_handshake_and_two_calls_with_direct_original_journals(self):
        with tempfile.TemporaryDirectory(dir='/private/tmp') as directory:
            root=Path(directory);peer=root/'fake.py';peer.write_text(FAKE)
            cases=[]
            for name in ('first','second'):
                case=root/'yee'/name;case.mkdir(mode=0o700,parents=True);(case/'model').mkdir(mode=0o700);cases.append(case)
            def select(case):
                p=root/'current.json';p.write_text(json.dumps({'case':str(case),'browser':'yee'}));p.chmod(0o600)
            select(cases[0])
            command=[str(Path(__file__).with_name('cohort-mcp-recording.py')),'--root',str(root),'--browser','yee','--mode','wire','--',sys.executable,str(peer)]
            async def run():
                async with stdio_client(StdioServerParameters(command=sys.executable,args=command)) as (rd,wr):
                    async with ClientSession(rd,wr,read_timeout_seconds=5) as client:
                        await client.initialize();await client.list_tools()
                        first=await client.call_tool('yee_browser',{'task':'first'})
                        self.assertEqual(json.loads(first.content[0].text),{'task':'first'})
                        select(cases[1]);second=await client.call_tool('yee_browser',{'task':'second'})
                        self.assertEqual(json.loads(second.content[0].text),{'task':'second'})
            anyio.run(run)
            index=[json.loads(l) for l in (root/'mcp-wire.jsonl.global').read_text().splitlines()]
            original=[{'sequence':r['sequence'],**r['event']} for r in index]
            ops,_=derive(original)
            self.assertEqual(sum(o['kind']=='mcp_request' and o['operation']=='initialize' for o in ops),1)
            self.assertEqual(sum(o['kind']=='mcp_request' and o['operation']=='tools/call' for o in ops),2)
            for case in cases:
                local=[json.loads(l) for l in (case/'model/mcp-wire.jsonl').read_text().splitlines()]
                selected=[r for r in index if r['case']==str(case)]
                self.assertEqual(len(local),len(selected))
                self.assertEqual([r['sequence'] for r in local],[r['task_sequence'] for r in selected])
            self.assertTrue((root/'connection-wire-yee.json').exists())


if __name__=='__main__':unittest.main()
