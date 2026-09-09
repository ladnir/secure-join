#!/usr/bin/env python3
"""Validate matched root-merge records; emit reproducible tables and plots."""
import argparse
from collections import defaultdict
import hashlib
import json
import math
from pathlib import Path
import statistics


NAMES = {'cube': 'CubeRootMerge', 'sqrt': 'SquareRootMerge', 'batcher': 'Batcher'}


def summarize(paths):
    trials, hashes, inputs = defaultdict(list), set(), {}
    for path in paths:
        inputs[str(path)] = hashlib.sha256(path.read_bytes()).hexdigest()
        for line in path.read_text().splitlines():
            r = json.loads(line)
            if r['type'] != 'benchmark':
                continue
            if not r['verified'] or not r['real_crypto'] or r['peak_sampled_swap_kib']:
                raise ValueError('Unverified, mock, or swapped execution')
            hashes.add(r['executable_sha256'])
            trials[r['trial_id']].append(r)
    if len(hashes) != 1:
        raise ValueError('Mixed executable hashes')
    groups = defaultdict(list)
    matched = defaultdict(dict)
    for records in trials.values():
        r = records[0]
        local = r['profile'] == 'local'
        if sorted(x['party'] for x in records) != ([-1] if local else [0, 1]):
            raise ValueError('Duplicate/incomplete endpoint records')
        fields = ('method', 'shape', 'm', 'n', 'key_bits', 'block_size', 'batch_size',
                  'concurrency', 'public_seed', 'profile', 'repeat', 'online_gmw_ands_padded',
                  'online_round_bound', 'comparisons')
        if any(x[f] != r[f] for x in records for f in fields):
            raise ValueError('Peer configuration mismatch')
        row = dict(repeat=r['repeat'], rss_gib=max(x['peak_rss_kib'] for x in records)/2**20,
                   rounds=r['online_round_bound'], ands=r['online_gmw_ands_padded'],
                   comparisons=r['comparisons'], block_size=r['block_size'], m=r['m'])
        row['setup_s'] = max(x['setup_ms'] for x in records)/1000
        for phase in ('offline', 'online'):
            row[phase+'_s'] = max(x[phase+'_ms'] for x in records)/1000
            row[phase+'_mib'] = (r[phase+'_total_sent_bytes'] if local else
                sum(x[f"{phase}_p{x['party']}_sent_bytes"] for x in records))/2**20
            if not local:
                peer = {x['party']: x for x in records}
                for party in (0, 1):
                    if peer[party][f'{phase}_p{party}_sent_bytes'] != peer[1-party][f'{phase}_p{1-party}_received_bytes']:
                        raise ValueError('Endpoint communication mismatch')
        row['total_s'] = row['offline_s'] + row['online_s']
        groups[r['profile'], r['n'], r['shape'], r['method']].append(row)
        key = r['profile'], r['n'], r['shape'], r['repeat']
        if r['method'] in matched[key]:
            raise ValueError('Duplicate method/repetition')
        matched[key][r['method']] = r
    for key, pair in matched.items():
        if set(pair) != {key[2], 'batcher'}:
            raise ValueError('Missing matched Batcher')
        if any(len({r[f] for r in pair.values()}) != 1 for f in
               ('m', 'n', 'key_bits', 'batch_size', 'concurrency', 'public_seed', 'pattern')):
            raise ValueError('Mismatched comparison')
    result = []
    for (profile, n, shape, method), rows in sorted(groups.items()):
        if sorted(r['repeat'] for r in rows) != [0, 1, 2]:
            raise ValueError('Require three distinct repetitions')
        out = dict(profile=profile, n=n, shape=shape, method=method, trials=len(rows))
        for metric in ('online_s', 'offline_s', 'total_s', 'setup_s'):
            out[metric] = statistics.median(r[metric] for r in rows)
            out[metric+'_min'] = min(r[metric] for r in rows)
            out[metric+'_max'] = max(r[metric] for r in rows)
        for metric in ('online_mib', 'offline_mib', 'rounds', 'ands', 'comparisons', 'block_size', 'm'):
            values = {r[metric] for r in rows}
            if len(values) != 1:
                raise ValueError('Variable public count: '+metric)
            out[metric] = values.pop()
        out['rss_gib'] = max(r['rss_gib'] for r in rows)
        result.append(out)
    # The same circuit and public parameters must be used across transports.
    schedules = defaultdict(set)
    for r in result:
        schedules[r['n'], r['shape'], r['method']].add(tuple(r[f] for f in
            ('m', 'block_size', 'ands', 'rounds', 'comparisons')))
    if any(len(v) != 1 for v in schedules.values()):
        raise ValueError('Network profile changed online circuit')
    local_rows = {(r['n'],r['shape'],r['method']):r for r in result if r['profile']=='local'}
    for r in result:
        local = local_rows[r['n'],r['shape'],r['method']]
        difference = round((local['online_mib']-r['online_mib'])*2**20)
        # Coproto starts a channel with an 8-byte header + 16-byte control
        # block per party. TCP's configuration handshake pays this before
        # timing. Local Batcher first uses the main channel online; the root
        # protocols already use it in prepare(). Preserve the measured bytes.
        expected = 48 if r['method']=='batcher' and r['profile']!='local' else 0
        if difference != expected:
            raise ValueError('Unexpected transport communication difference')
    for shape in ('cube', 'sqrt'):
        if {r['n'] for r in result if r['profile']=='local' and r['shape']==shape} != {1<<i for i in range(10,21)}:
            raise ValueError('Missing local power of two')
    return dict(executable_sha256=hashes.pop(), input_sha256=inputs,
                protocol_executions=len(trials), raw_records=sum(map(len,trials.values())), rows=result)


