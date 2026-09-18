"""Private, append-only interval timing across operator and runner processes.

Call start before fixture/browser/consent setup. Phase timestamps measure elapsed
time; they do not prove that the caller actually performed the named work.
"""
import argparse
import fcntl
import json
import os
from pathlib import Path
import stat
import subprocess
import sys
import time
import uuid

PHASES=('setup_started','execution_started','execution_finished','verification_finished','finished')


def boot_id():
    if sys.platform=='darwin':
        return subprocess.check_output(['/usr/sbin/sysctl','-n','kern.bootsessionuuid'],text=True).strip()
    return Path('/proc/sys/kernel/random/boot_id').read_text().strip()


def validate(rows):
    if not rows or len(rows)>len(PHASES):raise ValueError('invalid timeline length')
    first=rows[0]
    for i,row in enumerate(rows):
        if (not isinstance(row,dict) or row.get('schema')!='yee.trial-time.v1'
                or row.get('phase')!=PHASES[i] or type(row.get('sequence')) is not int
                or row['sequence']!=i+1 or row.get('trial_id')!=first.get('trial_id')
                or row.get('record')!=first.get('record')
                or row.get('boot_id')!=first.get('boot_id')
                or type(row.get('monotonic_ns')) is not int or row['monotonic_ns']<0
                or type(row.get('wall_ns')) is not int or row['wall_ns']<=0):
            raise ValueError('invalid or reordered timing event')
        if i and row['monotonic_ns']<rows[i-1]['monotonic_ns']:
            raise ValueError('monotonic clock moved backwards')
        outcome=row.get('outcome')
        if outcome is not None:
            if row['phase']!='execution_finished' or not isinstance(outcome,dict):
                raise ValueError('outcome belongs to execution_finished')
            if outcome.get('status')=='returned':
                if set(outcome)!={'status','returncode'} or type(outcome['returncode']) is not int:
                    raise ValueError('invalid execution return outcome')
            elif outcome.get('status')=='raised':
                if (set(outcome)!={'status','exception_type'}
                        or not isinstance(outcome['exception_type'],str)
                        or not outcome['exception_type'].isidentifier()):
                    raise ValueError('invalid execution exception outcome')
            else:raise ValueError('unknown execution outcome')
    if not isinstance(first.get('boot_id'),str) or not first['boot_id']:
        raise ValueError('missing boot identity')
    if not isinstance(first.get('record'),str) or not Path(first['record']).is_absolute():
        raise ValueError('missing bound model record path')
    if str(uuid.UUID(first.get('trial_id','')))!=first['trial_id']:
        raise ValueError('invalid trial identity')
    result={'trial_id':first['trial_id'],'complete':len(rows)==len(PHASES),
            'elapsed_seconds_so_far':(rows[-1]['monotonic_ns']-first['monotonic_ns'])/1e9,
            'recorded_interval_seconds':None,
            'trial_boundary_verified':False,
            'whole_task_elapsed_seconds':None,'phase_seconds':{}}
    for previous,current in zip(rows,rows[1:]):
        result['phase_seconds'][previous['phase']]=(current['monotonic_ns']-previous['monotonic_ns'])/1e9
    # Phase names alone do not prove that inventory/discovery preceded neither
    # setup_started nor that all final verification finished within this log.
    # Keep the observed interval, but never promote it to whole-task evidence.
    if result['complete']:result['recorded_interval_seconds']=result['elapsed_seconds_so_far']
    return result


def append(path, phase, *, record, clock=time.monotonic_ns, wall=time.time_ns, boot=boot_id,
           outcome=None):
    path=Path(path)
    info=path.parent.lstat()
    if (not path.is_absolute() or not stat.S_ISDIR(info.st_mode)
            or info.st_uid!=os.getuid() or info.st_mode&0o077):
        raise ValueError('timeline parent must be owned private absolute directory')
    creating=phase==PHASES[0]
    flags=os.O_RDWR|os.O_APPEND|os.O_NOFOLLOW
    if creating:flags|=os.O_CREAT|os.O_EXCL
    fd=os.open(path,flags,0o600)
    try:
        fcntl.flock(fd,fcntl.LOCK_EX)
        info=os.fstat(fd)
        if not stat.S_ISREG(info.st_mode) or info.st_uid!=os.getuid() or info.st_mode&0o077:
            raise ValueError('timeline must be owned private regular file')
        raw=os.read(fd,16385)
        if len(raw)>16384 or (raw and not raw.endswith(b'\n')):
            raise ValueError('oversized or interrupted timeline')
        rows=[json.loads(line) for line in raw.splitlines()]
        if rows:validate(rows)
        record=str(Path(record).resolve())
        if rows and rows[0]['record']!=record:raise ValueError('timeline belongs to another model run')
        if len(rows)>=len(PHASES) or phase!=PHASES[len(rows)]:
            raise ValueError('duplicate or out-of-order phase')
        boot_value=boot()
        if rows and rows[0]['boot_id']!=boot_value:raise ValueError('machine restarted during trial')
        event={'schema':'yee.trial-time.v1','sequence':len(rows)+1,'phase':phase,
               'trial_id':rows[0]['trial_id'] if rows else str(uuid.uuid4()),
               'record':record,
               'boot_id':boot_value,'monotonic_ns':clock(),'wall_ns':wall()}
        if outcome is not None:event['outcome']=outcome
        result=validate(rows+[event])
        data=(json.dumps(event,separators=(',',':'))+'\n').encode()
        while data:
            n=os.write(fd,data)
            if n<=0:raise OSError('timeline write failed')
            data=data[n:]
        os.fsync(fd)
        return result
    finally:os.close(fd)


def inspect(path, *, record, expected_phase=None, boot=boot_id):
    """Read a private timeline before setup without appending invented phases."""
    path=Path(path)
    parent=path.parent.lstat()
    if (not path.is_absolute() or not stat.S_ISDIR(parent.st_mode)
            or parent.st_uid!=os.getuid() or parent.st_mode&0o077):
        raise ValueError('timeline parent must be owned private absolute directory')
    fd=os.open(path,os.O_RDONLY|os.O_NOFOLLOW|os.O_NONBLOCK)
    try:
        fcntl.flock(fd,fcntl.LOCK_SH)
        info=os.fstat(fd)
        if not stat.S_ISREG(info.st_mode) or info.st_uid!=os.getuid() or info.st_mode&0o077:
            raise ValueError('timeline must be owned private regular file')
        raw=os.read(fd,16385)
        if len(raw)>16384 or not raw.endswith(b'\n'):
            raise ValueError('oversized or interrupted timeline')
        rows=[json.loads(line) for line in raw.splitlines()]
        result=validate(rows)
        if rows[0]['record']!=str(Path(record).resolve()):
            raise ValueError('timeline belongs to another model run')
        if rows[0]['boot_id']!=boot():raise ValueError('machine restarted during trial')
        if expected_phase is not None and rows[-1]['phase']!=expected_phase:
            raise ValueError('unexpected current timeline phase')
        return result
    finally:os.close(fd)


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('path',type=Path);parser.add_argument('phase',choices=PHASES)
    parser.add_argument('--record',type=Path,required=True,help='bound new model run directory')
    args=parser.parse_args()
    print(json.dumps(append(args.path,args.phase,record=args.record),indent=2))
