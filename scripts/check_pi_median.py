#!/usr/bin/env python3
"""Focused schedule/accounting and loopback TCP checks; no benchmark sweep."""
import argparse
import json
from pathlib import Path
import socket
import subprocess

from plan_pi_median import candidates, select, shape


def run(command):
    return json.loads(subprocess.check_output(command, text=True, timeout=120))


def validate(plan):
    assert plan['type'] == 'public_schedule'
    n, batches = plan['padded_size'], 1
    assert n >= plan['n'] and (n & (n-1)) == 0
    for level in plan['levels']:
        assert level['batches'] == batches and level['half_size'] == n
        child, k, block = level['child_size'], level['medians'], level['cube_block']
        assert n == child*k and n % block == 0 and child < n
        assert level['comparisons'] == 2*batches*(k*(n//block-1)+k*(k+1)//2*block)
        assert level['online_round_bound'] <= level['gmw_rounds_sum'] + 8
        batches *= 2*k
        n = child
    assert plan['expanded_size'] == 2*batches*n
    leaf_comparisons = batches * (n*n if plan['leaf'] == 'allpairs' else
                                  n*(n.bit_length()-1)+1 if plan['leaf'] == 'batcher' else
                                  n*n.bit_length())
    assert plan['comparisons'] == sum(level['comparisons'] for level in plan['levels']) + leaf_comparisons
    assert plan['offline_requests_per_party']['binary_ole'] == 2*plan['online_gmw_ands_padded']
    assert plan['online_payload_bytes'] >= plan['online_gmw_ands_padded']//2


def tcp(exe, config, mismatch=False):
    with socket.socket() as probe:
        probe.bind(('127.0.0.1', 0))
        port = probe.getsockname()[1]
    command = [str(exe), *config, '--address', f'127.0.0.1:{port}']
    # Asio retries the client connection while the server starts.
    processes = []
    try:
        for party in range(2):
            extra = (['--leaf', 'bitonic'] if mismatch == 'leaf' else ['--full-index']) if mismatch and party else []
            processes.append(subprocess.Popen([*command, '--party', str(party), *extra],
                                               stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True))
        output = [process.communicate(timeout=120) for process in processes]
        if mismatch:
            assert all(process.returncode != 0 for process in processes)
            assert all('Public configurations differ' in err for _, err in output)
            return
        assert all(process.returncode == 0 for process in processes), output
        rows = [json.loads(out) for out, _ in output]
        assert all(row['verified'] for row in rows)
        measured = rows[0]['online_p0_sent_bytes'] + rows[1]['online_p1_sent_bytes']
        planned = rows[0]['online_payload_bytes']
        assert rows[0]['online_p0_sent_bytes'] == rows[1]['online_p1_received_bytes']
        assert rows[1]['online_p1_sent_bytes'] == rows[0]['online_p0_received_bytes']
        # Coproto channel/message metadata is deliberately outside the payload model.
        assert 0 <= measured-planned < 16384, (measured, planned)
    finally:
        for process in processes:
            if process.poll() is None:
                process.kill()
            process.wait()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--exe', type=Path, default=Path('out/build/linux/frontend/pimedian'))
    args = parser.parse_args()
    exe = args.exe.resolve(strict=True)
    # Public schedule checks for every requested paper size: no online work.
    for exponent in range(10, 21):
        odd_even = run([str(exe), '--n', str(1 << exponent), '--plan'])
        bitonic = run([str(exe), '--n', str(1 << exponent), '--leaf', 'bitonic', '--plan'])
        validate(odd_even)
        validate(bitonic)
        assert odd_even['online_round_bound'] == bitonic['online_round_bound']
        assert odd_even['online_payload_bytes'] <= bitonic['online_payload_bytes']
        assert odd_even['online_gmw_ands_padded'] <= bitonic['online_gmw_ands_padded']
        for base in (8, 16, 32, 64):
            for rounding in ('floor', 'ceil'):
                children, blocks = shape(1 << exponent, base, rounding, (1, 2))
                assert len(children) == len(blocks)
                assert all(value and not value & (value-1) for value in children+blocks)
    # Keep actual development executions small. Test all leaf implementations,
    # irregular padding and exactly the same configurations over local and TCP.
    for leaf in ('allpairs', 'batcher', 'bitonic'):
        config = ['--n', '65', '--base-case', '8', '--leaf', leaf, '--pattern', 'duplicates', '--batch-size', '16384']
        plan = run([str(exe), *config, '--plan'])
        validate(plan)
        trial = run([str(exe), *config])
        assert trial['verified'] and trial['type'] == 'development_run'
        assert 0 <= trial['online_total_sent_bytes']-plan['online_payload_bytes'] < 16384
        assert sum(s['sent_bytes'] for s in trial['stages']) == trial['online_p0_sent_bytes']
        tcp(exe, config)
    tcp(exe, ['--n', '16'], mismatch=True)
    tcp(exe, ['--n', '16'], mismatch='leaf')
    # The latency weight can change the public selection independently of bytes.
    a = dict(online_payload_bytes=100, online_round_bound=20, online_gmw_ands_padded=128, expanded_size=16)
    b = dict(online_payload_bytes=200, online_round_bound=5, online_gmw_ands_padded=128, expanded_size=16)
    assert select([a, b]) is a and select([a, b], 10) is b and select([a, b], max_rounds=5) is b
    assert all('--n' in c and '--leaf' in c for c in candidates(1024, [8, 16], ['allpairs', 'batcher']))
    print('Pi-median: 22 large public schedules, 6 small local/TCP executions, mismatch rejection and selector checks passed')


if __name__ == '__main__':
    main()
