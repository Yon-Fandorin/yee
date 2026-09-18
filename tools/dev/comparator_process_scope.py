"""Experimental macOS MCP process scope, not whole-browser isolation."""
import json
from pathlib import Path


def browser_profile(directory, browser):
    """Blank-page compatibility probe only; no outbound TCP permitted."""
    directory=Path(directory).resolve(strict=True)
    bundle=next(p for p in Path(browser).parents if p.suffix=='.app')
    reads=['/System','/usr','/bin','/Library','/private/var/db/dyld','/dev',str(bundle),str(directory)]
    policy='(version 1)\n(allow default)\n'
    policy+='(deny file-read-data (require-all (require-not (literal "/")) '
    policy+=' '.join('(require-not (subpath '+json.dumps(p)+'))' for p in reads)+'))\n'
    policy+='(deny file-write* (require-all (require-not (subpath '+json.dumps(str(directory))+')) (require-not (literal "/dev/null"))))\n'
    policy+='(deny network*)\n(allow network-bind network-inbound (local ip "localhost:*"))\n'
    policy+='(allow network* (local unix-socket) (remote unix-socket))\n'
    path=directory/'browser-scope.sb'
    with path.open('x') as out:out.write(policy)
    return path


def mcp_profile(directory, python, sources):
    return _profile(directory,python,sources)


def daemon_profile(directory, python, sources, port):
    if type(port) is not int or not 1<=port<=65535:raise ValueError('exact browser port required')
    return _profile(directory,python,sources,port)


def _profile(directory, python, sources, port=None):
    directory=Path(directory).resolve(strict=True)
    venv=Path(python).absolute().parent.parent
    reads=['/System','/usr','/bin','/Library','/opt/homebrew','/private/var/db/dyld',
           '/dev',str(directory),str(venv),str(Path(sources).resolve(strict=True))]
    policy='(version 1)\n(allow default)\n'
    policy+='(deny file-read-data (require-all (require-not (literal "/")) '
    policy+=' '.join('(require-not (subpath '+json.dumps(p)+'))' for p in reads)+'))\n'
    policy+='(deny file-write* (require-all (require-not (subpath '+json.dumps(str(directory))+')) (require-not (literal "/dev/null"))))\n'
    policy+='(deny network*)\n'
    if port is None:
        policy+='(allow network-outbound (literal '+json.dumps(str(directory/'bu.sock'))+'))\n'
    else:
        policy+=f'(allow network-outbound (remote ip "localhost:{port}"))\n'
        policy+='(allow network-bind network-inbound (literal '+json.dumps(str(directory/'bu.sock'))+'))\n'
    path=directory/('mcp-scope.sb' if port is None else 'daemon-scope.sb')
    with path.open('x') as out:out.write(policy)
    return path
