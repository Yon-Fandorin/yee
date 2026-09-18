"""S10 read-only native trace acceptance, with existing per-tab grants.

Requires initial/final tab inventories. New attach/navigation/mutation is outside
this prepared-tab trial. Model/MCP correlation and log provenance are separate.
"""
from urllib.parse import urlsplit


def verify(data, rows, origin):
    url = urlsplit(origin)
    if (url.scheme != 'http' or url.hostname != '127.0.0.1' or url.port is None
            or url.path or url.query or url.fragment or url.username or url.password):
        raise ValueError('S10 requires exact synthetic loopback origin')
    if (not isinstance(rows, list) or not rows or len(rows) % 2
            or any(not isinstance(r, dict) or type(r.get('sequence')) is not int
                   or r['sequence'] != i for i, r in enumerate(rows, 1))):
        raise ValueError('incomplete or noncontiguous S10 trace')
    expected = {origin+d['source']: d for d in data['documents']}
    pairs = []
    ids = set()
    for i in range(0, len(rows), 2):
        begin, end = rows[i:i+2]
        request, response = begin.get('request'), end.get('response')
        if (begin.get('kind') != 'request' or end.get('kind') != 'response'
                or not isinstance(request, dict) or not isinstance(response, dict)):
            raise ValueError('invalid S10 native pair')
        rid = request.get('id')
        if (not isinstance(rid, str) or not rid or rid in ids or response.get('id') != rid
                or response.get('ok') is not True or response.get('execution_settled') is not True
                or end.get('response_source') != 'mailbox'):
            raise ValueError('failed, replayed, uncorrelated or unsettled S10 request')
        ids.add(rid); pairs.append((request, response))
    if pairs[0][0].get('command') != 'tabs' or pairs[-1][0].get('command') != 'tabs':
        raise ValueError('S10 must begin and end with a native tab inventory')

    def inventory(response):
        tabs = response.get('tabs')
        if not isinstance(tabs, list) or len(tabs) != 3:
            raise ValueError('expected exactly three preapproved S10 tabs')
        mapping = {}
        active = []
        for tab in tabs:
            if (not isinstance(tab, dict) or tab.get('url') not in expected
                    or tab.get('permission') != 'granted' or tab.get('metadata_truncated') is not False
                    or not isinstance(tab.get('tab'), str) or not tab['tab']
                    or tab['tab'] in mapping or type(tab.get('active')) is not bool):
                raise ValueError('invalid, expired or out-of-scope tab capability')
            mapping[tab['tab']] = tab['url']
            if tab['active']: active.append(tab['tab'])
        if len(set(mapping.values())) != 3 or len(active) != 1:
            raise ValueError('duplicate document or ambiguous active tab')
        return mapping, active[0]

    grants, selected = inventory(pairs[0][1])
    original = next((cap for cap, u in grants.items()
                     if expected[u]['id'] == data['original_document_id']), None)
    if selected != original:
        raise ValueError('trial did not start on the original tab')
    observed = set()
    for request, response in pairs:
        command = request.get('command')
        if command == 'tabs':
            current, active = inventory(response)
            if current != grants or active != selected:
                raise ValueError('grant/selection changed outside recorded selection')
            continue
        if command not in ('select-tab', 'observe', 'read', 'scroll', 'status'):
            raise ValueError('out-of-scope S10 navigation, attach or mutation')
        if command == 'select-tab':
            selected = request.get('tab')
            if selected not in grants:
                raise ValueError('selection outside original grants')
        if command != 'status' and response.get('tab') not in grants:
            raise ValueError('read outside approved S10 documents')
        if command == 'select-tab' and response.get('tab') != selected:
            raise ValueError('selection result mismatches requested tab')
        snapshot = response.get('snapshot')
        if command != 'status' and isinstance(snapshot, str) and response.get('truncated') is False:
            document = expected[grants[response['tab']]]
            facts = [document['id'], document['source'],
                     f"Processing: {document['processing_days']} days",
                     f"Free shipping minimum: {document['free_shipping_minimum_krw']} KRW",
                     f"Returns: {document['return_days']} days"]
            if all(fact in snapshot for fact in facts): observed.add(document['id'])
    if selected != original:
        raise ValueError('did not return to original tab')
    if observed != {d['id'] for d in data['documents']}:
        raise ValueError('complete policy facts were not observed for every tab')
    return {'tab_audit_status': 'verified', 'original_tab_return_verified': True,
            'original_grants_preserved': True, 'observed_document_ids': sorted(observed),
            'native_trace_requests': len(pairs), 'model_autonomy_verified': False}
