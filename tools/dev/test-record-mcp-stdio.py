import base64
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

RELAY = Path(__file__).with_name('record-mcp-stdio.py')


class RelayTests(unittest.TestCase):
    def run_relay(self, root, script, raw=b'', timeout=3):
        return subprocess.run([sys.executable, str(RELAY), '--record', str(root/'wire.jsonl'),
            '--stderr', str(root/'server.stderr'), '--timeout', str(timeout), '--',
            sys.executable, '-c', script], input=raw, capture_output=True, timeout=8)

    def test_bytes_notifications_server_requests_and_errors_preserved(self):
        notification = b'{"jsonrpc":"2.0", "method":"notifications/tools/list_changed"}\r\n'
        request = b'{"jsonrpc":"2.0","id":"server-1","method":"roots/list"}\n'
        incoming = (b'{ "jsonrpc":"2.0", "id":1,"method":"tools/call","params":{"code":"x"}}\n'
                    b'{"jsonrpc":"2.0","id":"server-1","error":{"code":-32601,"message":"no roots"}}\n')
        script = ('import sys\n'
                  f'sys.stdout.buffer.write({notification+request!r});sys.stdout.buffer.flush()\n'
                  'for line in sys.stdin.buffer:\n'
                  ' sys.stdout.buffer.write(line);sys.stdout.buffer.flush()\n')
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            result = self.run_relay(root, script, incoming)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(result.stdout, notification+request+incoming)
            rows = [json.loads(line) for line in (root/'wire.jsonl').read_text().splitlines()]
            self.assertEqual([row['sequence'] for row in rows], list(range(1, len(rows)+1)))
            for direction, expected in [('to_server', incoming), ('to_client', result.stdout)]:
                received = [row for row in rows if row['direction'] == direction and row['kind'] == 'wire_received']
                forwarded = [row['frame'] for row in rows if row['direction'] == direction and row['kind'] == 'wire_forwarded']
                self.assertEqual([row['frame'] for row in received], forwarded)
                self.assertEqual(b''.join(base64.b64decode(row['base64']) for row in received), expected)

    def test_oversized_frame_fails_instead_of_truncating(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            result = self.run_relay(root, 'import sys; sys.stdout.buffer.write(sys.stdin.buffer.read())',
                                    b'x'*(16*1024*1024)+b'\n')
            self.assertEqual(result.returncode, 2)
            self.assertEqual(result.stdout, b'')
            self.assertEqual((root/'wire.jsonl').read_bytes(), b'')

    def test_large_image_frame_and_double_base64_journal_are_preserved(self):
        data=base64.b64encode(b'p'*(600*1024)).decode()
        raw=(json.dumps({'jsonrpc':'2.0','id':1,'result':{'content':[
            {'type':'image','mimeType':'image/png','data':data}]}})+'\n').encode()
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory)
            result=self.run_relay(root,'import sys; sys.stdout.buffer.write(sys.stdin.buffer.read())',raw)
            self.assertEqual(result.returncode,0,result.stderr)
            self.assertEqual(result.stdout,raw)
            rows=[json.loads(line) for line in (root/'wire.jsonl').read_text().splitlines()]
            self.assertEqual([base64.b64decode(r['base64']) for r in rows if r['kind']=='wire_received'],[raw,raw])

    def test_timeout_is_failure_and_original_records_are_exclusive(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            result = self.run_relay(root, 'import time; time.sleep(30)', timeout=.2)
            self.assertEqual(result.returncode, 124)
            original = (root/'wire.jsonl').read_bytes()
            result = self.run_relay(root, 'print("must not start")')
            self.assertNotEqual(result.returncode, 0)
            self.assertEqual(result.stdout, b'')
            self.assertEqual((root/'wire.jsonl').read_bytes(), original)


if __name__ == '__main__': unittest.main()
