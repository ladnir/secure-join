#!/usr/bin/env python3
"""Matched asymmetric merges, serial fresh-crypto trials, public parameter tuning.

Tune block size using a public circuit/payload cost model, never secret inputs or
the fastest timing sample. The model omits transport framing. Save every candidate.
Reuse benchmark_logstar's isolated tc namespaces and process memory sampling.
"""
import argparse
import datetime
import hashlib
import json
import pathlib
import platform
import subprocess
import uuid

from benchmark_logstar import PROFILES, trial


def ceil_root(n, degree):
    lo, hi = 0, n
    while lo + 1 < hi:
        mid = (lo + hi) // 2
        if mid ** degree >= n:
            hi = mid
        else:
            lo = mid
    return hi


def payload(plan):
    """Application payload bytes, both parties, excluding message framing."""
    result = plan['online_gmw_ands_padded'] // 2
    if plan['method'] == 'batcher':
        return result
    m, n, b = plan['m'], plan['n'], plan['block_size']
    k = (n + b - 1) // b
    cw, iw, rw = max(1, m.bit_length()), max(1, (k - 1).bit_length()), (m+n-1).bit_length()
    record = b * (plan['key_bits'] + 1) + 2*cw + 1
    if plan.get('implementation_version', 1) < 2:
        record += iw + (cw if plan['method'] == 'sqrt' else 0)
    result += 2*(k+m)*((record+7)//8 + (b*cw+7)//8) + 8*(k+m)
    if plan.get('implementation_version', 1) >= 3:
        result += 4*(m+n)*((rw+7)//8)
    else:
        result += 2*(m+n)*((2*rw+1+7)//8) + 2*((m+n+7)//8) + 8*(m+n)
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--exe', type=pathlib.Path, default=pathlib.Path('out/build/linux/frontend/rootmerge'))
    parser.add_argument('--out', type=pathlib.Path, required=True)
    parser.add_argument('--min-log', type=int, default=10)
    parser.add_argument('--max-log', type=int, default=20)
    parser.add_argument('--profiles', default='local')
    parser.add_argument('--trials', type=int, default=3)
    parser.add_argument('--timeout', type=float, default=900)
    parser.add_argument('--parameters', type=pathlib.Path)
    parser.add_argument('--tune-only', action='store_true')
    parser.add_argument('--default-block', action='store_true')
    args = parser.parse_args()
    if not 1 <= args.min_log <= args.max_log <= 26 or args.trials < 1 or args.timeout <= 0:
        parser.error('invalid size range, trial count or timeout')
    profiles = args.profiles.split(',')
    if any(p not in {'local', *PROFILES} for p in profiles):
        parser.error('unknown profile')
    if args.parameters and args.default_block:
        parser.error('choose a parameter file or default blocks')
    exe = args.exe.resolve(strict=True)
    sha = hashlib.sha256(exe.read_bytes()).hexdigest()
    common = ['--bits', '32', '--batch-size', '1048576', '--concurrency', '2']
    choices = json.loads(args.parameters.read_text()) if args.parameters else {}
    args.out.parent.mkdir(parents=True, exist_ok=True)
    with args.out.open('x') as dest:
        def save(row):
            dest.write(json.dumps(row) + '\n')
            dest.flush()
        save(dict(type='environment', executable_sha256=sha, platform=platform.platform(),
                  profiles={p: PROFILES.get(p) for p in profiles},
                  utc=datetime.datetime.now(datetime.timezone.utc).isoformat(),
                  selection='public payload cost; ties by round bound',
                  default_block=args.default_block))
        for lg in range(args.min_log, args.max_log+1):
            n = 1 << lg
            for method, degree in [('cube', 3), ('sqrt', 2)]:
                m = ceil_root(n, degree)
                key = f'{method}:{n}'
                opts = ['--method', method, '--m', str(m), '--n', str(n), *common]
                if key not in choices:
                    default = 1 << (m-1).bit_length()
                    candidates = [default] if args.default_block else sorted({max(1, default//4), max(1, default//2), default, min(n, default*2)})
                    plans = []
                    for b in candidates:
                        command = [str(exe), *opts, '--block', str(b), '--plan']
                        row = json.loads(subprocess.run(command, check=True, capture_output=True, text=True, timeout=args.timeout).stdout)
                        row.update(type='parameter_candidate', executable_sha256=sha,
                                   estimated_payload_bytes=payload(row), command=command)
                        save(row)
                        plans.append(row)
                    best = min(plans, key=lambda p: (p['estimated_payload_bytes'], p['online_round_bound'], p['block_size']))
                    choices[key] = best['block_size']
                save(dict(type='selected_parameter', method=method, m=m, n=n, block_size=choices[key]))
                print('parameter', key, 'm', m, 'block', choices[key], flush=True)
        args.out.with_suffix('.parameters.json').write_text(json.dumps(choices, indent=2)+'\n')
        if args.tune_only:
            return
        for profile in profiles:
            for lg in range(args.min_log, args.max_log+1):
                n = 1 << lg
                for repeat in range(args.trials):
                    shapes = [('cube', 3), ('sqrt', 2)]
                    if repeat % 2:
                        shapes.reverse()
                    for shape, degree in shapes:
                        m = ceil_root(n, degree)
                        order = [shape, 'batcher'] if repeat % 2 == 0 else ['batcher', shape]
                        for method in order:
                            block = 0 if method == 'batcher' else choices[f'{shape}:{n}']
                            opts = ['--method', method, '--m', str(m), '--n', str(n),
                                    '--block', str(block), '--seed', str(repeat), *common]
                            print(profile, lg, shape, repeat, method, 'block', block, flush=True)
                            measured = trial(exe, opts, profile, args.timeout)
                            trial_id = uuid.uuid4().hex
                            for row in measured:
                                if not row['verified'] or not row['real_crypto'] or row.get('peak_sampled_swap_kib', 0):
                                    raise RuntimeError('invalid, mock, or swapped measurement')
                                row.update(shape=shape, profile=profile, repeat=repeat, trial_id=trial_id,
                                           executable_sha256=sha, command=[str(exe), *opts],
                                           measured_at_utc=datetime.datetime.now(datetime.timezone.utc).isoformat())
                                save(row)
                            print('offline_ms', max(r['offline_ms'] for r in measured),
                                  'online_ms', max(r['online_ms'] for r in measured), flush=True)


if __name__ == '__main__':
    main()