def tables(data, out):
    out.with_suffix('.json').write_text(json.dumps(data, indent=2)+'\n')
    index = {(r['profile'], r['n'], r['shape'], r['method']): r for r in data['rows']}
    md = ['# Matched asymmetric merge measurements', '',
          'Three fresh runs per entry; medians and observed ranges. Communication sums both parties. '
          'TCP uses the slower endpoint per phase. Offline + online excludes local circuit setup; '
          'setup medians and ranges are included in the JSON summary. No swap was observed.', '',
          'At long-list size **2^20**, with **32-bit keys**. Each pair below is **root / matched Batcher**.', '',
          '| Protocol | Short-list m | Online MiB | Local online s | WAN online s |',
          '|---|---:|---:|---:|---:|']
    for shape in ('cube','sqrt'):
        r,b=(index['local',1<<20,shape,k] for k in (shape,'batcher'))
        wan='-'
        if ('wan',1<<20,shape,shape) in index:
            w,wb=(index['wan',1<<20,shape,k] for k in (shape,'batcher'))
            wan=f"{w['online_s']:.3f} / {wb['online_s']:.3f}"
        md.append(f"| {NAMES[shape]} | {r['m']} | {r['online_mib']:.2f} / {b['online_mib']:.2f} | "
                  f"{r['online_s']:.3f} / {b['online_s']:.3f} | {wan} |")
    md += ['', 'These are online costs. The complete tables below also include preprocessing, '
           'which can offset online gains, and all smaller sizes.', '',
          f"Executable SHA-256: `{data['executable_sha256']}`.", '',
          '| Profile | Long n | Short m | Shape | Method | b | Online MiB | Online s (range) | Offline MiB | Offline s (range) | Offline + online s | Rounds | Peak RSS GiB |',
          '|---|---:|---:|---|---|---:|---:|---:|---:|---:|---:|---:|---:|']
    for r in data['rows']:
        md.append(f"| {r['profile']} | 2^{r['n'].bit_length()-1} | {r['m']} | {r['shape']} | {NAMES[r['method']]} | {r['block_size'] or '-'} | "
                  f"{r['online_mib']:.3f} | {r['online_s']:.6f} ({r['online_s_min']:.6f}–{r['online_s_max']:.6f}) | "
                  f"{r['offline_mib']:.3f} | {r['offline_s']:.3f} ({r['offline_s_min']:.3f}–{r['offline_s_max']:.3f}) | "
                  f"{r['total_s']:.3f} | {r['rounds']} | {r['rss_gib']:.3f} |")
    md += ['', 'A ratio below one favors the root protocol. Local medians alone do not establish '
           'a consistent advantage when run ranges overlap.', '',
           '| Profile | Long n | Shape | Online bytes ratio | Online time ratio | Offline + online time ratio |',
           '|---|---:|---|---:|---:|---:|']
    for r in data['rows']:
        if r['method']=='batcher':
            continue
        b = index[r['profile'],r['n'],r['shape'],'batcher']
        md.append(f"| {r['profile']} | 2^{r['n'].bit_length()-1} | {r['shape']} | "
                  f"{r['online_mib']/b['online_mib']:.3f} | {r['online_s']/b['online_s']:.3f} | {r['total_s']/b['total_s']:.3f} |")
    out.with_suffix('.md').write_text('\n'.join(md)+'\n')
    params = [r'\begin{tabular}{rrrrr}', r'\hline',
              r'$\log_2 n$ & Cube $m$ & Cube $b$ & Square $m$ & Square $b$ \\', r'\hline']
    for lg in range(10,21):
        c, s = (index['local',1<<lg,k,k] for k in ('cube','sqrt'))
        params.append(f"{lg} & {c['m']} & {c['block_size']} & {s['m']} & {s['block_size']} " + r'\\')
    params += [r'\hline', r'\end{tabular}']
    out.with_name('root-merge-parameters.tex').write_text('\n'.join(params)+'\n')
    tex = [r'\begin{tabular}{llrrrrr}',r'\hline',
           r'Shape & Protocol & Online & Offline & Local & Local & Depth \\',
           r' & & MiB & MiB & online s & offline s & \\',r'\hline']
    for shape in ('cube','sqrt'):
        for method in (shape,'batcher'):
            r=index['local',1<<20,shape,method]
            label='Cube' if shape=='cube' else 'Square'
            name='Root' if method==shape else 'Batcher'
            tex.append(f"{label} & {name} & {r['online_mib']:.1f} & {r['offline_mib']:.1f} & "
                       f"{r['online_s']:.3f} & {r['offline_s']:.2f} & {r['rounds']} " + r'\\')
        tex.append(r'\hline')
    tex += [r'\end{tabular}']
    out.with_name('root-merge-large-table.tex').write_text('\n'.join(tex)+'\n')
    findings=[]
    for shape in ('cube','sqrt'):
        r,b=(index['local',1<<20,shape,k] for k in (shape,'batcher'))
        findings.append(f"At $n=2^{{20}}$, {NAMES[shape]} ($m={r['m']}$, $b={r['block_size']}$) "
                        f"sends {r['online_mib']:.2f}\\,MiB online versus {b['online_mib']:.2f}\\,MiB "
                        f"for matched Batcher, a {100*(1-r['online_mib']/b['online_mib']):.1f}\\% reduction. "
                        f"Its local online median is {r['online_s']:.3f}\\,s "
                        f"(range {r['online_s_min']:.3f}--{r['online_s_max']:.3f}), versus "
                        f"{b['online_s']:.3f}\\,s ({b['online_s_min']:.3f}--{b['online_s_max']:.3f}).")
        key='wan',1<<20,shape,shape
        if key in index:
            r,b=index[key],index['wan',1<<20,shape,'batcher']
            findings.append(f"On the emulated WAN, the online medians are {r['online_s']:.3f} "
                            f"and {b['online_s']:.3f}\\,s, respectively "
                            f"({b['online_s']/r['online_s']:.2f}$\\times$ speedup). "
                            f"Including preprocessing, the medians are {r['total_s']:.2f} "
                            f"and {b['total_s']:.2f}\\,s.")
        findings.append('')
    out.with_name('root-merge-findings.tex').write_text('\n'.join(findings)+'\n')


