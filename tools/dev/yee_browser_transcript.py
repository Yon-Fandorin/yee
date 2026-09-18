"""Opt-in local JSONL transcript. Kimi proposal, safety-reviewed integration."""
import json
import os
import stat

MAX_EVENT_BYTES = 1024 * 1024


class TranscriptError(ValueError):
    pass


class Transcript:
    def __init__(self, path, max_event_bytes=MAX_EVENT_BYTES):
        if type(max_event_bytes) is not int or not 1 <= max_event_bytes <= 32*1024*1024:
            raise TranscriptError('event limit must be 1..32 MiB')
        self._max_event_bytes=max_event_bytes
        self._fd = None
        self._next_sequence = 1
        self._broken = False
        if not os.path.isabs(path):
            raise TranscriptError("path must be absolute")
        parent, name = os.path.split(path)
        if not parent or not name:
            raise TranscriptError("path must name a file")
        pst = os.lstat(parent)
        self._check_parent(pst)
        dirfd = os.open(parent, os.O_RDONLY | os.O_DIRECTORY | getattr(os, "O_NOFOLLOW", 0))
        fd = None
        try:
            live = os.fstat(dirfd)
            self._check_parent(live)
            if (pst.st_dev, pst.st_ino) != (live.st_dev, live.st_ino):
                raise TranscriptError("parent changed while opening")
            flags = os.O_WRONLY | os.O_CREAT | os.O_EXCL | getattr(os, "O_NOFOLLOW", 0)
            fd = os.open(name, flags, 0o600, dir_fd=dirfd)
        except OSError as exc:
            raise TranscriptError("cannot create transcript: %s" % exc) from exc
        finally:
            try:
                os.close(dirfd)
            except BaseException:
                if fd is not None:
                    os.close(fd)
                raise
        self._fd = fd

    @staticmethod
    def _check_parent(info):
        if not stat.S_ISDIR(info.st_mode) or info.st_uid != os.geteuid() or info.st_mode & 0o077:
            raise TranscriptError("parent must be a private, owned, non-symlink directory")

    def write(self, event):
        if self._broken or self._fd is None:
            raise TranscriptError("transcript is closed or unusable")
        try:
            if not isinstance(event, dict) or "sequence" in event:
                raise TranscriptError("event must be a dict without a sequence key")
            record = {"sequence": self._next_sequence, **event}
            data = (json.dumps(record, ensure_ascii=False, allow_nan=False,
                               separators=(",", ":")) + "\n").encode("utf-8")
            if len(data) > self._max_event_bytes:
                raise TranscriptError("event exceeds configured byte limit")
            view = memoryview(data)
            while view:
                try:
                    count = os.write(self._fd, view)
                except InterruptedError:
                    continue
                if count <= 0:
                    raise OSError("write made no progress")
                view = view[count:]
        except (OSError, ValueError, TypeError) as exc:
            self._broken = True
            raise TranscriptError("failed to record event: %s" % exc) from exc
        self._next_sequence += 1

    def close(self):
        if self._fd is not None:
            fd, self._fd = self._fd, None
            os.close(fd)

    @property
    def sequence_written(self):
        """Last completely written event; callers must serialize access."""
        return self._next_sequence - 1

    def __enter__(self):
        return self

    def __exit__(self, exc_type, exc_value, traceback):
        self.close()
        return False
