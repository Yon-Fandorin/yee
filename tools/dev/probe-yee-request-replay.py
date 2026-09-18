#!/usr/bin/env python3
"""Exercise native replay/recovery on an isolated, detached test mailbox.

Does not attach/read a tab, grant consent, or run a model. Retains original
records. Requires a fresh record directory and leaves uncertain failures fenced.
"""
import argparse
import fcntl
import importlib.util
import json
import os
from pathlib import Path
import subprocess
import sys
import time
import uuid

CLI = Path(__file__).with_name('yee-browser.py')
spec = importlib.util.spec_from_file_location('yee_cli', CLI)
cli = importlib.util.module_from_spec(spec)
spec.loader.exec_module(cli)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--bridge', required=True, type=Path)
    parser.add_argument('--record-dir', required=True, type=Path)
    parser.add_argument('--prior-record-dir', type=Path,
                        help='replay an earlier probe ID, e.g. after independently verified browser restart')
    parser.add_argument('--quarantine-probe', action='store_true',
                        help='inject an incomplete claim last; leaves this test mailbox quarantined and fenced')
    args = parser.parse_args()
    bridge = Path(cli.validate_bridge(str(args.bridge)))
    if not args.record_dir.is_absolute():
        parser.error('record directory must be absolute')
    args.record_dir.mkdir(mode=0o700)

    def save(name, value):
        fd = os.open(args.record_dir / name, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
        with os.fdopen(fd, 'w') as stream:
            json.dump(value, stream, ensure_ascii=False, indent=2)

    command = [sys.executable, str(CLI), '--bridge', str(bridge), '--timeout', '5']
    result = subprocess.run(command + ['--record', str(args.record_dir/'status.jsonl'), 'status'],
                            capture_output=True, text=True, timeout=10)
    save('status-process.json', {'returncode':result.returncode, 'stdout':result.stdout,
                                'stderr':result.stderr})
    if result.returncode:
        raise RuntimeError('status failed; no replay attempted')
    original_output = json.loads(result.stdout)
    if original_output.get('attached') is not False or original_output.get('execution_settled') is not True:
        raise RuntimeError('requires detached, settled test bridge')
    rows = [json.loads(line) for line in (args.record_dir/'status.jsonl').read_text().splitlines()]
    request = next(row['request'] for row in rows if row['kind']=='request')
    original = next(row['response'] for row in rows if row['kind']=='response')
    if args.prior_record_dir:
        prior = [json.loads(line) for line in (args.prior_record_dir/'status.jsonl').read_text().splitlines()]
        request = next(row['request'] for row in prior if row['kind']=='request')
        original = next(row['response'] for row in prior if row['kind']=='response')
        original_output = json.loads(json.loads((args.prior_record_dir/'status-process.json').read_text())['stdout'])
        save('prior-source.json', {'path':str(args.prior_record_dir), 'request':request,
                                   'response':original})
    archive = bridge / ('native-request-' + request['id'].encode().hex().upper() + '.result')
    if cli.read_json(archive) != original:
        raise RuntimeError('native archive missing or mismatched; no replay attempted')
    lock = cli.acquire_lock(str(bridge/'request.lock'), 5)
    try:
        if (bridge/'client-pending.json').exists() or (bridge/'request.json').exists():
            raise RuntimeError('another request exists; no replay attempted')
        # Changing the payload makes dispatch observable: a detached observe
        # would fail not_attached, whereas replay must return the old status.
        duplicate = {**request, 'command':'observe', 'expires_unix_ms':time.time()*1000+5000}
        save('duplicate-request.json', duplicate)
        cli.atomic_write(str(bridge/'client-pending.json'), json.dumps(duplicate).encode())
        (bridge/'response.json').unlink()
        cli.atomic_write(str(bridge/'request.json'), json.dumps(duplicate).encode())
        deadline = time.monotonic()+5
        response = None
        while time.monotonic() < deadline:
            response = cli.read_json(bridge/'response.json')
            if response is not None and not (bridge/'request.json').exists():
                break
            time.sleep(.02)
        save('duplicate-response.json', response)
        if response != original:
            raise RuntimeError('duplicate did not replay exact original response')
        # Simulate public response loss; recover must use the immutable archive,
        # without publishing another request. The pending journal stays in place.
        (bridge/'response.json').unlink()
    finally:
        fcntl.flock(lock.fileno(), fcntl.LOCK_UN)
        lock.close()
    result = subprocess.run(command + ['--record', str(args.record_dir/'recover.jsonl'), 'recover'],
                            capture_output=True, text=True, timeout=10)
    save('recover-process.json', {'returncode':result.returncode, 'stdout':result.stdout,
                                 'stderr':result.stderr})
    if (result.returncode or json.loads(result.stdout) != original_output or
            (bridge/'request.json').exists() or (bridge/'client-pending.json').exists()):
        raise RuntimeError('archive recovery failed or redispatched')
    recovered_rows = [json.loads(line) for line in (args.record_dir/'recover.jsonl').read_text().splitlines()]
    recovered = next(row for row in recovered_rows if row['kind']=='response')
    if (recovered['response'] != original or recovered.get('response_source') != 'native_archive'
            or any(row['kind']=='request' for row in recovered_rows)):
        raise RuntimeError('original native archive recovery not established')
    summary = {'duplicate_replayed':True, 'lost_public_response_recovered':True,
               'prior_record_used':bool(args.prior_record_dir),
               'model_calls':0, 'scope':'detached native protocol probe, not action/browser comparison'}
    if args.quarantine_probe:
        unknown = {'id':str(uuid.uuid4()), 'command':'status', 'timeout_ms':5000,
                   'expires_unix_ms':time.time()*1000+5000}
        save('incomplete-claim-request.json', unknown)
        lock = cli.acquire_lock(str(bridge/'request.lock'), 5)
        try:
            if (bridge/'client-pending.json').exists() or (bridge/'request.json').exists():
                raise RuntimeError('another request exists before quarantine probe')
            marker = bridge/('native-request-'+unknown['id'].encode().hex().upper()+'.claim')
            fd = os.open(marker, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
            os.fsync(fd)
            os.close(fd)
            payload = json.dumps(unknown).encode()
            cli.atomic_write(str(bridge/'client-pending.json'), payload)
            cli.atomic_write(str(bridge/'request.json'), payload)
        finally:
            fcntl.flock(lock.fileno(), fcntl.LOCK_UN)
            lock.close()
        result = subprocess.run(command + ['--record', str(args.record_dir/'unknown-recover.jsonl'), 'recover'],
                                capture_output=True, text=True, timeout=10)
        response = cli.read_json(bridge/'response.json')
        save('unknown-result.json', {'returncode':result.returncode, 'stdout':result.stdout,
                                    'stderr':result.stderr, 'response':response})
        if (result.returncode != 2 or not isinstance(response, dict)
                or response.get('id') != unknown['id']
                or response.get('error') != 'request_outcome_unknown'
                or response.get('execution_settled') is not False
                or not (bridge/'client-pending.json').exists()):
            raise RuntimeError('incomplete claim did not remain unresolved/fenced')
        summary['incomplete_claim_fenced'] = True
        summary['mailbox_quarantined'] = True
    save('result.json', summary)
    print(json.dumps(summary))


if __name__ == '__main__':
    main()
