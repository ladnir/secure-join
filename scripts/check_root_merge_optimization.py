#!/usr/bin/env python3
"""Focused version comparison; public plans by default, four small probes on request.

This never runs the scaling/network sweep, tunes parameters, or updates paper
artifacts. Output must be a fresh path. The old executable remains untouched.
"""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess

from benchmark_root_merge import payload


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--before', type=Path, default=Path('out/rootmerge-evaluated'))
    parser.add_argument('--after', type=Path, default=Path('out/build/linux/frontend/rootmerge'))
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--probe', action='store_true', help='also verify one small run per version and root method')
    args = parser.parse_args()
    if args.out.exists():
        parser.error('output already exists; choose a fresh path')
    binaries = {name: path.resolve(strict=True) for name, path in [('before', args.before), ('after', args.after)]}
    choices = json.loads(Path('docs/benchmarks/root-merge-scaling.parameters.json').read_text())
    records = []
    for version, binary in binaries.items():
        sha = hashlib.sha256(binary.read_bytes()).hexdigest()
        for n in (4096, 1048576):
            for method, m in [('cube', 16 if n == 4096 else 102), ('sqrt', 64 if n == 4096 else 1024)]:
                block = choices[f'{method}:{n}']
                command = [str(binary), '--method', method, '--m', str(m), '--n', str(n), '--block', str(block), '--bits', '32']
                plan = json.loads(subprocess.run(command+['--plan'], check=True, capture_output=True, text=True, timeout=120).stdout)
                records.append(dict(version=version, executable_sha256=sha, estimated_payload_bytes=payload(plan), command=command+['--plan'], **plan))
                if args.probe and n == 4096:
                    trial = json.loads(subprocess.run(command+['--seed', 'optimization-check'], check=True, capture_output=True, text=True, timeout=180).stdout)
                    assert trial['verified'] and trial['real_crypto']
                    assert trial['online_gmw_ands_padded'] == plan['online_gmw_ands_padded']
                    assert trial['online_round_bound'] == plan['online_round_bound']
                    assert trial['online_total_sent_bytes'] >= payload(plan)
                    records.append(dict(version=version, executable_sha256=sha, command=command+['--seed', 'optimization-check'], **trial))
        # Plan only: compare the shared optimized primitive on both Batcher shapes.
        for m in (102, 1024):
            command = [str(binary), '--method', 'batcher', '--m', str(m), '--n', '1048576', '--bits', '32', '--plan']
            plan = json.loads(subprocess.run(command, check=True, capture_output=True, text=True, timeout=120).stdout)
            records.append(dict(version=version, executable_sha256=sha, estimated_payload_bytes=payload(plan), command=command, **plan))
    for n in (4096, 1048576):
        for method in ('cube', 'sqrt'):
            old, new = [next(r for r in records if r['version'] == v and r['type'] == 'public_schedule' and r['method'] == method and r['n'] == n) for v in ('before','after')]
            assert new['estimated_payload_bytes'] < old['estimated_payload_bytes']
            assert new['online_round_bound'] < old['online_round_bound']
            print(method, n, 'payload', old['estimated_payload_bytes'], '->', new['estimated_payload_bytes'], 'rounds', old['online_round_bound'], '->', new['online_round_bound'])
    args.out.parent.mkdir(parents=True, exist_ok=True)
    with args.out.open('x') as dest:
        json.dump(dict(scope='public plans and optional single small correctness/byte probes; no benchmark sweep', records=records), dest, indent=2)
        dest.write('\n')


if __name__ == '__main__':
    main()
