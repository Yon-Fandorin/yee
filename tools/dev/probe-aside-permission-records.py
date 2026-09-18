#!/usr/bin/env python3
"""Inspect permission mode only; never enumerate or read session messages."""
import argparse
import asyncio
import hashlib
import json
from pathlib import Path
from mcp import ClientSession, StdioServerParameters
from mcp.client.stdio import stdio_client
from recorded_mcp_session import RecordedSession
from yee_browser_transcript import Transcript


async def run(root):
    root.mkdir(mode=0o700)
    aside = Path('/Users/yongjunkim/.local/bin/aside')
    with Transcript(str(root/'calls.jsonl')) as trace, (root/'stderr.log').open('x') as err:
        async with stdio_client(StdioServerParameters(command=str(aside), args=['mcp']), errlog=err) as (rd, wr):
            async with ClientSession(rd, wr, read_timeout_seconds=30) as raw:
                client = RecordedSession(raw, trace)
                init = await client.initialize()
                listed = await client.list_tools()
                for name, obj in [('initialize', init), ('tools', listed)]:
                    (root/(name+'.json')).write_text(json.dumps(obj.model_dump(mode='json', by_alias=True), indent=2))
                code = '''var currentProbeSession = aside.sessions.current();
console.log(JSON.stringify({hasSession:!!currentProbeSession,
permissionMode:currentProbeSession ? currentProbeSession.permissionMode : null}));'''
                result = await client.call_tool('repl', {'title':'Read permission mode only, excluding all conversation metadata and messages', 'code':code})
                payload = result.model_dump(mode='json', by_alias=True)
                (root/'own-session.json').write_text(json.dumps(payload, indent=2))
                print(json.dumps(payload, ensure_ascii=False))
    (root/'manifest.json').write_text(json.dumps({
        'source_sha256': hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
        'aside_sha256': hashlib.sha256(aside.read_bytes()).hexdigest(),
        'model_calls': 0, 'browser_page_calls': 0, 'settings_changed': False,
        'scope': 'Only hasSession and permissionMode; no session ID, title, list, message rows or transcripts'}, indent=2))


if __name__ == '__main__':
    p = argparse.ArgumentParser(); p.add_argument('--record', type=Path, required=True)
    asyncio.run(run(p.parse_args().record))
