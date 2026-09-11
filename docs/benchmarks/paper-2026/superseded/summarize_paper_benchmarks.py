#!/usr/bin/env python3
"""Validate raw pairs, then generate the paper's figures and complete tables."""
import argparse, collections, csv, hashlib, json, pathlib, statistics
from benchmark_root_merge import ceil_root

ROOT=pathlib.Path(__file__).resolve().parents[1]
NAMES={'logstar':'Logstar','median':'Median','batcher':'Batcher','quick':'Shuffled quicksort','cube':'CubeRootMerge','sqrt':'SquareRootMerge'}
SHORT={'logstar':'Logstar','median':'Median','batcher':'Batcher','quick':'QuickSort','cube':'CubeRoot','sqrt':'SquareRoot'}
COLORS={'logstar':'#176b6b','median':'#8d4ba8','batcher':'#52677d','quick':'#cb7638','cube':'#176b6b','sqrt':'#8d4ba8'}
MARKERS={'logstar':'o','median':'s','batcher':'^','quick':'D','cube':'o','sqrt':'s'}
PROFILES=['local','lan','wan'];PNAMES={'local':'Local','lan':'LAN','wan':'WAN'}
METHODS={'balanced':['logstar','median','batcher','quick'],'cube':['cube','batcher','quick'],'sqrt':['sqrt','batcher','quick']}
SHAPES={'balanced':'Equal-length','cube':'Cube-root','sqrt':'Square-root'}

def summarize(paths,complete=True,parameters=None):
    records=[json.loads(line) for path in paths for line in path.read_text().splitlines()]
    hashes={r['executable_sha256'] for r in records if r['type'] in ['benchmark','round_audit']}
    if len(hashes)!=1:raise ValueError('Expected exactly one executable hash')
    raw=collections.defaultdict(list);audits={}
    parameters=parameters or ROOT/'docs/benchmarks/paper-2026/parameters.json'
    plans=json.loads(parameters.read_text())
    paramsha=hashlib.sha256(parameters.read_bytes()).hexdigest()
    if any(r.get('parameters_sha256')!=paramsha for r in records if r['type']=='environment'):
        raise ValueError('Parameter file does not match the recorded experiment')
    selected={(s['n'],method):p for s in plans['sizes'] for method,p in s['selected'].items()}
    for r in records:
        if r['type'] in ['benchmark','round_audit']:
            expected_m=r['n'] if r['shape']=='balanced' else ceil_root(r['n'],3 if r['shape']=='cube' else 2)
            if r['shape'] not in METHODS or r['method'] not in METHODS[r['shape']] or r['m']!=expected_m:
                raise ValueError('Incorrect input shape or protocol')
            if r['key_bits']!=32 or r['os_threads']!=1 or r['correlation_concurrency']!=1 or r['batch_size']!=1<<20:
                raise ValueError('Inconsistent implementation settings')
        if r['type']=='round_audit':
            if (not r['verified'] or not r['real_crypto'] or r.get('refills',0)
                or r.get('peak_sampled_swap_kib',0) or r['offline_rounds']<1 or r['online_rounds_measured']<1):
                raise ValueError('Invalid audit')
            key=(r['shape'],r['method'],r['n'])
            if key in audits:raise ValueError('Duplicate audit')
            audits[key]=r
        elif r['type']=='benchmark':raw[r['trial_id']].append(r)
    groups=collections.defaultdict(list)
    for tid,rows in raw.items():
        first=rows[0];local=first['party']==-1
        if (local and len(rows)!=1) or (not local and (len(rows)!=2 or {r['party'] for r in rows}!={0,1})):raise ValueError('Incomplete party pair '+tid)
        if any(not r['verified'] or not r['real_crypto'] or r.get('refills',0) or r.get('peak_sampled_swap_kib',0) for r in rows):raise ValueError('Invalid measurement')
        if len({(r['method'],r['n'],r['m'],r['seed'],r['shape'],r['profile'],r['repeat']) for r in rows})!=1:raise ValueError('Mismatched endpoint metadata')
        if len({r['online_round_bound'] for r in rows})!=1:raise ValueError('Round counters disagree')
        expected=selected.get((first['n'],first['method']))
        if expected:
            ands=expected.get('padded_ands',expected.get('online_gmw_ands_padded'))
            if any(r['padded_ands']!=ands or r['online_round_bound']!=expected['rounds'] for r in rows):
                raise ValueError('Execution differs from the selected public circuit')
            if sum(r['online_sent_bytes'] for r in rows)<expected['payload']:
                raise ValueError('Measured bytes smaller than the application payload')
        sample=dict(repeat=first['repeat'],online_rounds=first['online_round_bound'],
                    peak_rss_kib=max(r['peak_rss_kib'] for r in rows))
        for phase in ['offline','online']:
            sample[phase+'_bytes']=sum(r[phase+'_sent_bytes'] for r in rows)
            sample[phase+'_seconds']=max(r[phase+'_ms'] for r in rows)/1000
        sample['total_seconds']=sample['offline_seconds']+sample['online_seconds']
        groups[(first['shape'],first['method'],first['n'],first['m'],first['profile'])].append(sample)
    result=[]
    for (shape,method,n,m,profile),samples in sorted(groups.items()):
        if len({s['repeat'] for s in samples})!=len(samples):raise ValueError('Duplicate repeat')
        r=dict(shape=shape,method=method,n=n,m=m,profile=profile,trials=len(samples),executable_sha256=next(iter(hashes)))
        for field in samples[0]:
            if field=='repeat':continue
            vals=[s[field] for s in samples];r[field]=statistics.median(vals);r[field+'_min']=min(vals);r[field+'_max']=max(vals)
        audit=audits.get((shape,method,n));r['offline_rounds']=audit['offline_rounds'] if audit else None
        r['audit_online_rounds']=audit['online_rounds_measured'] if audit else None
        if method!='quick' and audit and abs(r['online_bytes']-audit['online_sent_bytes'])>64:raise ValueError('Audit byte mismatch')
        result.append(r)
    if complete:
        expected={(shape,method,1<<e,p) for shape,methods in METHODS.items() for method in methods for e in range(8,21) for p in PROFILES}
        found={(r['shape'],r['method'],r['n'],r['profile']) for r in result}
        if expected!=found:raise ValueError(f'Missing {len(expected-found)} configurations; extra {len(found-expected)}')
        if len(audits)!=130:raise ValueError(f'Expected 130 audits, found {len(audits)}')
        for r in result:
            if r['trials']!=(3 if r['n']<=1<<16 else 1):raise ValueError('Incorrect repeat count')
    return result

