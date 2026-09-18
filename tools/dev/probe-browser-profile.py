#!/usr/bin/env python3
"""Provision a disposable blank Chrome and verify its own CDP metadata.

No model, daemon, page automation or existing browser discovery. This diagnostic
does not establish OS network/filesystem isolation or benchmark equivalence.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import stat
import subprocess
import time
import urllib.request


class NoRedirect(urllib.request.HTTPRedirectHandler):
    def redirect_request(self, *args, **kwargs):
        raise ValueError('metadata redirect refused')


def probe(browser, record, daemon_python=None, live_mcp=False, browser_loss=False, scoped_mcp=False, scoped_daemon=False, scoped_browser=False):
    browser=Path(browser);record=Path(record)
    if not browser.is_absolute() or not browser.is_file():raise ValueError('absolute browser executable required')
    if not record.is_absolute() or record.parent.resolve()!=record.parent:
        raise ValueError('absolute non-symlink record parent required')
    record.mkdir(mode=0o700)
    profile=record/'profile';profile.mkdir(mode=0o700)
    command=[str(browser),'--headless=new','--remote-debugging-address=127.0.0.1',
             '--remote-debugging-port=0',f'--user-data-dir={profile}',
             '--no-first-run','--no-default-browser-check','--disable-background-networking',
             'about:blank']
    result={'command':command,'profile':str(profile),'verified':False,
            'os_isolation_verified':False,'model_launched':False}
    prefix=[]
    environment=None
    if scoped_browser:
        from comparator_process_scope import browser_profile
        scope=browser_profile(record,browser)
        prefix=['/usr/bin/sandbox-exec','-f',str(scope)]
        result['browser_scope_profile']=str(scope)
        temp=record/'tmp';temp.mkdir(mode=0o700)
        # Chromium's macOS GetTempDir uses this explicit override, not TMPDIR.
        environment=dict(os.environ,TMPDIR=str(temp)+'/',MAC_CHROMIUM_TMPDIR=str(temp))
    with (record/'browser.stderr').open('xb') as log:
        process=subprocess.Popen(prefix+command,stdout=log,stderr=log,start_new_session=True,
                                 cwd=record if scoped_browser else None,env=environment)
        result['pid']=process.pid
        try:
            active=profile/'DevToolsActivePort';deadline=time.monotonic()+20
            while not active.exists():
                if process.poll() is not None:raise RuntimeError('owned browser exited before CDP readiness')
                if time.monotonic()>deadline:raise TimeoutError('owned browser CDP readiness timeout')
                time.sleep(.1)
            info=active.lstat()
            if not stat.S_ISREG(info.st_mode) or info.st_uid!=os.getuid():
                raise ValueError('invalid owned profile endpoint file')
            raw=active.read_bytes();lines=raw.decode().splitlines()
            if (len(lines)!=2 or not lines[0].isdigit() or not 1<=int(lines[0])<=65535
                    or not re.fullmatch(r'/devtools/browser/[a-zA-Z0-9-]+',lines[1])):
                raise ValueError('invalid CDP endpoint metadata')
            port=int(lines[0]);expected=f'ws://127.0.0.1:{port}{lines[1]}'
            def listener():
                output=subprocess.check_output(['/usr/sbin/lsof','-nP','-a','-p',str(process.pid),
                    f'-iTCP:{port}','-sTCP:LISTEN','-Fn'],text=True)
                if f'n127.0.0.1:{port}' not in output.splitlines():
                    raise ValueError('owned process does not own exact loopback listener')
                return output
            first=listener()
            opener=urllib.request.build_opener(urllib.request.ProxyHandler({}),NoRedirect())
            with opener.open(f'http://127.0.0.1:{port}/json/version',timeout=5) as response:
                payload=response.read(65537)
            if len(payload)>65536:raise ValueError('oversized CDP metadata')
            metadata=json.loads(payload)
            if metadata.get('webSocketDebuggerUrl')!=expected:
                raise ValueError('profile and HTTP endpoint differ')
            second=listener()
            if process.poll() is not None or active.read_bytes()!=raw or active.lstat().st_ino!=info.st_ino:
                raise ValueError('owned browser identity changed during verification')
            result.update(verified=True,websocket_url=expected,metadata=metadata,
                          listener_before=first,listener_after=second,
                          endpoint_file_sha256=hashlib.sha256(raw).hexdigest())
            if daemon_python is not None:
                from probe_bound_daemon import probe as probe_daemon
                def stop_owned_browser():
                    process.terminate();process.wait(timeout=10)
                    result['intentional_browser_exit']=process.returncode
                result['daemon']=probe_daemon(daemon_python,expected,record/'daemon',live_mcp,
                    stop_owned_browser if browser_loss else None,scoped_mcp,scoped_daemon)
                if not browser_loss:
                    result['listener_after_daemon']=listener()
                    if process.poll() is not None or active.read_bytes()!=raw:
                        raise ValueError('owned browser changed during daemon probe')
        except Exception as error:
            result['verified']=False
            result['error_type']=type(error).__name__
            raise
        finally:
            if process.poll() is None:
                process.terminate()
                try:process.wait(timeout=10)
                except subprocess.TimeoutExpired:process.kill();process.wait(timeout=5)
            result['browser_returncode']=process.returncode
            runtime_log=(record/'browser.stderr').read_bytes()
            result['sandbox_initialization_failed']=b'sandbox initialization failed' in runtime_log or b'Failed to initialize sandbox.' in runtime_log
            if process.returncode!=0 or result['sandbox_initialization_failed']:
                result['verified']=False
            with (record/'verification.json').open('x') as output:json.dump(result,output,indent=2)
    return result


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--browser',required=True,type=Path)
    parser.add_argument('--record',required=True,type=Path)
    parser.add_argument('--daemon-python',type=Path,help='optional installed harness venv; metadata probe only')
    parser.add_argument('--mcp',action='store_true',help='also record one current-tab call; requires --daemon-python')
    parser.add_argument('--browser-loss',action='store_true',help='stop owned browser between two MCP probes')
    parser.add_argument('--scoped-mcp',action='store_true',help='experimental MCP file/network scope')
    parser.add_argument('--scoped-daemon',action='store_true',help='experimental daemon file/network scope')
    parser.add_argument('--scoped-browser',action='store_true',help='experimental blank Chrome compatibility scope')
    args=parser.parse_args()
    if args.browser_loss and not args.mcp:parser.error('--browser-loss requires --mcp')
    if args.scoped_mcp and not args.mcp:parser.error('--scoped-mcp requires --mcp')
    if args.scoped_daemon and args.daemon_python is None:parser.error('--scoped-daemon requires --daemon-python')
    if args.mcp and args.daemon_python is None:parser.error('--mcp requires --daemon-python')
    result=probe(args.browser,args.record,args.daemon_python,args.mcp,args.browser_loss,args.scoped_mcp,args.scoped_daemon,args.scoped_browser)
    print(json.dumps(result,indent=2))
    raise SystemExit(0 if result['verified'] else 1)
