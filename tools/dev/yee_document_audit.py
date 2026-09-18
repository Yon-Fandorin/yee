"""Offline audit of the opt-in document presentation for direct MCP commands."""
import json
import re

from yee_browser_results import unpack_results
from yee_browser_compact import compact_response, native_ref
from yee_document_handles import DocumentHandles


def verify(calls, native):
    # The caller first verifies sequence coverage and request/response identity.
    # Named commands may expand to several requests; they need their own audit.
    direct = {'status', 'tabs', 'select-tab', 'observe', 'attach', 'detach',
              'cancel', 'read', 'fill', 'click', 'scroll', 'ask', 'wait-change'}
    handles = DocumentHandles()
    prefix_known = False
    checked = 0
    for offset in range(0, len(calls), 2):
        begin, end = calls[offset:offset+2]
        arguments = begin.get('arguments')
        if not isinstance(arguments, dict):raise ValueError('invalid MCP arguments')
        commands = arguments.get('commands')
        start, stop = begin['native_sequence_start'], end['native_sequence_end']
        count = (stop-start)//2
        if (not isinstance(commands, list) or not commands or count != len(commands)
                or len(end['content']) != 1):
            raise ValueError('document audit requires complete direct-command invocations')
        shown = unpack_results(json.loads(end['content'][0]))
        shown = [shown] if count == 1 else shown
        if not isinstance(shown, list) or len(shown) != count:
            raise ValueError('document audit result count mismatch')
        # Adapter expands every document argument before dispatching the batch.
        expanded = []
        for argv in commands:
            if not isinstance(argv, list) or not argv or any(not isinstance(v, str) for v in argv):
                raise ValueError('invalid direct command')
            doc = None
            if argv[0] == '--document':
                if len(argv) < 3:raise ValueError('missing document command')
                doc = handles.expand(argv[1]); argv = argv[2:]
            if argv[0] in ('read','fill','click','scroll','wait-change') and argv[1:2]==['--document']:
                if doc is not None or len(argv)<4:raise ValueError('duplicate or missing document capability')
                doc=handles.expand(argv[2]);argv=[argv[0],*argv[3:]]
            if argv[0] not in direct:raise ValueError('expanded command audit unsupported')
            expanded.append((argv, doc))
        for index, ((argv, doc), visible) in enumerate(zip(expanded, shown)):
            request = native[start+index*2]['request']
            response = native[start+index*2+1]['response']
            if request['command'] != argv[0]:raise ValueError('native command mismatch')
            if argv[0] in ('read', 'fill', 'click'):
                if len(argv) < 2 or request.get('ref') != native_ref(argv[1], doc):
                    raise ValueError('document reference expansion mismatch')
            elif argv[0] in ('scroll', 'wait-change') and request.get('document') != doc:
                raise ValueError('scroll document expansion mismatch')
            expected = compact_response({k: v for k, v in response.items() if k != 'timing'})
            if isinstance(expected.get('document'), str) and expected['document']:
                alias = visible.get('document') if isinstance(visible, dict) else None
                if not isinstance(alias, str) or not re.fullmatch(r'~[0-9a-f]{12}\.[1-9][0-9]*', alias):
                    raise ValueError('invalid presented document handle')
                if not prefix_known:
                    handles.prefix = alias.rsplit('.', 1)[0] + '.'
                    prefix_known = True
            if handles.present(expected) != visible:
                raise ValueError('document presentation changed content or mapping')
            checked += 1
    return {'verified': True, 'direct_commands_checked': checked,
            'scope': 'document expansion and complete compact response preservation'}
