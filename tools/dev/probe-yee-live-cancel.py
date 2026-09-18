#!/usr/bin/env python3
"""Operator-assisted native cancellation probe on the approved synthetic Name form."""
import argparse
import json
from pathlib import Path
import select
import subprocess
import sys
import time


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--bridge',type=Path,required=True)
    parser.add_argument('--record',type=Path,required=True)
    args=parser.parse_args()
    if not args.bridge.is_absolute() or not args.record.is_absolute():parser.error('absolute paths required')
    args.record.mkdir(mode=0o700)
    native=args.record/'native.jsonl';calls=args.record/'calls.jsonl'
    outcome={'verified':False,'model_trial':False}
    process=None
    with (args.record/'wire.jsonl').open('x') as wire, (args.record/'stderr.log').open('x') as errors:
        def log(direction,message):
            wire.write(json.dumps({'time_ns':time.time_ns(),'direction':direction,'message':message})+'\n');wire.flush()
        def send(message):
            log('sent',message);process.stdin.write(json.dumps(message)+'\n');process.stdin.flush()
        def receive(identifier,timeout=120):
            deadline=time.monotonic()+timeout
            while time.monotonic()<deadline:
                if not select.select([process.stdout],[],[],min(1,deadline-time.monotonic()))[0]:continue
                line=process.stdout.readline()
                if not line:raise RuntimeError('MCP EOF before reply')
                message=json.loads(line);log('received',message)
                if message.get('id')==identifier:return message
            raise TimeoutError('MCP reply deadline')
        try:
            process=subprocess.Popen([sys.executable,str(Path(__file__).with_name('yee-browser-mcp.py')),
                '--bridge',str(args.bridge),'--record',str(native),'--calls-record',str(calls)],
                stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=errors,text=True)
            send({'jsonrpc':'2.0','id':1,'method':'initialize','params':{'protocolVersion':'2025-11-25',
                'capabilities':{},'clientInfo':{'name':'yee-native-cancel-probe','version':'1'}}})
            receive(1,10);send({'jsonrpc':'2.0','method':'notifications/initialized'})
            send({'jsonrpc':'2.0','id':2,'method':'tools/call','params':{'name':'yee_browser',
                'arguments':{'commands':[['fill-named','Name','Cedar cancellation probe'],['click-named','Save locally']]}}})
            deadline=time.monotonic()+30
            pending=None
            while time.monotonic()<deadline:
                # Chromium consumes request.json immediately. Observe the
                # append-only native dispatch record instead of racing it.
                rows=[json.loads(line) for line in native.read_text().splitlines()] if native.exists() else []
                request=next((row['request'] for row in reversed(rows)
                              if row['kind']=='request' and row['request'].get('command')=='fill'),None)
                if request is not None:pending=request;break
                time.sleep(.05)
            if pending is None:raise TimeoutError('native fill did not reach approval')
            (args.record/'pending-fill.json').write_text(json.dumps(pending,indent=2))
            send({'jsonrpc':'2.0','method':'notifications/cancelled','params':{'requestId':2,'reason':'controlled native cancellation probe'}})
            print('CANCELLATION_SENT: approve only the displayed Name fill; no Save action is authorized.',flush=True)
            deadline=time.monotonic()+120
            aborted=None
            while time.monotonic()<deadline:
                rows=[json.loads(l) for l in calls.read_text().splitlines()] if calls.exists() else []
                aborted=next((r for r in rows if r['kind']=='mcp_aborted'),None)
                if aborted:break
                time.sleep(.05)
            if aborted is None:raise TimeoutError('no terminal cancellation audit after operator approval')
            assert aborted['reason']=='cancelled' and aborted['response_returned'] is False
            send({'jsonrpc':'2.0','id':3,'method':'tools/call','params':{'name':'yee_browser',
                'arguments':{'commands':[['observe','--full']]}}})
            reply=receive(3,20)
            result=reply['result'];assert not result.get('isError',False)
            observation=json.loads(result['content'][0]['text'])
            snapshot=observation['snapshot']
            assert 'Cedar cancellation probe' in snapshot and 'Saved: ' not in snapshot
            rows=[json.loads(l) for l in native.read_text().splitlines()]
            commands=[r['request']['command'] for r in rows if r['kind']=='request']
            assert commands.count('fill')==1 and 'click' not in commands
            assert not (args.bridge/'request.json').exists()
            outcome.update(verified=True,native_commands=commands,abort=aborted,observation=observation,
                scope='one admitted fill settles after MCP cancellation; following save is not dispatched; fresh observe succeeds')
        except Exception as error:
            outcome.update(error_type=type(error).__name__,error=str(error))
        finally:
            if process:
                process.stdin.close()
                try:process.wait(timeout=10)
                except subprocess.TimeoutExpired:
                    process.terminate();process.wait(timeout=10);outcome['verified']=False
                outcome['mcp_exit_code']=process.returncode
                if process.returncode:outcome['verified']=False
                process.stdout.close()
            (args.record/'verification.json').write_text(json.dumps(outcome,indent=2))
    print(json.dumps(outcome))
    return 0 if outcome['verified'] else 1


if __name__=='__main__':raise SystemExit(main())
