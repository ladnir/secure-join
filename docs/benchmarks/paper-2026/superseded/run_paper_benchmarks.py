#!/usr/bin/env python3
"""Reproducible, serial six-protocol sweep; resume only an identical executable.

All network trials use one OS thread per party. Public parameters are frozen
before execution. A round audit is a separate fresh-crypto execution whose
timing is never pooled with the real TCP measurements.
"""
import argparse, datetime, hashlib, json, os, pathlib, platform, subprocess, sys, time, uuid
from benchmark_logstar import trial, PROFILES
from benchmark_root_merge import ceil_root

ROOT=pathlib.Path(__file__).resolve().parents[1]
OUT=ROOT/'docs/benchmarks/paper-2026'
EXE=ROOT/'out/build/linux/frontend/paper_benchmark'
COMMON=['--bits','32','--batch-size','1048576','--concurrency','1']

def configurations(params):
    for item in params['sizes']:
        n=item['n']
        for shape,methods in [('balanced',['logstar','median','batcher','quick']),('cube',['cube','batcher','quick']),('sqrt',['sqrt','batcher','quick'])]:
            m=n if shape=='balanced' else ceil_root(n,3 if shape=='cube' else 2)
            for method in methods:
                opts=['--method',method,'--m',str(m),'--n',str(n),*COMMON]
                if method in item['selected']:opts+=item['selected'][method]['options']
                if method=='quick':opts+=['--terminal','8','--pivots','1']
                yield dict(shape=shape,method=method,m=m,n=n,options=opts)

def write_row(path,row):
    with path.open('a') as f:f.write(json.dumps(row)+'\n');f.flush()

def record_trial(config,profile,repeat,sha,timeout,audit=False):
    opts=[*config['options'],'--seed',str(1000+repeat)]
    if audit:opts+=['--audit-rounds']
    started=time.monotonic();rows=trial(EXE,opts,profile,timeout)
    for row in rows:
        if not row.get('verified') or not row.get('real_crypto'):raise RuntimeError('Unverified or mock run')
        if row.get('peak_sampled_swap_kib',0):raise RuntimeError('Paging observed; reject timing')
        if row.get('refills',0):raise RuntimeError('QuickSort reserve exhausted; record separately before changing configuration')
        if row.get('os_threads')!=1:raise RuntimeError('Incorrect thread setting')
    tid=uuid.uuid4().hex
    for row in rows:
        row.update(shape=config['shape'],profile=profile,repeat=repeat,trial_id=tid,
                   executable_sha256=sha,command=[str(EXE),*opts],utc=datetime.datetime.now(datetime.timezone.utc).isoformat())
    return rows,time.monotonic()-started

def validate(out):
    sha=hashlib.sha256(EXE.read_bytes()).hexdigest()
    for method in ['logstar','median','cube','sqrt','batcher','quick']:
        m,n=(256,256) if method in ['logstar','median','quick'] else (7,65)
        opts=['--method',method,'--m',str(m),'--n',str(n),'--batch-size','16384','--bits','32','--concurrency','1']
        if method=='median':opts+=['--base-case','8','--max-depth','1','--children','8','--cube-blocks','4','--leaf','allpairs']
        config=dict(shape='validation',method=method,m=m,n=n,options=opts)
        normal,_=record_trial(config,'local',0,sha,180)
        audit,_=record_trial(config,'local',0,sha,180,True)
        for key in ['offline_sent_bytes','online_sent_bytes','padded_ands']:
            if method!='quick' and normal[0][key]!=audit[0][key]:raise RuntimeError(f'Audit changed {method} {key}')
        if audit[0]['offline_rounds']<1 or audit[0]['online_rounds_measured']<1:raise RuntimeError('Missing round count')
        tcp,_=record_trial(config,'tcp',0,sha,180)
        if len(tcp)!=2 or {r['party'] for r in tcp}!={0,1}:raise RuntimeError('Unpaired endpoints')
        for phase in ['offline','online']:
            delta=sum(r[phase+'_sent_bytes'] for r in tcp)-normal[0][phase+'_sent_bytes']
            print('transport-byte-delta',method,phase,delta,flush=True)
            # TCP has already registered the root channel during the public
            # handshake. Local starts that framing inside preprocessing.
            if method!='quick' and abs(delta)>64:
                raise RuntimeError(f'Unexpected TCP payload change: {method} {phase} {delta}')
        for r in normal+audit+tcp:write_row(out,r)
        for pattern in ['equal','duplicates','max']:
            cfg=dict(config,options=[*opts,'--pattern',pattern])
            rows,_=record_trial(cfg,'local',1,sha,180)
            for r in rows:write_row(out,r)
        print('validated',method,'rounds',audit[0]['offline_rounds'],audit[0]['online_rounds_measured'],flush=True)

