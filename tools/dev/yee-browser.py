#!/usr/bin/env python3
"""Small command line client for the local browser-agent bridge.

The bridge is deliberately a pair of fixed files in a user-created private
directory.  Requests and responses are replaced atomically, so a native peer
never observes a partially written JSON document.
"""

import argparse
import errno
import fcntl
import io
import json
import math
import os
import re
import stat
import sys
import tempfile
import time
import uuid

from product_brand import product_name
from yee_browser_compact import CompactError, compact_response, native_ref
from yee_browser_transcript import Transcript
from yee_browser_wakeup import ResponseWakeup
from yee_browser_named import resolve as resolve_named
from yee_browser_named import parse_batch, parse_ref_batch, resolve_batch, validate_name


MAX_REQUEST = 64 * 1024
MAX_RESPONSE = 256 * 1024
POLL_INTERVAL = 0.1
MAX_SESSION_LINE = 64 * 1024
MAX_SESSION_COMMANDS = 256


class BridgeError(Exception):
    pass


class RecordingError(BridgeError):
    pass


class DocumentCapabilityAction(argparse.Action):
    def __call__(self, parser, namespace, value, option_string=None):
        current = getattr(namespace, self.dest, None)
        if current is not None and current != value:
            parser.error('conflicting --document capabilities')
        setattr(namespace, self.dest, value)


def record_event(args, kind, data):
    transcript = getattr(args, "_transcript", None)
    if transcript is None:
        return
    try:
        transcript.write({"kind": kind, "time_ns": time.time_ns(), **data})
    except (OSError, ValueError, TypeError) as exc:
        effect = "request was not dispatched" if kind == "request" else "command may already have executed"
        raise RecordingError("transcript recording failed; %s: %s" % (effect, exc)) from exc


def validate_bridge(path):
    if not os.path.isabs(path):
        raise BridgeError("bridge directory must be an absolute path")
    try:
        st = os.lstat(path)
    except OSError as exc:
        raise BridgeError("cannot access bridge directory: %s" % exc) from exc
    if stat.S_ISLNK(st.st_mode):
        raise BridgeError("bridge directory must not be a symlink")
    if not stat.S_ISDIR(st.st_mode):
        raise BridgeError("bridge path is not a directory")
    if st.st_uid != os.getuid():
        raise BridgeError("bridge directory is not owned by the current user")
    if st.st_mode & 0o077:
        raise BridgeError("bridge directory must be private (mode 0700 or stricter)")
    return path


