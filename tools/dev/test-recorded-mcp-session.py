import asyncio
import json
from pathlib import Path
import tempfile
import unittest
import sys

from mcp import types, ClientSession, StdioServerParameters
from mcp.client.stdio import stdio_client
from recorded_mcp_session import RecordedSession
from yee_browser_transcript import Transcript


class FakeSession:
    def __init__(self, result):
        self.result = result
        self.calls = []

    async def call_tool(self, name, arguments=None):
        self.calls.append((name, arguments))
        return self.result

    async def list_tools(self, params=None):
        self.calls.append(params)
        return self.result


class RecordingTests(unittest.IsolatedAsyncioTestCase):
    async def test_error_multiblock_and_structured_results_preserved(self):
        result = types.CallToolResult(content=[
            types.TextContent(type='text', text='first\n'),
            types.TextContent(type='text', text='second')],
            is_error=True, structured_content={'exact': [1, 'value']})
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory)/'calls.jsonl'
            with Transcript(str(path)) as transcript:
                session = FakeSession(result)
                recorded = RecordedSession(session, transcript)
                returned = await recorded.call_tool('repl', {'code': 'literal code'})
                self.assertIs(returned, result)
                self.assertEqual(session.calls, [('repl', {'code': 'literal code'})])
            rows = [json.loads(line) for line in path.read_text().splitlines()]
            self.assertEqual(rows[1]['result'], result.model_dump(mode='json', by_alias=True))
            self.assertEqual(rows[0]['invocation'], rows[1]['invocation'])
            self.assertGreaterEqual(rows[1]['elapsed_ns'], 0)

    async def test_admission_failure_prevents_dispatch_and_poison_connection(self):
        class Broken:
            def write(self, event): raise OSError('disk failure')
        session = FakeSession(None); recorded = RecordedSession(session, Broken())
        with self.assertRaises(OSError): await recorded.call_tool('repl', {})
        with self.assertRaises(RuntimeError): await recorded.call_tool('repl', {})
        self.assertEqual(session.calls, [])

    async def test_lost_response_is_not_retried_or_converted_to_success(self):
        class Lost(FakeSession):
            async def call_tool(self, name, arguments=None):
                self.calls.append(name)
                raise ConnectionError('lost reply')
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory)/'calls.jsonl'
            with Transcript(str(path)) as transcript:
                session = Lost(None); recorded = RecordedSession(session, transcript)
                with self.assertRaises(ConnectionError): await recorded.call_tool('repl', {})
                with self.assertRaises(RuntimeError): await recorded.call_tool('repl', {})
                self.assertEqual(session.calls, ['repl'])
            self.assertEqual(len(path.read_text().splitlines()), 1)

    async def test_response_record_failure_blocks_following_calls(self):
        class BrokenAfterAdmission:
            def __init__(self): self.count = 0
            def write(self, event):
                self.count += 1
                if self.count == 2: raise OSError('disk full')
        session = FakeSession(types.CallToolResult(content=[]))
        recorded = RecordedSession(session, BrokenAfterAdmission())
        with self.assertRaises(OSError): await recorded.call_tool('repl', {})
        with self.assertRaises(RuntimeError): await recorded.call_tool('repl', {})
        self.assertEqual(len(session.calls), 1)

    async def test_tool_schema_pagination_preserved(self):
        result = types.ListToolsResult(tools=[], next_cursor='page2')
        params = types.PaginatedRequestParams(cursor='page1')
        with tempfile.TemporaryDirectory() as directory:
            with Transcript(str(Path(directory)/'calls.jsonl')) as transcript:
                session = FakeSession(result)
                returned = await RecordedSession(session, transcript).list_tools(params)
                self.assertIs(returned, result)
                self.assertIs(session.calls[0], params)

    async def test_actual_stdio_error_and_multiblock_round_trip(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory)/'calls.jsonl'
            with Transcript(str(path)) as transcript:
                async with stdio_client(StdioServerParameters(command=sys.executable,
                        args=[str(Path(__file__).resolve()), '--synthetic-server'])) as (read, write):
                    async with ClientSession(read, write) as session:
                        recorded = RecordedSession(session, transcript)
                        await recorded.initialize()
                        tools = await recorded.list_tools()
                        self.assertEqual([tool.name for tool in tools.tools], ['synthetic'])
                        result = await recorded.call_tool('synthetic', {'value': 'unchanged'})
                        self.assertTrue(result.is_error)
                        self.assertEqual([item.text for item in result.content], ['unchanged', 'second'])
            rows = [json.loads(line) for line in path.read_text().splitlines()]
            self.assertEqual(len(rows), 6)
            self.assertEqual(rows[-1]['result'], result.model_dump(mode='json', by_alias=True))


async def synthetic_server():
    from mcp.server.lowlevel import Server
    from mcp.server.stdio import stdio_server
    async def listed(context, params):
        return types.ListToolsResult(tools=[types.Tool(name='synthetic',
            input_schema={'type': 'object', 'properties': {'value': {'type': 'string'}}})])
    async def called(context, params):
        return types.CallToolResult(content=[types.TextContent(type='text', text=params.arguments['value']),
            types.TextContent(type='text', text='second')], is_error=True)
    server = Server('synthetic-recording-test', on_list_tools=listed, on_call_tool=called)
    async with stdio_server() as streams:
        await server.run(*streams, server.create_initialization_options())


if __name__ == '__main__':
    if sys.argv[1:] == ['--synthetic-server']: asyncio.run(synthetic_server())
    else: unittest.main()
