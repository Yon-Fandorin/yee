"""Compare original ACP MCP outputs with their original recorded wire events."""
import json
from mcp_wire_operations import derive

TOOLS = {'yee__yee_browser': ('yee', 'yee_browser'), 'aside__repl': ('aside', 'repl'),
         'trial_host__request_user': ('trial_host', 'request_user')}


def correlate(parsed, wire_paths, received_text, session):
    recorded = []
    stats = []
    for path in wire_paths:
        events = [json.loads(line) for line in path.read_text().splitlines() if line.strip()]
        ops, detail = derive(events)
        stats.append(detail)
        for begin, end in zip(ops[::2], ops[1::2]):
            if begin['operation'] == 'tools/call':
                recorded.append((begin, end))
    recorded.sort(key=lambda pair: pair[0]['request_received_monotonic_ns'])
    uses = [call for call in parsed['calls'] if call['name'] == 'use_tool']
    if len(uses) != len(recorded):
        raise ValueError('model and MCP call counts differ')
    consumed = set()
    truncations = []
    media_transforms = []
    errors = error_joins = 0
    for call in uses:
        args = call['arguments']
        if args.get('tool_name') not in TOOLS:
            raise ValueError('undeclared ACP MCP tool')
        server, tool = TOOLS[args['tool_name']]
        choices = [(i, a, b) for i, (a, b) in enumerate(recorded)
                   if i not in consumed and a['params']['name'] == tool]
        if not choices:
            raise ValueError('missing original MCP call')
        i, begin, end = choices[0]
        if begin['params'].get('arguments') != args.get('tool_input'):
            raise ValueError('original MCP call order/input mismatch')
        consumed.add(i)
        raw = call['raw_output']
        if raw.get('type') != 'MCP' or raw.get('server_name') != server or raw.get('tool_name') != tool:
            raise ValueError('ACP result server/tool mismatch')
        original = end['result']
        is_error = bool(original.get('isError'))
        key = 'Error' if is_error else 'OkayOutput'
        output = raw.get('output')
        if not isinstance(output, dict) or set(output) != {key} or not isinstance(output[key], str):
            raise ValueError('ACP error/output variant mismatch')
        if raw.get('is_error', False) is not is_error:
            raise ValueError('ACP error flag differs from original MCP result')
        if call['tool_error'] != is_error or call['text'] != output[key]:
            raise ValueError('parsed ACP output differs from original ACP output')
        content = original['content']
        media = [item for item in content if item.get('type') != 'text']
        if media:
            if server != 'aside' or is_error or any(item.get('type') != 'image'
                    or not isinstance(item.get('data'), str) or not item['data']
                    or not isinstance(item.get('mimeType'), str) or not item['mimeType'] for item in media):
                raise ValueError('unsupported original ACP media transformation')
            text = '\n'.join(item['text'] if item['type'] == 'text'
                             else '[image content will be provided separately]' for item in content)
            if output[key] != text:
                raise ValueError('ACP media placeholder changed original text')
            media_transforms.append({'tool_use_id': call['id'], 'image_count': len(media),
                                     'mime_types': [item['mimeType'] for item in media]})
        else:
            text = ''.join(item['text'] for item in content)
            if server == 'aside' and is_error and output[key] == '\n'.join(item['text'] for item in content):
                error_joins += 1
            else:
                receipt = received_text(output[key], text, session)
                if receipt:
                    truncations.append({'tool_use_id': call['id'], **receipt})
        errors += is_error
    return {'verified': True, 'transport': 'original ACP rawOutput', 'mcp_calls': len(uses),
            'mcp_errors': errors, 'server_wire_stats': stats, 'client_truncations': truncations,
            'received_all_mcp_text': not truncations, 'media_transforms': media_transforms,
            'received_all_mcp_media': not media_transforms, 'aside_error_text_join_count': error_joins}
