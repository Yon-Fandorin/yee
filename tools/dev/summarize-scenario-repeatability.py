#!/usr/bin/env python3
"""Summarize independently audited cohorts without selecting successful repeats.

Fixture seeds are not model RNG seeds. With two observations, report the range
and maximum, never a purported p95 or fixed-task variance.
"""
import argparse
import hashlib
import json
import math
from pathlib import Path
import statistics


def distribution(values):
    if not values:
        return {'n': 0, 'median': None, 'minimum': None, 'maximum': None, 'range': None}
    return {'n': len(values), 'median': statistics.median(values),
            'minimum': min(values), 'maximum': max(values), 'range': max(values)-min(values)}


def summarize(rows):
    identities=set()
    samples=[]
    setup=[]
    freezes=set()
    for row in rows:
        key=row['case']
        if key in identities:
            raise ValueError('duplicate sample: '+key)
        identities.add(key)
        if row.get('collection_status')=='no_model_sample':
            setup.append(row)
            continue
        freeze=row['runtime_fingerprint']
        freezes.add(freeze)
        seconds=row.get('zero_wait_seconds')
        tokens=(row.get('usage') or {}).get('total_tokens')
        if type(seconds) not in (int,float) or not math.isfinite(seconds) or seconds<0:
            raise ValueError('missing/invalid zero-wait time: '+key)
        if type(tokens) is not int or tokens<0:
            raise ValueError('unknown tokens must not become zero: '+key)
        samples.append(row)
    if len(freezes)>1:
        raise ValueError('mixed runtime/native sources; summarize each checkpoint separately')
    groups={}
    for scenario in sorted({s['scenario'] for s in samples}):
        group={}
        for browser in ('yee','aside'):
            selected=[s for s in samples if s['scenario']==scenario and s['browser']==browser]
            group[browser]={
                'samples':len(selected),'accepted':sum(s['accepted'] is True for s in selected),
                'seeds':[s['fixture_seed'] for s in selected],
                'seconds':distribution([s['zero_wait_seconds'] for s in selected]),
                'tokens':distribution([s['usage']['total_tokens'] for s in selected]),
                'mcp_calls':distribution([s['correlation']['mcp_calls'] for s in selected if 'correlation' in s]),
                'failures':[{'case':s['case'],'failures':s['failures']} for s in selected if not s['accepted']],
            }
        group['all_samples_accepted_in_both']=all(group[b]['samples'] and
            group[b]['accepted']==group[b]['samples'] for b in ('yee','aside'))
        groups[scenario]=group
    totals={}
    for browser in ('yee','aside'):
        selected=[s for s in samples if s['browser']==browser]
        totals[browser]={'samples':len(selected),'accepted':sum(s['accepted'] is True for s in selected),
            'seconds_including_failures':sum(s['zero_wait_seconds'] for s in selected),
            'tokens_including_failures':sum(s['usage']['total_tokens'] for s in selected)}
    return {'schema':'yee.scenario-repeatability.v1','runtime_fingerprint':next(iter(freezes),None),
        'groups':groups,'totals':totals,'setup_failures':setup,'samples':samples,
        'limits':['Fixture seeds change tasks; they do not seed the model.',
                  'All model failures retain time and token cost. Setup failures are separate.',
                  'Two observations do not establish p95 or statistical superiority.',
                  'Approval wait contributes zero comparison seconds; approval counts require separate evidence.']}


def load(ledgers):
    rows=[]
    for ledger in ledgers:
        for value in json.loads(ledger.read_text()):
            row=dict(value)
            root=Path(row['case']).parent.parent
            manifest=root/'runtime-native-freeze.json'
            if not manifest.exists():
                raise ValueError('complete runtime/native freeze required: '+str(root))
            normalized=json.dumps(json.loads(manifest.read_text()),sort_keys=True,separators=(',',':'))
            row['runtime_fingerprint']=hashlib.sha256(normalized.encode()).hexdigest()
            row['fixture_seed']=json.loads((root/'plan.json').read_text())['seed']
            row['ledger_sha256']=hashlib.sha256(ledger.read_bytes()).hexdigest()
            rows.append(row)
    return rows


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('ledgers',nargs='+',type=Path);p.add_argument('--output',type=Path,required=True)
    args=p.parse_args();value=summarize(load(args.ledgers))
    args.output.write_text(json.dumps(value,ensure_ascii=False,indent=2)+'\n')
    print(json.dumps(value['totals']))


if __name__=='__main__':main()
