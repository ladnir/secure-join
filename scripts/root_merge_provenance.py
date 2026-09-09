#!/usr/bin/env python3
"""Record the exact source, executable, compiler and host used for root merges."""
import argparse
import datetime
import hashlib
import json
import os
from pathlib import Path
import platform
import subprocess


def command(args, cwd=None):
    if args[0]=='git':
        # The network runner uses root only for its isolated namespaces. Trust
        # the explicitly inspected checkout for this read, without global config.
        args=['git','-c','safe.directory='+str(Path(cwd or '.').resolve()),*args[1:]]
    return subprocess.run(args, cwd=cwd, check=True, text=True, capture_output=True).stdout.strip()


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--exe',type=Path,default=Path('out/build/linux/frontend/rootmerge'))
    p.add_argument('--out',type=Path,default=Path('docs/benchmarks/root-merge-provenance.json'))
    p.add_argument('--dependency',action='append',default=[],help='NAME=PATH of dependency Git checkout')
    a=p.parse_args()
    sources={}
    files=command(['git','ls-files','--cached','--others','--exclude-standard']).splitlines()
    for name in files:
        path=Path(name)
        if path.suffix in {'.cpp','.h','.hpp','.cmake'} or path.name=='CMakeLists.txt' or name in {
            'scripts/benchmark_root_merge.py','scripts/benchmark_logstar.py','scripts/calibrate_logstar_network.py',
            'scripts/summarize_root_merge.py','scripts/test_root_merge_model.py','scripts/root_merge_provenance.py'}:
            sources[name]=hashlib.sha256(path.read_bytes()).hexdigest()
    exe=a.exe.resolve(strict=True)
    result=dict(executable_sha256=hashlib.sha256(exe.read_bytes()).hexdigest(), executable=str(exe),
                source_sha256=sources, git_head=command(['git','rev-parse','HEAD']),
                git_branch=command(['git','branch','--show-current']),
                source_is_uncommitted=True, platform=platform.platform(), logical_cpus=os.cpu_count(),
                compiler=command(['c++','--version']), utc=datetime.datetime.now(datetime.timezone.utc).isoformat(),
                cxx_flags=Path('out/build/linux/frontend/CMakeFiles/rootmerge.dir/flags.make').read_text(),
                local_threads='Both parties on one OS thread',
                tcp_threads='Two processes; each has two Asio I/O workers and a main thread; no GMW worker pool',
                affinity='Not fixed', frequency='Not fixed', dependencies={})
    if Path('/proc/cpuinfo').exists():
        result['cpu_model']=next(line.split(':',1)[1].strip() for line in Path('/proc/cpuinfo').read_text().splitlines() if line.startswith('model name'))
        result['meminfo']=Path('/proc/meminfo').read_text()
    for dep in a.dependency:
        name,path=dep.split('=',1)
        result['dependencies'][name]=dict(path=path,head=command(['git','rev-parse','HEAD'],path),
            status=command(['git','status','--porcelain'],path))
    result['new_protocol_physical_lines']=sum(len(Path(f).read_text().splitlines()) for f in
        ('secure-join/Sort/RootMerge.cpp','secure-join/Sort/RootMerge.h'))
    a.out.parent.mkdir(parents=True,exist_ok=True)
    a.out.write_text(json.dumps(result,indent=2)+'\n')
    print(result['executable_sha256'],result['new_protocol_physical_lines'])


if __name__=='__main__':
    main()
