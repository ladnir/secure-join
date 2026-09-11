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

def summarize(paths,complete=True,parameters=None,pooled_out=None):
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
            if (r['key_bits']!=32 or r['os_threads']!=1 or r['correlation_concurrency']!=2
                or r['batch_size']!=1<<20 or r.get('transport_revision')!='separate-streams'):
                raise ValueError('Inconsistent implementation settings')
        if r['type']=='round_audit':
            if (not r['verified'] or not r['real_crypto'] or r.get('refills',0)
                or (r.get('peak_sampled_swap_kib',0) and not r.get('audit_paging_allowed',False))
                or r['offline_rounds']<1 or r['online_rounds_measured']<1):
                raise ValueError('Invalid audit')
            if r['online_rounds_measured']>r['online_round_bound']:
                raise ValueError('Measured online waves exceed the reported dependency bound')
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
                    setup_seconds=max(r['setup_ms'] for r in rows)/1000,
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
            if r['offline_rounds'] is None:raise ValueError('Missing audit for a measured configuration')
    # Network conditions affect time, not protocol depth. Pool individual
    # executions (never medians of profile medians) for the common cost plots.
    cost_groups=collections.defaultdict(list)
    for (shape,method,n,m,profile),samples in groups.items():
        cost_groups[(shape,method,n,m)].extend(samples)
    pooled=[]
    for (shape,method,n,m),samples in sorted(cost_groups.items()):
        if method!='quick' and any(len({s[field] for s in samples})!=1
                                  for field in ['online_bytes','online_rounds']):
            raise ValueError('Deterministic protocol cost varies across executions or transports')
        cost=dict(shape=shape,method=method,n=n,m=m,samples=len(samples),
                  executable_sha256=next(iter(hashes)))
        for field in ['offline_bytes','online_bytes','online_rounds']:
            values=[sample[field] for sample in samples]
            cost[field]=statistics.median(values)
            cost[field+'_min']=min(values);cost[field+'_max']=max(values)
        audit=audits.get((shape,method,n))
        cost['offline_rounds']=audit['offline_rounds'] if audit else None
        pooled.append(cost)
    if pooled_out is not None:pooled_out.extend(pooled)
    return result

