#!/usr/bin/env python3
"""Compare public Pi-median schedules only. NEVER executes a merge or benchmark.

The candidate set covers recursion rounding, leaf thresholds/backends and cube
block scales. Scores are estimates, not measured runtime or claims of optimality.
"""
import argparse
import hashlib
import itertools
import json
import math
from pathlib import Path
import subprocess


def integers(text):
    result = [int(value) for value in text.split(',')]
    if not result or any(value < 1 or value & (value - 1) for value in result):
        raise argparse.ArgumentTypeError('Expected comma-separated positive powers of two')
    return result


def shape(n, base, rounding, scale, max_depth=0):
    size = 1 << (n - 1).bit_length()
    children, blocks = [], []
    while size > base and (not max_depth or len(children) < max_depth):
        exponent = size.bit_length() - 1
        # Rounded versions of n^(2/3), bounded to make strict progress.
        child_exp = (2 * exponent + (2 if rounding == 'ceil' else 0)) // 3
        child = 1 << min(exponent - 1, child_exp)
        children.append(child)
        blocks.append(min(size, max(1, (size // child) * scale[0] // scale[1])))
        size = child
    return children, blocks


def candidates(n, bases, leaves):
    seen = set()
    for base, leaf, rounding, scale, depth in itertools.product(bases, leaves, ('floor', 'ceil'), ((1, 2), (1, 1), (2, 1)), (0, 2)):
        children, blocks = shape(n, base, rounding, scale, depth)
        signature = (base, leaf, tuple(children), tuple(blocks))
        if signature in seen:
            continue
        seen.add(signature)
        args = ['--n', str(n), '--base-case', str(base), '--max-depth', str(depth), '--leaf', leaf]
        if children:
            args += ['--children', ','.join(map(str, children)), '--cube-blocks', ','.join(map(str, blocks))]
        yield args


def select(plans, round_cost_bytes=0, max_rounds=None):
    feasible = [p for p in plans if max_rounds is None or p['online_round_bound'] <= max_rounds]
    if not feasible:
        raise ValueError('No candidate meets the round bound')
    return min(feasible, key=lambda p: (
        p['online_payload_bytes'] + round_cost_bytes * p['online_round_bound'],
        p['online_round_bound'], p['online_gmw_ands_padded'], p['expanded_size']))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--exe', type=Path, default=Path('out/build/linux/frontend/pimedian'))
    parser.add_argument('--n', type=int, help='Plan one size; otherwise use the exponent range')
    parser.add_argument('--min-log', type=int, default=10)
    parser.add_argument('--max-log', type=int, default=20)
    parser.add_argument('--bases', type=integers, default=[8, 16, 32, 64])
    parser.add_argument('--leaves', default='allpairs,batcher')
    parser.add_argument('--bits', type=int, default=32)
    parser.add_argument('--batch-size', type=int, default=1 << 20)
    parser.add_argument('--concurrency', type=int, default=2)
    parser.add_argument('--max-expanded-rows', type=int, default=1 << 28)
    parser.add_argument('--round-cost-bytes', type=float, default=0,
                        help='Public latency weight in byte equivalents per dependent step')
    parser.add_argument('--max-rounds', type=int)
    parser.add_argument('--out', type=Path, required=True, help='New JSON file; refuses to overwrite')
    parser.add_argument('--timeout', type=float, default=120)
    args = parser.parse_args()
    leaves = args.leaves.split(',')
    if any(leaf not in ('allpairs', 'batcher', 'bitonic') for leaf in leaves):
        parser.error('leaves must be allpairs, batcher and/or bitonic')
    if not 0 <= args.min_log <= args.max_log <= 26 or (args.n is not None and not 1 <= args.n <= 1 << 26):
        parser.error('invalid n or exponent range')
    if not math.isfinite(args.round_cost_bytes) or args.round_cost_bytes < 0 or args.timeout <= 0:
        parser.error('invalid score weight or timeout')
    exe = args.exe.resolve(strict=True)
    sizes = [args.n] if args.n is not None else [1 << e for e in range(args.min_log, args.max_log + 1)]
    result = dict(type='pi_median_public_parameter_plans', measured=False,
                  selection='payload + round_cost_bytes * dependency bound; public candidate set only',
                  round_cost_bytes=args.round_cost_bytes, executable=str(exe),
                  executable_sha256=hashlib.sha256(exe.read_bytes()).hexdigest(), sizes=[])
    # Reserve the destination before doing expensive planning; never truncate.
    with args.out.open('x', encoding='utf-8') as output:
        for n in sizes:
            plans, rejected = [], []
            for config in candidates(n, args.bases, leaves):
                command = [str(exe), *config, '--bits', str(args.bits), '--batch-size', str(args.batch_size),
                           '--concurrency', str(args.concurrency), '--max-expanded-rows', str(args.max_expanded_rows), '--plan']
                run = subprocess.run(command, check=False, capture_output=True, text=True, timeout=args.timeout)
                if run.returncode:
                    rejected.append(dict(command=command, error=run.stderr.strip()))
                    continue
                plan = json.loads(run.stdout)
                if plan.get('type') != 'public_schedule':
                    raise RuntimeError('Expected a public plan, never a protocol execution')
                plan['command'] = command
                plans.append(plan)
            best = select(plans, args.round_cost_bytes, args.max_rounds)
            result['sizes'].append(dict(n=n, selected=best, candidates=plans, rejected=rejected))
            print(f"n={n}: {len(plans)} public plans, selected {best['leaf']}, "
                  f"base={best['base_case']}, bytes={best['online_payload_bytes']}, "
                  f"steps={best['online_round_bound']}", flush=True)
        json.dump(result, output, indent=2)
        output.write('\n')


if __name__ == '__main__':
    main()