def texnum(x,decimals=2):
    if x is None:return '--'
    if decimals==3 and 0<x<0.001:return r'$<0.001$'
    return f'{x:,.{decimals}f}'.replace(',','{,}')

def plots(rows,out):
    import matplotlib
    matplotlib.use('Agg')
    import matplotlib.pyplot as plt
    plt.rcParams.update({'font.family':'DejaVu Sans','font.size':9,'axes.spines.top':False,'axes.spines.right':False,
                         'pdf.fonttype':42,'ps.fonttype':42,'axes.titleweight':'bold','axes.labelcolor':'#243746',
                         'xtick.color':'#34495e','ytick.color':'#34495e','savefig.facecolor':'white'})
    for shape,methods in METHODS.items():
        for runtime in [False,True]:
            fig,axs=plt.subplots(2,3,figsize=(10.7,5.4),sharex=True)
            fields=['online_seconds','offline_seconds'] if runtime else ['online_bytes','online_rounds']
            labels=['Online time (s)','Offline time (s)'] if runtime else ['Online communication (MiB)','Online rounds (bound)']
            for j,profile in enumerate(PROFILES):
                for method in methods:
                    series=sorted((r for r in rows if r['shape']==shape and r['method']==method and r['profile']==profile),key=lambda r:r['n'])
                    for i,field in enumerate(fields):
                        xs=[r['n'].bit_length()-1 for r in series];ys=[r[field]/(1<<20) if field.endswith('_bytes') else r[field] for r in series]
                        axs[i,j].plot(xs,ys,color=COLORS[method],marker=MARKERS[method],ms=3,lw=1.5,label=NAMES[method])
                        if runtime:
                            axs[i,j].fill_between(xs,[r[field+'_min'] for r in series],[r[field+'_max'] for r in series],color=COLORS[method],alpha=.12)
                axs[0,j].set_title(PNAMES[profile]);axs[1,j].set_xlabel(r'Input size $n=2^k$ (exponent $k$)')
                for i in range(2):
                    ax=axs[i,j];ax.set_xlim(7.7,20.3);ax.set_xticks([8,10,12,14,16,18,20]);ax.grid(axis='y',alpha=.18)
                    if runtime or i==0:ax.set_yscale('log')
                    if j==0:ax.set_ylabel(labels[i])
            handles,labels=axs[0,0].get_legend_handles_labels();fig.legend(handles,labels,loc='upper center',ncol=len(methods),frameon=False,bbox_to_anchor=(.5,1.015))
            fig.tight_layout(rect=[0,0,1,.94]);stem=f'{shape}-'+('runtime' if runtime else 'online')
            fig.savefig(out/(stem+'.pdf'),bbox_inches='tight');fig.savefig(out/(stem+'.png'),dpi=180,bbox_inches='tight');plt.close(fig)

