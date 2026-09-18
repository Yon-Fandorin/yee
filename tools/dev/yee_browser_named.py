"""Exact displayed-name resolution over a fresh, complete native snapshot.

No fuzzy matching or first-match selection. Native refs and approval remain the
execution authority. This is limited to the existing visible main-document DOM.
"""
import json
import re


def validate_name(name):
    if (not isinstance(name, str) or not name or '…' in name
            or any(ord(c) < 32 or ord(c) == 127 for c in name)
            or len(name.encode('utf-8')) > 160):
        raise ValueError('named target requires a short, nonempty exact displayed name')


MAX_FILL_BYTES = 4000


def parse_batch(encoded):
    actions = json.loads(encoded)
    if not isinstance(actions, list) or not 1 <= len(actions) <= 8:
        raise ValueError('batch requires 1..8 ordered named actions')
    names = set()
    for i, action in enumerate(actions):
        if not isinstance(action, list) or len(action) not in (2, 3):
            raise ValueError('invalid named batch action')
        kind = action[0]
        if not ((kind == 'fill' and len(action) == 3 and isinstance(action[2], str)) or
                (kind == 'check' and len(action) == 3 and type(action[2]) is bool) or
                (kind == 'click' and len(action) == 2 and i == len(actions) - 1)):
            raise ValueError('batch allows fills/checks followed by at most one final button click')
        validate_name(action[1])
        if action[1] in names:
            raise ValueError('batch target names must be distinct')
        names.add(action[1])
        if kind == 'fill' and len(action[2].encode('utf-8')) > MAX_FILL_BYTES:
            raise ValueError('batch fill value exceeds 4000 UTF-8 bytes; preserve content within the supported limit')
    return actions


def resolve_batch(response, actions):
    resolved = []
    for action in actions:
        doc, ref = resolve(response, {'fill':'field', 'check':'checkbox', 'click':'button'}[action[0]], action[1])
        item = {'command': action[0], 'ref': ref}
        if action[0] == 'fill':
            item['value'] = action[2]
        elif action[0] == 'check':
            item['checked'] = action[2]
        resolved.append(item)
    return doc, resolved


def parse_ref_batch(encoded):
    """Validate a reference plan before dispatch; binding stays in the CLI/native."""
    actions = json.loads(encoded)
    if not isinstance(actions, list) or not 1 <= len(actions) <= 8:
        raise ValueError('batch requires 1..8 ordered reference actions')
    for i, item in enumerate(actions):
        if not isinstance(item, dict):
            raise ValueError('reference batch actions must be objects')
        kind = item.get('command')
        extra = {'fill':'value', 'check':'checked'}.get(kind) if isinstance(kind, str) else None
        keys = {'command', 'ref'} | ({extra} if extra else set())
        if (kind not in ('fill', 'check', 'click') or set(item) != keys or
                not isinstance(item['ref'], str) or not item['ref'] or
                (kind == 'fill' and not isinstance(item['value'], str)) or
                (kind == 'check' and type(item['checked']) is not bool) or
                (kind == 'click' and i != len(actions) - 1)):
            raise ValueError('batch allows fills/checks followed by at most one final button click')
        if kind == 'fill' and len(item['value'].encode('utf-8')) > MAX_FILL_BYTES:
            raise ValueError('batch fill value exceeds 4000 UTF-8 bytes; preserve content within the supported limit')
    return actions


def resolve(response, role, name):
    validate_name(name)
    # Click ambiguity is checked across both supported control roles. Never
    # prefer a button over a same-named checkbox (or vice versa).
    roles = ('button', 'checkbox') if role == 'click' else (role,)
    document = response.get('document')
    snapshot = response.get('snapshot')
    if (response.get('ok') is not True or response.get('truncated') is not False
            or not isinstance(document, str) or not document or not isinstance(snapshot, str)
            or ' delta\n' in snapshot or '!truncated' in snapshot):
        raise ValueError('named target requires a fresh complete observation')
    matches = []
    checkbox_match = False
    for line in snapshot.splitlines()[1:]:
        match = re.fullmatch(r'\+@([^ ]+) ([a-z]+) ("(?:[^"\\]|\\.)*")(.*)', line)
        if not match:
            continue
        try:
            label = json.loads(match[3])
        except ValueError:
            raise ValueError('unsupported native name encoding') from None
        if label == name and match[2] == 'checkbox':
            checkbox_match = True
        if label == name and match[2] in roles:
            matches.append(match)
    if len(matches) != 1:
        if not matches and role == 'button' and checkbox_match:
            raise ValueError('batch click requires a button; use check with the observed checkbox ref and desired checked boolean')
        raise ValueError('named target must match exactly one visible control')
    match = matches[0]
    if not re.fullmatch(re.escape(document) + r'_[1-9][0-9]*', match[1]):
        raise ValueError('named target has invalid document reference')
    if ' disabled' in match[4] or 'value=<redacted>' in match[4]:
        raise ValueError('named target is disabled or secret')
    return document, match[1]
