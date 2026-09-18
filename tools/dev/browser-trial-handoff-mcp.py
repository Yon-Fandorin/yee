#!/usr/bin/env python3
"""Host interaction MCP, explicitly separate from original Aside/Yee tools."""
import argparse
import json
import time
import anyio
from mcp import types
from mcp.server.lowlevel import Server
from mcp.server.stdio import stdio_server
from browser_trial_handoff import Channel
from yee_browser_transcript import Transcript

TOOL = types.Tool(name='request_user', description=(
    'Ask the real user and wait for a correlated reply. This host tool is separate from the browser. '
    'Use choice for the reservation slot, authentication to ask the user to complete synthetic authentication '
    '(never request passwords or OTPs), then document_permission before reading the new report URL. '
    'Pass the exact prepared target_id and URL. On cancelled or timeout, stop the task without browser actions. '
    'Do not poll the browser while waiting; this call stays pending until the user replies.'),
    inputSchema={'type':'object','additionalProperties':False,
      'properties':{'kind':{'type':'string','enum':['choice','authentication','document_permission']},
                    'question':{'type':'string','minLength':1,'maxLength':2048},
                    'target_id':{'type':'string'},'url':{'type':'string'}},
      'required':['kind','question','target_id','url']})

async def serve(root, record):
    channel = Channel(root)
    with Transcript(record) as trace:
        async def list_tools(context, params): return types.ListToolsResult(tools=[TOOL])
        async def call_tool(context, params):
            if params.name != TOOL.name: raise ValueError('unknown host tool')
            args = params.arguments or {}
            trace.write({'kind':'host_request','time_ns':time.time_ns(),'arguments':args})
            result = await channel.ask(args)
            trace.write({'kind':'host_response','time_ns':time.time_ns(),'result':result})
            return types.CallToolResult(content=[types.TextContent(type='text',text=json.dumps(result,ensure_ascii=False))])
        server = Server('browser-trial-host',version='0.1.0',on_list_tools=list_tools,on_call_tool=call_tool)
        async with stdio_server() as streams:
            await server.run(*streams,server.create_initialization_options())

if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--channel',required=True);p.add_argument('--record',required=True)
    a=p.parse_args();anyio.run(serve,a.channel,a.record)
