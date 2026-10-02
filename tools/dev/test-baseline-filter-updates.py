#!/usr/bin/env python3
"""Local actual-Yee gate for updated-list selection, rollback and child pinning."""
import hashlib
import http.server
import json
from pathlib import Path
import subprocess
import tempfile
import threading
import time

ROOT = Path(__file__).resolve().parents[2]
APP = ROOT / '.local-build/chromium/src/out/YeePilot/Yee.app/Contents/MacOS/Yee'
FIXTURE = ROOT / 'tests/fixtures/content-blocking/updated-lists.html'
URLS = ['https://easylist-downloads.adblockplus.org/easylist.txt',
        'https://easylist-downloads.adblockplus.org/easyprivacy.txt']


def shutdown():
    subprocess.run(['zsh', '-c', 'set -e; source "$1"; gracefully_quit_yee',
                    'yee-list-shutdown', str(ROOT / 'tools/dev/common.zsh')], check=True)


def seed(store, name):
    originals = [f'[Adblock Plus 2.0]\n! Title: EasyList\n||yee-update-{name}.test^\n'
                 f'yee-fixture.test,yee-late-update.test##.yee-update-{name}:has-text(generation advertisement)\n',
                 '[Adblock Plus 1.1]\n! Title: EasyPrivacy\n||yee-unused-tracker.test^\n']
    hashes = [hashlib.sha256(text.encode()).hexdigest() for text in originals]
    generation = hashlib.sha256(('\n'.join(hashes)).encode()).hexdigest()
    directory = store / 'generations' / generation
    directory.mkdir(parents=True, exist_ok=True)
    for filename, text in zip(['easylist.txt', 'easyprivacy.txt'], originals):
        (directory / filename).write_text(text)
    (directory / 'manifest.json').write_text(json.dumps({
        'schema_version': 1, 'checked_at': time.time(), 'lists': [
            {'file': filename, 'source': url, 'sha256': digest}
            for filename, url, digest in zip(['easylist.txt', 'easyprivacy.txt'], URLS, hashes)]}))
    return generation


subprocess.run(['zsh', '-c', 'set -e; source "$1"; require_integrated_yee_app_current',
                'yee-list-preflight', str(ROOT / 'tools/dev/common.zsh')], check=True)
shutdown()
reports = []
for mode, expected in [('current', 'b'), ('corrupt-current', 'a'),
                       ('corrupt-state', 'a'), ('bundled-fallback', 'none')]:
    with tempfile.TemporaryDirectory(prefix='yee-baseline-update-') as temporary:
        profile = Path(temporary).resolve() / 'profile'; profile.mkdir()
        store = profile / 'YeeContentBlockingLists'
        a, b = seed(store, 'a'), seed(store, 'b')
        (store / 'state.json').write_text(json.dumps({'schema_version': 1, 'current': b, 'previous': a}))
        (store / 'state.previous.json').write_text(json.dumps({'schema_version': 1, 'current': a}))
        if mode in ['corrupt-current', 'bundled-fallback']:
            (store / 'generations' / b / 'easylist.txt').write_text('corrupt')
        if mode == 'corrupt-state': (store / 'state.json').write_text('corrupt')
        if mode == 'bundled-fallback':
            (store / 'generations' / a / 'easylist.txt').write_text('corrupt')

        requests = {'adRequests': 0}
        class Handler(http.server.BaseHTTPRequestHandler):
            def do_GET(self):
                if self.path == '/asset' and self.headers.get('Host', '').split(':')[0] == 'doubleclick.net':
                    requests['adRequests'] += 1
                if self.path == '/request-count': body = json.dumps(requests).encode()
                elif self.path == '/publish-next':
                    # Publish a different valid generation while Yee is live.
                    next_generation = seed(store, 'a' if expected == 'b' else 'b')
                    (store / 'state.json').write_text(json.dumps({'schema_version': 1, 'current': next_generation}))
                    if expected != 'none':
                        running = a if expected == 'a' else b
                        (store / 'generations' / running / 'easylist.txt').write_text('changed after browser startup')
                    body = b'pending generation published'
                elif self.path.startswith('/fixture'): body = FIXTURE.read_bytes()
                else: body = b'allowed resource'
                self.send_response(200)
                self.send_header('Access-Control-Allow-Origin', '*')
                self.send_header('Content-Type', 'text/html' if self.path.startswith('/fixture') else 'text/plain')
                self.send_header('Content-Length', str(len(body)))
                self.end_headers(); self.wfile.write(body)

            def log_message(self, *_): pass

        server = http.server.ThreadingHTTPServer(('127.0.0.1', 0), Handler)
        threading.Thread(target=server.serve_forever, daemon=True).start()
        url = f'http://yee-fixture.test:{server.server_port}/fixture?expected={expected}'
        with tempfile.TemporaryFile(mode='w+') as diagnostics:
            process = subprocess.Popen([str(APP), '--user-data-dir=' + str(profile),
                '--no-first-run', '--no-default-browser-check', '--no-proxy-server',
                '--disable-background-networking', '--disable-component-update',
                '--disable-sync', '--disable-extensions', '--remote-debugging-port=0',
                '--yee-content-blocking-test-rules',
                '--host-resolver-rules=MAP *.test 127.0.0.1,MAP doubleclick.net 127.0.0.1', url],
                stdout=diagnostics, stderr=diagnostics, start_new_session=True)
            try:
                completed = subprocess.run(['node', str(ROOT / 'tools/dev/read-content-blocking-fixture.mjs'),
                    str(profile / 'DevToolsActivePort'), url], capture_output=True, text=True,
                    timeout=45, check=True)
                report = json.loads(completed.stdout)
                if report.get('error'):
                    commands = subprocess.check_output(['ps', '-axo', 'args='], text=True).splitlines()
                    report['ownedProcesses'] = [line for line in commands if str(profile) in line]
                    report['store'] = str(store)
                    raise RuntimeError(json.dumps(report, indent=2))
                assert len(report['assertions']) == 8, report
                assert report['lateRenderer']['expected'] == expected, report
                assert len(report['lateRenderer']['assertions']) == 6, report
                reports.append({'mode': mode, **report})
                print(f'{mode}: 8 actual Yee assertions and 6 late renderer assertions passed', flush=True)
            finally:
                shutdown(); process.wait(timeout=15)
                server.shutdown(); server.server_close()

sources = ['components/content_blocking/baseline_list_store.cc',
           'components/content_blocking/engine.cc', 'components/content_blocking/rust/src/lib.rs',
           'browser/content_blocking/baseline_list_updater.cc',
           'tools/dev/test-baseline-filter-updates.py',
           'patches/0001-integrate-yee-shell.patch', 'tests/fixtures/content-blocking/updated-lists.html']
output = ROOT / '.local-build/content-blocking-baseline-update-fixture.json'
output.write_text(json.dumps({'actualYee': True, 'network': 'local HTTP only', 'reports': reports,
    'sourceSHA256': {name: hashlib.sha256((ROOT / name).read_bytes()).hexdigest() for name in sources}}, indent=2) + '\n')
print('Evidence: ' + str(output))
