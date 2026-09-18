#!/usr/bin/env python3
"""Inspect a local Grok Messages stream without printing thinking or signatures.

New native logs separate user wait; older logs retain unknown wait latency.
This is evidence extraction, not an automatic competitive-success gate.
"""
import argparse
import importlib.util
import json
import math
from pathlib import Path


def native_timing(response):
    timing = response.get('timing')
    if not isinstance(timing, dict):
        return None
    elapsed, wait = timing.get('native_elapsed_ms'), timing.get('user_wait_ms')
    if (any(type(x) not in (int, float) or not math.isfinite(x) or x < 0
            for x in (elapsed, wait)) or wait > elapsed):
        return None
    return {'native_elapsed_seconds': elapsed / 1000, 'user_wait_seconds': wait / 1000,
            'native_non_wait_seconds': (elapsed - wait) / 1000}


def model_turn_costs(events):
    """Expose actual per-turn usage, never thinking/text/signatures or estimates."""
    fields = ('input_tokens', 'cache_read_input_tokens', 'cache_creation_input_tokens', 'output_tokens')
    turns = []
    for event in events:
        if not isinstance(event, dict) or event.get('type') != 'assistant':
            continue
        message = event.get('message')
        if not isinstance(message, dict):
            continue
        usage = message.get('usage')
        valid = isinstance(usage, dict) and all(type(usage.get(k)) is int and usage[k] >= 0 for k in fields)
        turns.append({'index': len(turns) + 1,
                      'input_tokens_including_cache': sum(usage[k] for k in fields[:3]) if valid else None,
                      'output_tokens': usage['output_tokens'] if valid else None,
                      'total_tokens': sum(usage[k] for k in fields) if valid else None,
                      'cached_input_tokens': usage['cache_read_input_tokens'] if valid else None,
                      'tools': [b.get('name') for b in message.get('content', [])
                                if isinstance(b, dict) and b.get('type') == 'tool_use']})
    return turns


def tool_result(block):
    """Extract only documented tool-result payloads, never assistant thinking."""
    content = json.loads(block['content'])
    result = {'id': block['tool_use_id'], 'command': None, 'exit_code': None,
              'output': '', 'tool_error': bool(block.get('is_error', False))}
    if not isinstance(content, dict):
        return {**result, 'output': json.dumps(content), 'tool_error': True}
    result['result_type'] = content.get('type')
    if 'command' in content:
        result.update(command=content.get('command'), exit_code=content.get('exit_code'),
                      output=bytes(content.get('output', [])).decode('utf-8'))
    elif content.get('type') == 'ReadFile' and isinstance(content.get('FileContent'), dict):
        payload = content['FileContent']
        result.update(path=payload.get('absolute_path'), output=payload.get('raw_output', ''))
    elif content.get('type') == 'SearchReplace' and isinstance(content.get('EditsApplied'), dict):
        payload = content['EditsApplied']
        result.update(path=payload.get('absolute_path'),
                      output=payload.get('tool_output_for_prompt', ''))
    elif content.get('type') == 'MCP' and isinstance(content.get('output'), dict):
        payload = content['output']
        result.update(server=content.get('server_name'), tool=content.get('tool_name'))
        if isinstance(payload.get('OkayOutput'), str):
            result['output'] = payload['OkayOutput']
        else:
            result['unrecognized_result'] = True
    elif content.get('type') == 'SearchTool':
        result.update(output=json.dumps(content.get('content'), ensure_ascii=False),
                      discovered_count=content.get('result_count'))
    else:
        # Unknown shapes are evidence gaps, not empty successful executions.
        result['unrecognized_result'] = True
    return result


def inspect(record_path, native_paths):
    spec = importlib.util.spec_from_file_location('recorder', Path(__file__).with_name('run-grok-recorded.py'))
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    raw = (record_path / 'stdout.json').read_bytes()
    events = [json.loads(line) for line in raw.splitlines() if line.strip()]
    terminal = module.parse_grok_output(raw, True)
    calls, results = [], []
    for event in events:
        for block in event.get('message', {}).get('content', []):
            if block.get('type') == 'tool_use':
                calls.append({'id': block['id'], 'tool': block['name'], 'input': block['input']})
            elif block.get('type') == 'tool_result':
                results.append(tool_result(block))
    native = []
    for path in native_paths:
        rows = [json.loads(line) for line in path.read_text().splitlines()]
        if not rows or len(rows) % 2:
            raise ValueError('incomplete native request/response pairs')
        seen = set()
        for offset in range(0, len(rows), 2):
            request, response = rows[offset:offset + 2]
            if request['kind'] != 'request' or response['kind'] != 'response':
                raise ValueError('expected native request/response pairs')
            request_id = request['request']['id']
            if request_id != response['response']['id'] or request_id in seen:
                raise ValueError('native request/response correlation mismatch')
            seen.add(request_id)
            native.append({'path': str(path), 'command': request['request']['command'],
                       'elapsed_seconds_including_consent': (response['time_ns'] - request['time_ns']) / 1e9,
                       'timing': native_timing(response['response']),
                           'ok': response['response']['ok']})
    return {'provider_usage': module.normalize_usage(terminal), 'model_turns': model_turn_costs(events),
            'reported_cost_usd': terminal.get('total_cost_usd'),
            'terminal_error': terminal.get('is_error'), 'terminal_subtype': terminal.get('subtype'),
            'provider_duration_ms': terminal.get('duration_ms'),
            'provider_api_duration_ms': terminal.get('duration_api_ms'),
            'answer': terminal.get('result'), 'calls': calls, 'results': results, 'native': native,
            'total_native_user_wait_seconds': (sum(n['timing']['user_wait_seconds'] for n in native)
                                              if native and all(n['timing'] is not None for n in native) else None),
            'competitive_gate': 'not_evaluated'}


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('record', type=Path)
    parser.add_argument('native', nargs='*', type=Path)
    args = parser.parse_args()
    print(json.dumps(inspect(args.record, args.native), ensure_ascii=False, indent=2))
