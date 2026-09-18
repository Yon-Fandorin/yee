#!/usr/bin/env python3
"""Verify serialized MCP/native log coverage, without exporting page contents.

This checks local log correlation, not model autonomy or scenario success.
Unsettled/recovery/unknown timing records remain unmeasured, never zero-cost.
"""
import argparse
import hashlib
import json
import math
from pathlib import Path


def correlate_model(events, calls, *, model_tool='mcp__yee__yee_browser', recorded_tool='yee_browser', tool_mapping=None, allow_parallel=False, input_schemas=None, local_rejections=None):
    """Correlate every Kimi call/result; parallel calls need unique signatures."""
    if any(not isinstance(e, dict) for e in events):
        raise ValueError('expected model event objects')
    if len(calls) % 2:
        raise ValueError('incomplete MCP call/result pairs')
    pending = {}
    rejected_pending = {}
    index = 0
    block_end = 0
    seen = set()
    mapping = tool_mapping if tool_mapping is not None else {recorded_tool:model_tool}
    if not isinstance(mapping,dict) or not mapping or any(
            not isinstance(k,str) or not isinstance(v,str) or not v.startswith('mcp__')
            or not v.endswith('__'+k) for k,v in mapping.items()):
        raise ValueError('explicit matching tool identities required')

    def invalid_input(name, arguments):
        schema = (input_schemas or {}).get(name)
        if not isinstance(schema, dict): return False
        # Validation is offline. Recorded tool definitions cannot cause network
        # resolution or substitute a schema fetched after the recorded trial.
        def has_reference(value):
            if isinstance(value, dict):
                return any(k in ('$ref','$dynamicRef') or has_reference(v) for k,v in value.items())
            return isinstance(value,list) and any(has_reference(v) for v in value)
        if has_reference(schema): raise ValueError('referenced input schemas unsupported')
        try:
            from jsonschema import Draft202012Validator, SchemaError
        except ImportError:
            raise ValueError('independent input validator unavailable; use Yee MCP venv') from None
        try: Draft202012Validator.check_schema(schema)
        except SchemaError: raise ValueError('invalid recorded input schema') from None
        return not Draft202012Validator(schema).is_valid(arguments)

    for event in events:
        if event.get('role') == 'assistant' and event.get('tool_calls'):
            if pending:
                raise ValueError('overlapping model steps unsupported')
            tools = event['tool_calls']
            if (not isinstance(tools,list) or not tools or
                    (len(tools)!=1 and not allow_parallel)):
                raise ValueError('parallel or excess model calls unsupported')
            block_end = index+2*len(tools)
            available = set(range(index,min(block_end,len(calls)),2))
            for tool in tools:
                if not isinstance(tool,dict) or not isinstance(tool.get('function'),dict):
                    raise ValueError('invalid model tool call shape')
                function = tool['function']; call_id = tool.get('id')
                if not isinstance(call_id,str) or not call_id or call_id in seen:
                    raise ValueError('duplicate or missing model call identity')
                arguments = json.loads(function.get('arguments','null'))
                matches = [offset for offset in available
                           if calls[offset].get('name') in mapping
                           and function.get('name') == mapping[calls[offset]['name']]
                           and arguments == calls[offset].get('arguments')]
                # Identical concurrent requests cannot be assigned to distinct
                # provider IDs from this trace alone. Do not guess by ordering.
                if len(matches)!=1:
                    if (not matches and len(tools)==1 and function.get('name') in mapping.values()
                            and invalid_input(function['name'], arguments)):
                        # A single SDK-local input rejection has no MCP/native
                        # interval. Require its own correlated tool response;
                        # never skip schema-valid, parallel or ambiguous calls.
                        pending[call_id] = None
                        rejected_pending[call_id] = (function['name'], arguments)
                        seen.add(call_id)
                        block_end = index
                        continue
                    raise ValueError('model/MCP tool arguments or identity mismatch or ambiguous parallel calls')
                offset = matches[0];available.remove(offset)
                seen.add(call_id);pending[call_id] = offset
        elif event.get('role') == 'tool':
            call_id = event.get('tool_call_id')
            if call_id not in pending:
                raise ValueError('model tool result identity mismatch')
            offset = pending.pop(call_id)
            if offset is None:
                name, arguments = rejected_pending.pop(call_id)
                content = event.get('content')
                prefix = f'Invalid args for tool "{name}": '
                if not isinstance(content,str) or not content.startswith(prefix) or not content[len(prefix):].strip():
                    raise ValueError('missing explicit client input rejection')
                if local_rejections is not None:
                    digest = lambda value: hashlib.sha256(json.dumps(value,sort_keys=True,separators=(',',':')).encode()).hexdigest()
                    local_rejections.append({'call_id':call_id,'tool':name,
                                             'arguments_sha256':digest(arguments),
                                             'schema_sha256':digest(input_schemas[name]),
                                             'result_sha256':hashlib.sha256(content.encode()).hexdigest()})
                if not pending:index = block_end
                continue
            expected_parts=calls[offset+1].get('content_parts')
            matches = (json.loads(event.get('content','null'))==expected_parts if expected_parts is not None
                       else event.get('content')==''.join(calls[offset+1]['content']))
            if not matches:
                raise ValueError('model-visible result differs from MCP output')
            if not pending:index = block_end
    if pending or index != len(calls):
        raise ValueError('missing model/MCP call or result')
    return True