def atomic_write(path, data, limit=MAX_REQUEST):
    if len(data) > limit:
        raise BridgeError("data exceeds %d KiB" % (limit // 1024))
    directory = os.path.dirname(path)
    fd, temporary = tempfile.mkstemp(prefix=".yee-", dir=directory)
    try:
        os.fchmod(fd, 0o600)
        with os.fdopen(fd, "wb") as stream:
            stream.write(data)
            stream.flush()
            os.fsync(stream.fileno())
        os.replace(temporary, path)
    except Exception:
        try:
            os.unlink(temporary)
        except OSError:
            pass
        raise


def read_json(path, limit=MAX_RESPONSE):
    try:
        flags = os.O_RDONLY | getattr(os, "O_NOFOLLOW", 0)
        fd = os.open(path, flags)
        with os.fdopen(fd, "rb") as stream:
            raw = stream.read(limit + 1)
        if len(raw) > limit:
            return None
        return json.loads(raw.decode("utf-8"))
    except (OSError, UnicodeDecodeError, json.JSONDecodeError):
        return None


def peer_is_gone(pid):
    """A launcher-supplied PID is a failure hint, never a grant or settlement proof.

    Signal 0 does not signal/terminate a process. PID reuse/permission ambiguity
    is treated conservatively as unknown, leaving the normal timeout in place.
    """
    if pid is None:
        return False
    if type(pid) is not int or pid <= 0:
        raise BridgeError('peer-pid must be a positive integer')
    try:
        os.kill(pid, 0)
    except ProcessLookupError:
        return True
    except PermissionError:
        pass
    return False


def acquire_lock(path, timeout):
    try:
        flags = os.O_RDWR | os.O_CREAT | getattr(os, "O_NOFOLLOW", 0)
        fd = os.open(path, flags, 0o600)
        stream = os.fdopen(fd, "a+")
    except OSError as exc:
        raise BridgeError("cannot open bridge lock: %s" % exc) from exc
    deadline = time.monotonic() + max(0.0, timeout)
    while True:
        try:
            fcntl.flock(stream.fileno(), fcntl.LOCK_EX | fcntl.LOCK_NB)
            return stream
        except OSError as exc:
            if exc.errno not in (errno.EACCES, errno.EAGAIN):
                stream.close()
                raise BridgeError("cannot lock bridge: %s" % exc) from exc
            if time.monotonic() >= deadline:
                stream.close()
                raise BridgeError("timed out waiting for another request")
            time.sleep(POLL_INTERVAL)


def build_parser():
    parser = argparse.ArgumentParser(
        prog="yee-browser.py",
        description=f"Send one command to a running {product_name()} browser agent bridge.",
        epilog=(f"Setup: create a private mode-0700 directory, launch {product_name()} with "
                "--yee-agent-bridge=/absolute/dir, then run: "
                "yee-browser.py --bridge /absolute/dir COMMAND [arguments]."),
    )
    parser.add_argument("--bridge", required=True, metavar="DIR", help="absolute private bridge directory")
    parser.add_argument("--peer-pid", type=int, help="optional exact browser PID supplied by its launcher; fail early if it exits")
    parser.add_argument("--timeout", type=float, default=180.0, metavar="SECONDS", help="request timeout (default: 180)")
    parser.add_argument("--request-timeout", type=float, default=120.0, metavar="SECONDS",
                        help="native request expiry, 1..120 seconds (default: 120)")
    parser.add_argument("--text", action="store_true", help="print a returned snapshot; with --compact retain a JSON metadata header")
    parser.add_argument("--compact", action="store_true", help="use compact human snapshot/ref presentation")
    parser.add_argument("--document", metavar="DOCUMENT_UUID", action=DocumentCapabilityAction,
                        help="explicit document capability for compact numeric refs")
    parser.add_argument("--record", metavar="NEW_ABSOLUTE_FILE",
                        help="opt-in private native request/response JSONL; may contain page text")
    sub = parser.add_subparsers(dest="command", required=True)
    attach = sub.add_parser("attach", help="bind explicitly to the currently active tab")
    attach.add_argument('--permissions', help='JSON fill/click/navigate rules: allow, ask or deny; native task approval required')
    observe = sub.add_parser("observe", help="read the bound tab")
    observe.add_argument("--full", action="store_true", help="request a full snapshot")
    wait = sub.add_parser('wait-change', help='wait for observable changes; requires --document and acknowledged baseline')
    wait.add_argument('wait_ms', type=int, help='maximum wait in milliseconds, 1..30000')
    wait.add_argument('--content', action='store_true', help='ignore only ref replacement while waiting; return full fresh refs')
    scroll = sub.add_parser("scroll", help="scroll the active bound document then observe; requires --document")
    scroll.add_argument("direction", choices=("up", "down"))
    scroll.add_argument("pages", nargs="?", type=int, choices=(1, 2, 3), default=1)
    for observation_parser in (wait, scroll):
        observation_parser.add_argument('--full', action='store_true',
                                       help='return a full current viewport observation')
    read = sub.add_parser("read", help="read a scoped field")
    read.add_argument("ref")
    click = sub.add_parser("click", help="click an element reference")
    click.add_argument("ref")
    fill = sub.add_parser("fill", help="fill an element reference")
    fill.add_argument("ref")
    fill.add_argument("value")
    fill_named = sub.add_parser("fill-named", help="observe then fill one exact visible field name; asks approval")
    fill_named.add_argument("name")
    fill_named.add_argument("value")
    click_named = sub.add_parser("click-named", help="observe then click one exact visible button or checkbox name; asks approval")
    click_named.add_argument("name")
    batch_named = sub.add_parser(
        'batch-named', help='one scoped approval for ordered named fills/checks and final click',
        description='Approve 1..8 ordered actions on the currently visible form. '
        'Use JSON action arrays: ["fill", "exact field name", "value"] or '
        '["check", "exact checkbox name", true/false] or ["click", "exact button name"]. Only a button click may be last. '
        'Observe again before acting on a new step. Stops on error; no rollback.',
        epilog='Example: batch-named \'[["fill","Name","Cedar"],'
        '["click","Save locally"]]\'. Typed action objects belong to the MCP '
        'batch input, not this CLI argument.')
    batch_named.add_argument('actions_json', help='JSON array of action arrays (shell-quote the complete JSON)')
    batch_ref = sub.add_parser('batch-ref', help='ordered reference fills/checks and final button click in one document')
    batch_ref.add_argument('actions_json', help='JSON objects with command/ref and value (fill) or checked boolean (check); requires --document')
    for action_parser in (fill, click, fill_named, click_named, batch_named, batch_ref):
        action_parser.add_argument('--full', action='store_true',
                                   help='return a full observation with this action, useful for final verification')
    # Keep action-local spelling intuitive without silently overriding the
    # global capability. Subparser namespaces are merged after parsing, so use
    # a separate destination and reconcile before opening logs or dispatching.
    for action_parser in (read, click, fill, batch_ref):
        action_parser.add_argument("--document", dest="action_document",
                                   action=DocumentCapabilityAction, metavar="DOCUMENT_UUID",
                                   help="explicit document capability (also accepted before command)")
    navigate = sub.add_parser("navigate", help="navigate the bound tab")
    navigate.add_argument("url")
    ask = sub.add_parser("ask", help=f"ask the user through a native {product_name()} dialog")
    ask.add_argument("question")
    sub.add_parser("recover", help="retrieve an unresolved request result without redispatch")
    sub.add_parser("status", help="get agent status")
    sub.add_parser("tabs", help="list only previously approved tabs and cached consent metadata")
    select_tab = sub.add_parser("select-tab", help="select an approved unchanged tab and return a fresh observation")
    select_tab.add_argument("tab")
    sub.add_parser("cancel", help=f"release the idle binding; cancel pending work in the {product_name()} dialog")
    cancel_wait = sub.add_parser('cancel-wait', help='interrupt one exact pending wait; preserve its settlement fence')
    cancel_wait.add_argument('request_id', help='exact pending wait request ID; requires --document')
    sub.add_parser("detach", help="detach from the bound tab")
    session = sub.add_parser("session", help="run bounded JSONL commands from stdin or a private file")
    session.add_argument("--input", metavar="PRIVATE_JSONL_FILE",
                         help="absolute private regular command file, at most 64 KiB")
    return parser


def tab_capability(value):
    try:
        if len(value) != 36 or str(uuid.UUID(value)) != value:
            raise ValueError()
    except (ValueError, TypeError, AttributeError):
        raise BridgeError("select-tab requires the exact tab UUID returned by attach or tabs")
    return value


def permission_rules(encoded):
    try:
        rules = json.loads(encoded)
    except (ValueError, TypeError) as exc:
        raise BridgeError('invalid permission JSON') from exc
    if (not isinstance(rules, dict) or any(k not in ('fill', 'click', 'navigate') or
            v not in ('allow', 'ask', 'deny') for k, v in rules.items())):
        raise BridgeError('permissions require fill/click/navigate: allow/ask/deny')
    return rules


def request_for(args, baseline=None):
    request = {"id": str(uuid.uuid4()), "command": args.command,
               "timeout_ms": int(args.request_timeout * 1000),
               "expires_unix_ms": (time.time() + min(args.timeout, args.request_timeout)) * 1000}
    if args.command == "attach" and getattr(args, 'permissions', None) is not None:
        request['permissions'] = permission_rules(args.permissions)
    elif args.command == "select-tab":
        request['tab'] = tab_capability(args.tab)
    elif args.command == "observe":
        request["full"] = bool(args.full)
    elif args.command == 'wait-change':
        if (type(args.wait_ms) is not int or not 1 <= args.wait_ms <= 30000):
            raise BridgeError('wait-change expects 1..30000 milliseconds')
        if not args.document or not baseline or baseline['document'] != args.document:
            raise BridgeError('wait-change requires the acknowledged --document')
        request.update(document=args.document, wait_ms=args.wait_ms)
        if getattr(args,'content',False):request['wait_mode']='content'
    elif args.command == "scroll":
        if not args.document:
            raise BridgeError("scroll requires --document DOCUMENT_UUID")
        if baseline and baseline.get("document") != args.document:
            raise BridgeError("--document does not match the acknowledged client state")
        request.update(document=args.document, direction=args.direction, pages=args.pages)
    elif args.command in ("click", "read"):
        request["ref"] = command_ref(args, baseline)
    elif args.command == "fill":
        request.update(ref=command_ref(args, baseline), value=args.value)
    elif args.command == "navigate":
        request["url"] = args.url
    elif args.command == "ask":
        request["question"] = args.question
    elif args.command == 'batch':
        request['actions'] = args.actions
    elif args.command == 'batch-ref':
        if not args.document or not baseline or baseline.get('document') != args.document:
            raise BridgeError('reference batch requires the acknowledged --document')
        try:
            actions = parse_ref_batch(args.actions_json)
        except ValueError as exc:
            raise BridgeError(str(exc)) from exc
        resolved = []
        for item in actions:
            target = argparse.Namespace(**vars(args))
            target.ref = item['ref']
            ref = command_ref(target, baseline)
            if not re.fullmatch(re.escape(args.document) + r'_[1-9][0-9]*', ref):
                raise BridgeError('batch reference does not belong to the acknowledged document')
            resolved.append({**item, 'ref': ref})
        if len({item['ref'] for item in resolved}) != len(resolved):
            raise BridgeError('batch target references must be distinct')
        request.update(command='batch', actions=resolved)
    if args.command in ('fill', 'click', 'batch', 'batch-ref', 'scroll', 'wait-change') and getattr(args, 'full', False):
        request['full'] = True
    return request


def command_ref(args, baseline):
    if args.document and baseline and baseline["document"] != args.document:
        raise BridgeError("--document does not match the acknowledged client state")
    if not args.compact:
        return args.ref
    if not baseline:
        raise BridgeError("compact action/read requires an acknowledged client state")
    if not args.document:
        raise BridgeError("compact action/read requires --document DOCUMENT_UUID")
    if baseline["document"] != args.document:
        raise BridgeError("--document does not match the acknowledged client state")
    try:
        return native_ref(args.ref, args.document)
    except CompactError as exc:
        raise BridgeError(str(exc)) from exc


def load_state(bridge):
    state = read_json(os.path.join(bridge, "client-state.json"), MAX_RESPONSE)
    if not isinstance(state, dict):
        return {}
    document = state.get("document")
    revision = state.get("revision")
    if isinstance(document, str) and isinstance(revision, int) and not isinstance(revision, bool):
        return {"document": document, "revision": revision}
    return {}


def update_state(bridge, response):
    if (response.get("ok") is not True or not isinstance(response.get("document"), str)
            or not isinstance(response.get("truncated"), bool)):
        return
    revision = response.get("revision")
    if not isinstance(revision, int) or isinstance(revision, bool):
        return
    state = {} if response.get("truncated") is True else {
        "document": response["document"], "revision": revision
    }
    payload = json.dumps(state, ensure_ascii=False, separators=(",", ":")).encode("utf-8")
    atomic_write(os.path.join(bridge, "client-state.json"), payload, MAX_RESPONSE)


def emit(result, text_mode, compact=False):
    # Timing remains in the opt-in private transcript, never in model context.
    if isinstance(result, dict) and "timing" in result:
        result = {key: value for key, value in result.items() if key != "timing"}
    if text_mode and isinstance(result, dict) and result.get("snapshot") is not None:
        if compact:
            shown = compact_response(result)
            metadata = {key: value for key, value in shown.items() if key != "snapshot"}
            sys.stdout.write(json.dumps(metadata, ensure_ascii=False, separators=(",", ":")) + "\n")
            sys.stdout.write(str(shown["snapshot"]) + "\n")
        else:
            sys.stdout.write(str(result["snapshot"]) + "\n")
    else:
        shown = compact_response(result) if compact else result
        sys.stdout.write(json.dumps(shown, ensure_ascii=False, separators=(",", ":")) + "\n")


def cancel_wait(args, bridge, emit_output):
    pending_path = os.path.join(bridge, 'client-pending.json')
    pending = read_json(pending_path)
    if (not args.document or not isinstance(pending, dict)
            or pending.get('command') != 'wait-change'
            or pending.get('id') != args.request_id
            or pending.get('document') != args.document):
        raise BridgeError('cancel-wait requires the exact pending wait ID and document')
    control = {'id': args.request_id, 'document': args.document}
    record_event(args, 'cancel_request', {'control': control})
    atomic_write(os.path.join(bridge, 'cancel-wait-' + args.request_id.encode().hex().upper() + '.json'),
                 json.dumps(control).encode())
    deadline = time.monotonic() + args.timeout
    while True:
        # The normal waiter may consume the mailbox and another client may
        # advance it. The immutable original archive is authoritative too.
        response = read_json(os.path.join(bridge, 'native-request-' +
                             args.request_id.encode().hex().upper() + '.result'))
        if not isinstance(response, dict) or response.get('id') != args.request_id:
            response = read_json(os.path.join(bridge, 'response.json'))
        if (isinstance(response, dict) and response.get('id') == args.request_id
                and response.get('execution_settled') is True
                and type(response.get('ok')) is bool):
            record_event(args, 'cancel_response', {'response': response})
            code = 0 if response.get('error') == 'wait_cancelled' else 1
            # Only the original owner/recover clears client-pending.json.
            # Returning the original response avoids inventing a second native
            # request or claiming a cancellation that lost a completion race.
            if emit_output:
                emit(response, args.text, args.compact)
                return code
            return code, response
        if time.monotonic() >= deadline:
            raise BridgeError('wait cancellation outcome unknown; original pending fence retained')
        time.sleep(min(POLL_INTERVAL, max(0, deadline-time.monotonic())))


def run(args, emit_output=True, _held_lock=None, _before_named_action=None):
    bridge = validate_bridge(args.bridge)
    if not math.isfinite(args.timeout) or args.timeout < 0:
        raise BridgeError("timeout must be a finite non-negative number")
    if (not math.isfinite(args.request_timeout)
            or not 1 <= args.request_timeout <= 120):
        raise BridgeError("request-timeout must be finite and between 1 and 120 seconds")
    if args.command == 'cancel-wait':
        return cancel_wait(args, bridge, emit_output)
    lock = _held_lock or acquire_lock(os.path.join(bridge, "request.lock"), args.timeout)
    wakeup = None
    try:
        pending_path = os.path.join(bridge, "client-pending.json")
        pending = read_json(pending_path)
        if not os.path.lexists(pending_path):
            # Migrate an observable pre-journal mailbox conservatively. Native
            # may consume it at any point; never delete or replace that request.
            mailbox_path = os.path.join(bridge, "request.json")
            if os.path.lexists(mailbox_path):
                orphan = read_json(mailbox_path, MAX_REQUEST)
                previous = read_json(os.path.join(bridge, "response.json"))
                if not isinstance(orphan, dict) or not isinstance(orphan.get("id"), str):
                    raise BridgeError("unreadable pre-journal mailbox; refusing dispatch")
                acknowledged = (isinstance(previous, dict) and previous.get("id") == orphan["id"]
                                and previous.get("ok") is True)
                if not acknowledged:
                    atomic_write(pending_path, json.dumps(orphan, ensure_ascii=False,
                                                         separators=(",", ":")).encode("utf-8"))
                    pending = orphan
        if os.path.lexists(pending_path):
            if (not isinstance(pending, dict) or not isinstance(pending.get("id"), str)
                    or not 1 <= len(pending["id"].encode("utf-8")) <= 64):
                raise BridgeError("invalid pending request journal; refusing dispatch")
            if args.command != "recover":
                raise BridgeError("unresolved request %s; use recover, never retry automatically" % pending["id"])
        elif args.command == "recover":
            reference = getattr(args, '_recover_reference', None)
            if (not isinstance(reference, dict) or set(reference) != {'id', 'command'}
                    or not isinstance(reference.get('id'), str)
                    or not 1 <= len(reference['id'].encode('utf-8')) <= 64
                    or not isinstance(reference.get('command'), str)):
                raise BridgeError("no unresolved request")
            archive = os.path.join(bridge, 'native-request-' +
                                   reference['id'].encode('utf-8').hex().upper() + '.result')
            archived = read_json(archive)
            if (not isinstance(archived, dict) or archived.get('id') != reference['id']
                    or type(archived.get('ok')) is not bool
                    or archived.get('execution_settled') is not True
                    or archived.get('receipt_persisted') is not True):
                raise BridgeError('no settled persisted receipt for the retained MCP request')
            # Adopt only an already persisted result, never publish its action.
            pending = dict(reference)
            atomic_write(pending_path, json.dumps(pending).encode('utf-8'))
        if args.command in ('fill-named', 'click-named', 'batch-named'):
            if args.document:
                raise BridgeError('named actions resolve their own fresh document; omit --document')
            try:
                batch = parse_batch(args.actions_json) if args.command == 'batch-named' else None
                if batch is None:
                    validate_name(args.name)
            except ValueError as exc:
                raise BridgeError(str(exc)) from exc
            observed_args = argparse.Namespace(**vars(args))
            observed_args.command, observed_args.full = 'observe', True
            code, observed = run(observed_args, False, lock)
            if code:
                result = observed
            else:
                try:
                    if batch is not None:
                        document, resolved = resolve_batch(observed, batch)
                    else:
                        document, ref = resolve_named(observed, 'field' if args.command == 'fill-named' else 'click', args.name)
                except ValueError as exc:
                    raise BridgeError(str(exc)) from exc
                action = argparse.Namespace(**vars(args))
                if batch is not None:
                    action.command, action.actions, action.document = 'batch', resolved, document
                else:
                    action.command = 'fill' if args.command == 'fill-named' else 'click'
                    action.document, action.ref = document, ref
                if _before_named_action is not None:
                    _before_named_action()
                code, result = run(action, False, lock)
            if emit_output:
                emit(result, args.text, args.compact)
                return code
            return code, result
        if args.command != 'recover' and peer_is_gone(getattr(args, 'peer_pid', None)):
            raise BridgeError('browser_disconnected before dispatch; no request published')
        wakeup = ResponseWakeup(bridge)
        baseline = load_state(bridge)
        request = pending if args.command == "recover" else request_for(args, baseline)
        if (args.command == 'observe' and hasattr(args, '_expected_observation_baseline')
                and baseline != args._expected_observation_baseline):
            # Another client may have acknowledged a different snapshot while
            # this caller waited for the lock. Read it fully in this SAME request.
            request['full'] = True
        if getattr(args, "_transcript", None) is not None:
            request["record_timing"] = True
        request_id = request["id"]
        request_path = os.path.join(bridge, "request.json")
        response_path = os.path.join(bridge, "response.json")
        if not (args.command == "observe" and request.get('full') is True):
            if baseline:
                request.update(baseline_document=baseline["document"], baseline_revision=baseline["revision"])
        payload = json.dumps(request, ensure_ascii=False, separators=(",", ":")).encode("utf-8")
        if args.command != "recover":
            record_event(args, "request", {"request": request})
            # Journal before publication: process death or publication failure
            # must never allow another client to overwrite an uncertain action.
            atomic_write(pending_path, payload)
            atomic_write(request_path, payload)
            dispatched = getattr(args, '_native_request_started', None)
            if dispatched:
                dispatched(request)
        else:
            record_event(args, "recovery", {"request_id": request_id})
        deadline = time.monotonic() + args.timeout
        while True:
            response = read_json(response_path)
            response_source = "mailbox"
            if args.command == "recover":
                archive_path = os.path.join(
                    bridge, "native-request-" + request_id.encode("utf-8").hex().upper() + ".result")
                archived = read_json(archive_path)
                if (isinstance(archived, dict) and archived.get("id") == request_id
                        and isinstance(archived.get("ok"), bool)
                        and isinstance(archived.get("execution_settled"), bool)):
                    response, response_source = archived, "native_archive"
            if (isinstance(response, dict) and response.get("id") == request_id
                    and isinstance(response.get("ok"), bool)):
                record_event(args, "response", {"response": response,
                                                "response_source": response_source})
                # Legacy native expiry does not establish renderer quiescence.
                # Preserve the fence until that protocol can prove settlement.
                if response.get("receipt_persisted") is False:
                    raise BridgeError("request %s completion receipt unavailable; pending fence retained; recover the SAME request" % request_id)
                if (response.get("execution_settled") is False or
                        (response.get("error") in ("request_expired", "renderer_unavailable")
                         and response.get("execution_settled") is not True)):
                    raise BridgeError("request %s has uncertain execution outcome (%s); pending fence retained" %
                                      (request_id, response.get("error", "unsettled")))
                # A disconnected/cancelled MCP caller must still be able to
                # recover this exact settled receipt rather than retry it.
                before_ack = getattr(args, '_before_native_response_ack', None)
                if before_ack:
                    before_ack()
                update_state(bridge, response)
                os.unlink(pending_path)
                if (args.command == 'wait-change' and getattr(args,'content',False)
                        and response.get('ok') is True
                        and (not isinstance(response.get('wait'),dict)
                             or response['wait'].get('comparison') != 'content')):
                    raise BridgeError('native did not confirm content wait mode; result retained in transcript')
                if emit_output:
                    emit(response, args.text, args.compact)
                    return 0 if response.get("ok") is True else 1
                return (0 if response.get("ok") is True else 1), response
            if peer_is_gone(getattr(args, 'peer_pid', None)):
                raise BridgeError("browser_disconnected; request %s outcome unknown; pending fence retained; recover the SAME request, never replay" % request_id)
            if time.monotonic() >= deadline:
                raise BridgeError("request %s timed out; outcome unknown; use recover without retrying; cancel approval in %s UI if still pending" % (request_id, product_name()))
            wakeup.wait(min(POLL_INTERVAL, deadline - time.monotonic()))
    except BridgeError as exc:
        if not isinstance(exc, RecordingError) and not getattr(exc, "_recorded", False):
            record_event(args, "client_error", {"error": str(exc)})
            # Named operations recurse through observe/action under one lock.
            # Propagating one failure must not count as two client failures.
            exc._recorded = True
        raise
    finally:
        if wakeup is not None:
            wakeup.close()
        if _held_lock is None:
            fcntl.flock(lock.fileno(), fcntl.LOCK_UN)
            lock.close()


def session_command(argv, config):
    if not isinstance(argv, list) or not all(isinstance(value, str) for value in argv):
        raise BridgeError("session command must be a JSON array of strings")
    if not argv:
        raise BridgeError("session command must not be empty")
    if argv[0] in ("session", "--help", "-h"):
        raise BridgeError("session command cannot be nested or request help")

    # Global configuration may not be changed inside a session.  Document is
    # the sole per-command capability. Accept its two unambiguous option
    # positions only; never search inside literal names/values.
    document = None
    command_argv = list(argv)
    if command_argv[:1] == ["--document"]:
        if len(command_argv) < 3:
            raise BridgeError("--document requires DOCUMENT_UUID")
        document = command_argv[1]
        command_argv = command_argv[2:]
    scoped_commands = {'read', 'fill', 'click', 'scroll', 'wait-change', 'batch-ref'}
    if command_argv[0] in scoped_commands and command_argv[1:2] == ['--document']:
        if document is not None or len(command_argv) < 4:
            raise BridgeError('one --document DOCUMENT_UUID is required before or immediately after the command')
        document = command_argv[2]
        command_argv = [command_argv[0], *command_argv[3:]]
    forbidden = {"--bridge", "--timeout", "--request-timeout", "--compact", "--text", "--record"}
    if any(value in forbidden for value in command_argv[:1]):
        raise BridgeError("session command cannot override bridge, timeout, or output configuration")
    # Preserve option-looking positional data (for example a fill value of
    # "--bridge") exactly as supplied.
    command = command_argv[0]
    action_arity = {'fill': 3, 'click': 2, 'fill-named': 3, 'click-named': 2, 'batch-named': 2, 'batch-ref': 2}
    full_action = (command in action_arity and
                   len(command_argv) == action_arity[command] + 1 and command_argv[-1] == '--full')
    if full_action:
        command_argv = command_argv[:-1]
    full_observation = (command in ('scroll', 'wait-change') and
                        3 <= len(command_argv) <= 4 and command_argv[-1] == '--full')
    if full_observation:
        command_argv = command_argv[:-1]
    if command == 'attach' and len(command_argv) == 3 and command_argv[1] == '--permissions':
        permission_rules(command_argv[2])
        args = argparse.Namespace(command=command, permissions=command_argv[2])
    elif command == "select-tab" and len(command_argv) == 2:
        args = argparse.Namespace(command=command, tab=tab_capability(command_argv[1]))
    elif command == 'wait-change' and (len(command_argv) == 2 or
            (len(command_argv) == 3 and command_argv[2] == '--content')):
        value = command_argv[1]
        if not value.isascii() or not value.isdecimal() or len(value) > 5 or not 1 <= int(value) <= 30000:
            raise BridgeError('wait-change expects 1..30000 milliseconds')
        args = argparse.Namespace(command=command, wait_ms=int(value),content=len(command_argv)==3)
    elif command == "scroll" and len(command_argv) in (2, 3):
        pages = command_argv[2] if len(command_argv) == 3 else "1"
        if command_argv[1] not in ("up", "down") or pages not in ("1", "2", "3"):
            raise BridgeError("scroll expects up|down and 1..3 pages")
        args = argparse.Namespace(command=command, direction=command_argv[1], pages=int(pages))
    elif command == 'batch-named' and len(command_argv) == 2:
        try:
            parse_batch(command_argv[1])
        except ValueError as exc:
            raise BridgeError(str(exc)) from exc
        args = argparse.Namespace(command=command, actions_json=command_argv[1])
    elif command == 'batch-ref' and len(command_argv) == 2:
        if not document:
            raise BridgeError('reference batch requires --document')
        try:
            parse_ref_batch(command_argv[1])
        except ValueError as exc:
            raise BridgeError(str(exc)) from exc
        args = argparse.Namespace(command=command, actions_json=command_argv[1])
    elif command == 'fill-named' and len(command_argv) == 3:
        args = argparse.Namespace(command=command, name=command_argv[1], value=command_argv[2])
    elif command == 'click-named' and len(command_argv) == 2:
        args = argparse.Namespace(command=command, name=command_argv[1])
    elif len(command_argv) == 3 and command == "fill":
        args = argparse.Namespace(command="fill", ref=command_argv[1], value=command_argv[2])
    elif len(command_argv) == 2 and command in ("read", "click", "navigate", "ask"):
        args = argparse.Namespace(command=command)
        if command in ("read", "click"):
            args.ref = command_argv[1]
        elif command == "navigate":
            args.url = command_argv[1]
        else:
            args.question = command_argv[1]
    elif command in ("attach", "status", "tabs", "cancel", "detach", "recover") and len(command_argv) == 1:
        args = argparse.Namespace(command=command)
    elif command == "observe" and (len(command_argv) == 1 or command_argv[1:] == ["--full"]):
        args = argparse.Namespace(command=command, full=len(command_argv) == 2)
    else:
        raise BridgeError("invalid session command")
    if command in action_arity:
        args.full = full_action
    if command in ('scroll', 'wait-change'):
        args.full = full_observation
    args.bridge = config.bridge
    args.peer_pid = getattr(config, "peer_pid", None)
    args.timeout = config.timeout
    args.request_timeout = config.request_timeout
    args.compact = config.compact
    args.text = False
    args.document = document
    args._transcript = getattr(config, "_transcript", None)
    args._native_request_started = getattr(config, "_native_request_started", None)
    args._before_native_response_ack = getattr(config, "_before_native_response_ack", None)
    return args


def load_command_input(path):
    if not os.path.isabs(path):
        raise BridgeError("session input must be an absolute path")
    validate_bridge(os.path.dirname(path))
    fd = None
    try:
        fd = os.open(path, os.O_RDONLY | os.O_NOFOLLOW | os.O_NONBLOCK)
        info = os.fstat(fd)
        if not stat.S_ISREG(info.st_mode) or info.st_uid != os.getuid() or info.st_mode & 0o077:
            raise BridgeError("session input must be an owned private regular file")
        if info.st_size > MAX_SESSION_LINE:
            raise BridgeError("session input file exceeds 64 KiB")
        with os.fdopen(fd, "rb") as stream:
            fd = None
            data = stream.read(MAX_SESSION_LINE + 1)
        if len(data) > MAX_SESSION_LINE:
            raise BridgeError("session input file exceeds 64 KiB")
        # File plans are bounded in total size. Editor-added blank lines are
        # formatting, not commands; streaming stdin retains strict JSONL rules.
        return io.BytesIO(b''.join(line for line in data.splitlines(keepends=True)
                                  if line.strip()))
    except OSError as exc:
        raise BridgeError("cannot open session input: %s" % exc) from exc
    finally:
        if fd is not None:
            os.close(fd)


def preflight_command_input(stream, args):
    """Reject a known malformed file plan before its first side effect.

    This checks syntax only, not future DOM state or approval. Runtime failures
    still stop execution and do not roll back earlier actions.
    """
    for count, line in enumerate(stream, 1):
        if count > MAX_SESSION_COMMANDS:
            raise BridgeError("session command limit exceeded")
        try:
            session_command(json.loads(line.decode('utf-8')), args)
        except (UnicodeDecodeError, json.JSONDecodeError) as exc:
            raise BridgeError("invalid session input command %d: %s" % (count, exc)) from exc
    stream.seek(0)
    return stream


def run_session(args, input_stream=None):
    input_stream = sys.stdin.buffer if input_stream is None else input_stream
    count = 0
    while True:
        raw_line = input_stream.readline(MAX_SESSION_LINE + 1)
        if not raw_line:
            return 0
        count += 1
        if count > MAX_SESSION_COMMANDS:
            result = {"ok": False, "error": "session command limit exceeded"}
            sys.stdout.write(json.dumps(result, ensure_ascii=True, separators=(",", ":")) + "\n")
            sys.stdout.flush()
            return 2
        if len(raw_line) > MAX_SESSION_LINE:
            result = {"ok": False, "error": "session input line exceeds 64 KiB"}
            sys.stdout.write(json.dumps(result, ensure_ascii=True, separators=(",", ":")) + "\n")
            sys.stdout.flush()
            return 2
        try:
            command = json.loads(raw_line.decode("utf-8"))
            command_args = session_command(command, args)
            code, response = run(command_args, emit_output=False)
            emit(response, False, args.compact)
            sys.stdout.flush()
            if code != 0:
                return code
        except (BridgeError, UnicodeDecodeError, UnicodeEncodeError, json.JSONDecodeError) as exc:
            result = {"ok": False, "error": str(exc)}
            sys.stdout.write(json.dumps(result, ensure_ascii=True, separators=(",", ":")) + "\n")
            sys.stdout.flush()
            return 2


def main(argv=None):
    parser = build_parser()
    args = parser.parse_args(argv)
    action_document = getattr(args, 'action_document', None)
    if action_document is not None:
        if args.document is not None and args.document != action_document:
            parser.error('conflicting --document capabilities')
        args.document = action_document
    transcript = None
    try:
        if args.record:
            validate_bridge(args.bridge)
            try:
                transcript = Transcript(args.record)
            except (OSError, ValueError) as exc:
                raise BridgeError("cannot create private transcript: %s" % exc) from exc
        args._transcript = transcript
        if args.command == "session":
            if args.text:
                raise BridgeError("--text is not available in session mode")
            stream = (preflight_command_input(load_command_input(args.input), args)
                      if args.input else None)
            return run_session(args, stream)
        return run(args)
    except BridgeError as exc:
        result = {"ok": False, "error": str(exc)}
        sys.stderr.write(json.dumps(result, ensure_ascii=False, separators=(",", ":")) + "\n")
        return 2
    finally:
        if transcript is not None:
            transcript.close()


if __name__ == "__main__":
    raise SystemExit(main())
