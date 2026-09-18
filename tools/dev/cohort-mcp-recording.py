#!/usr/bin/env python3
"""Recording entry points for persistent Native/Aside benchmark connections.

Yee still serves its original single Adapter. Aside's upstream protocol and
arguments are forwarded unchanged. Task routing affects journals only.
"""
import argparse
import importlib.util
import json
import os
from pathlib import Path
import sys
import time
import anyio
from mcp import types
from mcp.server.lowlevel import Server
from mcp.server.stdio import stdio_server

from browser_trial_handoff import Channel
from cohort_recording import RoutedTranscript, Routing


def module(name, filename):
    spec = importlib.util.spec_from_file_location(name, Path(__file__).with_name(filename))
    result = importlib.util.module_from_spec(spec)
    sys.modules[name] = result
    spec.loader.exec_module(result)
    return result


async def host(routing):
    original = module('original_handoff', 'browser-trial-handoff-mcp.py')
    channels = {}
    with RoutedTranscript(routing, 'host-calls.jsonl', routing.root/'host-global.jsonl') as record:
        async def tools(context, params):
            return types.ListToolsResult(tools=[original.TOOL])

        async def call(context, params):
            with routing.pin():
                case = routing.selected()
                if params.name != 'request_user':
                    raise ValueError('unknown host tool')
                if case not in channels:
                    channels[case] = Channel(case/'channel')
                record.write({'kind':'host_request','time_ns':time.time_ns(),
                              'arguments':params.arguments or {}})
                result = await channels[case].ask(params.arguments or {})
                record.write({'kind':'host_response','time_ns':time.time_ns(),
                              'result':result})
                return types.CallToolResult(content=[types.TextContent(type='text',text=json.dumps(result,ensure_ascii=False))])

        server = Server('trial-host', version='0.1.0', on_list_tools=tools, on_call_tool=call)
        async with stdio_server() as streams:
            await server.run(*streams,server.create_initialization_options())


async def native(routing, bridge, peer_pid):
    adapter = module('original_native_adapter', 'yee-browser-mcp.py')
    base = adapter.AuditedCalls
    class PinnedCalls(base):
        async def call(self, *args):
            with routing.pin():
                return await super().call(*args)
    adapter.AuditedCalls = PinnedCalls
    with RoutedTranscript(routing,'mcp-native.jsonl',routing.root/'native-global.jsonl') as transcript:
        with RoutedTranscript(routing,'mcp-calls.jsonl',routing.root/'calls-global.jsonl') as calls:
            config=argparse.Namespace(bridge=adapter.cli.validate_bridge(bridge),
                timeout=180.0,request_timeout=120.0,peer_pid=peer_pid,
                compact=True,_transcript=transcript,short_documents=False)
            await adapter.serve(config,calls)


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root',type=Path,required=True)
    parser.add_argument('--browser',choices=['yee','aside'],required=True)
    parser.add_argument('--mode',choices=['wire','native','host'],required=True)
    parser.add_argument('--bridge')
    parser.add_argument('--peer-pid',type=int)
    parser.add_argument('command',nargs=argparse.REMAINDER)
    args=parser.parse_args();routing=Routing(args.root,args.browser)
    marker=routing.root/('connection-'+args.mode+'-'+('host' if args.mode=='wire' and 'host' in args.command else args.browser)+'.json')
    fd=os.open(marker,os.O_WRONLY|os.O_CREAT|os.O_EXCL,0o600)
    with os.fdopen(fd,'w') as f:
        json.dump({'pid':os.getpid(),'mode':args.mode,'browser':args.browser,
                   'peer_pid':args.peer_pid,'started_monotonic_ns':time.monotonic_ns()},f)
    if args.mode=='native':
        if args.browser!='yee' or not args.bridge or not args.peer_pid or args.peer_pid<=0:
            parser.error('native recording requires exact Yee bridge/peer PID')
        anyio.run(native,routing,args.bridge,args.peer_pid)
    elif args.mode=='host':
        if args.browser!='aside':parser.error('host is Aside-only')
        anyio.run(host,routing)
    else:
        command=args.command[1:] if args.command[:1]==['--'] else args.command
        if not command:parser.error('upstream command required')
        relay=module('original_recording_relay','record-mcp-stdio.py')
        filename='host-wire.jsonl' if '--mode' in command and 'host' in command else 'mcp-wire.jsonl'
        with RoutedTranscript(routing,filename,routing.root/(filename+'.global'),max_event_bytes=24*1024*1024) as trace:
            fd=os.open(routing.root/(filename+'.stderr'),os.O_WRONLY|os.O_CREAT|os.O_EXCL,0o600)
            with os.fdopen(fd,'wb') as errors:
                raise SystemExit(relay.relay(command,trace,errors,3600))


if __name__=='__main__':main()
