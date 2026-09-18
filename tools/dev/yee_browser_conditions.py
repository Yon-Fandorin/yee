"""Literal predicates over complete native viewport text; no page execution."""
import json
import re

_STRING = r'"(?:[^"\\]|\\.)*"'
_NODE = re.compile(r'^\+@\S+ ([a-z]+) (' + _STRING + r')(.*)$')
_ATTRIBUTE = re.compile(r'^ (text|value|href)=(' + _STRING + r')(.*)$')


def viewport_text(lines):
    """Decode node labels/values only. Metadata and flags are never text."""
    texts = []
    for line in lines:
        match = _NODE.fullmatch(line)
        if not match:
            return None
        tail = match[3]
        values = []
        while attribute := _ATTRIBUTE.match(tail):
            if attribute[1] != 'href':
                values.append(attribute[2])
            tail = attribute[3]
        flags = tail.split()
        # A clipped/redacted field cannot prove literal absence or presence.
        if ('<redacted>' in tail or match[2] == '"<redacted>"' or
                any(flag in ('text_truncated', 'value_truncated', 'secret') for flag in flags)):
            return None
        try:
            label = json.loads(match[2])
            if match[1] != 'text' and len(label.encode('utf-8')) >= 160:
                return None
            texts.append(label)
            texts.extend(json.loads(value) for value in values)
        except ValueError:
            return None
    return texts if texts else None


def matches(texts, value, absent=False):
    if texts is None:
        return None
    present = any(value in text for text in texts)
    return not present if absent else present
