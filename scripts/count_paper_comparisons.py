#!/usr/bin/env python3
"""Collect exact C++ comparison counts for the active paper benchmark schedules.

Build the comparison_counts target first. This runs public circuit construction,
not fresh cryptography. It verifies gate counts, round bounds, and correlation
requests against the frozen plans before saving the comparison counts.
"""
import argparse
import datetime
import hashlib
import json
import pathlib
import subprocess
from summarize_paper_benchmarks import load_paper_study

ROOT = pathlib.Path(__file__).resolve().parents[1]
RAW = ROOT / 'docs/benchmarks/paper-2026'

def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--executable', type=pathlib.Path, default=ROOT/'out/build/linux/frontend/comparison_counts')
    parser.add_argument('--out', type=pathlib.Path, default=RAW/'implementation-comparison-counts.json')
    args = parser.parse_args()
    rows, costs, params, revision = load_paper_study()
    benchmark = ROOT/'out/build/linux/frontend/paper_benchmark'
    assert sha(benchmark) == rows[0]['executable_sha256'], 'Frozen timed benchmark changed'
    records = []
    for size in params['sizes']:
        n = size['n']
        for method in ['logstar', 'median']:
            plan = size['selected'][method]
            command = [str(args.executable.resolve()), '--method', method, '--n', str(n),
                       '--bits', str(params['key_bits']), '--batch-size', str(1 << 20),
                       '--concurrency', '2', *plan['options']]
            result = subprocess.run(command, check=True, capture_output=True, text=True)
            record = json.loads(result.stdout)
            assert (record['method'], record['n'], record['key_bits']) == (method, n, 32)
            fields = {'padded_ands': 'padded_ands' if method == 'logstar' else 'online_gmw_ands_padded',
                      'online_round_bound': 'round_bound' if method == 'logstar' else 'online_round_bound',
                      'offline_requests_per_party': 'offline_requests_per_party'}
            for field, planned in fields.items():
                assert record[field] == plan[planned], (n, method, field, record[field], plan[planned])
            if method == 'median':
                assert record['comparisons'] == plan['comparisons'], 'Existing Median counter changed'
            record['command'] = command
            record['selected_options'] = plan['options']
            record['original_schedule_verified'] = True
            records.append(record)
            print(f"{method} n={n}: {record['comparisons']} comparisons; original schedule verified", flush=True)
    sources = ['frontend/comparison_counts.cpp', 'frontend/CMakeLists.txt',
               'secure-join/Sort/PiLogStar.h', 'secure-join/Sort/PiLogStar.cpp',
               'secure-join/Sort/PackedLogStar.h', 'secure-join/Sort/PackedLogStar.cpp',
               'secure-join/Sort/BatcherMerge.h', 'secure-join/Sort/BatcherMerge.cpp',
               'secure-join/Sort/PiMedian.h', 'secure-join/Sort/PiMedian.cpp',
               'secure-join/Sort/MergeInternal.h', 'scripts/count_paper_comparisons.py']
    result = {
        'created_utc': datetime.datetime.now(datetime.timezone.utc).isoformat(),
        'definition': 'Scalar secure key comparisons in the constructed C++ schedule, summed over logical SIMD lanes and batched subproblems; includes semantic dummies, excludes 128-lane SIMD padding; reused results count once; parties do not duplicate the count.',
        'measurement_type': 'Exact public circuit accounting, not a timing measurement or asymptotic estimate',
        'executable_sha256': sha(args.executable),
        'frozen_timed_executable_sha256': sha(benchmark),
        'original_parameters_sha256': sha(RAW/'parameters.json'),
        'active_median_manifest_sha256': sha(RAW/'active-median-revision.json'),
        'source_sha256': {name: sha(ROOT/name) for name in sources},
        'records': records,
        'bbdlo_estimates': {'full': 127000000, 'subprotocol': 115000000,
                            'source': 'evaluation-before-rewrite.tex; retained approximate analytical estimates, not measured BBDLO costs'},
    }
    args.out.write_text(json.dumps(result, indent=2) + '\n')

if __name__ == '__main__':
    main()
