#!/usr/bin/env python3
"""No-model scope probe using only two newly created synthetic Aside tabs.

Does not enumerate tabs/accounts, read files through Aside, change permissions,
or inspect personal pages. A successful cross-session attach disproves tab
confinement for this configuration; a denied attach alone cannot prove full
confinement. Original operations and cleanup outcomes remain in the record.
"""
import argparse
import asyncio
from contextlib import AsyncExitStack
import hashlib
import json
import os
from pathlib import Path
import re
import uuid

from mcp import ClientSession, StdioServerParameters
from mcp.client.stdio import stdio_client
from recorded_mcp_session import RecordedSession
from yee_browser_transcript import Transcript

MARKER = 'YEE_SCOPE_PROBE_JSON:'


def payload(result):
    wire = result.model_dump(mode='json', by_alias=True)
    if wire.get('isError', False):
        raise ValueError('Aside reported a tool error; inspect original record')
    lines = [line[len(MARKER):] for block in wire['content'] if block['type'] == 'text'
             for line in block['text'].splitlines() if line.startswith(MARKER)]
    if len(lines) != 1:
        raise ValueError('Expected one unambiguous probe receipt')
    value = json.loads(lines[0])
    if not isinstance(value, dict):
        raise ValueError('Probe receipt is not an object')
    return value


def target(receipt, expected_url):
    value = receipt.get('targetId')
    if (not isinstance(value, str) or not re.fullmatch(r'[A-F0-9]{32}', value)
            or receipt.get('url') != expected_url):
        raise ValueError('Refusing attach without an exact owned-tab receipt')
    return value


async def probe(root, executable):
    if not root.is_absolute():
        raise ValueError('record path must be absolute and new')
    root.mkdir(mode=0o700)
    executable = executable.resolve(strict=True)
    source = Path(__file__).read_bytes()
    (root/'probe.py').write_bytes(source)
    result = {'model_calls': 0, 'personal_tab_inventory_called': False,
              'permissions_changed': False, 'confinement_proven': False,
              'source_sha256': hashlib.sha256(source).hexdigest(),
              'aside_sha256': hashlib.sha256(executable.read_bytes()).hexdigest(),
              'cleanup': {}, 'opened': {}}
    marker = uuid.uuid4().hex
    variables = {side: 'yeeScope_' + marker + '_' + side for side in ('a', 'b')}
    urls = {side: 'data:text/html,%3Ctitle%3EYee-scope-' + marker + '-' + side
            + '%3C%2Ftitle%3E%3Cp%3ESynthetic-scope-probe-only%3C%2Fp%3E'
            for side in variables}
    clients = {}

    async def call(side, title, code):
        return await clients[side].call_tool('repl', {'title': title, 'code': code})

    def emit(expression):
        return 'console.log(' + json.dumps(MARKER) + '+JSON.stringify(' + expression + '));'

    try:
        async with AsyncExitStack() as stack:
            for side in variables:
                transcript = stack.enter_context(Transcript(str(root/(side+'.jsonl'))))
                errors = stack.enter_context((root/(side+'.stderr')).open('x'))
                os.chmod(root/(side+'.stderr'), 0o600)
                read, write = await stack.enter_async_context(stdio_client(
                    StdioServerParameters(command=str(executable), args=['mcp']), errlog=errors))
                session = await stack.enter_async_context(ClientSession(
                    read, write, read_timeout_seconds=30))
                clients[side] = RecordedSession(session, transcript)
                await clients[side].initialize()
            try:
                for side, variable in variables.items():
                    code = ('var ' + variable + '=await openTab(' + json.dumps(urls[side]) + ');'
                            + emit('{targetId:' + variable + '.targetId,url:await '
                                   + variable + '.evaluate(()=>location.href),session:aside.sessions.current()}'))
                    # The session summary is this new MCP connection's own
                    # metadata; never invoke a session or browser inventory.
                    receipt = payload(await call(side, 'Create owned synthetic scope tab', code))
                    target(receipt, urls[side])
                    result['opened'][side] = receipt
                result['same_session'] = (result['opened']['a']['session']['id']
                                          == result['opened']['b']['session']['id'])
                borrowed = target(result['opened']['b'], urls['b'])
                code = ('var yeeBorrowed_' + marker + '=await attachBrowserTab('
                        + json.dumps(borrowed) + ');'
                        + emit('{url:await yeeBorrowed_' + marker + '.evaluate(()=>location.href)}'))
                raw = await call('a', 'Probe access to the other owned synthetic tab', code)
                result['cross_attach_tool_error'] = bool(
                    raw.model_dump(mode='json', by_alias=True).get('isError', False))
                if not result['cross_attach_tool_error']:
                    receipt = payload(raw)
                    result['cross_attach_url_matches'] = receipt.get('url') == urls['b']
                    if not result['cross_attach_url_matches']:
                        raise ValueError('Unexpected URL: stop without reading page content')
                    result['tab_confinement_disproved'] = not result['same_session']
                result['probe_completed'] = True
            finally:
                # Close only the objects returned by our own openTab calls.
                # Never close borrowed targets, inferred IDs or user tabs.
                for side, variable in variables.items():
                    if side not in clients or clients[side].broken:
                        result['cleanup'][side] = 'unconfirmed'
                        continue
                    try:
                        raw = await call(side, 'Close only the owned scope probe tab',
                            'if(typeof ' + variable + '!=="undefined")await closeTab(' + variable + ');'
                            + emit('{cleanupReturned:true}'))
                        result['cleanup'][side] = payload(raw)['cleanupReturned']
                    except Exception as error:
                        result['cleanup'][side] = type(error).__name__
    except Exception as error:
        result['error_type'] = type(error).__name__
        def causes(exc):
            if isinstance(exc, BaseExceptionGroup):
                return [cause for child in exc.exceptions for cause in causes(child)]
            return [{'type': type(exc).__name__, 'message': str(exc)[:1000]}]
        result['error_causes'] = causes(error)
    (root/'result.json').write_text(json.dumps(result, indent=2)+'\n')
    os.chmod(root/'result.json', 0o600)
    return result


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--record', type=Path, required=True)
    parser.add_argument('--aside', type=Path, default=Path('/Users/yongjunkim/.local/bin/aside'))
    args = parser.parse_args()
    value = asyncio.run(probe(args.record, args.aside))
    print(json.dumps({k: v for k, v in value.items() if k != 'opened'}, indent=2))
    raise SystemExit(0 if value.get('probe_completed') and all(
        done is True for done in value['cleanup'].values()) else 1)
