"""Audited MCP client operations for comparator runners, not an access sandbox.

Preserves SDK result objects and serialized protocol fields. No retries, result
rewrites, tool filtering, or implicit browser operations. An incomplete/failed
record poisons this connection so later requests cannot silently escape audit.
The transcript's explicit size limit fails the trial instead of truncating data.
"""
import time
import uuid

import anyio


class RecordedSession:
    def __init__(self, session, transcript):
        self.session = session
        self.transcript = transcript
        self.lock = anyio.Lock()
        self.broken = False

    async def _invoke(self, operation, params, dispatch):
        async with self.lock:
            if self.broken:
                raise RuntimeError('MCP recording connection is unusable')
            invocation = str(uuid.uuid4())
            started = time.monotonic_ns()
            try:
                self.transcript.write({'kind': 'mcp_request', 'invocation': invocation,
                                       'operation': operation, 'params': params,
                                       'time_ns': time.time_ns(), 'monotonic_ns': started})
                result = await dispatch()
                payload = result.model_dump(mode='json', by_alias=True)
                self.transcript.write({'kind': 'mcp_response', 'invocation': invocation,
                                       'operation': operation, 'result': payload,
                                       'time_ns': time.time_ns(),
                                       'elapsed_ns': time.monotonic_ns()-started})
                return result
            except BaseException:
                # A lost reply or failed append is an unknown outcome, not an
                # empty successful result. Preserve the admitted request and
                # propagate the original error/cancellation without retrying.
                self.broken = True
                raise

    async def initialize(self):
        return await self._invoke('initialize', {}, self.session.initialize)

    async def list_tools(self, params=None):
        payload = params.model_dump(mode='json', by_alias=True) if params else None
        return await self._invoke('tools/list', payload,
                                  lambda: self.session.list_tools(params=params))

    async def call_tool(self, name, arguments=None):
        return await self._invoke('tools/call', {'name': name, 'arguments': arguments},
                                  lambda: self.session.call_tool(name, arguments=arguments))