def load_paper_study(median_manifest=None):
    """Validate the frozen study and an optional complete Median replacement.

    Each rerun dataset retains its own immutable parameter hash. Separate
    datasets may cover disjoint input sizes, as when a public schedule is
    revised after a memory-limited attempt. A supplied manifest can be checked
    before writing the activation file; failed/incomplete runs never activate.
    """
    raw = ROOT / 'docs/benchmarks/paper-2026'
    costs = []
    rows = summarize([raw / 'measurements.jsonl'], complete=True, pooled_out=costs)
    params = json.loads((raw / 'parameters.json').read_text())
    active = raw / 'active-median-revision.json'
    if median_manifest is None:
        if not active.exists():
            return rows, costs, params, None
        manifest = json.loads(active.read_text())
    else:
        manifest = median_manifest
    folder = (raw / manifest['directory']).resolve()
    if raw.resolve() not in folder.parents:
        raise ValueError('Median revision must stay inside the study directory')
    datasets = manifest.get('datasets', [dict(measurements='measurements.jsonl', parameters='parameters.json')])
    required = {spec[key] for spec in datasets for key in ('measurements', 'parameters')}
    if not required <= manifest['sha256'].keys():
        raise ValueError('Every rerun data and parameter file needs a manifest hash')
    for name, digest in manifest['sha256'].items():
        path = (folder / name).resolve()
        if folder not in path.parents or hashlib.sha256(path.read_bytes()).hexdigest() != digest:
            raise ValueError('Median revision artifact hash mismatch: ' + name)
    revised = []; revised_costs = []; records = []; schedules = {}
    original_sizes = {s['n']: s for s in params['sizes']}
    for spec in datasets:
        data_path = folder / spec['measurements']; parameter_path = folder / spec['parameters']
        piece_records = [json.loads(line) for line in data_path.read_text().splitlines()]
        observations = [r for r in piece_records if r['type'] in ('benchmark', 'round_audit')]
        if not observations or any(r['method'] != 'median' or r['shape'] != 'balanced' for r in observations):
            raise ValueError('A Median dataset is empty or contains another protocol')
        environments = [r for r in piece_records if r['type'] == 'environment']
        if not environments or any(int(r['meminfo'].splitlines()[0].split()[1]) < 25 * 1024**2 for r in environments):
            raise ValueError('The Median revision did not use the matching Linux memory cap')
        piece_params = json.loads(parameter_path.read_text())
        updated_sizes = {s['n']: s for s in piece_params['sizes']}
        if len(piece_params['sizes']) != len(original_sizes) or updated_sizes.keys() != original_sizes.keys():
            raise ValueError('Median revision changed the input-size grid')
        for n, updated in updated_sizes.items():
            if {k:v for k,v in original_sizes[n]['selected'].items() if k != 'median'} != {
                    k:v for k,v in updated['selected'].items() if k != 'median'}:
                raise ValueError('Median revision changed another protocol schedule')
        observed_sizes = {r['n'] for r in observations}
        if observed_sizes & schedules.keys():
            raise ValueError('Median datasets must cover disjoint input sizes')
        schedules.update({n: updated_sizes[n]['selected']['median'] for n in observed_sizes})
        revised.extend(summarize([data_path], complete=False, parameters=parameter_path, pooled_out=revised_costs))
        records.extend(piece_records)
    audits = [r for r in records if r['type'] == 'round_audit']
    if len(audits) != 13 or len({r['n'] for r in audits}) != 13:
        raise ValueError('The Median revision must contain exactly 13 fresh audits')
    expected = {('balanced', 'median', 1 << e, profile)
                for e in range(8, 21) for profile in PROFILES}
    found = {(r['shape'], r['method'], r['n'], r['profile']) for r in revised}
    if found != expected or len(revised) != 39 or len(revised_costs) != 13:
        raise ValueError('Median revision is not a complete 39-configuration rerun')
    for r in revised:
        if r['trials'] != (3 if r['n'] <= 1 << 16 else 1) or r['offline_rounds'] is None:
            raise ValueError('Median revision has missing trials or round audits')
    if {r['executable_sha256'] for r in revised} != {r['executable_sha256'] for r in rows}:
        raise ValueError('Parameter-only Median revision changed the executable')
    for n, selected in schedules.items():
        original_sizes[n]['selected']['median'] = selected
    return (sorted([r for r in rows if r['method'] != 'median'] + revised,
                   key=lambda r: (r['shape'], r['method'], r['n'], r['m'], r['profile'])),
            sorted([r for r in costs if r['method'] != 'median'] + revised_costs,
                   key=lambda r: (r['shape'], r['method'], r['n'], r['m'])),
            params, manifest)


def texnum(x,decimals=2):
    if x is None:return '--'
    if decimals==3 and 0<x<0.001:return r'$<0.001$'
    return f'{x:,.{decimals}f}'.replace(',','{,}')

