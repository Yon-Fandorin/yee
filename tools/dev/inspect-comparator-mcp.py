"""Join complete comparator client operations to Kimi tool messages.

Explicit browser/tool identity is required. This checks call/result text, not
browser effects, model usage, tool-discovery equivalence or access isolation.
Screenshot PNG and explicitly scoped Aside PNG/JPEG blocks require exact Kimi
content-part matching. Other media or transformed images remain unsupported
evidence; never drop them to obtain a pass.
"""
import importlib.util
import base64
import json
from pathlib import Path
from mcp_wire_operations import derive

spec = importlib.util.spec_from_file_location('yee_correlation',
        Path(__file__).with_name('inspect-yee-mcp-calls.py'))
correlation = importlib.util.module_from_spec(spec)
spec.loader.exec_module(correlation)


def browser_harness_error(texts):
    """Installed 0.1.13 wrapper uses a single JSON text block for tool errors."""
    if len(texts) != 1:
        raise ValueError('Browser Harness outcome requires one JSON text block')
    try:
        value = json.loads(texts[0])
    except (TypeError, ValueError) as exc:
        raise ValueError('invalid Browser Harness JSON outcome') from exc
    return isinstance(value, dict) and 'error' in value


def inspect_wire(events, rows, *, model_tool=None, recorded_tool=None, outcome_profile=None, tool_mapping=None):
    operations, wire = derive(rows)
    result = inspect(events, operations, model_tool=model_tool, recorded_tool=recorded_tool,
                     outcome_profile=outcome_profile, tool_mapping=tool_mapping)
    result['wire'] = wire
    return result


def inspect(events, records, *, model_tool=None, recorded_tool=None, outcome_profile=None, tool_mapping=None):
    if outcome_profile not in (None, 'browser-harness-0.1.13', 'aside-repl-media-v1'):
        raise ValueError('unknown comparator outcome profile')
    mapping = tool_mapping if tool_mapping is not None else {recorded_tool:model_tool}
    if (not isinstance(mapping,dict) or not mapping or any(
            not isinstance(key,str) or not key or not isinstance(value,str)
            or not value.startswith('mcp__') or not value.endswith('__'+key)
            for key,value in mapping.items())):
        raise ValueError('explicit matching model and recorded tool identity required')
    if outcome_profile == 'browser-harness-0.1.13' and any(not key.startswith('browser_') for key in mapping):
        raise ValueError('Browser Harness profile requires a browser_ tool')
    if outcome_profile == 'aside-repl-media-v1' and mapping != {'repl': 'mcp__aside__repl'}:
        raise ValueError('Aside media profile requires the exact Aside repl tool')
    if (not isinstance(records, list) or not records or len(records) % 2
            or any(not isinstance(row, dict) or type(row.get('sequence')) is not int
                   or row['sequence'] != i for i, row in enumerate(records, 1))):
        raise ValueError('incomplete, duplicate or reordered comparator records')
    calls = []
    ids = set()
    errors = elapsed = metadata = tool_errors = image_blocks = image_bytes = 0
    for i in range(0, len(records), 2):
        begin, end = records[i:i+2]
        invocation = begin.get('invocation')
        operation = begin.get('operation')
        if (begin.get('kind') != 'mcp_request' or end.get('kind') != 'mcp_response'
                or not isinstance(invocation, str) or not invocation or invocation in ids
                or end.get('invocation') != invocation or end.get('operation') != operation
                or operation not in ('initialize', 'tools/list', 'tools/call')
                or type(end.get('elapsed_ns')) is not int or end['elapsed_ns'] < 0
                or not isinstance(end.get('result'), dict)):
            raise ValueError('invalid comparator operation pair')
        ids.add(invocation)
        elapsed += end['elapsed_ns']
        if operation != 'tools/call':
            metadata += 1
            continue
        params, result = begin.get('params'), end['result']
        if (not isinstance(params, dict) or params.get('name') not in mapping
                or type(result.get('isError')) is not bool
                or not isinstance(result.get('content'), list)):
            raise ValueError('invalid comparator tool result')
        texts = []
        parts = []
        has_image = False
        for item in result['content']:
            if not isinstance(item,dict):raise ValueError('invalid comparator content')
            if (item.get('type')=='image' and outcome_profile=='browser-harness-0.1.13'
                    and params['name']=='browser_screenshot'):
                if item.get('mimeType')!='image/png' or not isinstance(item.get('data'),str):
                    raise ValueError('unsupported screenshot image encoding')
                raw=base64.b64decode(item['data'],validate=True)
                if not raw.startswith(b'\x89PNG\r\n\x1a\n'):
                    raise ValueError('screenshot payload is not PNG')
                parts.append({'type':'image_url','imageUrl':{'url':'data:image/png;base64,'+item['data']}})
                has_image=True;image_blocks+=1;image_bytes+=len(raw)
            elif item.get('type') == 'image' and outcome_profile == 'aside-repl-media-v1':
                mime = item.get('mimeType')
                signatures = {'image/png': b'\x89PNG\r\n\x1a\n', 'image/jpeg': b'\xff\xd8\xff'}
                if mime not in signatures or not isinstance(item.get('data'), str):
                    raise ValueError('unsupported Aside image encoding')
                raw = base64.b64decode(item['data'], validate=True)
                if not raw.startswith(signatures[mime]):
                    raise ValueError('Aside image payload does not match MIME type')
                # No resizing, re-encoding, text removal or model-event rewriting.
                # correlate_model compares the entire ordered content-part list.
                parts.append({'type': 'image_url', 'imageUrl': {
                    'url': 'data:' + mime + ';base64,' + item['data']}})
                has_image = True; image_blocks += 1; image_bytes += len(raw)
            elif item.get('type')=='text' and isinstance(item.get('text'),str):
                texts.append(item['text']);parts.append({'type':'text','text':item['text']})
            else:
                raise ValueError('non-text comparator result correlation unsupported')
        reply={'content':texts}
        if has_image:reply['content_parts']=parts
        calls.extend([{'name': params['name'], 'arguments': params.get('arguments')},
                      reply])
        errors += int(result['isError'])
        if outcome_profile == 'browser-harness-0.1.13':
            # Preserve transport errors separately; a JSON error with isError
            # false is still a failed tool call. Do not double-count both forms.
            vendor_error = False if result['isError'] else browser_harness_error(texts)
            tool_errors += int(result['isError'] or vendor_error)
    if not calls:
        raise ValueError('metadata-only records are not model/browser usage')
    correlation.correlate_model(events, calls, tool_mapping=mapping, allow_parallel=True)
    return {'schema': 'yee.comparator-call-audit.v1', 'model_tool': model_tool,
            'recorded_tool': recorded_tool, 'model_call_correlation_verified': True,
            'tool_mapping':mapping,
            'image_blocks':image_blocks, 'image_bytes':image_bytes,
            'mcp_calls': len(calls)//2, 'mcp_errors': errors,
            'tool_errors': tool_errors if outcome_profile == 'browser-harness-0.1.13' else None,
            'outcome_profile': outcome_profile,
            'metadata_operations': metadata, 'recorded_operation_seconds': elapsed/1e9,
            'native_settlement_verified': False, 'scenario_success': None,
            'whole_task_elapsed_seconds': None, 'comparison_ready': False,
            'structured_result_semantics_verified': False,
            'tool_discovery_equivalence_verified': False}
