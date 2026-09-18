#!/usr/bin/env python3
"""Split one recorded browser trial into process, MCP and un-attributed time.

This is an accounting aid for future Yee/Aside cohorts.  It does not label time
outside MCP as model latency, and it refuses overlapping or incomplete protocol
intervals rather than assigning their cost heuristically.
"""
import argparse
import importlib.util
import json
from pathlib import Path


def module(name, filename):
    spec = importlib.util.spec_from_file_location(name, Path(__file__).with_name(filename))
    result = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(result)
    return result


wire_operations = module('mcp_wire_operations', 'mcp_wire_operations.py')


def process_window(summary):
    timing = summary.get('model_process_timing')
    if not isinstance(timing, dict) or timing.get('clock') != 'monotonic_ns':
        raise ValueError('model process monotonic timing is required')
    start = timing.get('started_monotonic_ns')
    end = timing.get('finished_monotonic_ns')
    if (type(start) is not int or type(end) is not int or start < 0 or end <= start
            or type(timing.get('elapsed_seconds')) not in (int, float)):
        raise ValueError('invalid model process timing')
    elapsed = (end - start) / 1e9
    if abs(timing['elapsed_seconds'] - elapsed) > .000001:
        raise ValueError('model process timing duration mismatch')
    return start, end, elapsed


def yee_intervals(rows):
    if not isinstance(rows, list) or len(rows) % 2:
        raise ValueError('complete Yee MCP call pairs required')
    intervals = []
    for request, response in zip(rows[::2], rows[1::2]):
        start = request.get('monotonic_ns')
        end = response.get('monotonic_ns')
        if (request.get('kind') != 'mcp_request' or response.get('kind') != 'mcp_response'
                or request.get('invocation') != response.get('invocation')
                or type(start) is not int or type(end) is not int or start < 0 or end < start):
            raise ValueError('invalid Yee MCP interval')
        intervals.append({'operation': request.get('name'), 'start_ns': start, 'end_ns': end,
                          'browser_tool': request.get('name') == 'yee_browser'})
    return intervals


def aside_intervals(rows):
    operations, _ = wire_operations.derive(rows)
    if len(operations) % 2:
        raise ValueError('complete Aside MCP operation pairs required')
    intervals = []
    for request, response in zip(operations[::2], operations[1::2]):
        start = request.get('request_received_monotonic_ns')
        end = response.get('response_forwarded_monotonic_ns')
        if (request.get('kind') != 'mcp_request' or response.get('kind') != 'mcp_response'
                or request.get('invocation') != response.get('invocation')
                or type(start) is not int or type(end) is not int or start < 0 or end < start):
            raise ValueError('invalid Aside MCP interval')
        intervals.append({'operation': request.get('operation'), 'start_ns': start, 'end_ns': end,
                          'browser_tool': request.get('operation') == 'tools/call'})
    return intervals


def analyze(summary, browser, rows):
    start, end, process_seconds = process_window(summary)
    if browser == 'yee':
        intervals = yee_intervals(rows)
    elif browser == 'aside':
        intervals = aside_intervals(rows)
    else:
        raise ValueError('browser must be yee or aside')
    if not intervals:
        raise ValueError('at least one complete MCP interval is required')
    previous = start
    before = between = active = browser_active = 0
    for index, interval in enumerate(intervals):
        interval_start, interval_end = interval['start_ns'], interval['end_ns']
        if interval_start < previous or interval_end > end:
            raise ValueError('MCP interval falls outside model process or overlaps another interval')
        gap = interval_start - previous
        if index == 0:
            before = gap / 1e9
        else:
            between += gap / 1e9
        elapsed = (interval_end - interval_start) / 1e9
        active += elapsed
        if interval['browser_tool']:
            browser_active += elapsed
        previous = interval_end
    after = (end - previous) / 1e9
    breakdown = {
        'before_first_mcp_seconds': before,
        'inside_mcp_seconds': active,
        'between_mcp_seconds': between,
        'after_last_mcp_seconds': after,
    }
    if abs(sum(breakdown.values()) - process_seconds) > .000001:
        raise ValueError('latency accounting mismatch')
    return {
        'schema': 'yee.browser-trial-latency.v1',
        'browser': browser,
        'model_process_seconds': process_seconds,
        'mcp_intervals': len(intervals),
        'browser_tool_mcp_seconds': browser_active,
        'seconds': breakdown,
        'limits': ('MCP intervals cover local transport and browser execution. Time outside MCP is '
                   'unattributed and can include model inference, provider service, process startup and host work; '
                   'it is not model-only latency.'),
    }


def read_json_lines(path):
    return [json.loads(line) for line in path.read_text().splitlines() if line]


def aside_wire_path(record, summary, override=None):
    path = override or summary.get('mcp_wire_record') or record / 'mcp-wire.jsonl'
    if not isinstance(path, Path):
        path = Path(path)
    if not path.is_absolute():
        raise ValueError('Aside raw MCP journal path must be absolute')
    return path


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('record', type=Path)
    parser.add_argument('--browser', choices=('yee', 'aside'), required=True)
    parser.add_argument('--mcp-wire', type=Path,
                        help='explicit absolute Aside raw relay journal, when it is outside the model record')
    args = parser.parse_args()
    record = args.record
    summary = json.loads((record / 'summary.json').read_text())
    if args.browser == 'yee':
        rows = read_json_lines(record / 'mcp-calls.jsonl')
    else:
        rows = read_json_lines(aside_wire_path(record, summary, args.mcp_wire))
    print(json.dumps(analyze(summary, args.browser, rows), indent=2))


if __name__ == '__main__':
    main()
