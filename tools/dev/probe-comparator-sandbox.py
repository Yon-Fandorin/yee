#!/usr/bin/env python3
"""Synthetic macOS file/network boundary probe; no model or browser."""
import argparse
import json
import os
from pathlib import Path
import socket
import subprocess
import sys


CHILD = '''import json,socket,sys,subprocess
from pathlib import Path
allowed,blocked,good,bad=sys.argv[1:]
out={}
for label,path in [('allowed',Path(allowed)),('blocked',Path(blocked))]:
    try:path.read_text();out[label+'_read']=True
    except PermissionError:out[label+'_read']=False
    try:path.with_suffix('.write').write_text('synthetic');out[label+'_write']=True
    except PermissionError:out[label+'_write']=False
for label,port in [('allowed',good),('blocked',bad)]:
    try:
        with socket.create_connection(('127.0.0.1',int(port)),timeout=1):out[label+'_connect']=True
    except PermissionError:out[label+'_connect']=False
link=Path(allowed).parent/'outside-link'
link.symlink_to(blocked)
try:link.read_text();out['symlink_read']=True
except PermissionError:out['symlink_read']=False
child=subprocess.run([sys.executable,'-c',
    'from pathlib import Path; import sys; Path(sys.argv[1]).read_text()',blocked],
    capture_output=True,text=True)
out['descendant_read_denied']=child.returncode!=0 and 'PermissionError' in child.stderr
print(json.dumps(out))
'''


def probe(root):
    root=Path(root)
    if not root.is_absolute() or root.parent.resolve()!=root.parent:raise ValueError('new absolute root required')
    root.mkdir(mode=0o700)
    allowed=root/'allowed';allowed.mkdir(mode=0o700)
    good_file=allowed/'input.txt';bad_file=root/'blocked.txt'
    good_file.write_text('synthetic allowed');bad_file.write_text('synthetic forbidden')
    child=allowed/'child.py';child.write_text(CHILD)
    with socket.socket() as good, socket.socket() as bad:
        for listener in (good,bad):listener.bind(('127.0.0.1',0));listener.listen(1)
        good_port=good.getsockname()[1];bad_port=bad.getsockname()[1]
        reads=['/System','/usr','/bin','/Library','/opt/homebrew','/private/var/db/dyld',str(allowed)]
        reads+=['/dev']
        policy='(version 1)\n(allow default)\n'
        policy+='(deny file-read-data (require-all (require-not (literal "/")) '+ ' '.join('(require-not (subpath '+json.dumps(p)+'))' for p in reads)+'))\n'
        policy+='(deny file-write* (require-all (require-not (subpath '+json.dumps(str(allowed))+')) (require-not (literal "/dev/null"))))\n'
        policy+='(deny network*)\n'
        policy+=f'(allow network-outbound (remote ip "localhost:{good_port}"))\n'
        profile=root/'scope.sb';profile.write_text(policy)
        command=['/usr/bin/sandbox-exec','-f',str(profile),sys.executable,str(child),
                 str(good_file),str(bad_file),str(good_port),str(bad_port)]
        run=subprocess.run(command,capture_output=True,text=True,timeout=15,cwd=allowed,
                           env={k:v for k,v in os.environ.items() if k in ('PATH','HOME','LANG')})
        (root/'stdout.txt').write_text(run.stdout);(root/'stderr.txt').write_text(run.stderr)
        expected={'allowed_read':True,'allowed_write':True,'blocked_read':False,
                  'blocked_write':False,'allowed_connect':True,'blocked_connect':False,
                  'symlink_read':False,'descendant_read_denied':True}
        actual=json.loads(run.stdout) if run.returncode==0 else None
        result={'passed':actual==expected,'returncode':run.returncode,'actual':actual,
                'blocked_write_absent':not bad_file.with_suffix('.write').exists(),
                'browser_or_model_launched':False,'full_comparator_scope_verified':False}
        with (root/'verification.json').open('x') as out:json.dump(result,out,indent=2)
        return result


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--record',type=Path,required=True)
    result=probe(parser.parse_args().record);print(json.dumps(result,indent=2))
    raise SystemExit(0 if result['passed'] else 1)
