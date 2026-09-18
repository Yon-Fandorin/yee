#!/usr/bin/env python3
"""Join initial tab consent and execution wait; not whole-task timing or isolation."""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path

spec = importlib.util.spec_from_file_location('native_timing', Path(__file__).with_name('inspect-native-timing.py'))
timing = importlib.util.module_from_spec(spec)
spec.loader.exec_module(timing)


def inspect(consent, execution):
    if len(consent) != 2:
        raise ValueError('one exact initial consent request/response required')
    consent_audit = timing.analyze(consent)
    execution_audit = timing.analyze(execution)
    if not consent_audit['timing_audit_pass'] or not execution_audit['timing_audit_pass']:
        raise ValueError('missing or invalid timing cannot be treated as zero')
    request, response = consent[0]['request'], consent[1]['response']
    tab = response.get('tab')
    if (request.get('command') != 'attach' or response.get('ok') is not True or
            response.get('execution_settled') is not True or
            consent[1].get('response_source') != 'mailbox' or not isinstance(tab, str) or not tab):
        raise ValueError('successful settled native attach required')
    rules = request.get('permissions', {})
    if not isinstance(rules, dict) or any(k not in ('fill', 'click', 'navigate') or
            v not in ('allow', 'ask', 'deny') for k, v in rules.items()):
        raise ValueError('unsupported declared task rules')
    end = consent[1].get('time_ns')
    if type(end) is not int or end <= 0:
        raise ValueError('consent timestamp missing')
    request_ids = {request['id']}
    for event in execution:
        stamp = event.get('time_ns')
        if type(stamp) is not int or stamp < end:
            raise ValueError('execution does not follow initial consent')
        if event['kind'] == 'request':
            action = event['request']
            if action['id'] in request_ids:
                raise ValueError('overlapping consent and execution records')
            request_ids.add(action['id'])
            if action['command'] in ('attach', 'detach', 'cancel', 'select-tab'):
                raise ValueError('authority changes require a separate consent segment')
        elif event['kind'] == 'response':
            result = event['response']
            if (result.get('tab') != tab or result.get('execution_settled') is not True or
                    event.get('response_source') != 'mailbox'):
                raise ValueError('execution must be settled on the consented tab')
    initial = consent_audit['totals_ms']['user_wait_ms'] / 1000
    actions = execution_audit['totals_ms']['user_wait_ms'] / 1000
    return {'schema': 'yee.approval-cost.v1', 'record_link_verified': True,
            'tab': tab, 'declared_task_rules': {key: rules.get(key, 'ask') for key in ('fill', 'click', 'navigate')},
            'initial_consent_wait_seconds': initial, 'execution_wait_seconds': actions,
            'combined_recorded_wait_seconds': initial + actions,
            'whole_task_seconds': None, 'runtime_permission_enforcement_verified': False,
            'limits': 'Only the supplied initial consent and execution records. Other setup, prompts, retries, model time and reporting are excluded. Tab/timestamp linkage is not OS confinement or proof of unchanged runtime policy.'}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('consent', type=Path)
    parser.add_argument('execution', type=Path)
    args = parser.parse_args()
    raws = [p.read_bytes() for p in (args.consent, args.execution)]
    result = inspect(*[[json.loads(line) for line in data.splitlines()] for data in raws])
    result['input_sha256'] = {str(p): hashlib.sha256(raw).hexdigest()
                              for p, raw in zip((args.consent, args.execution), raws)}
    print(json.dumps(result, indent=2))


if __name__ == '__main__':
    main()