def tables(rows,out):
    header=r'''\begin{longtable}{llrrrrrr}
\caption{%s}\\
\hline
$n$ & Protocol & \multicolumn{3}{c}{Offline} & \multicolumn{3}{c}{Online}\\
 & & MiB & Rounds & Seconds & MiB & Rounds & Seconds\\\hline
\endfirsthead
\hline $n$ & Protocol & \multicolumn{3}{c}{Offline} & \multicolumn{3}{c}{Online}\\
 & & MiB & Rounds & Seconds & MiB & Rounds & Seconds\\\hline
\endhead
'''
    alltex=[]
    for shape,methods in METHODS.items():
        for profile in PROFILES:
            title=f'{SHAPES[shape]} inputs, {PNAMES[profile]}. Communication is total sent by both parties.'
            lines=[header%title]
            subset=sorted((r for r in rows if r['shape']==shape and r['profile']==profile),key=lambda r:(r['n'],methods.index(r['method'])))
            for r in subset:
                lines.append(f"$2^{{{r['n'].bit_length()-1}}}$ & {SHORT[r['method']]} & "+' & '.join([
                    texnum(r['offline_bytes']/(1<<20)),texnum(r['offline_rounds'],0),texnum(r['offline_seconds'],3),
                    texnum(r['online_bytes']/(1<<20)),texnum(r['online_rounds'],0),texnum(r['online_seconds'],3)])+r' \\'+ '\n')
            lines.append('\\hline\n\\end{longtable}\n')
            text=''.join(lines);(out/f'{shape}-{profile}-table.tex').write_text(text);alltex.append(text+'\\clearpage\n')
    (out/'all-tables.tex').write_text(''.join(alltex))
    for shape,methods in METHODS.items():
        lines=[r'\begin{tabular}{llrrrrrr}'+'\n'+r'\hline'+'\n'+r'Link & Protocol & \multicolumn{3}{c}{Offline} & \multicolumn{3}{c}{Online}\\'+'\n'+r' & & MiB & Rounds & Seconds & MiB & Rounds & Seconds\\\hline'+'\n']
        for profile in PROFILES:
            for method in methods:
                matches=[r for r in rows if r['shape']==shape and r['profile']==profile and r['method']==method and r['n']==1<<20]
                if not matches:continue
                r=matches[0];lines.append(PNAMES[profile]+' & '+SHORT[method]+' & '+' & '.join([
                    texnum(r['offline_bytes']/(1<<20),1),texnum(r['offline_rounds'],0),texnum(r['offline_seconds'],2),
                    texnum(r['online_bytes']/(1<<20),1),texnum(r['online_rounds'],0),texnum(r['online_seconds'],2)])+r' \\'+ '\n')
            lines.append('\\hline\n')
        lines.append('\\end{tabular}\n');(out/f'{shape}-main-table.tex').write_text(''.join(lines))

def main():
    ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('raw',type=pathlib.Path,nargs='+');ap.add_argument('--out',type=pathlib.Path,required=True);ap.add_argument('--allow-incomplete',action='store_true')
    ap.add_argument('--parameters',type=pathlib.Path,default=ROOT/'docs/benchmarks/paper-2026/parameters.json');a=ap.parse_args()
    rows=summarize(a.raw,not a.allow_incomplete,a.parameters);a.out.mkdir(parents=True,exist_ok=True)
    (a.out/'summary.json').write_text(json.dumps(rows,indent=2)+'\n')
    with (a.out/'all-results.csv').open('w',newline='') as f:
        writer=csv.DictWriter(f,fieldnames=list(rows[0]));writer.writeheader();writer.writerows(rows)
    plots(rows,a.out);tables(rows,a.out);print('Verified and summarized',len(rows),'configurations')

if __name__=='__main__':main()
