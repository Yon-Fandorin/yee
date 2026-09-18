#!/usr/bin/env python3
"""Derive a zero-user-wait comparison without rewriting original trial records."""
import argparse
import hashlib
import json
import math
from pathlib import Path


def read(path):
    return json.loads(path.read_text())


def lines(path):
    return [json.loads(line) for line in path.read_text().splitlines()]


def normalized(seconds, wait):
    if not all(type(v) in (int, float) and math.isfinite(v) for v in (seconds, wait)):
        raise ValueError('missing or invalid recorded timing')
    if not 0 <= wait <= seconds:
        raise ValueError('user wait outside runner duration')
    return seconds - wait


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--yee-root', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    docs = Path(__file__).resolve().parents[2] / 'docs/agent-browser-validation'
    suite_path = docs / 'aside-scenario-suite-20260909.json'
    supplement_path = docs / 'aside-approved-three-20260910.json'
    yee_path = args.yee_root / 'after-summary.json'
    suite, supplement = read(suite_path), read(supplement_path)
    latest = {c['scenario']: c for c in supplement['cases']}
    yee = {c['scenario']: c for c in read(yee_path)}
    sources = [suite_path, supplement_path, yee_path]
    cases = []
    for original in suite['cases']:
        sid = original['scenario']
        a = latest.get(sid, original)
        y = yee[sid]
        native_path = args.yee_root / y['attempt'] / 'model/native.jsonl'
        sources.append(native_path)
        native = lines(native_path)
        requests = {r['request']['id']: r['request'] for r in native if r['kind'] == 'request'}
        responses = [r['response'] for r in native if r['kind'] == 'response']
        assert len(requests) == len(responses) == y['native_requests']
        assert len({r['id'] for r in responses}) == len(responses)
        ywait = sum(r['timing']['user_wait_ms'] / 1000 for r in responses)
        assert math.isclose(ywait, y['native_user_wait_seconds'], abs_tol=1e-8)
        # A waiting non-ask request is an observed native permission interruption.
        # Zero-duration uninstrumented UI prompts cannot be inferred from this log.
        yapproval = sum(requests[r['id']]['command'] != 'ask' and
                        r['timing']['user_wait_ms'] > 0 for r in responses)
        yinput = sum(r['command'] == 'ask' for r in requests.values())
        await_seconds = 0.0
        aapproval = ainput = 0
        if sid in latest:
            host_path = Path(a['source_root']) / 'model/host-calls.jsonl'
            if host_path.exists():
                sources.append(host_path)
                host = lines(host_path)
                asks = [r['arguments'] for r in host if r['kind'] == 'host_request']
                replies = [r['result'] for r in host if r['kind'] == 'host_response']
                assert len(asks) == len(replies) == a['host_calls']
                assert all(r['status'] == 'answered' for r in replies)
                assert all(q['kind'] == r['kind'] for q, r in zip(asks, replies))
                aapproval = sum(q['kind'] == 'document_permission' for q in asks)
                ainput = len(asks) - aapproval
                await_seconds = sum(r['user_wait_seconds'] for r in replies)
                assert math.isclose(await_seconds, a['synthetic_operator_wait_seconds'], abs_tol=1e-8)
            else:
                assert a['host_calls'] == 0
        # Earlier non-handoff cases have no host channel; native Aside UI wait
        # was not instrumented. Zero is the controlled reporting assumption,
        # not a claim that unobserved approvals did not occur.
        def entry(raw, wait, tokens, approval, user_input, status):
            return {'runner_seconds_raw': raw, 'recorded_user_wait_seconds': wait,
                    'controlled_user_wait_seconds': 0,
                    'zero_wait_runner_seconds': normalized(raw, wait),
                    'total_tokens': tokens, 'observed_approval_requests': approval,
                    'observed_input_or_auth_requests': user_input,
                    'observed_interruption_requests': approval + user_input,
                    'complete_native_approval_count_verified': False,
                    'status': status}
        cases.append({'scenario': sid,
                      'aside': entry(a.get('runner_seconds', a.get('seconds')), await_seconds,
                                     a['usage']['total_tokens'], aapproval, ainput,
                                     'declared_host_AX_gate_pass' if sid in latest else a['completion_class']),
                      'yee': entry(y['seconds'], ywait, y['tokens'], yapproval, yinput,
                                   'pass' if y['pass'] else 'answer_or_scenario_evidence_invalid')})
    assert len(cases) == 12 and len({c['scenario'] for c in cases}) == 12
    result = {'schema': 'yee.aside-zero-user-wait-comparison.v1', 'date': '2026-09-10',
              'policy': {'controlled_user_wait_seconds': 0,
                         'formula': 'runner_seconds_raw - recorded_user_wait_seconds',
                         'scope': 'Model execution only; permission, choice and authentication waits excluded. Setup approvals excluded from runner scope.',
                         'counts': 'Observed requests, not MCP calls or automatically allowed operations. Failures remain counted. Native Aside approval count is unknown.',
                         'unmeasured_wait': 'Controlled zero assumption, not an observed zero. No time is invented or subtracted for missing native Aside telemetry.',
                         'limitations': 'Historical single-run cohort; viewport and permission implementations differ. Not a matched experiment or total-work metric.'},
              'coverage': {'scenarios_with_execution_data': 12, 'aside_declared_passes': 11,
                           'yee_passes': 11, 'all_scenarios_pass': False},
              'cases': cases,
              'totals': {engine: {key: sum(c[engine][key] for c in cases)
                                 for key in ('runner_seconds_raw', 'recorded_user_wait_seconds',
                                             'zero_wait_runner_seconds', 'total_tokens',
                                             'observed_approval_requests', 'observed_input_or_auth_requests',
                                             'observed_interruption_requests')}
                         for engine in ('aside', 'yee')},
              'source_sha256': {str(p): hashlib.sha256(p.read_bytes()).hexdigest() for p in sources}}
    args.output.write_text(json.dumps(result, ensure_ascii=False, indent=2) + '\n')
    print(json.dumps(result['totals'], ensure_ascii=False, indent=2))


if __name__ == '__main__':
    main()