def plots(rows,out,costs,shapes=None):
    import matplotlib
    matplotlib.use('Agg')
    import matplotlib.pyplot as plt
    plt.rcParams.update({'font.family':'DejaVu Sans','font.size':11,'axes.spines.top':False,'axes.spines.right':False,
                         'pdf.fonttype':42,'ps.fonttype':42,'axes.titleweight':'bold','axes.labelcolor':'#243746',
                         'xtick.color':'#34495e','ytick.color':'#34495e','savefig.facecolor':'white'})
    def xaxis(ax):
        ax.set_xlim(7.7,20.4);ax.set_xticks([8,12,16,20])
        ax.set_xticklabels([rf'$2^{{{e}}}$' for e in [8,12,16,20]])
        ax.set_xlabel(r'List length $n$');ax.grid(axis='y',alpha=.18)
    def save(fig,stem):
        fig.savefig(out/(stem+'.pdf'),bbox_inches='tight')
        fig.savefig(out/(stem+'.png'),dpi=180,bbox_inches='tight');plt.close(fig)
    for shape,methods in METHODS.items():
        if shapes is not None and shape not in shapes:continue
        # These panels are shared by all network settings. Linear, zero-based
        # axes show absolute costs; the ratio panel preserves the small-n view.
        fig,axs=plt.subplots(1,3,figsize=(7.5,2.95))
        baseline={r['n']:r for r in costs if r['shape']==shape and r['method']=='batcher'}
        for method in methods:
            series=sorted((r for r in costs if r['shape']==shape and r['method']==method),key=lambda r:r['n'])
            xs=[r['n'].bit_length()-1 for r in series]
            vals=[[r['online_bytes']/(1<<20) for r in series],
                  [baseline[r['n']]['online_bytes']/r['online_bytes'] for r in series],
                  [r['online_rounds'] for r in series]]
            lows=[[r['online_bytes_min']/(1<<20) for r in series],
                  [baseline[r['n']]['online_bytes']/r['online_bytes_max'] for r in series],
                  [r['online_rounds_min'] for r in series]]
            highs=[[r['online_bytes_max']/(1<<20) for r in series],
                   [baseline[r['n']]['online_bytes']/r['online_bytes_min'] for r in series],
                   [r['online_rounds_max'] for r in series]]
            for i,ax in enumerate(axs):
                ax.plot(xs,vals[i],color=COLORS[method],marker=MARKERS[method],ms=3,lw=1.5,label=NAMES[method])
                if method=='quick':ax.fill_between(xs,lows[i],highs[i],color=COLORS[method],alpha=.18)
        titles=['Online communication','Relative communication','Online rounds']
        labels=['MiB (both parties)','Batcher / protocol','Dependency bound']
        for ax,title,label in zip(axs,titles,labels):
            xaxis(ax);ax.set_title(title,fontsize=11);ax.set_ylabel(label);ax.set_ylim(bottom=0)
        axs[1].axhline(1,color='#52677d',lw=.8,ls=':',zorder=0)
        handles,labels=axs[0].get_legend_handles_labels()
        fig.legend(handles,labels,loc='upper center',ncol=len(methods),frameon=False,bbox_to_anchor=(.5,1.02),fontsize=10)
        fig.tight_layout(rect=[0,0,1,.9],w_pad=.9);save(fig,shape+'-online')
        # Elapsed times retain separate network panels and logarithmic axes.
        fig,axs=plt.subplots(2,3,figsize=(7.0,5.4),sharex=True,sharey='row')
        for j,profile in enumerate(PROFILES):
            for method in methods:
                series=sorted((r for r in rows if r['shape']==shape and r['method']==method and r['profile']==profile),key=lambda r:r['n'])
                xs=[r['n'].bit_length()-1 for r in series]
                for i,field in enumerate(['online_seconds','offline_seconds']):
                    axs[i,j].plot(xs,[r[field] for r in series],color=COLORS[method],marker=MARKERS[method],ms=3,lw=1.5,label=NAMES[method])
                    axs[i,j].fill_between(xs,[r[field+'_min'] for r in series],[r[field+'_max'] for r in series],color=COLORS[method],alpha=.12)
            axs[0,j].set_title(PNAMES[profile])
            for i in range(2):
                ax=axs[i,j];xaxis(ax);ax.set_yscale('log')
                if i==0:ax.set_xlabel('')
                if j==0:ax.set_ylabel(['Online time (s)','Offline time (s)'][i])
        handles,labels=axs[0,0].get_legend_handles_labels()
        fig.legend(handles,labels,loc='upper center',ncol=len(methods),frameon=False,bbox_to_anchor=(.5,1.015))
        fig.tight_layout(rect=[0,0,1,.94]);save(fig,shape+'-runtime')

