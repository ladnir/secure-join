#!/usr/bin/env python3
"""Freeze public per-size candidates for the two paper experiments.

No secret data, runtime samples or network timings influence this search.
The selected online objective is bytes + weight * dependent rounds.
"""
import argparse, hashlib, itertools, json, math, pathlib, subprocess
from benchmark_root_merge import ceil_root, payload

ROOT=pathlib.Path(__file__).resolve().parents[1]
BIN=ROOT/'out/build/linux/frontend'
COMMON=['--bits','32','--batch-size','1048576','--concurrency','2']

def plan(exe,args):
    cmd=[str(BIN/exe),*args,*COMMON,'--plan']
    p=subprocess.run(cmd,text=True,capture_output=True,timeout=180)
    if p.returncode: return dict(command=cmd,error=p.stderr[-1000:])
    r=json.loads(p.stdout);r['command']=cmd;return r

def score(p,weight):
    return (p['payload']+weight*p['rounds'],p['payload'],p['rounds'])

def main():
    ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('--out',type=pathlib.Path,required=True)
    ap.add_argument('--min-exp',type=int,default=8);ap.add_argument('--max-exp',type=int,default=20)
    ap.add_argument('--round-weight',type=float,default=250000,
                    help='Fixed byte-equivalent charge per dependent round; not a fitted runtime model')
    a=ap.parse_args();a.out.parent.mkdir(parents=True,exist_ok=True)
    if a.out.exists():raise RuntimeError('Choose a fresh output')
    result=dict(key_bits=32,round_weight=a.round_weight,selection='Public payload + round weight * bound; same settings on all networks',
                planner_hashes={name:hashlib.sha256((BIN/name).read_bytes()).hexdigest() for name in ['logstar','pimedian','rootmerge']},sizes=[])
    for e in range(a.min_exp,a.max_exp+1):
        n=1<<e;row=dict(n=n,candidates={},selected={})
        logs=[]
        for b in [2,4,8,16]:
            r=plan('logstar',['--n',str(n),'--base',str(b),'--block',str(b)])
            if 'error' in r or 'online_payload_bytes' not in r:continue
            r.update(payload=r['online_payload_bytes'],rounds=r['round_bound'],options=['--block',str(b)])
            logs.append(r)
        row['candidates']['logstar']=logs;row['selected']['logstar']=min(logs,key=lambda r:score(r,a.round_weight))
        med=[];seen=set()
        # One and two alignment levels; independently select median count,
        # asymmetric block size, leaf width and leaf algorithm. Require at
        # least one alignment level, so Median cannot silently become Batcher.
        shapes=[]
        for ce in sorted({max(1,min(e-1,e//2+d)) for d in [0,1,2]}|{max(1,min(e-1,(2*e)//3+d)) for d in [-1,0,1]}):
            shapes.append([1<<ce])
            for le in [2,3,4,5]:
                if le<ce:shapes.append([1<<ce,1<<le])
        for children in shapes:
            k0=n//children[0]
            # Balances k*n/b against k^2*b/2 in the cube-count primitive.
            ideal=max(1,int(round(math.log2(math.sqrt(2*n/k0)))))
            for shift in [-1,0,1]:
                blocks=[min(n,1<<max(0,ideal+shift))]
                if len(children)>1:
                    parent=children[0];k=parent//children[1]
                    blocks.append(min(parent,1<<max(0,int(round(math.log2(math.sqrt(2*parent/k)))))))
                for leaf in ['batcher','allpairs']:
                    if leaf=='allpairs' and children[-1]>64:continue
                    sig=(tuple(children),tuple(blocks),leaf)
                    if sig in seen:continue
                    seen.add(sig)
                    opts=['--base-case',str(children[-1]),'--max-depth',str(len(children)),
                          '--children',','.join(map(str,children)),'--cube-blocks',','.join(map(str,blocks)),'--leaf',leaf]
                    r=plan('pimedian',['--n',str(n),*opts])
                    if 'error' in r:continue
                    r.update(payload=r['online_payload_bytes'],rounds=r['online_round_bound'],options=opts)
                    med.append(r)
        row['candidates']['median']=med;row['selected']['median']=min(med,key=lambda r:score(r,a.round_weight))
        for method,degree in [('cube',3),('sqrt',2)]:
            m=ceil_root(n,degree);default=1<<(m-1).bit_length();roots=[]
            for b in sorted({max(1,min(n,default*scale//4)) for scale in [1,2,4,8,16]}):
                r=plan('rootmerge',['--method',method,'--m',str(m),'--n',str(n),'--block',str(b)])
                if 'error' in r:continue
                r.update(payload=payload(r),rounds=r['online_round_bound'],options=['--block',str(b)])
                roots.append(r)
            row['candidates'][method]=roots;row['selected'][method]=min(roots,key=lambda r:score(r,a.round_weight))
        result['sizes'].append(row)
        a.out.write_text(json.dumps(result,indent=2)+'\n')
        print(e,{k:(v['payload'],v['rounds'],v['options']) for k,v in row['selected'].items()},flush=True)

if __name__=='__main__':main()
