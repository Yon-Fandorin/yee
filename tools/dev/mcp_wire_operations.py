"""Validate complete relay forwarding and derive client request/result pairs.

Raw frames remain the authority. Notifications/reverse RPC are counted, never
recast as browser operations. Protocol error replies and unknown client methods
are unsupported by the downstream text audit, not successful empty responses.
"""
import base64
import json


def unique_object(pairs):
    value = {}
    for key, item in pairs:
        if key in value: raise ValueError('duplicate protocol key')
        value[key] = item
    return value


def derive(rows):
    if not isinstance(rows, list) or not rows:
        raise ValueError('missing wire events')
    frames = {}
    order = []
    for i, row in enumerate(rows, 1):
        if (not isinstance(row, dict) or type(row.get('sequence')) is not int
                or row['sequence'] != i or row.get('direction') not in ('to_server', 'to_client')
                or type(row.get('monotonic_ns')) is not int or row['monotonic_ns'] < 0):
            raise ValueError('invalid wire event')
        frame = row.get('frame')
        if not isinstance(frame, str) or not frame: raise ValueError('missing frame identity')
        if row.get('kind') == 'wire_received':
            if frame in frames: raise ValueError('duplicate received frame')
            raw = base64.b64decode(row.get('base64', ''), validate=True)
            if not raw.endswith(b'\n'): raise ValueError('incomplete wire frame')
            message = json.loads(raw, object_pairs_hook=unique_object)
            if not isinstance(message, dict) or message.get('jsonrpc') != '2.0':
                raise ValueError('invalid JSON-RPC message')
            frames[frame] = {'row': row, 'message': message, 'forwarded': None}
            order.append(frame)
        elif row.get('kind') == 'wire_forwarded':
            entry = frames.get(frame)
            if (entry is None or entry['forwarded'] is not None
                    or entry['row']['direction'] != row['direction']
                    or row['monotonic_ns'] < entry['row']['monotonic_ns']):
                raise ValueError('invalid wire forwarding')
            entry['forwarded'] = row
        else: raise ValueError('unsupported wire event')
    if any(entry['forwarded'] is None for entry in frames.values()):
        raise ValueError('received but unforwarded frame; outcome unverified')
    pending = {}
    requests = []
    notifications = reverse = 0
    for frame in order:
        entry = frames[frame]; message = entry['message']; direction = entry['row']['direction']
        if 'method' in message:
            if not isinstance(message['method'], str) or 'result' in message or 'error' in message:
                raise ValueError('invalid RPC request')
            if 'id' not in message:
                notifications += 1
                continue
            identity = message['id']
            if type(identity) not in (str, int): raise ValueError('invalid RPC identity')
            key = (direction, type(identity).__name__, identity)
            if key in pending: raise ValueError('overlapping RPC identity')
            pending[key] = entry
            if direction == 'to_server': requests.append(entry)
            else: reverse += 1
        else:
            identity = message.get('id')
            if type(identity) not in (str, int) or ('result' in message) == ('error' in message):
                raise ValueError('invalid RPC reply')
            opposite = 'to_client' if direction == 'to_server' else 'to_server'
            request = pending.pop((opposite, type(identity).__name__, identity), None)
            if request is None: raise ValueError('unmatched or duplicate RPC reply')
            request['reply'] = entry
    if pending: raise ValueError('missing RPC reply')
    operations = []
    for request in requests:
        message = request['message']; reply = request['reply']; result = reply['message']
        if message['method'] not in ('initialize', 'tools/list', 'tools/call'):
            raise ValueError('client method unsupported by text audit')
        if 'error' in result or not isinstance(result.get('result'), dict):
            raise ValueError('protocol error reply unsupported by text audit')
        elapsed = reply['forwarded']['monotonic_ns'] - request['row']['monotonic_ns']
        if elapsed < 0: raise ValueError('reply predates request')
        payload = dict(result['result'])
        if message['method'] == 'tools/call': payload.setdefault('isError', False)
        n = len(operations)
        common = {'invocation': request['row']['frame'], 'operation': message['method'],
                  'request_received_monotonic_ns': request['row']['monotonic_ns'],
                  'response_forwarded_monotonic_ns': reply['forwarded']['monotonic_ns']}
        operations.extend([
            {'sequence': n+1, 'kind': 'mcp_request', **common, 'params': message.get('params')},
            {'sequence': n+2, 'kind': 'mcp_response', **common, 'result': payload, 'elapsed_ns': elapsed}])
    return operations, {'frames_forwarded': len(frames), 'notifications': notifications,
                        'reverse_requests': reverse, 'notification_semantics_verified': False,
                        'timing_scope': 'sum of request receipt to reply forwarding; not whole task'}
