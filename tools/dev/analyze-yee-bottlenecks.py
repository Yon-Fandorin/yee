#!/usr/bin/env python3
"""Attribute recorded runner time conservatively; never claim model-only latency."""
import argparse
import importlib.util
import json
from pathlib import Path

spec = importlib.util.spec_from_file_location('native_timing', Path(__file__).with_name('inspect-native-timing.py'))
native_timing = importlib.util.module_from_spec(spec)
spec.loader.exec_module(native_timing)


def analyze(summary, native, calls, usage):
    audit = native_timing.analyze(native)
    if not audit['timing_audit_pass']:
        raise ValueError('native timing audit failed')
    elapsed = summary.get('runner_elapsed_seconds')
    if type(elapsed) not in (int, float) or not 0 < elapsed < float('inf'):
        raise ValueError('valid runner duration required')
    pending = None
    previous_end = None
    intervals = []
    responses = []
    for event in calls:
        stamp = event.get('time_ns')
        if type(stamp) is not int or stamp < 0:
            raise ValueError('invalid MCP timestamp')
        if event.get('kind') == 'mcp_request':
            if pending is not None or (previous_end is not None and stamp < previous_end):
                raise ValueError('overlapping or reversed MCP intervals')
            pending = event
        elif event.get('kind') == 'mcp_response':
            if pending is None or event.get('invocation') != pending.get('invocation') or stamp < pending['time_ns']:
                raise ValueError('unmatched or reversed MCP response')
            intervals.append((stamp - pending['time_ns']) / 1e9)
            payloads = [json.loads(text) for text in event.get('content', [])]
            for payload in payloads:
                items = payload if isinstance(payload, list) else [payload]
                snapshots = [item for item in items if isinstance(item, dict) and isinstance(item.get('snapshot'), str)]
                duplicates = 0
                for prior, current in zip(snapshots, snapshots[1:]):
                    # Only identical node lines on the same document are candidates.
                    # Headers/metadata remain distinct; do not rewrite these records.
                    if (prior.get('document') == current.get('document') and
                            prior['snapshot'].splitlines()[1:] == current['snapshot'].splitlines()[1:] and
                            len(current['snapshot'].splitlines()) > 1):
                        duplicates += 1
                responses.append({'utf8_bytes': sum(len(text.encode('utf-8')) for text in event.get('content', [])),
                                  'snapshot_count': len(snapshots), 'same_node_lines_candidates': duplicates})
            previous_end = stamp
            pending = None
        else:
            raise ValueError('incomplete or unknown MCP event')
    if pending is not None or not intervals:
        raise ValueError('incomplete MCP intervals')
    totals = audit['totals_ms']
    native_seconds = totals['native_elapsed_ms'] / 1000
    mcp_seconds = sum(intervals)
    if mcp_seconds > elapsed or native_seconds > mcp_seconds:
        raise ValueError('timing scopes are inconsistent')
    breakdown = {
        'native_approval_wait': totals['user_wait_ms'] / 1000,
        'native_processing': totals['native_nonwait_ms'] / 1000,
        'inside_mcp_other': mcp_seconds - native_seconds,
        'outside_mcp_unattributed': elapsed - mcp_seconds,
    }
    return {'schema': 'yee.bottleneck-diagnostic.v1', 'runner_seconds': elapsed,
            'seconds': breakdown, 'percent': {k: v / elapsed * 100 for k, v in breakdown.items()},
            'native_requests': audit['requests'], 'mcp_response_sizes': responses,
            'reported_model_calls': usage.get('model_calls'), 'provider_usage': usage.get('usage'),
            'limits': 'Recorded runner only, not whole task. Outside-MCP includes model, startup, client and transport; not model-only latency. Wall-clock MCP intervals require stable clocks. Duplicate node lines are diagnostic candidates, not removable observations.'}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('record', type=Path)
    args = parser.parse_args()
    def read(name, lines=False):
        text = (args.record/name).read_text()
        return [json.loads(line) for line in text.splitlines() if line] if lines else json.loads(text)
    print(json.dumps(analyze(read('summary.json'), read('native.jsonl', True),
                             read('mcp-calls.jsonl', True), read('usage.json')), indent=2))


if __name__ == '__main__':
    main()
