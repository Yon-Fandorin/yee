import os
from pathlib import Path
import select
import tempfile
import time
import unittest
from unittest.mock import patch

from yee_browser_wakeup import ResponseWakeup


class WakeupTests(unittest.TestCase):
    def test_registration_failure_falls_back_and_closes_fd(self):
        with tempfile.TemporaryDirectory() as directory:
            with patch('yee_browser_wakeup.select.kqueue', side_effect=OSError, create=True):
                watcher = ResponseWakeup(directory)
            self.assertIsNone(watcher.fd)
            self.assertIsNone(watcher.queue)
            with patch('yee_browser_wakeup.time.sleep') as sleep:
                watcher.wait(20)
                sleep.assert_called_once_with(0.1)
            watcher.close()

    def test_wait_error_falls_back(self):
        with tempfile.TemporaryDirectory() as directory:
            watcher = ResponseWakeup(directory)
            watcher.close()
            from unittest.mock import Mock
            queue = Mock()
            queue.control.side_effect = OSError
            watcher.queue = queue
            with patch('yee_browser_wakeup.time.sleep') as sleep:
                watcher.wait(0.02)
                sleep.assert_called_once_with(0.02)
            queue.close.assert_called_once()
            self.assertIsNone(watcher.queue)

    @unittest.skipUnless(hasattr(select, 'kqueue'), 'kqueue platform required')
    def test_atomic_replace_before_wait_is_not_lost(self):
        with tempfile.TemporaryDirectory() as directory:
            watcher = ResponseWakeup(directory)
            self.assertIsNotNone(watcher.queue)
            try:
                target = Path(directory) / 'response.json'
                for value in ('first', 'second'):
                    temporary = Path(directory) / 'response.tmp'
                    temporary.write_text(value)
                    os.replace(temporary, target)
                    started = time.monotonic()
                    watcher.wait(0.1)
                    self.assertLess(time.monotonic() - started, 0.09)
                    self.assertEqual(target.read_text(), value)
            finally:
                watcher.close()
            watcher.close()

    def test_symlink_directory_is_not_watched(self):
        with tempfile.TemporaryDirectory() as directory:
            link = Path(directory) / 'link'
            link.symlink_to(directory, target_is_directory=True)
            watcher = ResponseWakeup(str(link))
            self.assertIsNone(watcher.fd)
            watcher.close()


if __name__ == '__main__':
    unittest.main()
