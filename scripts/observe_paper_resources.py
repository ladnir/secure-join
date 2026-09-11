#!/usr/bin/env python3
"""Optional, read-only resource snapshots while a benchmark controller runs."""
import argparse, datetime, json, pathlib, time

def fields(path):
    return dict(line.split(':',1) for line in path.read_text().splitlines() if ':' in line)

def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--parent',type=int,required=True)
    ap.add_argument('--out',type=pathlib.Path,required=True)
    a=ap.parse_args();parent=pathlib.Path('/proc')/str(a.parent)
    if b'run_paper_benchmarks.py' not in (parent/'cmdline').read_bytes():
        raise RuntimeError('The selected controller is not the benchmark sweep')
    a.out.parent.mkdir(parents=True,exist_ok=True)
    with a.out.open('a') as out:
        while parent.exists():
            mem=fields(pathlib.Path('/proc/meminfo'));peers=[]
            for comm in pathlib.Path('/proc').glob('[0-9]*/comm'):
                try:
                    if comm.read_text().strip()!='paper_benchmark':continue
                    p=comm.parent;s=fields(p/'status')
                    peers.append(dict(pid=int(p.name),threads=int(s['Threads']),
                                      rss_kib=int(s.get('VmRSS','0 kB').split()[0]),
                                      swap_kib=int(s.get('VmSwap','0 kB').split()[0]),
                                      command=(p/'cmdline').read_bytes().decode().strip('\0').split('\0')))
                except (FileNotFoundError,ProcessLookupError):pass
            row=dict(utc=datetime.datetime.now(datetime.timezone.utc).isoformat(),
                     available_kib=int(mem['MemAvailable'].split()[0]),
                     system_swap_used_kib=int(mem['SwapTotal'].split()[0])-int(mem['SwapFree'].split()[0]),
                     benchmark_rss_sum_kib=sum(p['rss_kib'] for p in peers),peers=peers)
            out.write(json.dumps(row)+'\n');out.flush();time.sleep(15)

if __name__=='__main__':main()
