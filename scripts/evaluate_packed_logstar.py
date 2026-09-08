#!/usr/bin/env python3
"""Public per-size tuning and serial, paired, verified benchmark runs."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import subprocess
import sys
import time
import uuid
from benchmark_logstar import trial, PROFILES


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--exe', type=Path, default=Path('out/build/linux/frontend/logstar'))
    p.add_argument('--min-exp', type=int, default=10)
    p.add_argument('--max-exp', type=int, default=20)
    p.add_argument('--bits', type=int, default=32)
    p.add_argument('--batch-size', type=int, default=1048576)
    p.add_argument('--trials', type=int, default=3)
    p.add_argument('--profiles', default='local')
    p.add_argument('--output', type=Path, default=Path('out/logstar/packed-scaling.jsonl'))
    p.add_argument('--schedule', type=Path, default=Path('out/logstar/packed-schedule.json'))
    p.add_argument('--plan-only', action='store_true')
    p.add_argument('--resume', action='store_true')
    p.add_argument('--timeout', type=int, default=3600)
    args = p.parse_args()
    exe = args.exe.resolve(strict=True)
    binary_hash = hashlib.sha256(exe.read_bytes()).hexdigest()
    run_id = uuid.uuid4().hex
    metadata = dict(type='environment', platform=platform.platform(), cpu_count=os.cpu_count(),
                    cpu_model=next(x.partition(':')[2].strip() for x in Path('/proc/cpuinfo').read_text().splitlines()
                                   if x.startswith('model name')),
                    binary_sha256=binary_hash, executable=str(exe), run_id=run_id,
                    profiles=PROFILES, argv=sys.argv, utc=time.strftime('%Y-%m-%dT%H:%M:%SZ', time.gmtime()),
                    methodology='Paired serial trials; public circuit-byte model chooses block; OS-seeded crypto.')
    schedules = []
    for exp in range(args.min_exp, args.max_exp + 1):
        n = 1 << exp
        candidates = []
        for block in (2, 4, 8, 16):
            cmd = [str(exe), '--plan', '--n', str(n), '--bits', str(args.bits),
                   '--base', str(block), '--block', str(block), '--batch-size', str(args.batch_size)]
            result = json.loads(subprocess.check_output(cmd, text=True))
            assert result['path'] == 'packed_partition'
            blocks = 2 * n // block
            block_bytes = (blocks.bit_length() - 1 + 1 + block * args.bits + 7) // 8
            # GMW communicates four bits per AND; the only block-dependent
            # non-GMW payload is block permutation/application (2*rowBytes+12).
            # Constant extraction traffic and transport framing are omitted.
            result['selection_cost_bytes'] = result['padded_ands'] // 2 + blocks * (2 * block_bytes + 12)
            candidates.append(result)
        best = min(candidates, key=lambda r: (r['selection_cost_bytes'], r['round_bound']))
        schedules.append(dict(n=n, bits=args.bits, block=best['block_override'],
                              base=best['base_case'], batch_size=args.batch_size, candidates=candidates))
    args.schedule.parent.mkdir(parents=True, exist_ok=True)
    args.schedule.write_text(json.dumps(dict(binary_sha256=binary_hash, schedules=schedules), indent=2) + '\n')
    if args.plan_only:
        for s in schedules:
            print(f"2^{s['n'].bit_length()-1}: m={s['block']}, " + ', '.join(
                f"m={r['block_override']}:{r['selection_cost_bytes']/2**20:.3f} MiB model/{r['round_bound']} depth"
                for r in s['candidates']))
        return
    completed = set()
    if args.resume and args.output.exists():
        for line in args.output.read_text().splitlines():
            r = json.loads(line)
            if r.get('type') == 'benchmark':
                if r.get('binary_sha256') != binary_hash:
                    raise RuntimeError('Resume requires exactly the same executable hash')
                if r.get('party') == -1 and r.get('verified'):
                    completed.add((r['n'], r['comparison_protocol'], r['profile'], r['trial']))
    elif args.output.exists():
        raise RuntimeError('Output already exists; choose another path or explicitly --resume')
    args.output.parent.mkdir(parents=True, exist_ok=True)
    with args.output.open('a') as out:
        def save(r):
            out.write(json.dumps(r) + '\n'); out.flush()
        save(metadata)
        for s in schedules:
            for profile in args.profiles.split(','):
                for repeat in range(args.trials):
                    methods = ['packed', 'batcher'] if repeat % 2 == 0 else ['batcher', 'packed']
                    for method in methods:
                        if (s['n'], method, profile, repeat) in completed:
                            continue
                        base, block = (s['base'], s['block']) if method == 'packed' else (s['n'], 0)
                        opts = ['--n', str(s['n']), '--bits', str(s['bits']), '--base', str(base),
                                '--block', str(block), '--batch-size', str(s['batch_size']),
                                '--concurrency', '2', '--seed', str(1000 + repeat)]
                        trial_id = uuid.uuid4().hex
                        print(f"n={s['n']} {method} m={block} batch={s['batch_size']} {profile} trial={repeat}",
                              file=sys.stderr, flush=True)
                        for r in trial(exe, opts, profile, args.timeout):
                            if not r.get('verified') or not r.get('real_crypto'):
                                raise RuntimeError('Refusing unverified or mocked benchmark')
                            r.update(profile=profile, trial=repeat, trial_id=trial_id, run_id=run_id,
                                     binary_sha256=binary_hash, comparison_protocol=method)
                            save(r)
                            if r['party'] == -1:
                                print(f"  online={r['online_total_sent_bytes']/2**20:.3f} MiB/{r['online_ms']:.1f} ms; "
                                      f"offline={r['offline_total_sent_bytes']/2**20:.3f} MiB/{r['offline_ms']:.1f} ms; "
                                      f"RSS={r['peak_rss_kib']/2**20:.2f} GiB", file=sys.stderr, flush=True)


if __name__ == '__main__':
    main()