def plots(data, dest):
    import matplotlib
    matplotlib.use('Agg')
    import matplotlib.pyplot as plt
    import numpy as np
    plt.rcParams.update({'font.size':8.5, 'axes.spines.top':False,'axes.spines.right':False,
                         'pdf.fonttype':42,'ps.fonttype':42})
    dest.mkdir(parents=True,exist_ok=True)
    index={(r['profile'],r['n'],r['shape'],r['method']):r for r in data['rows']}
    logs=list(range(10,21))
    colors={'root':'#126c8a','batcher':'#ba592a'}
    fig,axs=plt.subplots(2,2,figsize=(5.0,4.4),layout='constrained')
    for col,shape in enumerate(('cube','sqrt')):
        root=[index['local',1<<lg,shape,shape] for lg in logs]
        base=[index['local',1<<lg,shape,'batcher'] for lg in logs]
        for rows,label,color,marker in ((root,NAMES[shape],colors['root'],'o'),(base,'Matched Batcher',colors['batcher'],'s')):
            axs[0,col].plot(logs,[r['online_mib'] for r in rows],label=label,color=color,marker=marker,markersize=3)
        axs[0,col].set(yscale='log',ylabel='Online communication (MiB)',title=NAMES[shape])
        axs[0,col].legend(fontsize=7)
        for metric,label,style in (('online_mib','Bytes','-'),('online_s','Local online time','--')):
            axs[1,col].plot(logs,[r[metric]/b[metric] for r,b in zip(root,base)],style,marker='o',markersize=3,label=label)
        axs[1,col].axhline(1,color='0.4',linewidth=.7)
        axs[1,col].set(ylabel='Root / matched Batcher',xlabel=r'Long-list size $\log_2 n$')
        axs[1,col].legend(fontsize=7)
    for ax in axs.flat:
        ax.set_xticks(logs);ax.grid(alpha=.18)
    fig.savefig(dest/'root-merge-communication.pdf');fig.savefig(dest/'root-merge-communication.png',dpi=180);plt.close(fig)
    fig,axs=plt.subplots(2,2,figsize=(5.0,4.4),layout='constrained')
    for col,shape in enumerate(('cube','sqrt')):
        for row,metric in enumerate(('online_s','offline_s')):
            for method,color,marker in ((shape,colors['root'],'o'),('batcher',colors['batcher'],'s')):
                rs=[index['local',1<<lg,shape,method] for lg in logs]
                y=np.array([r[metric] for r in rs])
                error=np.array([[r[metric]-r[metric+'_min'] for r in rs],[r[metric+'_max']-r[metric] for r in rs]])
                axs[row,col].errorbar(logs,y,yerr=error,label=NAMES[method],color=color,marker=marker,markersize=3,capsize=2)
            axs[row,col].set(yscale='log',ylabel=('Online' if row==0 else 'Preprocessing')+' time (s)',xlabel=r'Long-list size $\log_2 n$')
            axs[row,col].set_xticks(logs);axs[row,col].grid(alpha=.18)
        axs[0,col].set_title(NAMES[shape]);axs[0,col].legend(fontsize=7)
    fig.savefig(dest/'root-merge-local.pdf');fig.savefig(dest/'root-merge-local.png',dpi=180);plt.close(fig)
    settings=sorted({(r['profile'],r['n']) for r in data['rows'] if r['profile']!='local'})
    if settings:
        fig,axs=plt.subplots(2,2,figsize=(5.0,4.4),layout='constrained')
        x=np.arange(len(settings))
        for col,shape in enumerate(('cube','sqrt')):
            for row,metric in enumerate(('online_s','total_s')):
                for shift,method,color in ((-.19,shape,colors['root']),(.19,'batcher',colors['batcher'])):
                    rs=[index[p,n,shape,method] for p,n in settings]
                    y=np.array([r[metric] for r in rs])
                    error=np.array([[r[metric]-r[metric+'_min'] for r in rs],[r[metric+'_max']-r[metric] for r in rs]])
                    axs[row,col].bar(x+shift,y,width=.36,label=NAMES[method],color=color,yerr=error,capsize=2)
                axs[row,col].set(yscale='log',ylabel=('Online' if row==0 else 'Offline + online')+' time (s)')
                axs[row,col].set_xticks(x,[f'{p.upper()}\n$2^{{{n.bit_length()-1}}}$' for p,n in settings]);axs[row,col].grid(axis='y',alpha=.18)
            axs[0,col].set_title(NAMES[shape]);axs[0,col].legend(fontsize=7)
        fig.savefig(dest/'root-merge-network.pdf');fig.savefig(dest/'root-merge-network.png',dpi=180);plt.close(fig)


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('inputs',nargs='+',type=Path)
    p.add_argument('--out',type=Path,default=Path('docs/benchmarks/root-merge-summary'))
    p.add_argument('--plots',type=Path)
    a=p.parse_args()
    data=summarize(a.inputs)
    a.out.parent.mkdir(parents=True,exist_ok=True)
    tables(data,a.out)
    if a.plots:plots(data,a.plots)
    print(json.dumps({k:v for k,v in data.items() if k!='rows'},indent=2))


if __name__=='__main__':
    main()
