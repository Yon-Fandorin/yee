"""Caller-side recording for independent tasks on a single live MCP connection.

Events are written directly to their task stream and an append-only global
index. No recorded event is reindexed after execution. Pins keep late native
receipts with the originating task even if the control pointer changes.
"""
from contextlib import contextmanager
from contextvars import ContextVar
import json
import os
from pathlib import Path
import stat
import threading

from yee_browser_transcript import Transcript


def private_directory(path):
    path = Path(path)
    info = path.lstat()
    if (not path.is_absolute() or path.resolve() != path or not stat.S_ISDIR(info.st_mode)
            or info.st_uid != os.getuid() or info.st_mode & 0o077):
        raise ValueError('private owned non-symlink recording directory required')
    return path


class Routing:
    def __init__(self, root, browser):
        self.root = private_directory(root)
        if browser not in ('yee', 'aside'):
            raise ValueError('unsupported browser')
        self.browser = browser
        self.pinned = ContextVar('recording_task', default=None)

    def selected(self):
        pinned = self.pinned.get()
        if pinned is not None:
            return pinned
        path = self.root / 'current.json'
        info = path.lstat()
        if (not stat.S_ISREG(info.st_mode) or info.st_uid != os.getuid()
                or info.st_mode & 0o077):
            raise ValueError('private regular task pointer required')
        current = json.loads(path.read_text())
        case = Path(current['case'])
        if current['browser'] != self.browser or case.parent != self.root / self.browser:
            raise ValueError('task pointer escaped browser/cohort scope')
        private_directory(case)
        private_directory(case / 'model')
        return case

    @contextmanager
    def pin(self):
        token = self.pinned.set(self.selected())
        try:
            yield
        finally:
            self.pinned.reset(token)


class RoutedTranscript:
    def __init__(self, routing, filename, global_path, max_event_bytes=1024*1024):
        if Path(filename).name != filename:
            raise ValueError('task transcript basename required')
        self.routing = routing
        self.filename = filename
        self.limit = max_event_bytes
        self.global_stream = Transcript(str(global_path), max_event_bytes=24*1024*1024)
        self.streams = {}
        self.lock = threading.Lock()
        self.closed = False

    def _stream(self):
        if self.closed:
            raise ValueError('routed transcript closed')
        case = self.routing.selected()
        if case not in self.streams:
            self.streams[case] = Transcript(str(case / 'model' / self.filename), self.limit)
        return case, self.streams[case]

    @property
    def sequence_written(self):
        with self.lock:
            return self._stream()[1].sequence_written

    def write(self, event):
        with self.lock:
            case, stream = self._stream()
            stream.write(event)
            self.global_stream.write({'case': str(case), 'task_stream': self.filename,
                                      'task_sequence': stream.sequence_written, 'event': event})

    def close(self):
        with self.lock:
            if self.closed:
                return
            self.closed = True
            for stream in self.streams.values():
                stream.close()
            self.global_stream.close()

    def __enter__(self):
        return self

    def __exit__(self, *unused):
        self.close()
