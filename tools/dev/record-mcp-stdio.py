#!/usr/bin/env python3
"""Byte-preserving stdio MCP recorder; no protocol/tool filtering or retries.

Records both directions, including notifications and server-to-client requests.
This is transport recording, not a browser/network/filesystem access sandbox.
Caller owns the outer process group (including upstream descendants). Receipt
and forwarding are separate events; forwarding is not browser execution proof.
"""
import argparse
import base64
import queue
import subprocess
import sys
import threading
import time
import uuid

from yee_browser_transcript import Transcript

# A 10 MiB MCP image expands once on the wire and again in the raw-frame journal.
MAX_LINE = 16 * 1024 * 1024


def relay(command, transcript, errors, timeout):
    process = subprocess.Popen(command, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                               stderr=errors)
    outcomes = queue.Queue()
    lock = threading.Lock()
    broken = threading.Event()

    def record(event):
        with lock:
            if broken.is_set():
                raise RuntimeError('transport recording is unusable')
            try:
                transcript.write(event)
            except BaseException:
                broken.set()
                raise

    def pump(source, target, direction):
        try:
            while not broken.is_set():
                raw = source.readline(MAX_LINE + 1)
                if not raw:
                    if direction == 'to_server': target.close()
                    outcomes.put((direction, 'eof'))
                    return
                if len(raw) > MAX_LINE or not raw.endswith(b'\n'):
                    raise ValueError('oversized or incomplete MCP frame')
                frame = str(uuid.uuid4())
                record({'kind': 'wire_received', 'frame': frame, 'direction': direction,
                        'monotonic_ns': time.monotonic_ns(), 'time_ns': time.time_ns(),
                        'base64': base64.b64encode(raw).decode('ascii')})
                target.write(raw)
                target.flush()
                record({'kind': 'wire_forwarded', 'frame': frame, 'direction': direction,
                        'monotonic_ns': time.monotonic_ns(), 'time_ns': time.time_ns()})
        except BaseException as exc:
            broken.set()
            outcomes.put((direction, type(exc).__name__))

    workers = [threading.Thread(target=pump, daemon=True, args=args) for args in (
        (sys.stdin.buffer, process.stdin, 'to_server'),
        (process.stdout, sys.stdout.buffer, 'to_client'))]
    for worker in workers: worker.start()
    deadline = time.monotonic() + timeout
    output_finished = False
    try:
        while time.monotonic() < deadline:
            try:
                direction, outcome = outcomes.get(timeout=.1)
                if outcome != 'eof': return 2
                if direction == 'to_client': output_finished = True
            except queue.Empty:
                pass
            if broken.is_set(): return 2
            if output_finished and process.poll() is not None:
                return process.returncode
        return 124
    finally:
        broken.set()
        if process.poll() is None:
            process.terminate()
            try: process.wait(timeout=2)
            except subprocess.TimeoutExpired:
                process.kill(); process.wait(timeout=2)
        # Input may be blocked until the upstream client closes stdin. These
        # daemon pumps are contained in this dedicated relay process, not reused.
        workers[1].join(timeout=2)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--record', required=True)
    parser.add_argument('--stderr', required=True)
    parser.add_argument('--timeout', type=float, default=180)
    parser.add_argument('command', nargs=argparse.REMAINDER)
    args = parser.parse_args()
    command = args.command[1:] if args.command[:1] == ['--'] else args.command
    if not command or not 0 < args.timeout <= 3600:
        parser.error('upstream command and timeout in (0,3600] required')
    import os
    with Transcript(args.record, max_event_bytes=24*1024*1024) as transcript:
        fd = os.open(args.stderr, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
        with os.fdopen(fd, 'wb') as errors:
            raise SystemExit(relay(command, transcript, errors, args.timeout))
