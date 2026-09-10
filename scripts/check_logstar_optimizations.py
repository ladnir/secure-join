#!/usr/bin/env python3
"""Development-only fixed-parameter audit; never execute the paper-size sweep.

Public schedules at n=512 and n=2^20, optionally three small n=512 executions.
The block size stays four throughout; this script does not tune parameters.
"""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--exe', type=Path, default=Path('out/build/linux/frontend/logstar'))
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--execute-small', action='store_true')
    args = parser.parse_args()
    if args.output.exists():
        parser.error('Output exists; choose a fresh development output path.')
    exe = args.exe.resolve(strict=True)
    variants = [('previous_logstar', 0, False), ('optimized_logstar', 1, False),
                ('previous_batcher', 0, True), ('matched_batcher', 1, True)]
    result = dict(type='development_audit', formal_benchmark=False,
                  executable_sha256=hashlib.sha256(exe.read_bytes()).hexdigest(),
                  key_bits=32, fixed_logstar_block=4, batch_size=1048576,
                  public_schedules=[], small_executions=[])

    def run(n, optimized, baseline, plan, repeat=0):
        command = [str(exe), '--n', str(n), '--bits', '32', '--base', str(n if baseline else 4),
                   '--block', str(0 if baseline else 4), '--optimized', str(optimized),
                   '--batch-size', '1048576', '--seed', f'development-{repeat}']
        if plan:
            command.append('--plan')
        else:
            assert n == 512, 'Only sub-paper-size executions are permitted in this audit.'
        record = json.loads(subprocess.check_output(command, text=True, timeout=180))
        if not plan:
            assert record['verified'] and record['real_crypto']
            record['type'] = 'development_execution'
            record['repeat'] = repeat
        return record

    for n in (512, 1 << 20):
        for label, optimized, baseline in variants:
            record = run(n, optimized, baseline, True)
            assert 'online_payload_bytes' in record, 'Rebuild logstar to include the public byte estimator.'
            record['variant'] = label
            result['public_schedules'].append(record)
    if args.execute_small:
        for repeat in range(3):
            for label, optimized, baseline in variants[::1 if repeat % 2 == 0 else -1]:
                record = run(512, optimized, baseline, False, repeat)
                record['variant'] = label
                schedule = next(item for item in result['public_schedules']
                                if item['n'] == 512 and item['variant'] == label)
                assert record['online_gmw_ands_padded'] == schedule['padded_ands']
                assert record['online_round_bound'] == schedule['round_bound']
                record['framing_bytes'] = record['online_total_sent_bytes'] - schedule['online_payload_bytes']
                assert 0 <= record['framing_bytes'] < 8192, 'Unexpected unmodeled small-run communication.'
                result['small_executions'].append(record)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, indent=2) + '\n')
    for record in result['public_schedules']:
        print(record['n'], record['variant'], record['online_payload_bytes'], record['round_bound'])
    for record in result['small_executions']:
        print(record['variant'], record['repeat'], record['online_ms'], record['verified'])


if __name__ == '__main__':
    main()
