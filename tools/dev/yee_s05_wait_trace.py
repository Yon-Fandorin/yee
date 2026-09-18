"""Classify recorded S05 waits; not an autonomy or full scenario proof."""
import re
import math
import json
from urllib.parse import urlsplit


def inspect(rows, origin, created_ns, tickets=None):
    url = urlsplit(origin)
    if (url.scheme != 'http' or url.hostname != '127.0.0.1' or url.port is None
            or url.path or url.query or url.fragment or url.username or url.password):
        raise ValueError('S05 wait audit requires exact loopback origin')
    if not isinstance(rows, list) or not rows:
        raise ValueError('incomplete S05 wait trace')
    baseline = None
    previous_tickets = None
    arrivals = None
    if tickets is not None:
        if (not isinstance(tickets,list) or not tickets or any(
                not isinstance(t,dict) or not isinstance(t.get('id'),str)
                or not re.fullmatch(r'T-\d+-\d+',t['id'])
                or type(t.get('after_ms')) is not int or t['after_ms']<0 for t in tickets)
                or len({t['id'] for t in tickets})!=len(tickets)):
            raise ValueError('invalid S05 fixture ticket evidence')
        arrivals={t['id'] for t in tickets if t['after_ms']>0}
    seen = set()
    stamp = created_ns
    waits = []
    completion_observed = False
    pairs = []
    pending = None
    client_errors = []
    for offset, event in enumerate(rows, 1):
        if (not isinstance(event, dict) or type(event.get('sequence')) is not int
                or event['sequence'] != offset or type(event.get('time_ns')) is not int
                or event['time_ns'] < stamp):
            raise ValueError('reordered or invalid S05 trace')
        stamp = event['time_ns']
        if event.get('kind') == 'client_error':
            if pending is not None or not isinstance(event.get('error'), str) or not event['error']:
                raise ValueError('invalid or interleaved S05 client error')
            client_errors.append({'sequence': offset, 'error': event['error']})
        elif event.get('kind') == 'request' and pending is None:
            pending = event
        elif event.get('kind') == 'response' and pending is not None:
            pairs.append((pending, event))
            pending = None
        else:
            raise ValueError('invalid S05 event order')
    if pending is not None:
        raise ValueError('incomplete S05 wait trace')
    for begin, end in pairs:
        req, res = begin.get('request'), end.get('response')
        if (begin.get('kind') != 'request' or end.get('kind') != 'response'
                or not isinstance(req, dict) or not isinstance(res, dict)):
            raise ValueError('invalid S05 native pair')
        rid = req.get('id')
        if (not isinstance(rid, str) or not rid or rid in seen or res.get('id') != rid
                or type(res.get('ok')) is not bool or res.get('execution_settled') is not True
                or end.get('response_source') != 'mailbox'):
            raise ValueError('replayed or unsettled S05 native pair')
        seen.add(rid)
        snapshot = res.get('snapshot')
        observed = None
        current_tickets = None
        if isinstance(snapshot, str):
            document, revision, tab = res.get('document'), res.get('revision'), res.get('tab')
            header = snapshot.split('\n', 1)[0]
            if (not isinstance(document, str) or not document or type(revision) is not int
                    or revision < 1 or not isinstance(tab, str) or not tab
                    or not header.startswith(f'page @{document} rev={revision} ')
                    or not re.search(r' origin="'+re.escape(origin)+r'"(?: delta)?$', header)):
                raise ValueError('invalid S05 snapshot scope')
            if res.get('truncated') is False:
                observed = (document, revision, tab)
                if arrivals is not None and not header.endswith(' delta'):
                    current_tickets={ticket for ticket in arrivals if re.search(
                        r'(?<![\w-])'+re.escape(ticket)+r'(?![\w-])',snapshot)}
            # A fixture /view event proves server delivery, not model-visible
            # completion. Require an actual added/current text node, never a
            # title, removed delta node or incidental substring.
            if res['ok']:
                for line in snapshot.splitlines()[1:]:
                    match = re.fullmatch(r'\+@\S+ text ("(?:[^"\\]|\\.)*")', line)
                    if match and json.loads(match[1]) == 'All scheduled arrivals delivered':
                        completion_observed = True
        if req.get('command') == 'wait-change':
            duration = req.get('wait_ms')
            if (type(duration) is not int or not 1 <= duration <= 30000
                    or baseline is None or req.get('document') != baseline[0]
                    or req.get('baseline_document') != baseline[0]
                    or type(req.get('baseline_revision')) is not int
                    or req['baseline_revision'] != baseline[1]):
                raise ValueError('wait lacks acknowledged document baseline')
            timing = res.get('timing')
            elapsed = timing.get('native_elapsed_ms') if isinstance(timing, dict) else None
            known = type(elapsed) in (int, float) and math.isfinite(elapsed) and elapsed >= 0
            if res['ok']:
                report = res.get('wait')
                mode = req.get('wait_mode')
                if mode not in (None,'content'):
                    raise ValueError('unknown wait comparison mode')
                if (not isinstance(report, dict) or report.get('mechanism') != 'accessibility_events'
                        or report.get('reason') not in ('changed', 'limit', 'scope_limit')
                        or type(report.get('probes')) is not int or report['probes'] < 1
                        or res.get('document') != baseline[0] or res.get('tab') != baseline[2]
                        or not isinstance(snapshot, str) or snapshot.split('\n',1)[0].endswith(' delta')
                        or type(res.get('truncated')) is not bool
                        or (report['reason'] == 'scope_limit') != res['truncated']):
                    raise ValueError('invalid wait result or scope')
                if (mode == 'content' and report.get('comparison') != 'content') or (
                        mode is None and report.get('comparison') not in (None,'identity')):
                    raise ValueError('wait comparison mode not confirmed')
                waits.append({'ok': True, 'reason': report['reason'], 'probes': report['probes'],
                              'requested_ms': duration, 'native_elapsed_ms': elapsed if known else None,
                              'early_reprobe_change': (report['reason'] == 'changed' and
                                  report['probes'] >= 2 and elapsed < duration) if known else None})
                if 'comparison' in report:waits[-1]['comparison']=report['comparison']
                if arrivals is not None:
                    newly_seen=(sorted(current_tickets-previous_tickets)
                                if current_tickets is not None and previous_tickets is not None else None)
                    early=waits[-1]['early_reprobe_change']
                    waits[-1].update(new_fixture_ticket_ids=newly_seen,
                        early_arrival_observed=(bool(newly_seen) and early)
                        if newly_seen is not None and early is not None else None)
            else:
                waits.append({'ok': False, 'reason': 'error', 'probes': None,
                              'error': res.get('error'), 'requested_ms': duration,
                              'native_elapsed_ms': elapsed if known else None})
        if not res['ok'] or req.get('command') in ('detach', 'cancel', 'navigate'):
            baseline = None
            previous_tickets = None
        elif isinstance(snapshot, str):
            baseline = observed
            previous_tickets = current_tickets
    successful = [wait for wait in waits if wait['ok']]
    measured = [wait['native_elapsed_ms'] for wait in waits
                if wait['native_elapsed_ms'] is not None]
    return {'trace_valid': True, 'wait_attempts': len(waits),
            'completion_message_observed': completion_observed,
            'client_errors': client_errors,
            'successful_waits': len(successful), 'failed_waits': len(waits)-len(successful),
            'reported_probes': sum(wait['probes'] for wait in successful),
            # Include failed attempts. A partial sum cannot stand for the cost
            # of all waits, and native time cannot stand for whole-task time.
            'measured_native_wait_ms': sum(measured),
            'unmeasured_wait_attempts': len(waits)-len(measured),
            'total_native_wait_ms': sum(measured) if len(measured)==len(waits) else None,
            'waits': waits,
            'strategy': 'recorded_accessibility_wait' if successful else
                        'failed_wait_attempts' if waits else 'no_recorded_wait',
            'model_strategy_verified': False, 'event_delivery_completeness_verified': False}
