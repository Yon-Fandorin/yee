"""Installed Grok ACP per-prompt usage, distinct from final-model-call metadata."""

FIELDS = ('inputTokens', 'outputTokens', 'totalTokens', 'cachedReadTokens',
          'reasoningTokens', 'modelCalls', 'apiDurationMs')


def normalize(prompts, session_id):
    """Reject missing/replayed scopes; never substitute last-call token counts."""
    if not isinstance(prompts, list) or not prompts:
        raise ValueError('prompt responses required')
    seen_requests, seen_ids, rows = set(), set(), []
    for prompt in prompts:
        response = prompt['response']
        meta = response['result']['_meta']
        identity, request = response['id'], meta['requestId']
        if (meta['sessionId'] != session_id or meta['promptId'] != request
                or not isinstance(request, str) or not request
                or request in seen_requests or identity in seen_ids):
            raise ValueError('unmatched or replayed prompt response')
        seen_requests.add(request); seen_ids.add(identity)
        usage = meta.get('usage')
        if not isinstance(usage, dict) or any(
                type(usage.get(k)) is not int or usage[k] < 0 for k in FIELDS):
            raise ValueError('complete per-prompt usage unavailable')
        if (usage['totalTokens'] != usage['inputTokens'] + usage['outputTokens']
                or usage['cachedReadTokens'] > usage['inputTokens']
                or usage['reasoningTokens'] > usage['outputTokens']):
            raise ValueError('inconsistent usage counters')
        models = usage.get('modelUsage')
        if (not isinstance(models, dict) or not models or any(
                not isinstance(v, dict) or any(type(v.get(k)) is not int or v[k] < 0
                                              for k in FIELDS) for v in models.values())
                or any(sum(v[k] for v in models.values()) != usage[k] for k in FIELDS)):
            raise ValueError('per-model usage does not reconcile')
        rows.append({'request_id': request, 'rpc_id': identity,
                     'stop_reason': response['result']['stopReason'],
                     'model_id': meta['modelId'], 'usage': dict(usage),
                     'final_call_total_tokens': meta.get('totalTokens')})
    return {'session_id': session_id, 'prompts': rows,
            'totals': {k: sum(r['usage'][k] for r in rows) for k in FIELDS},
            'cached_input_included_in_input': True,
            'reasoning_included_in_output': True,
            'provider_billing_reconciled': False}
