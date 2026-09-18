"""Private operator channel for synthetic S12 authentication; never served over HTTP."""
import argparse
import json
import os
from pathlib import Path
import stat
import tempfile


def read_control(directory, manifest):
    path=Path(directory)/'operator-auth.json'
    try:fd=os.open(path,os.O_RDONLY|os.O_NOFOLLOW|os.O_NONBLOCK)
    except FileNotFoundError:return None
    with os.fdopen(fd,'rb') as stream:
        info=os.fstat(stream.fileno())
        if (not stat.S_ISREG(info.st_mode) or info.st_uid!=os.geteuid()
                or info.st_mode & 0o077 or info.st_size>1024):
            raise ValueError('invalid private operator control')
        value=json.loads(stream.read(1025))
    if (not isinstance(value,dict) or value.get('schema')!='yee.synthetic-auth.v1'
            or value.get('created_ns')!=manifest['created_ns']
            or value.get('dataset_sha256')!=manifest['dataset_sha256']
            or value.get('action') not in ('complete','cancel')):
        raise ValueError('operator control does not match this run')
    if value['action']!=('cancel' if manifest['variant']=='cancel' else 'complete'):
        raise ValueError('operator outcome does not match the declared variant')
    return value['action']


def publish(directory, action):
    root=Path(directory)
    info=root.lstat()
    if (not root.is_absolute() or not stat.S_ISDIR(info.st_mode)
            or info.st_uid!=os.geteuid() or info.st_mode & 0o077):
        raise ValueError('expected private absolute fixture directory')
    manifest=json.loads((root/'manifest.json').read_text())
    if manifest['scenario']!='S12' or action!=('cancel' if manifest['variant']=='cancel' else 'complete'):
        raise ValueError('wrong scenario or outcome for declared variant')
    value={'schema':'yee.synthetic-auth.v1','created_ns':manifest['created_ns'],
           'dataset_sha256':manifest['dataset_sha256'],'action':action}
    fd,temporary=tempfile.mkstemp(prefix='.operator-auth-',dir=root)
    try:
        with os.fdopen(fd,'w') as stream:
            json.dump(value,stream);stream.flush();os.fsync(stream.fileno())
        # Atomic exclusive publication. Repeated commands cannot overwrite it.
        os.link(temporary,root/'operator-auth.json')
    finally:os.unlink(temporary)


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('run_dir');parser.add_argument('action',choices=('complete','cancel'))
    args=parser.parse_args();publish(args.run_dir,args.action)
    print(json.dumps({'synthetic_operator_action':args.action,'credentials_used':False}))
