#!/usr/bin/env python3
"""Explore Median's existing public parameters; never execute other methods.

A comparison/traffic proxy screens recursion shapes. The compiled public planner
then supplies all reported payload and dependency bounds. No proxy value is
published as a protocol cost. Original benchmark records remain immutable.
"""
import argparse, hashlib, itertools, json, math, pathlib, subprocess, time

ROOT=pathlib.Path(__file__).resolve().parents[1]
RAW=ROOT/'docs/benchmarks/paper-2026'
EXE=ROOT/'out/build/linux/frontend/pimedian'

def blocks_for(children,n):
    result=[]; parent=n
    for child in children:
        k=parent//child
        candidates=[1<<x for x in range(parent.bit_length())]
        result.append(min(candidates,key=lambda b:k*(parent//b-1)+k*(k+1)*b//2))
        parent=child
    return result

def proxy(n,children,blocks,leaf):
    batches=1;parent=n;cost=0
    for child,block in zip(children,blocks):
        k=parent//child
        comps=2*batches*(k*(parent//block-1)+k*(k+1)*block//2)
        cost+=33*comps+70*batches*parent
        batches*=2*k;parent=child
    expanded=2*batches*parent
    if leaf=='allpairs':cost+=33*batches*parent*parent+20*expanded
    else:cost+=batches*(parent*(parent.bit_length()-1)+1)*(65+1.5*parent.bit_length()+0.5*n.bit_length())
    return cost+30*expanded

def options(children,blocks,leaf):
    return ['--base-case',str(children[-1]),'--max-depth',str(len(children)),
            '--children',','.join(map(str,children)),'--cube-blocks',','.join(map(str,blocks)),'--leaf',leaf]

def frontier(plans):
    out=[]
    for p in sorted(plans,key=lambda p:(p['rounds'],p['payload'])):
        if not out or p['payload']<out[-1]['payload']:out.append(p)
    return out

def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--min-exp',type=int,default=8);ap.add_argument('--max-exp',type=int,default=20)
    ap.add_argument('--out',type=pathlib.Path,required=True);ap.add_argument('--top-shapes',type=int,default=3)
    a=ap.parse_args();a.out.mkdir(parents=True,exist_ok=True)
    original=json.loads((RAW/'parameters.json').read_text())
    summary=json.loads((ROOT.parent/'64c0aeaf1c1f5473b45f1e06/plots/benchmark/summary.json').read_text())
    cache=a.out/'plans.jsonl';known={}
    if cache.exists():
        for line in cache.read_text().splitlines():
            p=json.loads(line);known[(p['n'],tuple(p['options']))]=p
    sha=hashlib.sha256(EXE.read_bytes()).hexdigest();report=[]
    if any(p.get('planner_sha256') != sha for p in known.values()):
        raise RuntimeError('Changed public planner on resume; use a fresh output directory')
    for e in range(a.min_exp,a.max_exp+1):
        n=1<<e;old=next(s for s in original['sizes'] if s['n']==n)
        baseline=next(r for r in summary if r['shape']=='balanced' and r['method']=='batcher' and r['n']==n and r['profile']=='local')
        requests={tuple(p['options']) for p in old['candidates']['median']}
        # All decreasing power-of-two recursion shapes up to four levels are
        # considered by the screening proxy. Keep three per depth/leaf size.
        screened=0
        for depth in range(1,min(4,e-1)+1):
            groups={}
            for exponents in itertools.combinations(range(1,e),depth):
                children=[1<<x for x in reversed(exponents)];blocks=blocks_for(children,n)
                for leaf in ['batcher','allpairs']:
                    if leaf=='allpairs' and children[-1]>64:continue
                    score=proxy(n,children,blocks,leaf);screened+=1
                    if score>baseline['online_bytes']*8:continue
                    groups.setdefault((leaf,children[-1]),[]).append((score,children,blocks))
            for (leaf,_),shapes in groups.items():
                for _,children,blocks in sorted(shapes)[:a.top_shapes]:
                    requests.add(tuple(options(children,blocks,leaf)))
        plans=[];start=time.monotonic()
        for i,opts in enumerate(sorted(requests)):
            key=(n,opts)
            if key in known:p=known[key]
            else:
                cmd=[str(EXE),'--n',str(n),'--bits','32','--batch-size','1048576','--concurrency','2',*opts,'--plan']
                run=subprocess.run(cmd,text=True,capture_output=True,timeout=180)
                if run.returncode:
                    p=dict(n=n,options=list(opts),error=run.stderr[-2000:],planner_sha256=sha)
                else:
                    p=json.loads(run.stdout);p.update(options=list(opts),command=cmd,planner_sha256=sha,
                        payload=p['online_payload_bytes'],rounds=p['online_round_bound'])
                with cache.open('a') as f:f.write(json.dumps(p)+'\n')
                known[key]=p
            if 'error' not in p:plans.append(p)
            if (i+1)%50==0:print('PROGRESS',e,i+1,len(requests),round(time.monotonic()-start,1),flush=True)
        # Probe all independent cube-block exponents around each frontier
        # schedule, and the alternate network leaves, with real public costs.
        extra=set()
        for p in frontier(plans):
            children=[l['child_size'] for l in p['levels']];blocks=[l['cube_block'] for l in p['levels']]
            for i in range(len(blocks)):
                for shift in [-2,-1,1,2]:
                    trial=blocks.copy();trial[i]=max(1,blocks[i]*2**shift)
                    if not isinstance(trial[i],int):trial[i]=int(trial[i])
                    if trial[i]>(n if i==0 else children[i-1]):continue
                    extra.add(tuple(options(children,trial,p['leaf'])))
            if p['leaf']=='batcher':extra.add(tuple(options(children,blocks,'bitonic')))
        for opts in sorted(extra):
            key=(n,opts)
            if key in known:p=known[key]
            else:
                cmd=[str(EXE),'--n',str(n),'--bits','32','--batch-size','1048576','--concurrency','2',*opts,'--plan']
                run=subprocess.run(cmd,text=True,capture_output=True,timeout=180)
                if run.returncode:p=dict(n=n,options=list(opts),error=run.stderr[-2000:],planner_sha256=sha)
                else:
                    p=json.loads(run.stdout);p.update(options=list(opts),command=cmd,planner_sha256=sha,payload=p['online_payload_bytes'],rounds=p['online_round_bound'])
                with cache.open('a') as f:f.write(json.dumps(p)+'\n')
                known[key]=p
            if 'error' not in p:plans.append(p)
        best=frontier(plans)
        row=dict(n=n,proxy_shapes=screened,exact_candidates=len({tuple(p['options']) for p in plans}),
            baseline_bytes=baseline['online_bytes'],baseline_rounds=baseline['online_rounds'],frontier=best,
            planner_sha256=sha,method='Proxy-screen all power-of-two schedules up to four levels, exact-plan three per depth/leaf size plus original candidates and frontier block/leaf variants; not global optimality')
        report.append(row);(a.out/'frontier.json').write_text(json.dumps(report,indent=2)+'\n')
        print('FRONTIER',e,[(p['rounds'],round(p['payload']/2**20,2),round(p['payload']/baseline['online_bytes'],2),p['options']) for p in best],flush=True)

if __name__=='__main__':main()
