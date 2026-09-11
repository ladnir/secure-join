#!/usr/bin/env python3
"""Install the completed measured evaluation and reproducibility metadata.

Requires the strict summarizer's complete 390-configuration output. Does not
substitute planned values or partial measurements into the manuscript.
"""
import argparse, hashlib, json, pathlib, re, shutil, subprocess
from summarize_paper_benchmarks import summarize, load_paper_study

ROOT=pathlib.Path(__file__).resolve().parents[1]
PAPER=ROOT.parent/'64c0aeaf1c1f5473b45f1e06'
RAW=ROOT/'docs/benchmarks/paper-2026'
FIG=PAPER/'plots/benchmark'


def write_text_if_changed(path, text):
    text = text.rstrip() + '\n'
    if not path.exists() or path.read_text() != text:
        path.write_text(text)


def main():
    rows,costs,params,median_revision=load_paper_study()
    if costs!=json.loads((FIG/'protocol-costs.json').read_text()):
        raise RuntimeError('Regenerate the pooled protocol costs from the validated measurements')
    if rows!=json.loads((FIG/'summary.json').read_text()):
        raise RuntimeError('Regenerate the figures and tables from the complete validated measurements')
    if hashlib.sha256((ROOT/'out/build/linux/frontend/paper_benchmark').read_bytes()).hexdigest()!=rows[0]['executable_sha256']:
        raise RuntimeError('The benchmark executable changed after measurement')
    def get(shape,method,profile='wan',n=1<<20):
        result=dict(next(r for r in rows if (r['shape'],r['method'],r['profile'],r['n'])==(shape,method,profile,n)))
        common=next(r for r in costs if (r['shape'],r['method'],r['n'])==(shape,method,n))
        for field in ['online_bytes','offline_bytes','online_rounds']:result[field]=common[field]
        return result
    def f(v,d=2):return f'{v:,.{d}f}'.replace(',','{,}')
    def ratio(a,b,field):return a[field]/b[field]
    l,m,b,q=[get('balanced',x) for x in ['logstar','median','batcher','quick']]
    wins=[e for e in range(8,21) if get('balanced','logstar',n=1<<e)['online_bytes']<get('balanced','batcher',n=1<<e)['online_bytes']]
    crossover=''
    if wins and wins==list(range(wins[0],21)):
        crossover=('With the selected parameters, Logstar first uses fewer online bytes than Batcher at $n=2^{'
                   +str(wins[0])+r'}$ and retains this advantage at every larger tested size. ')
    small_m,small_b=get('balanced','median',n=1<<11),get('balanced','batcher',n=1<<11)
    small_note=''
    if small_m['online_seconds']<small_b['online_seconds']:
        small_note=('At $n=2^{11}$, Median uses '
                    +f(small_m['online_rounds'],0)+' online rounds versus '+f(small_b['online_rounds'],0)+' for Batcher, with '
                    +f(small_m['online_bytes']/2**20)+r'\,MiB versus '+f(small_b['online_bytes']/2**20)+r'\,MiB. '
                    +'Its WAN online time is '+f(small_m['online_seconds'])+r'\,s versus '
                    +f(small_b['online_seconds'])+r'\,s, a $'+f(small_b['online_seconds']/small_m['online_seconds'])+r'\times$ speedup. ')
    balanced=(r'\paragraph{Discussion.} At $n=2^{20}$, Logstar communicates '
        +f(l['online_bytes']/2**20,1)+r'\,MiB online, compared with '+f(b['online_bytes']/2**20,1)+r'\,MiB for Batcher and '
        +f(q['online_bytes']/2**20,1)+r'\,MiB for shuffled quicksort: reductions of $'+f(ratio(b,l,'online_bytes'))+r'\times$ and $'
        +f(ratio(q,l,'online_bytes'))+r'\times$, respectively. This saves bytes at the cost of '+f(l['online_rounds'],0)+' rounds, versus '+f(b['online_rounds'],0)+' for Batcher. Median uses '+f(m['online_rounds'],0)+' online rounds, versus '
        +f(b['online_rounds'],0)+' for Batcher and '+f(q['online_rounds'],0)+' for the pooled quicksort median. '
        +'This is a '+f(100*(1-m['online_rounds']/b['online_rounds']),1)+r'\% reduction in rounds relative to Batcher. '
        +'Its '+f(m['online_bytes']/2**20,1)+r'\,MiB online traffic is $'+f(m['online_bytes']/b['online_bytes'])+r'\times$ Batcher\textquotesingle s. '
        +crossover
        +small_note
        +('At the largest input on the WAN, Median remains slower than Batcher despite its smaller round count. ' if m['online_seconds']>b['online_seconds'] else '')
        +'At $n=2^{20}$ on the WAN, Logstar completes the online merge in '+f(l['online_seconds'])+r'\,s, versus '
        +f(b['online_seconds'])+r'\,s for Batcher. Including fresh correlations, their offline-plus-online totals become '
        +f(l['total_seconds'])+' and '+f(b['total_seconds'])+r'\,s, respectively. '
        +('Batcher therefore has the lower measured fresh-correlation total at this size. ' if b['total_seconds']<l['total_seconds'] else '')
        +'\n')
    write_text_if_changed(FIG/'balanced-discussion.tex', balanced)
    texts=[]
    for shape,name in [('cube','CubeRootMerge'),('sqrt','SquareRootMerge')]:
        r,b,q=[get(shape,x) for x in [shape,'batcher','quick']]
        texts.append(name+' communicates '+f(r['online_bytes']/2**20,1)+r'\,MiB online in '+f(r['online_rounds'],0)
            +' dependent rounds at $n=2^{20}$. Its communication is $'+f(ratio(b,r,'online_bytes'))+r'\times$ smaller than Batcher and $'
            +f(ratio(q,r,'online_bytes'))+r'\times$ smaller than shuffled quicksort on the same shape. '
            +'The WAN online times are '+', '.join(f(x['online_seconds']) for x in [r,b,q])+r'\,s, respectively. '
            +'With fresh correlations, '+name+' and Batcher take '+f(r['total_seconds'])+' and '+f(b['total_seconds'])+r'\,s. ')
    all_root_wins=all(get(shape,shape,n=1<<e)[field]<get(shape,'batcher',n=1<<e)[field]
                      for shape in ['cube','sqrt'] for e in range(8,21) for field in ['online_bytes','online_rounds'])
    range_note='Both asymmetric protocols improve on Batcher in online bytes and rounds at every tested size. ' if all_root_wins else ''
    reversals=[name for shape,name in [('cube','CubeRootMerge'),('sqrt','SquareRootMerge')]
               if get(shape,shape)['online_seconds']<get(shape,'batcher')['online_seconds']
               and get(shape,shape)['total_seconds']>get(shape,'batcher')['total_seconds']]
    fresh_note=('At $n=2^{20}$, including fresh preprocessing reverses the measured WAN timing comparison with Batcher for '
                +' and '.join(reversals)+'. ' if reversals
                else 'The complete tables include preprocessing costs for every size and transport. ')
    write_text_if_changed(FIG/'roots-discussion.tex', r'\paragraph{Discussion.} '+''.join(texts)+range_note
        +'The shorter list reduces the work in both asymmetric protocols, whereas shuffled quicksort still sorts the concatenation. '
        +'Batcher also benefits from unequal lengths, so each reduction is against that smaller baseline. '
        +fresh_note.rstrip()+'\n')
    lines=[r'\begin{tabular}{rrrrlrr}'+'\n'
           +r'\hline $\log_2 n$ & Logstar & \multicolumn{3}{c}{Median} & CubeRoot & SquareRoot\\'+'\n'
           +r' & block & child & cube block & leaf & block & block\\\hline'+'\n']
    for schedule in params['sizes']:
        sel=schedule['selected'];p=sel['median'];levels=p['levels']
        child='/'.join(str(level['child_size']) for level in levels)
        blocks='/'.join(str(level['cube_block']) for level in levels)
        leaf='all-pairs' if p['leaf']=='allpairs' else 'Batcher'
        lines.append(f"{schedule['n'].bit_length()-1} & {sel['logstar']['block_override']} & {child} & {blocks} & {leaf} & {sel['cube']['block_size']} & {sel['sqrt']['block_size']}"+r' \\'+'\n')
    lines.append('\\hline\n\\end{tabular}\n');(FIG/'parameters-table.tex').write_text(''.join(lines))
    source_files=sorted((ROOT/'secure-join').rglob('*.h'))+sorted((ROOT/'secure-join').rglob('*.cpp'))+[ROOT/'frontend/paper_benchmark.cpp']
    metadata=dict(executable_sha256=rows[0]['executable_sha256'],compiler='GCC 13.3.0, -O3 -march=native -std=c++20',
                  windows_model='Lenovo 83DF',windows_installed_memory_bytes=34070192128,
                  wsl_memory_cap_gib=26,
                  boost_version='1.86.0',
                  silent_ot_vole_security_parameter=128,
                  transport_revision='separate-streams',
                  configurations=len(rows),timed_executions=sum(r['trials'] for r in rows),round_audits=130,
                  dataset_sha256={name:hashlib.sha256((RAW/name).read_bytes()).hexdigest() for name in
                                  ['measurements.jsonl','parameters.json','validation.jsonl','calibration-26gb.jsonl']},
                  compile_flags=(ROOT/'out/build/linux/frontend/CMakeFiles/paper_benchmark.dir/flags.make').read_text(),
                  cmake_cache_sha256=hashlib.sha256((ROOT/'out/build/linux/CMakeCache.txt').read_bytes()).hexdigest(),
                  source_sha256={str(p.relative_to(ROOT)):hashlib.sha256(p.read_bytes()).hexdigest() for p in source_files},
                  script_sha256={p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in (ROOT/'scripts').glob('*paper*py')},
                  counting='Nonempty lines after removing C++ comments, Sort directory only; tests and dependencies excluded',dependencies={})
    for name,path in [('secure-join',ROOT),('libOTe',ROOT/'out/libOTe'),('cryptoTools',ROOT/'out/libOTe/cryptoTools'),('coproto',ROOT/'out/coproto'),('macoro',ROOT/'out/macoro')]:
        path=path.resolve()
        if not (path/'.git').exists():
            metadata['dependencies'][name]='Source archive; independent Git revision unavailable'
            continue
        r=subprocess.run(['git','-c',f'safe.directory={path}','-C',str(path),'rev-parse','HEAD'],capture_output=True,text=True)
        metadata['dependencies'][name]=r.stdout.strip() if not r.returncode else 'unavailable'
    metadata['sort_code_lines']=sum(sum(bool(l.strip()) for l in re.sub(r'/\*.*?\*/|//[^\n]*','',p.read_text(),flags=re.S).splitlines())
                                     for p in (ROOT/'secure-join/Sort').iterdir() if p.suffix in ['.h','.cpp'])
    if median_revision:metadata['median_revision']=median_revision
    provenance=RAW/'provenance.json'
    if provenance.exists():
        original=json.loads(provenance.read_text())
        if original['executable_sha256']!=metadata['executable_sha256'] or original['dataset_sha256']!=metadata['dataset_sha256']:
            raise RuntimeError('Frozen experiment provenance does not match the current measurements')
        metadata['experiment_provenance_sha256']=hashlib.sha256(provenance.read_bytes()).hexdigest()
        metadata['cost_aggregation']='Median of all individual executions across transports; 9 samples through 2^16 and 3 above; observed min-max ranges; times remain per transport'
        (RAW/'presentation-provenance.json').write_text(json.dumps(metadata,indent=2)+'\n')
    else:provenance.write_text(json.dumps(metadata,indent=2)+'\n')
    # Keep the existing introductory benchmark summaries consistent with the
    # replaced evaluation. Other claims and the protocol analysis stay intact.
    intro_path=PAPER/'intro.tex';intro=intro_path.read_text()
    old_intro=RAW/'intro-before-benchmark-rewrite.tex'
    if not old_intro.exists():old_intro.write_text(intro)
    stale=(r'\cite{EPRINT:BBDLO22} does not discuss concrete performance; we estimate their cost in \Cref{sec:eval} and show that their merge is concretely slower than existing techniques. Our main merging protocol reduces their bandwidth/rounds by $\approx8.3\times$/$\approx3.1\times$, respectively.')
    intro=intro.replace(stale,r'\cite{EPRINT:BBDLO22} does not report implementation measurements. We compare implementations of our protocols with Batcher and shuffled quicksort in \Cref{sec:eval}.')
    intro=intro.replace('while maintaining their small constants, resulting in total concrete improvement.',
                        'while permitting optimizations of their concrete costs; our experiments quantify the resulting tradeoffs.')
    b_bal=get('balanced','batcher');log=get('balanced','logstar');med=get('balanced','median')
    ci,cb=get('cube','cube'),get('cube','batcher');sr,sb=get('sqrt','sqrt'),get('sqrt','batcher')
    summary=(r'\noindent We implement all four protocols and compare them with Batcher\textquotesingle s merge and shuffled quicksort on the same two-party backend, for every $n=2^8,\ldots,2^{20}$ and 32-bit keys. '
        +r'At $n=2^{20}$ keys per list, \ourapproach\ reduces online communication by $'+f(ratio(b_bal,log,'online_bytes'))
        +r'\times$ relative to Batcher, while \ourapproachtwo\ uses '+f(med['online_rounds'],0)+' online rounds compared with '
        +f(b_bal['online_rounds'],0)+r' for Batcher, at a higher bandwidth cost. For the corresponding unequal input shapes, \knmerge\ and \sqrtmerge\ reduce online communication by $'
        +f(ratio(cb,ci,'online_bytes'))+r'\times$ and $'+f(ratio(sb,sr,'online_bytes'))+r'\times$, respectively. '
        +r'\Cref{sec:eval} reports offline and online communication, rounds, and elapsed time on local, LAN, and WAN transports. The implementation uses per-size concrete parameters, rather than enforcing the asymptotic recursion schedules.'+'\n')
    begin=intro.find(r'\noindent We analytically benchmark our protocols')
    if begin<0:begin=intro.find(r'\noindent We implement all four protocols')
    if begin>=0:
        end=intro.find('\n\\iffalse',begin)
        if end<0:raise RuntimeError('Cannot locate old introduction benchmark block')
        intro=intro[:begin]+summary+'\n'+intro[end:]
    intro_path.write_text(intro)
    abstract_path=PAPER/'abstract.tex';abstract=abstract_path.read_text();old_abstract=RAW/'abstract-before-benchmark-rewrite.tex'
    if not old_abstract.exists():old_abstract.write_text(abstract)
    begin=abstract.find(r'\qquad We optimize our constructions for \emph{concrete efficiency}.')
    if begin<0:begin=abstract.find(r'\qquad We implement our constructions and optimize their public parameters')
    if begin>=0:
        end=abstract.find('\n\n',begin)
        abstract=abstract[:begin]+(r'\qquad We implement our constructions and optimize their public parameters for concrete efficiency. In two-party experiments with $n=2^{20}$ keys per input list and 32-bit keys, \ourapproach\ uses $'
            +f(ratio(b_bal,log,'online_bytes'))+r'\times$ less online communication than Batcher\textquotesingle s merge. '
            +r'\ourapproachtwo\ offers a different tradeoff, using '+f(med['online_rounds'],0)+' online rounds instead of '+f(b_bal['online_rounds'],0)
            +r' at a higher bandwidth cost. Our evaluation also compares both asymmetric merges with the same baselines and reports preprocessing costs separately.')+abstract[end:]
    abstract_path.write_text(abstract)
    related_edits={
        'relwork.tex':[
            (r'While asymptotically intriguing, the protocol is complex and has high constants. As discussed in \Cref{sec:intro}, we show that our \ourapproach\ reduces their bandwidth/rounds by $\approx8.3\times$/$\approx3.1\times$, respectively.', ''),
            (r'Concretely, our \ourapproach\ reduces bandwidth over this subprotocol by $\approx 7.5\times$ and rounds by $\approx 1.2\times$.', ''),
            (r'See Section \ref{sec:eval} for a more detailed comparison.',
             r'Section \ref{sec:eval} compares our implementations with Batcher and shuffled quicksort.')],
        'overview.tex':[
            (r'However, the complexity of their subprotocols results in a significantly higher round complexity as detailed in Section \ref{sec:eval}.', '')]
    }
    for filename,replacements in related_edits.items():
        path=PAPER/filename;text=path.read_text()
        original=RAW/(path.stem+'-before-benchmark-rewrite.tex')
        if not original.exists():original.write_text(text)
        for before,after in replacements:text=text.replace(before,after)
        path.write_text(text)
    if median_revision:
        template = PAPER/'evaluation-new.tex'
        evaluation = template.read_text()
        begin = evaluation.index(r'\paragraph{Concrete parameters.}')
        end = evaluation.index(r'\begin{figure}', begin)
        description = r'''\paragraph{Concrete parameters.}
We choose public parameters separately for each $n$ and reuse them on all
three transports. Logstar uses one packed partition level, terminal block
sizes from $\{2,4,8,16\}$, and direct-rank extraction; its recorded candidates
are scored by online payload plus $250{,}000$ bytes per dependent step.
For Median, we screen power-of-two recursion shapes with up to four alignment
levels, then obtain exact public cost counts for shortlisted child sizes,
internal cube-merge blocks, and all-pairs or Batcher leaves. We prioritize
fewer online rounds subject to a payload budget of the larger of three times
Batcher's online traffic and $8$\,MiB, breaking ties by payload. To limit peak
memory, we restrict the two largest sizes to one alignment level. The selected
schedules use one or two levels, as shown in \Cref{tab:benchmark-parameters}.
These are concrete bandwidth--latency choices from the recorded candidate set,
not asymptotic schedules or a claim of global optimality. Batcher exploits both
sorted runs; quicksort jointly shuffles their concatenation and batches active
partitions, with all-pairs leaves of at most eight rows. Its comparison
correlations are reserved offline; any refill must be charged online.

'''
        evaluation = evaluation[:begin] + description + evaluation[end:]
        evaluation = evaluation.replace('scale with the same selection rule as above.',
                                        'scale using the same weighted payload--depth score as Logstar.')
        evaluation = evaluation.replace('Median uses\none alignment level at every size. The cube-block column belongs to Median\'s',
            'Slash-separated Median child and cube-block entries specify consecutive\nalignment levels. The cube-block column belongs to Median\'s')
        revision_folder = RAW/median_revision['directory']
        paged_audits = [r for spec in median_revision['datasets']
                        for r in (json.loads(line) for line in (revision_folder/spec['measurements']).read_text().splitlines())
                        if r.get('type') == 'round_audit' and r.get('peak_sampled_swap_kib', 0)]
        if paged_audits and "audit uses swap to accommodate its buffering overhead" not in evaluation:
            evaluation = evaluation.replace('Audit timings are excluded from the runtime results.',
                'Audit timings are excluded from the runtime results. The largest Median\n'
                'audit uses swap to accommodate its buffering overhead; timed trials with\n'
                'observed Linux process swapping are rejected.')
        template.write_text(evaluation)
    backup=RAW/'evaluation-before-rewrite.tex'
    if not backup.exists():shutil.copy2(PAPER/'evaluation.tex',backup)
    shutil.copy2(PAPER/'evaluation-new.tex',PAPER/'evaluation.tex')
    print('Installed complete measured evaluation; old section preserved at',backup)

if __name__=='__main__':main()
