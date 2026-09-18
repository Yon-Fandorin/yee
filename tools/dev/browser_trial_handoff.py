"""Explicit host/user bridge for synthetic browser trials; not a browser permission system.

The model receives only question results through MCP. The operator publishes an
actual user response on the private channel. No default answer, auth transition,
native trace fabrication, or automatic approval occurs in this module.
"""
import asyncio
import hashlib
import json
import os
from pathlib import Path
import stat
import tempfile
import time
import uuid


def private_root(path):
    p = Path(path)
    st = p.lstat()
    if not p.is_absolute() or not stat.S_ISDIR(st.st_mode) or st.st_uid != os.getuid() or st.st_mode & 0o077:
        raise ValueError('private owned absolute channel directory required')
    return p


def read(path):
    fd = os.open(path, os.O_RDONLY | os.O_NOFOLLOW | os.O_NONBLOCK)
    with os.fdopen(fd, 'rb') as f:
        st = os.fstat(f.fileno())
        if not stat.S_ISREG(st.st_mode) or st.st_uid != os.getuid() or st.st_mode & 0o077 or st.st_size > 16384:
            raise ValueError('invalid private channel file')
        return json.loads(f.read(16385))


def publish(path, value):
    """Atomic, exclusive publication; partial or duplicate replies cannot replace one."""
    private_root(path.parent)
    fd, name = tempfile.mkstemp(prefix='.publish-', dir=path.parent)
    try:
        with os.fdopen(fd, 'w') as f:
            json.dump(value, f, ensure_ascii=False); f.flush(); os.fsync(f.fileno())
        os.link(name, path)
    finally:
        os.unlink(name)


def identity(request):
    return hashlib.sha256(json.dumps(request, sort_keys=True, separators=(',', ':')).encode()).hexdigest()


def respond(root, request_id, answer, *, source, user_text):
    root = private_root(root)
    if str(uuid.UUID(request_id)) != request_id:
        raise ValueError('canonical request ID required')
    request = read(root/(request_id+'.request.json'))
    if (time.monotonic_ns() >= request['deadline_monotonic_ns'] or
            (root/(request_id+'.closed.json')).exists()):
        raise ValueError('request expired or closed')
    if answer not in request['allowed_answers']:
        raise ValueError('answer not offered by this request')
    if source not in ('codex_user_message', 'synthetic_operator', 'unit_test') or not isinstance(user_text,str) or not 1 <= len(user_text) <= 2048:
        raise ValueError('explicit actual user response provenance required')
    reply = {'schema':'yee.host-reply.v1', 'request_id':request_id,
             'run_id':request['run_id'], 'request_sha256':identity(request),
             'answer':answer, 'source':source, 'user_text':user_text,
             'time_ns':time.time_ns(), 'monotonic_ns':time.monotonic_ns()}
    publish(root/(request_id+'.reply.json'), reply)
    return reply


class Channel:
    def __init__(self, root):
        self.root = private_root(root)
        self.config = read(self.root/'config.json')
        c = self.config
        if (c.get('schema') != 'yee.host-channel.v1' or c.get('scenario') not in ('S08','S12')
                or not isinstance(c.get('target_id'),str) or len(c['target_id']) != 32
                or c.get('origin') != 'http://127.0.0.1:8787'
                or not isinstance(c.get('run_id'),str) or not c['run_id']):
            raise ValueError('invalid synthetic channel configuration')
        publish(self.root/'connection.json',{'run_id':c['run_id'],'time_ns':time.time_ns(),
                'policy':'single connection; reconnect requires a new trial, never an implicit resume'})
        self.lock = asyncio.Lock()
        self.accepted = []
        self.stopped = False

    async def ask(self, args, timeout=600):
        async with self.lock:
            c = self.config
            if self.stopped: raise ValueError('channel is stopped')
            if set(args) != {'kind','question','target_id','url'}:
                raise ValueError('exact question fields required')
            kind = args['kind']
            expected = 'choice' if c['scenario']=='S08' else ('authentication' if not self.accepted else 'document_permission')
            url = c['origin'] + ('/?report=1' if kind=='document_permission' else '/')
            if (kind != expected or kind in self.accepted or args['target_id'] != c['target_id'] or args['url'] != url
                    or not isinstance(args['question'],str) or not 1 <= len(args['question']) <= 2048):
                raise ValueError('wrong phase, target, URL, or question')
            if not 0 < timeout <= 600: raise ValueError('invalid timeout')
            rid = str(uuid.uuid4())
            allowed = ['09:00','15:00','cancel'] if kind=='choice' else ['complete','cancel'] if kind=='authentication' else ['allow','cancel']
            request = {'schema':'yee.host-question.v1','run_id':c['run_id'], 'request_id':rid,
                       **args, 'allowed_answers':allowed, 'time_ns':time.time_ns(),
                       'monotonic_ns':time.monotonic_ns(),
                       'deadline_monotonic_ns':time.monotonic_ns()+int(timeout*1e9)}
            publish(self.root/(rid+'.request.json'),request)
            try:
                while time.monotonic_ns() < request['deadline_monotonic_ns']:
                    try: reply = read(self.root/(rid+'.reply.json'))
                    except FileNotFoundError:
                        await asyncio.sleep(0.1); continue
                    if (reply.get('schema')!='yee.host-reply.v1' or reply.get('request_id')!=rid
                            or reply.get('run_id')!=c['run_id'] or reply.get('request_sha256')!=identity(request)
                            or reply.get('answer') not in allowed or reply.get('source') not in ('codex_user_message','synthetic_operator','unit_test')
                            or not request['monotonic_ns'] <= reply.get('monotonic_ns',0) < request['deadline_monotonic_ns']):
                        raise ValueError('uncorrelated or late user reply')
                    cancelled = reply['answer']=='cancel'
                    result = {'request_id':rid,'kind':kind,'status':'cancelled' if cancelled else 'answered',
                              'answer':reply['answer'],'target_id':args['target_id'],'url':url,
                              'response_source':reply['source'],
                              'user_wait_seconds':(reply['monotonic_ns']-request['monotonic_ns'])/1e9}
                    self.accepted.append(kind)
                    self.stopped = cancelled or kind in ('choice','document_permission')
                    publish(self.root/(rid+'.closed.json'),{'status':result['status'],'time_ns':time.time_ns(),'result':result})
                    return result
                self.stopped = True
                publish(self.root/(rid+'.closed.json'),{'status':'timeout','time_ns':time.time_ns()})
                return {'request_id':rid,'kind':kind,'status':'timeout','answer':None}
            except BaseException:
                self.stopped = True
                if not (self.root/(rid+'.closed.json')).exists():
                    publish(self.root/(rid+'.closed.json'),{'status':'aborted','time_ns':time.time_ns()})
                raise
