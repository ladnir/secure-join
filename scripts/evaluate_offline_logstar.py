#!/usr/bin/env python3
"""Matched preprocessing experiments; fresh correlations, unchanged online circuit.

Run serially, with alternating protocol order and rotating configuration order.
Each record identifies its executable and complete public parameters. Never mix
these measurements into the earlier packed online sweep without relabeling it.
"""
import argparse
import datetime
import hashlib
import json
import pathlib
import uuid

from benchmark_logstar import PROFILES, trial


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--exe', type=pathlib.Path, required=True)
    parser.add_argument('--out', type=pathlib.Path, required=True)
    parser.add_argument('--n', type=int, default=65536)
    parser.add_argument('--profiles', default='local,wan')
    parser.add_argument('--trials', type=int, default=3)
    parser.add_argument('--timeout', type=float, default=900)
    parser.add_argument('--configs', default='inline,large-batch')
    args = parser.parse_args()
    if args.n < 4 or args.n & (args.n - 1) or args.trials < 1 or args.timeout <= 0:
        parser.error('require power-of-two n >= 4, positive trials, and positive timeout')
    if any(p not in {'local', *PROFILES} for p in args.profiles.split(',')):
        parser.error('unknown network profile')
    exe = args.exe.resolve()
    sha = hashlib.sha256(exe.read_bytes()).hexdigest()
    configs = {'quarter-batch': 1 << 18, 'half-batch': 1 << 19,
               'inline': 1 << 20, 'large-batch': 1 << 22}
    names = args.configs.split(',')
    for name in names:
        if name not in configs:
            parser.error('unknown configuration: ' + name)
    args.out.parent.mkdir(parents=True, exist_ok=True)
    with args.out.open('x') as dest:
        for profile in args.profiles.split(','):
            for repeat in range(args.trials):
                order = names[repeat % len(names):] + names[:repeat % len(names)]
                for name in order:
                    batch = configs[name]
                    methods = ['pi-logstar', 'batcher']
                    if repeat % 2:
                        methods.reverse()
                    for method in methods:
                        options = ['--n', str(args.n), '--bits', '32',
                                   '--base', str(4 if method == 'pi-logstar' else args.n),
                                   '--block', '4', '--batch-size', str(batch),
                                   '--concurrency', '2',
                                   '--pattern', 'random', '--seed', str(repeat)]
                        print(profile, repeat, name, method, flush=True)
                        pair = uuid.uuid4().hex
                        measured = trial(exe, options, profile, args.timeout)
                        for row in measured:
                            if not row['verified'] or not row['real_crypto']:
                                raise RuntimeError('unverified or mock measurement')
                            row.update(executable_sha256=sha, profile=profile, benchmark_method=method,
                                       offline_config=name, repeat=repeat, trial_id=pair,
                                       command=[str(exe), *options],
                                       measured_at_utc=datetime.datetime.now(datetime.timezone.utc).isoformat())
                            dest.write(json.dumps(row) + '\n')
                            dest.flush()
                        print('offline_ms=', max(r['offline_ms'] for r in measured),
                              'online_ms=', max(r['online_ms'] for r in measured), flush=True)


if __name__ == '__main__':
    main()
