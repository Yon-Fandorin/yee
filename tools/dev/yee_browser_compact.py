"""Lossless human presentation for Yee agent snapshots.

The native bridge continues to use opaque, document-scoped references.  This
module only changes what the CLI prints (and expands the deliberately small
numeric capability notation when sending a command).
"""

import re


# Snapshot node lines put the reference immediately after an optional
# operation marker.  Deliberately do not search arbitrary line content: page
# labels and values are untrusted text and may themselves contain @doc_N.
_NODE_REF = re.compile(r"^([+~-]?)(@)([^\s]+)(?=\s|$)")
_NUMERIC = re.compile(r"^@?([1-9][0-9]*)$")


class CompactError(ValueError):
    pass


def compact_ref(native_ref, document):
    """Return @N for a canonical native ref belonging to document.

    Refs that cannot be proved to be ``document_<positive integer>`` remain
    unchanged, since they may be intentionally opaque.
    """
    if not isinstance(native_ref, str) or not isinstance(document, str):
        return native_ref
    prefix = document + "_"
    value = native_ref[1:] if native_ref.startswith("@") else native_ref
    suffix = value[len(prefix):] if value.startswith(prefix) else ""
    if suffix and re.fullmatch(r"[1-9][0-9]*", suffix):
        return "@" + suffix
    return native_ref


def native_ref(ref, document):
    """Expand a compact numeric ref, requiring its explicit capability."""
    if not isinstance(ref, str) or not isinstance(document, str) or not document:
        raise CompactError("compact action/read requires --document DOCUMENT_UUID")
    match = _NUMERIC.fullmatch(ref)
    if not match:
        return ref
    return document + "_" + match.group(1)


def format_compact_snapshot(snapshot, document=None, include_capability=True):
    """Format a native snapshot without dropping semantic content.

    The response document is preferred by the caller; when unavailable, the
    document is recovered from the native ``page @...`` header.  Only the
    header and proven canonical node references are rewritten.
    """
    if not isinstance(snapshot, str):
        return snapshot
    lines = snapshot.splitlines(keepends=True)
    if not lines:
        return snapshot
    first = lines[0]
    ending = "\n" if first.endswith("\n") else ""
    content = first[:-1] if ending else first
    header = re.match(r"^(page) @([^\s]+)(\s+[^\r\n]*)$", content)
    if not header:
        return snapshot
    encoded_document = header.group(2)
    if document is not None and document != encoded_document:
        # Never shorten refs against a capability that does not describe the
        # snapshot being presented.
        return snapshot
    document = document or encoded_document
    # Keep the complete capability exactly once.  Native document refs are
    # UUIDs in current Yee; percent-escaped values are retained verbatim.
    page = header.group(1) + header.group(3) + ending
    lines[0] = (("capability document=" + document + "\n") if include_capability else "") + page
    prefix = document + "_"
    for index, line in enumerate(lines[1:], 1):
        def replace(match):
            ref = match.group(3)
            suffix = ref[len(prefix):] if ref.startswith(prefix) else ""
            if suffix and re.fullmatch(r"[1-9][0-9]*", suffix):
                return match.group(1) + match.group(2) + suffix
            return match.group(0)
        lines[index] = _NODE_REF.sub(replace, line, count=1)
    return "".join(lines)


def compact_response(response):
    """Copy a response for human compact JSON output."""
    if not isinstance(response, dict):
        return response
    result = dict(response)
    result.pop("id", None)
    result.pop("observation_bytes", None)
    if isinstance(result.get("snapshot"), str):
        result["snapshot"] = format_compact_snapshot(
            result["snapshot"], result.get("document"),
            include_capability=not bool(result.get("document")))
    return result
