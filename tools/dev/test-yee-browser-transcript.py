import json
import math
import os
import stat
import tempfile
import unittest
from unittest import mock
import yee_browser_transcript as ybt


class TranscriptTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.directory = self.tmp.name
        self.path = os.path.join(self.directory, "trace.jsonl")

    def events(self):
        with open(self.path, encoding="utf-8") as stream:
            return [json.loads(line) for line in stream]

    def test_private_sequence_and_unchanged_caller(self):
        event = {"action": "open"}
        with ybt.Transcript(self.path) as writer:
            writer.write(event)
            writer.write({"action": "click"})
        self.assertEqual(stat.S_IMODE(os.stat(self.path).st_mode), 0o600)
        self.assertEqual([e["sequence"] for e in self.events()], [1, 2])
        self.assertNotIn("sequence", event)

    def test_existing_file_and_symlink_never_truncate(self):
        with open(self.path, "w") as stream:
            stream.write("keep")
        for path in (self.path, os.path.join(self.directory, "link.jsonl")):
            if path != self.path:
                os.symlink(self.path, path)
            with self.assertRaises(ybt.TranscriptError):
                ybt.Transcript(path)
        with open(self.path) as stream:
            self.assertEqual(stream.read(), "keep")

    def test_relative_public_and_symlink_parent_rejected(self):
        with self.assertRaises(ybt.TranscriptError):
            ybt.Transcript("relative.jsonl")
        link = os.path.join(self.directory, "linked")
        os.symlink(self.directory, link)
        with self.assertRaises(ybt.TranscriptError):
            ybt.Transcript(os.path.join(link, "trace.jsonl"))
        os.chmod(self.directory, 0o755)
        with self.assertRaises(ybt.TranscriptError):
            ybt.Transcript(self.path)
        self.assertFalse(os.path.exists(self.path))

    def test_invalid_events_poison_writer(self):
        for index, event in enumerate(({"sequence": 4}, [], {"n": math.nan},
                                       {"blob": "x" * ybt.MAX_EVENT_BYTES})):
            with ybt.Transcript(self.path + str(index)) as writer:
                with self.assertRaises(ybt.TranscriptError):
                    writer.write(event)
                with self.assertRaises(ybt.TranscriptError):
                    writer.write({"good": True})

    def test_closed_writer_rejected(self):
        writer = ybt.Transcript(self.path)
        writer.close()
        writer.close()
        with self.assertRaises(ybt.TranscriptError):
            writer.write({})

    def test_short_and_interrupted_writes(self):
        actual = os.write
        attempts = 0
        def short(fd, data):
            nonlocal attempts
            attempts += 1
            if attempts == 1:
                raise InterruptedError()
            return actual(fd, data[:1])
        with ybt.Transcript(self.path) as writer, mock.patch.object(os, "write", side_effect=short):
            writer.write({"text": "한글"})
        self.assertEqual(self.events(), [{"sequence": 1, "text": "한글"}])

    def test_zero_write_fails_without_looping(self):
        with ybt.Transcript(self.path) as writer, mock.patch.object(os, "write", return_value=0) as write:
            with self.assertRaises(ybt.TranscriptError):
                writer.write({})
            self.assertEqual(write.call_count, 1)
        self.assertEqual(self.events(), [])

    def test_io_failure_latches_writer(self):
        with ybt.Transcript(self.path) as writer:
            with mock.patch.object(os, "write", side_effect=OSError("disk gone")):
                with self.assertRaises(ybt.TranscriptError):
                    writer.write({})
            with self.assertRaises(ybt.TranscriptError):
                writer.write({})

    def test_opened_parent_is_revalidated(self):
        fake = mock.Mock(st_mode=stat.S_IFDIR | 0o755, st_uid=os.geteuid())
        with mock.patch.object(os, "fstat", return_value=fake):
            with self.assertRaises(ybt.TranscriptError):
                ybt.Transcript(self.path)
        self.assertFalse(os.path.exists(self.path))


if __name__ == "__main__":
    unittest.main()