def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--validate',action='store_true');ap.add_argument('--parameters',type=pathlib.Path,default=OUT/'parameters.json')
    ap.add_argument('--out',type=pathlib.Path,default=OUT/'measurements.jsonl');ap.add_argument('--profiles',default='local,lan,wan')
    ap.add_argument('--min-exp',type=int,default=8);ap.add_argument('--max-exp',type=int,default=20)
    ap.add_argument('--trials',type=int,default=3);ap.add_argument('--large-trials',type=int,default=1)
    ap.add_argument('--timeout',type=int,default=7200);ap.add_argument('--audit-only',action='store_true')
    ap.add_argument('--skip-audits',action='store_true');a=ap.parse_args();a.out.parent.mkdir(parents=True,exist_ok=True)
    if a.validate:validate(a.out);return
    params=json.loads(a.parameters.read_text());configs=list(configurations(params))
    configs=[c for c in configs if (1<<a.min_exp)<=c['n']<=(1<<a.max_exp)]
    if len(configs)!=(a.max_exp-a.min_exp+1)*10:raise RuntimeError('Incomplete parameter file')
    sha=hashlib.sha256(EXE.read_bytes()).hexdigest();paramsha=hashlib.sha256(a.parameters.read_bytes()).hexdigest()
    previous=[json.loads(l) for l in a.out.read_text().splitlines()] if a.out.exists() else []
    for r in previous:
        if r.get('executable_sha256')!=sha:raise RuntimeError('Changed executable on resume')
        if r.get('type')=='environment' and r['parameters_sha256']!=paramsha:raise RuntimeError('Changed parameters on resume')
    grouped={}
    for r in previous:
        if r.get('verified'):grouped.setdefault((r['type'],r['shape'],r['method'],r['n'],r['profile'],r['repeat']),[]).append(r)
    done={k for k,v in grouped.items() if (v[0]['party']==-1 and len(v)==1) or (len(v)==2 and {r['party'] for r in v}=={0,1})}
    meta=dict(type='environment',executable_sha256=sha,parameters_sha256=paramsha,platform=platform.platform(),
              cpu_model=next(l.split(':',1)[1].strip() for l in pathlib.Path('/proc/cpuinfo').read_text().splitlines() if l.startswith('model name')),
              meminfo=pathlib.Path('/proc/meminfo').read_text(),profiles={p:PROFILES.get(p) for p in a.profiles.split(',')},
              trials=a.trials,large_trials=a.large_trials,large_from_exp=17,argv=sys.argv,utc=datetime.datetime.now(datetime.timezone.utc).isoformat())
    write_row(a.out,meta)
    for profile in (['local-audit'] if a.audit_only else ([] if a.skip_audits else ['local-audit'])+a.profiles.split(',')):
        for exp in range(a.min_exp,a.max_exp+1):
            count=1 if profile=='local-audit' else a.trials if exp<=16 else a.large_trials
            selected=[c for c in configs if c['n']==1<<exp]
            for repeat in range(count):
                order=selected[repeat%len(selected):]+selected[:repeat%len(selected)]
                if repeat%2:order=list(reversed(order))
                for c in order:
                    audit=profile=='local-audit';transport='local' if audit else profile
                    key=('round_audit' if audit else 'benchmark',c['shape'],c['method'],c['n'],transport,repeat)
                    if key in done:continue
                    print('START',profile,exp,c['shape'],c['method'],repeat,flush=True)
                    try:rows,elapsed=record_trial(c,transport,repeat,sha,a.timeout,audit)
                    except Exception as error:
                        write_row(a.out,dict(type='failure',executable_sha256=sha,shape=c['shape'],method=c['method'],n=c['n'],profile=transport,repeat=repeat,error=str(error)))
                        raise
                    for r in rows:write_row(a.out,r)
                    print('DONE',profile,exp,c['shape'],c['method'],repeat,'elapsed_s',round(elapsed,2),
                          'offline_ms',max(r['offline_ms'] for r in rows),'online_ms',max(r['online_ms'] for r in rows),
                          'rss_MiB',round(max(r['peak_sampled_rss_kib'] for r in rows)/1024,1),flush=True)

if __name__=='__main__':main()
