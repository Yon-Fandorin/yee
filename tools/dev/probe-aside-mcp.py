#!/usr/bin/env python3
"""Record installed Aside MCP initialization/tools only; no browser/model calls.

Run with the pinned MCP Python environment. The output is connection metadata,
not browser isolation, model compatibility, or scenario success evidence.
"""
import argparse
import asyncio
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import time

from mcp import ClientSession, StdioServerParameters
from mcp.client.stdio import stdio_client
from recorded_mcp_session import RecordedSession
from yee_browser_transcript import Transcript


def save(root, name, value):
    fd = os.open(root/name, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
    with os.fdopen(fd, 'w') as stream:
        json.dump(value, stream, indent=2)


async def probe(root, executable, wire_relay=False):
    if not root.is_absolute():
        raise ValueError('record directory must be absolute and new')
    root.mkdir(mode=0o700)
    executable = executable.resolve(strict=True)
    start = time.monotonic()
    result = {'browser_tools_called': 0, 'model_calls': 0,
              'browser_connection_tested': False,
              'initialize_received': False, 'tools_received': False,
              'handshake_passed': False,
              'wire_relay': wire_relay,
              'executable_sha256': hashlib.sha256(executable.read_bytes()).hexdigest()}
    try:
        result['version'] = subprocess.check_output(
            [str(executable), '--version'], text=True, timeout=10).strip()
        parameters = StdioServerParameters(command=str(executable), args=['mcp'])
        if wire_relay:
            relay = Path(__file__).with_name('record-mcp-stdio.py').resolve()
            result['relay_sha256'] = hashlib.sha256(relay.read_bytes()).hexdigest()
            parameters = StdioServerParameters(command=sys.executable, args=[str(relay),
                '--record', str(root/'wire.jsonl'), '--stderr', str(root/'upstream.stderr'),
                '--timeout', '30', '--', str(executable), 'mcp'])
        async with asyncio.timeout(30):
            with (root/'stderr.txt').open('x') as errors, Transcript(str(root/'mcp-calls.jsonl')) as transcript:
                os.chmod(root/'stderr.txt', 0o600)
                async with stdio_client(parameters, errlog=errors) as (read, write):
                    async with ClientSession(read, write) as session:
                        recorded = RecordedSession(session, transcript)
                        initialized = (await recorded.initialize()).model_dump(mode='json', by_alias=True)
                        save(root, 'initialize.json', initialized)
                        result['initialize_received'] = True
                        listed = (await recorded.list_tools()).model_dump(mode='json', by_alias=True)
                        save(root, 'tools.json', listed)
                        result['tools_received'] = True
                        result.update(protocol=initialized['protocolVersion'],
                                      tools=[tool['name'] for tool in listed['tools']],
                                      next_cursor=listed.get('nextCursor'))
        result['handshake_passed'] = result.get('next_cursor') is None
    except Exception as exc:
        def kinds(error):
            if isinstance(error, BaseExceptionGroup):
                return [kind for child in error.exceptions for kind in kinds(child)]
            return [type(error).__name__]
        result['error_types'] = kinds(exc)
    result['elapsed_seconds'] = time.monotonic()-start
    save(root, 'result.json', result)
    return result


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--record', type=Path, required=True)
    parser.add_argument('--aside', type=Path, default=Path('/Users/yongjunkim/.local/bin/aside'))
    parser.add_argument('--wire-relay', action='store_true', help='also record exact bidirectional stdio frames')
    args = parser.parse_args()
    result = asyncio.run(probe(args.record, args.aside, args.wire_relay))
    print(json.dumps(result, indent=2))
    raise SystemExit(0 if result['handshake_passed'] else 1)
