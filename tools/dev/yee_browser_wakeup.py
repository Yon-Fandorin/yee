"""Best-effort directory notification; callers must still validate responses.

Watch the directory, not response.json: the bridge atomically replaces files.
Registration precedes dispatch. A bounded heartbeat survives missed events and
unsupported platforms; a notification never grants trust in a response.
"""
import os
import select
import time


class ResponseWakeup:
    def __init__(self, directory):
        self.queue = None
        self.fd = None
        try:
            if not hasattr(select, 'kqueue'):
                return
            self.fd = os.open(directory, os.O_RDONLY | os.O_NOFOLLOW | os.O_DIRECTORY)
            self.queue = select.kqueue()
            event = select.kevent(self.fd, filter=select.KQ_FILTER_VNODE,
                                 flags=select.KQ_EV_ADD | select.KQ_EV_CLEAR,
                                 fflags=select.KQ_NOTE_WRITE | select.KQ_NOTE_RENAME | select.KQ_NOTE_DELETE)
            self.queue.control([event], 0, 0)
        except (OSError, AttributeError):
            self.close()

    def wait(self, seconds):
        seconds = max(0, min(seconds, 0.1))
        if self.queue is not None:
            try:
                events = self.queue.control(None, 1, seconds)
                if any(e.flags & select.KQ_EV_ERROR for e in events):
                    self.close()
                return
            except OSError:
                self.close()
        time.sleep(seconds)

    def close(self):
        if self.queue is not None:
            self.queue.close()
            self.queue = None
        if self.fd is not None:
            os.close(self.fd)
            self.fd = None