def inspect(calls, native):
    for rows in (calls, native):
        if not isinstance(rows, list) or any(not isinstance(r, dict) for r in rows):
            raise ValueError('expected event objects')
        if any(type(r.get('sequence')) is not int or r['sequence'] != i
               for i, r in enumerate(rows, 1)):
            raise ValueError('missing, reordered or duplicated sequence')
    if not calls or len(calls) % 2:
        raise ValueError('incomplete MCP invocation')
    cursor = 0
    ids = set()
    native_ids = set()
    completed_receipts = {}
    intervals = []
    waits = []
    timing_complete = settled = True
    for offset in range(0, len(calls), 2):
        begin, end = calls[offset:offset+2]
        invocation = begin.get('invocation')
        if (begin.get('kind') != 'mcp_request' or end.get('kind') != 'mcp_response'
                or not isinstance(invocation, str) or not invocation or invocation in ids
                or end.get('invocation') != invocation):
            raise ValueError('MCP invocation identity mismatch')
        ids.add(invocation)
        start, stop = begin.get('native_sequence_start'), end.get('native_sequence_end')
        if (type(start) is not int or type(stop) is not int or start != cursor
                or not start <= stop <= len(native)):
            raise ValueError('overlapping, incomplete or uncovered native interval')
        if type(end.get('is_error')) is not bool or not isinstance(end.get('content'), list):
            raise ValueError('invalid MCP result envelope')
        if any(not isinstance(text, str) for text in end['content']):
            raise ValueError('unsupported MCP result content')
        commands = []
        client_errors = []
        recoveries = []
        i = start
        while i < stop:
            event = native[i]
            if event.get('kind') == 'recovery':
                # Receipt recovery is neither another dispatch nor another
                # charge of the original native wait.
                request_id = event.get('request_id')
                if not isinstance(request_id, str):
                    raise ValueError('invalid recovery request identity')
                previous = completed_receipts.get(request_id)
                if i + 1 >= stop or previous is None:
                    raise ValueError('recovery without completed correlated request')
                response = native[i + 1]
                receipt = response.get('response')
                stamps = (previous.get('time_ns'), event.get('time_ns'),
                          response.get('time_ns'))
                if (response.get('kind') != 'response'
                        or response.get('response_source') not in ('mailbox', 'native_archive')
                        or not isinstance(receipt, dict)
                        or receipt.get('id') != request_id
                        or any(type(t) is not int or t <= 0 for t in stamps)
                        or not stamps[0] <= stamps[1] <= stamps[2]
                        or json.dumps(receipt, sort_keys=True, allow_nan=False)
                           != json.dumps(previous['response'], sort_keys=True, allow_nan=False)):
                    raise ValueError('recovered receipt differs from original completion')
                recoveries.append({'request_id': request_id,
                                   'response_source': response['response_source']})
                i += 2
                continue
            if event.get('kind') == 'client_error':
                # The CLI can fail before dispatch, including after an earlier
                # command in the same batch completed. Never skip an unmatched
                # request or accept further dispatch after this terminal error.
                error = event.get('error')
                if (i != stop - 1 or end['is_error'] is not True
                        or not isinstance(error, str) or not error
                        or type(event.get('time_ns')) is not int or event['time_ns'] <= 0):
                    raise ValueError('invalid terminal native client error')
                try:
                    payload = json.loads(end['content'][-1])
                    terminal = payload[-1] if isinstance(payload, list) and payload else payload
                except (ValueError, IndexError):
                    raise ValueError('invalid client error MCP payload') from None
                if (not isinstance(terminal, dict) or terminal.get('ok') is not False
                        or terminal.get('error') != error):
                    raise ValueError('native client error differs from MCP output')
                client_errors.append({'sequence': event['sequence'], 'error': error})
                i += 1
                continue
            if i + 1 >= stop:
                raise ValueError('incomplete native request/response pair')
            request, response = native[i:i+2]
            req, res = request.get('request'), response.get('response')
            if (request.get('kind') != 'request' or response.get('kind') != 'response'
                    or not isinstance(req, dict) or not isinstance(res, dict)):
                raise ValueError('unsupported native request/response shape')
            request_id = req.get('id')
            if (not isinstance(request_id, str) or not request_id or request_id in native_ids
                    or res.get('id') != request_id):
                raise ValueError('duplicate or uncorrelated native request')
            native_ids.add(request_id)
            if not isinstance(req.get('command'), str) or type(res.get('ok')) is not bool:
                raise ValueError('invalid native command/result')
            commands.append(req['command'])
            settled = settled and res.get('execution_settled') is True
            timing = res.get('timing', {})
            elapsed = timing.get('native_elapsed_ms') if isinstance(timing, dict) else None
            wait = timing.get('user_wait_ms') if isinstance(timing, dict) else None
            valid = (all(type(v) in (int, float) and math.isfinite(v) and v >= 0
                         for v in (elapsed, wait)) and wait <= elapsed
                     and response.get('response_source') == 'mailbox')
            timing_complete = timing_complete and valid
            if valid: waits.append(wait/1000)
            completed_receipts[request_id] = response
            i += 2
        intervals.append({'invocation': invocation, 'native_start': start,
                          'native_end': stop, 'native_commands': commands,
                          'client_errors': client_errors,
                          'receipt_recoveries': recoveries,
                          'tool_error': end['is_error']})
        cursor = stop
    if cursor != len(native):
        raise ValueError('native requests exist outside MCP invocations')
    return {'schema': 'yee.mcp-native-audit.v1', 'interval_coverage_verified': True,
            'mcp_calls': len(intervals), 'native_requests': len(native_ids),
            'receipt_recoveries': sum(len(r['receipt_recoveries']) for r in intervals),
            'client_errors': sum(len(interval['client_errors']) for interval in intervals),
            'native_settlement_verified': settled,
            'native_user_wait_seconds': sum(waits) if timing_complete and settled else None,
            'intervals': intervals, 'model_call_correlation_verified': False,
            'single_text_block_per_call': all(len(calls[i]['content']) == 1
                                             for i in range(1, len(calls), 2)),
            'scenario_success': None, 'competitive_gate': 'not_evaluated'}


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('calls', type=Path)
    parser.add_argument('native', type=Path)
    parser.add_argument('--kimi-stdout', type=Path)
    args = parser.parse_args()
    inputs = [p.read_bytes() for p in (args.calls, args.native)]
    calls, native = [[json.loads(line) for line in data.splitlines() if line.strip()]
                     for data in inputs]
    result = inspect(calls, native)
    if args.kimi_stdout:
        raw = args.kimi_stdout.read_bytes()
        result['model_call_correlation_verified'] = correlate_model(
            [json.loads(line) for line in raw.splitlines() if line.strip()], calls)
        result['model_stdout_sha256'] = hashlib.sha256(raw).hexdigest()
    result['input_sha256'] = [hashlib.sha256(data).hexdigest() for data in inputs]
    print(json.dumps(result, indent=2))