def tables(rows,out,costs):
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
        lines=[r'\begin{tabular}{lrrrr}'+'\n'+r'\hline'+'\n'
               +r'Protocol & \multicolumn{2}{c}{Offline} & \multicolumn{2}{c}{Online}\\'+'\n'
               +r' & MiB & Rounds & MiB & Rounds\\\hline'+'\n']
        for method in methods:
            matches=[r for r in costs if r['shape']==shape and r['method']==method and r['n']==1<<20]
            if not matches:continue
            r=matches[0]
            lines.append(SHORT[method]+' & '+' & '.join([texnum(r['offline_bytes']/(1<<20),1),
                texnum(r['offline_rounds'],0),texnum(r['online_bytes']/(1<<20),1),texnum(r['online_rounds'],0)])+r' \\'+'\n')
        lines.extend([r'\hline\end{tabular}'+'\n',r'\par\smallskip'+'\n',r'\begin{tabular}{lrrrrrr}'+'\n',
            r'\hline Protocol & \multicolumn{2}{c}{Local (seconds)} & \multicolumn{2}{c}{LAN (seconds)} & \multicolumn{2}{c}{WAN (seconds)}\\'+'\n',
            r' & Offline & Online & Offline & Online & Offline & Online\\\hline'+'\n'])
        for method in methods:
            values=[]
            for profile in PROFILES:
                matches=[r for r in rows if r['shape']==shape and r['method']==method and r['profile']==profile and r['n']==1<<20]
                if matches:values.extend(texnum(matches[0][field],2) for field in ['offline_seconds','online_seconds'])
            if len(values)==6:lines.append(SHORT[method]+' & '+' & '.join(values)+r' \\'+'\n')
        lines.append(r'\hline\end{tabular}'+'\n')
        (out/f'{shape}-main-table.tex').write_text(''.join(lines))

def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('raw',type=pathlib.Path,nargs='*')
    ap.add_argument('--out',type=pathlib.Path,required=True)
    ap.add_argument('--allow-incomplete',action='store_true')
    ap.add_argument('--paper-study',action='store_true',help='Validate the frozen study plus any activated Median-only revision')
    ap.add_argument('--plot-shapes',help='Regenerate only these comma-separated plot shapes; tables and data remain complete')
    ap.add_argument('--parameters',type=pathlib.Path,default=ROOT/'docs/benchmarks/paper-2026/parameters.json')
    a=ap.parse_args()
    if a.paper_study:
        if a.raw or a.allow_incomplete:ap.error('--paper-study requires complete recorded datasets and no positional raw files')
        rows,costs,_,_=load_paper_study()
    else:
        if not a.raw:ap.error('Specify raw records or --paper-study')
        costs=[];rows=summarize(a.raw,not a.allow_incomplete,a.parameters,pooled_out=costs)
    shapes=a.plot_shapes.split(',') if a.plot_shapes else None
    if shapes and not set(shapes)<=set(METHODS):ap.error('Unknown plot shape')
    a.out.mkdir(parents=True,exist_ok=True)
    (a.out/'summary.json').write_text(json.dumps(rows,indent=2)+'\n')
    with (a.out/'all-results.csv').open('w',newline='') as f:
        writer=csv.DictWriter(f,fieldnames=list(rows[0]));writer.writeheader();writer.writerows(rows)
    (a.out/'protocol-costs.json').write_text(json.dumps(costs,indent=2)+'\n')
    with (a.out/'protocol-costs.csv').open('w',newline='') as f:
        writer=csv.DictWriter(f,fieldnames=list(costs[0]));writer.writeheader();writer.writerows(costs)
    plots(rows,a.out,costs,shapes=shapes);tables(rows,a.out,costs)
    print('Verified and summarized',len(rows),'configurations and',len(costs),'pooled protocol costs')

if __name__=='__main__':main()
