"""Response-local, exact snapshot string factoring; no semantic filtering."""
from collections import Counter
import json
import re

MAX_TEXTS = 16
MAX_SNAPSHOT_CHARS = 256 * 1024
MAX_EXPANDED_CHARS = 2 * 1024 * 1024
MIN_FRAGMENT = 24
MAX_FRAGMENT = 192


def wire_size(value):
    return len(json.dumps(value, ensure_ascii=False, separators=(',', ':')).encode(
        'utf-8', errors='backslashreplace'))


def factor_snapshots(results):
    """Return (dictionary, copied results), only when factoring saves real bytes.

    Integer parts refer to exact dictionary strings. Everything else, including
    source paths, refs, node operations, revision fences and clipping flags,
    retains its original position. Dictionary selection sees no task or domain.
    """
    snapshots = [r.get('snapshot') for r in results]
    if any(s is not None and not isinstance(s, str) for s in snapshots):
        return None
    strings = [s for s in snapshots if isinstance(s, str)]
    if not strings or not 1024 <= sum(map(len, strings)) <= MAX_SNAPSHOT_CHARS:
        return None
    frequencies = Counter()
    for snapshot in strings:
        words = re.findall(r'\S+\s*', snapshot)
        # Two adjacent settled snapshots commonly repeat a long field value or
        # prose draft.  Include useful intermediate widths so that exact text
        # can be factored even when 16 words exceed MAX_FRAGMENT.
        for width in (3, 5, 8, 12, 16):
            for index in range(len(words) - width + 1):
                fragment = ''.join(words[index:index + width])
                if MIN_FRAGMENT <= len(fragment) <= MAX_FRAGMENT:
                    frequencies[fragment] += 1
    candidates = sorted(
        ((fragment, count) for fragment, count in frequencies.items() if count >= 2),
        key=lambda item: (-((len(item[0]) - 8) * item[1] - len(item[0])), item[0]))
    # Select on remaining literal spans, so overlapping candidates never count
    # an already-factored region twice. Bound candidate work on large documents.
    parts = {index: [s] for index, s in enumerate(snapshots) if isinstance(s, str)}
    texts = []
    for fragment, _ in candidates[:128]:
        if len(texts) == MAX_TEXTS:
            break
        count = sum(part.count(fragment) for row in parts.values()
                    for part in row if isinstance(part, str))
        if count < 2 or (len(fragment) - 8) * count - len(fragment) < 40:
            continue
        reference = len(texts)
        texts.append(fragment)
        for index, row in parts.items():
            expanded = []
            for part in row:
                if not isinstance(part, str) or fragment not in part:
                    expanded.append(part)
                    continue
                pieces = part.split(fragment)
                for position, piece in enumerate(pieces):
                    if position:
                        expanded.append(reference)
                    if piece:
                        expanded.append(piece)
            parts[index] = expanded
    if not texts:
        return None
    factored = [dict(r) for r in results]
    for index, row in parts.items():
        if any(type(part) is int for part in row):
            factored[index]['snapshot'] = {'parts': row}
    original_size = wire_size(results)
    new_size = wire_size({'texts': texts, 'results': factored})
    # Small dictionaries make the model reconstruct ordinary page text for a
    # marginal wire saving.  Reserve the indirection for responses where both
    # the absolute and relative reductions pay for that extra interpretation.
    if new_size + 1024 >= original_size or new_size > original_size * .80:
        return None
    return texts, factored


def expand_snapshots(texts, results):
    """Strictly expand v3 snapshots; text fragments never create envelope keys."""
    if (not isinstance(texts, list) or not 1 <= len(texts) <= MAX_TEXTS or
            any(not isinstance(text, str) or not MIN_FRAGMENT <= len(text) <= MAX_FRAGMENT
                for text in texts) or len(set(texts)) != len(texts)):
        raise ValueError('invalid snapshot dictionary')
    expanded = [dict(r) for r in results]
    used = set()
    total = 0
    for result in expanded:
        snapshot = result.get('snapshot')
        if isinstance(snapshot, dict):
            parts = snapshot.get('parts')
            if (set(snapshot) != {'parts'} or not isinstance(parts, list) or not parts or
                    len(parts) > MAX_SNAPSHOT_CHARS):
                raise ValueError('invalid snapshot parts')
            values = []
            for part in parts:
                if isinstance(part, str):
                    value = part
                elif type(part) is int and 0 <= part < len(texts):
                    value = texts[part]
                    used.add(part)
                else:
                    raise ValueError('invalid snapshot dictionary reference')
                total += len(value)
                if total > MAX_EXPANDED_CHARS:
                    raise ValueError('expanded snapshots exceed limit')
                values.append(value)
            result['snapshot'] = ''.join(values)
        elif isinstance(snapshot, str):
            total += len(snapshot)
            if total > MAX_EXPANDED_CHARS:
                raise ValueError('expanded snapshots exceed limit')
        elif snapshot is not None:
            raise ValueError('invalid dictionary snapshot')
    if used != set(range(len(texts))):
        raise ValueError('unused snapshot dictionary entry')
    return expanded
