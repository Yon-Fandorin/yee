#!/usr/bin/env python3
"""Import usage from one explicitly selected Kimi task's local wire log.

No global log, config, credentials, prompts or model reasoning are exported.
Defaults to one request. --multi-call accepts sequential v0.41.0 agent-core
loop and compaction calls from a fresh, task-local wire log. It does not establish
trial boundaries, detect replayed complete pairs, or reconcile provider billing.
--v2-main explicitly supports sequential main-agent v2 records and rejects
repeated/reversed loop steps. Replayed compaction pairs still require runner
provenance checks; model aliases alone do not establish provider identity.
"""
import argparse
import hashlib
import json
import os
import re
from pathlib import Path


KEYS = ('inputOther', 'inputCacheRead', 'inputCacheCreation', 'output')


def normalize(events, *, multi_call=False, v2_main=False):
    if not isinstance(events, list) or any(not isinstance(e, dict) for e in events):
        raise ValueError('expected wire event objects')
    if v2_main:
        if not multi_call:
            raise ValueError('v2-main requires explicit multi-call mode')
        return normalize_v2_main(events)
    if any('agentId' in e for e in events
           if e.get('type') in ('llm.request', 'usage.record')):
        raise ValueError('agent-core-v2 requires explicit v2-main mode')
    if multi_call:
        return normalize_multiple(events)
    requests = [e for e in events if e.get('type') == 'llm.request']
    records = [e for e in events if e.get('type') == 'usage.record']
    if len(requests) != 1 or len(records) != 1:
        raise ValueError('expected exactly one model request and one usage record')
    request, record = requests[0], records[0]
    if (request.get('modelAlias') != 'kimi-code/kimi-for-coding'
            or request.get('model') != 'kimi-for-coding'
            or record.get('model') != 'kimi-code/kimi-for-coding'
            or record.get('usageScope') != 'turn'):
        raise ValueError('unexpected model or usage scope')
    raw = record.get('usage', {})
    keys = KEYS
    if not isinstance(raw, dict):
        raise ValueError('incomplete or invalid usage')
    if any(type(raw.get(k)) is not int or raw[k] < 0 for k in keys):
        raise ValueError('incomplete or invalid usage')
    total_input = sum(raw[k] for k in keys[:3])
    return {'requested_model': request['modelAlias'], 'model': request['model'],
            'model_calls': 1, 'raw_usage': {k: raw[k] for k in keys},
            'usage': {'input_tokens': total_input, 'output_tokens': raw['output'],
                      'cached_input_tokens': raw['inputCacheRead'],
                      'reasoning_tokens': None, 'total_tokens': total_input + raw['output']},
            'reported_cost_usd': None, 'billing_reconciled': False,
            'source_kind': 'Kimi local wire usage.record; step.end mirror not counted'}


def normalize_v2_main(events):
    """Sequential main-agent v2 records, with no implicit version coercion.

    This validates wire structure, not endpoint identity or completeness of
    lower-level provider retry accounting. A runner must establish those.
    """
    converted = []
    last_step = None
    for event in events:
        if 'agentId' in event and event['agentId'] != 'main':
            raise ValueError('non-main agent event; multi-agent accounting is unsupported')
        if event.get('type') in ('llm.request', 'usage.record'):
            if event.get('agentId') != 'main':
                raise ValueError('mixed or missing v2 main-agent identity')
            if event['type'] == 'llm.request':
                # Each loop step must have a fresh identity. Replayed complete
                # request/usage pairs must not silently inflate trial usage.
                if event.get('kind') == 'loop':
                    step = event.get('turnStep')
                    if not isinstance(step, str) or not re.fullmatch(r'(0|[1-9][0-9]*)\.[1-9][0-9]*', step):
                        raise ValueError('missing or invalid v2 loop turnStep')
                    position = tuple(map(int, step.split('.')))
                    if last_step is not None and position <= last_step:
                        raise ValueError('repeated or reversed v2 loop step; replay or retry is unmeasured')
                    last_step = position
        converted.append({k: v for k, v in event.items() if k != 'agentId'})
    result = normalize_multiple(converted)
    result.update(wire_format='agent-core-v2', agent_id='main',
                  model_identity_verified=False, provider_retry_accounting_verified=False)
    return result


def normalize_multiple(events):
    pending = None
    calls = []
    for index, event in enumerate(events):
        kind = event.get('type')
        if kind not in ('llm.request', 'usage.record'):
            continue
        if 'agentId' in event:
            raise ValueError('agent-core-v2 or multi-agent records are not supported')
        if kind == 'llm.request':
            if pending is not None:
                raise ValueError('overlapping request or retry without usage; total is unmeasured')
            if event.get('kind') not in ('loop', 'compaction'):
                raise ValueError('missing or unsupported request kind')
            pending = (index, event)
            continue
        if pending is None:
            raise ValueError('usage without a request; possible replay or duplicate')
        request_index, request = pending
        expected_scope = 'turn' if request['kind'] == 'loop' else 'session'
        if event.get('usageScope') != expected_scope:
            raise ValueError('usage scope does not match request kind')
        # Reuse the strict model and disjoint-counter validation. Scope is
        # checked above; compaction records use the same counter semantics.
        normalized = normalize([request, {**event, 'usageScope': 'turn'}])
        calls.append({'request_event_index': request_index, 'usage_event_index': index,
                      'kind': request['kind'], 'scope': expected_scope,
                      'raw_usage': normalized['raw_usage']})
        pending = None
    if pending is not None or not calls:
        raise ValueError('missing request usage; total is unmeasured')
    raw = {key: sum(call['raw_usage'][key] for call in calls) for key in KEYS}
    result = normalize([
        {'type': 'llm.request', 'modelAlias': 'kimi-code/kimi-for-coding',
         'model': 'kimi-for-coding'},
        {'type': 'usage.record', 'model': 'kimi-code/kimi-for-coding',
         'usageScope': 'turn', 'usage': raw}])
    result.update(model_calls=len(calls), calls=calls,
                  loop_calls=sum(call['kind'] == 'loop' for call in calls),
                  compaction_calls=sum(call['kind'] == 'compaction' for call in calls),
                  trial_boundary_verified=False,
                  source_semantics_commit='95478e8c7ba248fd2470d5bb151555ec7fedd19d')
    return result


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('wire', type=Path)
    parser.add_argument('output', type=Path)
    parser.add_argument('--multi-call', action='store_true',
                        help='sum sequential loop and compaction usage; requires a fresh task-local log')
    parser.add_argument('--v2-main', action='store_true',
                        help='explicit main-agent v0.41.0 v2 wire format; requires --multi-call')
    args = parser.parse_args()
    with args.wire.open('rb') as stream:
        data = stream.read(8 * 1024 * 1024 + 1)
    if len(data) > 8 * 1024 * 1024:
        raise ValueError('wire log exceeds 8 MiB import limit')
    events = [json.loads(line) for line in data.splitlines() if line.strip()]
    result = normalize(events, multi_call=args.multi_call, v2_main=args.v2_main)
    result['wire_sha256'] = hashlib.sha256(data).hexdigest()
    fd = os.open(args.output, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
    with os.fdopen(fd, 'w', encoding='utf-8') as stream:
        json.dump(result, stream, ensure_ascii=False, indent=2)
        stream.write('\n')
    print(json.dumps(result, ensure_ascii=False))
