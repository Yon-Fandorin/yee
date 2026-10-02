#!/usr/bin/env python3
"""Validate native procedural rules in newly built Yee, using local HTTP only."""
import http.server
import hashlib
import json
from pathlib import Path
import subprocess
import tempfile
import threading

ROOT = Path(__file__).resolve().parents[2]
APP = ROOT / '.local-build/chromium/src/out/YeePilot/Yee.app/Contents/MacOS/Yee'
FIXTURE = ROOT / 'tests/fixtures/content-blocking/procedural.html'
subprocess.run(['zsh', '-c', 'set -e; source "$1"; require_integrated_yee_app_current; gracefully_quit_yee',
                'yee-procedural-fixture', str(ROOT / 'tools/dev/common.zsh')], check=True)


class FixtureHandler(http.server.BaseHTTPRequestHandler):
    def do_GET(self):
        if 'frame=1' in self.path:
            body = b'<!doctype html><div id="frame-ad" class="text-ad">Advertisement</div>'
        else:
            body = FIXTURE.read_bytes()
        self.send_response(200)
        self.send_header('Content-Type', 'text/html; charset=utf-8')
        self.send_header('Content-Length', str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def log_message(self, *_):
        pass


server = http.server.ThreadingHTTPServer(('127.0.0.1', 0), FixtureHandler)
threading.Thread(target=server.serve_forever, daemon=True).start()
reports = []
try:
    for mode in ['on', 'off', 'site-exception']:
        with tempfile.TemporaryDirectory(prefix='yee-procedural-') as temporary:
            profile = Path(temporary) / 'profile'
            if mode == 'site-exception':
                (profile / 'Default').mkdir(parents=True)
                (profile / 'Default/Preferences').write_text(json.dumps({
                    'yee': {'content_blocking': {'disabled_sites': ['yee-procedural.test']}}
                }))
            url = f'http://yee-procedural.test:{server.server_port}/fixture?mode={mode}'
            command = [str(APP), '--user-data-dir=' + str(profile), '--no-first-run',
                       '--no-default-browser-check', '--disable-background-networking',
                       '--disable-component-update', '--disable-sync', '--disable-extensions',
                       '--remote-debugging-port=0', '--yee-content-blocking-test-rules',
                       '--host-resolver-rules=MAP yee-procedural.test 127.0.0.1',
                       '--disable-features=' + ('YeeContentBlocking,' if mode == 'off' else '') + 'OptimizationHints', url]
            with tempfile.TemporaryFile(mode='w+') as diagnostics:
                process = subprocess.Popen(command, stdout=diagnostics, stderr=diagnostics,
                                           start_new_session=True)
                try:
                    result = subprocess.run(['node', str(ROOT / 'tools/dev/read-content-blocking-fixture.mjs'),
                                             str(profile / 'DevToolsActivePort'), url],
                                            text=True, capture_output=True, timeout=45, check=True)
                    report = json.loads(result.stdout)
                    if report.get('error'):
                        raise RuntimeError(json.dumps(report, indent=2))
                    reports.append(report)
                    print(f"{mode}: {len(report['assertions'])} native assertions passed", flush=True)
                finally:
                    subprocess.run(['zsh', '-c', 'set -e; source "$1"; gracefully_quit_yee',
                                    'yee-procedural-shutdown', str(ROOT / 'tools/dev/common.zsh')], check=True)
                    process.wait(timeout=15)
    output = ROOT / '.local-build/content-blocking-procedural-fixture.json'
    sources = ['renderer/content_blocking/procedural_cosmetic.js',
               'renderer/content_blocking/document_filter_agent.cc',
               'components/content_blocking/rust/src/lib.rs',
               'components/content_blocking/rust/Cargo.toml',
               'components/content_blocking/data/community/sources.json',
               'components/content_blocking/data/test-filters.txt',
               'tests/fixtures/content-blocking/procedural.html']
    output.write_text(json.dumps({'actualYee': True, 'nativeInjection': True,
                                 'sourceSHA256': {path: hashlib.sha256((ROOT / path).read_bytes()).hexdigest()
                                                  for path in sources},
                                 'reports': reports}, indent=2) + '\n')
    print('Evidence: ' + str(output))
finally:
    server.shutdown()
    server.server_close()
