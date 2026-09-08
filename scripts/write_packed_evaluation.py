#!/usr/bin/env python3
"""Generate paper numbers from the complete verified packed experiment summary."""
import argparse
import hashlib
import json
from pathlib import Path
import re


ROOT = Path(__file__).resolve().parent.parent
ORIGINAL_BYTES = 31854
ORIGINAL_SHA256 = '1b8f96614154745314b397080e6a397a3e9a7df08e1498ffd6ca2e399476886b'


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--paper', type=Path, required=True)
    p.add_argument('--append', action='store_true', help='Append the new subsection, preserving the prior evaluation byte for byte')
    args = p.parse_args()
    dest = args.paper / 'plots/packed'
    summary = json.loads((dest / 'packed-summary.json').read_text())
    rows = {(r['profile'], r['n'], r['method']): r for r in summary['rows']}
    expected = {('local', 1 << e, method): 3 for e in range(10, 21)
                for method in ('Pi-logstar', 'Batcher')}
    expected.update({(profile, 1 << e, method): 3 for e in (12, 16)
                     for profile in ('lan', 'wan', 'slow') for method in ('Pi-logstar', 'Batcher')})
    expected.update({('wan', 1 << 20, method): 1 for method in ('Pi-logstar', 'Batcher')})
    if set(rows) != set(expected) or len(rows) != len(summary['rows']):
        raise ValueError('The paper requires the complete declared experiment grid without duplicates')
    for key, count in expected.items():
        if rows[key]['trials'] != count or rows[key]['batch'] != 1 << 20:
            raise ValueError(f'Unexpected trial count or batch size at {key}')
        if rows[key]['block'] != (4 if key[2] == 'Pi-logstar' else 0):
            raise ValueError(f'Unexpected selected block at {key}')
    schedule = json.loads((ROOT / 'docs/benchmarks/packed-schedule.json').read_text())
    if schedule['binary_sha256'] != summary['binary_sha256']:
        raise ValueError('Schedule and measurement executable hashes differ')
    if {s['n'] for s in schedule['schedules']} != {1 << e for e in range(10, 21)}:
        raise ValueError('Incomplete per-size public parameter search')
    for s in schedule['schedules']:
        if len(s['candidates']) != 4 or {c['block_override'] for c in s['candidates']} != {2, 4, 8, 16}:
            raise ValueError('Incomplete block search')
        best = min(s['candidates'], key=lambda c: (c['selection_cost_bytes'], c['round_bound']))
        if s['block'] != best['block_override'] or s['block'] != 4:
            raise ValueError('Selection does not match the declared objective')

    def row(exp, method='Pi-logstar', profile='local'):
        return rows[profile, 1 << exp, method]

    def reduction(a, b):
        return 100 * (1 - a / b)

    def f(value, decimals=3):
        return f'{value:,.{decimals}f}'.replace(',', r'{,}')

    library = [ROOT / ('secure-join/' + stem + ext)
               for stem in ('Sort/PiLogStar', 'Sort/PackedLogStar', 'Sort/StableSecretExtract',
                            'Sort/BatcherMerge', 'AggTree/BatchPrefix') for ext in ('.cpp', '.h')]
    library.append(ROOT / 'secure-join/Perm/Permutation.h')
    tests = [ROOT / s for s in ('frontend/logstar.cpp', 'tests/BatchPrefix_Tests.cpp',
                                'tests/StableSecretExtract_Tests.cpp', 'tests/ComposedPerm_Test.cpp')]
    counts = {str(path.relative_to(ROOT)): len(path.read_text().splitlines()) for path in library + tests}
    pilots = {}
    for filename, method in (('packed-batch-sweep.jsonl', 'Pi-logstar'),
                             ('packed-batcher-batch-sweep.jsonl', 'Batcher')):
        for line in (ROOT / 'docs/benchmarks' / filename).read_text().splitlines():
            r = json.loads(line)
            if r.get('type') != 'benchmark':
                continue
            if not r['verified'] or not r['real_crypto'] or r['binary_sha256'] != summary['binary_sha256']:
                raise ValueError('Unverified or different-build batch pilot')
            key = (r['n'], method, r['batch_size'])
            if key in pilots or r['key_bits'] != 32 or r['party'] != -1:
                raise ValueError('Duplicate or mismatched batch pilot')
            pilots[key] = r
    if set(pilots) != {(1 << e, method, 1 << b) for e in (14, 16)
                      for method in ('Pi-logstar', 'Batcher') for b in (20, 22)}:
        raise ValueError('Incomplete matched batch-size pilot grid')
    byte_changes, time_ratios = [], []
    for n, method in {(k[0], k[1]) for k in pilots}:
        a, b = pilots[n, method, 1 << 20], pilots[n, method, 1 << 22]
        byte_changes.append(reduction(b['offline_total_sent_bytes'], a['offline_total_sent_bytes']))
        time_ratios.append(b['offline_ms'] / a['offline_ms'])
    if min(byte_changes) <= 0:
        raise ValueError('Batch traffic no longer follows the template claim; review the prose')
    small, small_b = row(10), row(10, 'Batcher')
    large, large_b = row(20), row(20, 'Batcher')
    values = dict(
        packedSmallReduction=f(reduction(small['online_mib'], small_b['online_mib']), 1),
        packedLargeReduction=f(reduction(large['online_mib'], large_b['online_mib']), 1),
        packedLibraryLoc=f(sum(counts[str(path.relative_to(ROOT))] for path in library), 0),
        packedTestLoc=f(sum(counts[str(path.relative_to(ROOT))] for path in tests), 0),
        packedSmallPi=f(small['online_mib']), packedSmallBatcher=f(small_b['online_mib']),
        packedLargePi=f(large['online_mib']), packedLargeBatcher=f(large_b['online_mib']),
        packedReductionOld=f(reduction(large['online_mib'], 2125.159), 1),
        packedLargeOnlinePi=f(large['online_s']), packedLargeOnlineBatcher=f(large_b['online_s']),
        packedLargeOfflinePi=f(large['offline_s'], 1), packedLargeOfflineBatcher=f(large_b['offline_s'], 1),
        packedLargeRssPi=f(large['rss_gib'], 2), packedLargeRssBatcher=f(large_b['rss_gib'], 2),
        packedLargeOfflineBytesPi=f(large['offline_mib']), packedLargeOfflineBytesBatcher=f(large_b['offline_mib']),
        packedLargeDepthPi=str(large['rounds']), packedLargeDepthBatcher=str(large_b['rounds']))
    values['packedBatchStatement'] = (
        f"The larger batch reduces offline traffic by {f(min(byte_changes), 1)}--{f(max(byte_changes), 1)}\\% across these pilots. "
        f"Its preprocessing time is {f(min(time_ratios), 2)}--{f(max(time_ratios), 2)} times that of the smaller batch.")
    max_swap = max(r['sampled_swap_gib'] for r in rows.values())
    local_swap = max(r['sampled_swap_gib'] for key, r in rows.items() if key[0] == 'local')
    values['packedPagingStatement'] = (
        'No process swap was observed in the sampled local executions.' if local_swap == 0 else
        f"Sampled local process swap reached {f(local_swap, 2)}\\,GiB; the affected local timings include paging.")
    template = (ROOT / 'docs/packed-evaluation.tex').read_text()
    used = set(re.findall(r'\\(packed[A-Z][A-Za-z]+)', template))
    if used != set(values):
        raise ValueError(f'Template/value mismatch: {used ^ set(values)}')
    (dest / 'packed-values.tex').write_text('% Generated from verified measurements; do not edit numbers manually.\n' +
        ''.join(f'\\newcommand{{\\{key}}}{{{value}}}\n' for key, value in values.items()))
    source_hashes = {str(path.relative_to(ROOT)): hashlib.sha256(path.read_bytes()).hexdigest()
                     for path in library + tests}
    (dest / 'packed-loc.json').write_text(json.dumps(dict(files=counts, source_sha256=source_hashes,
                                                       binary_sha256=summary['binary_sha256']), indent=2) + '\n')

    table = [r'\begin{table}[!htbp]', r'\centering', r'\small', r'\setlength{\tabcolsep}{4pt}',
             r'\caption{Complete local sweep. Times are three-run medians; communication sums sent bytes over both parties. The depth column is the online dependency bound.}',
             r'\label{tab:packed-scaling}', r'\begin{tabular}{rlrrrrr}', r'\hline',
             r'$n$/list & Protocol & On. MiB & Off. MiB & On. s & Off. s & Depth \\', r'\hline']
    for exp in range(10, 21):
        for method in ('Pi-logstar', 'Batcher'):
            r = row(exp, method)
            name = r'\ourprotocol' if method == 'Pi-logstar' else 'Batcher'
            n = f'$2^{{{exp}}}$' if method == 'Pi-logstar' else ''
            table.append(f"{n} & {name} & {f(r['online_mib'], 2)} & {f(r['offline_mib'], 2)} & "
                         f"{f(r['online_s'], 3)} & {f(r['offline_s'], 2)} & {r['rounds']} " + r'\\')
        if exp != 20:
            table.append(r'\noalign{\smallskip}')
    table += [r'\hline', r'\end{tabular}', r'\end{table}']
    (dest / 'packed-table.tex').write_text('\n'.join(table) + '\n')

    net = [r'At $2^{16}$, the median online times for \ourprotocol\ and Batcher are']
    segments = []
    for profile, name in (('lan', 'LAN'), ('wan', 'WAN'), ('slow', 'slow WAN')):
        a, b = row(16, profile=profile), row(16, 'Batcher', profile)
        segments.append(f"{f(a['online_s'])}/{f(b['online_s'])}\\,s on {name}")
    net.append(', '.join(segments[:-1]) + ', and ' + segments[-1] + '.')
    net.append('The corresponding offline-plus-online times are')
    segments = []
    for profile, name in (('lan', 'LAN'), ('wan', 'WAN'), ('slow', 'slow WAN')):
        a, b = row(16, profile=profile), row(16, 'Batcher', profile)
        segments.append(f"{f(a['total_s'], 1)}/{f(b['total_s'], 1)}\\,s on {name}")
    net.append(', '.join(segments[:-1]) + ', and ' + segments[-1] + '.')
    a, b = row(20, profile='wan'), row(20, 'Batcher', 'wan')
    net += [f"The single $2^{{20}}$ WAN trial takes {f(a['online_s'], 2)} versus {f(b['online_s'], 2)}\\,s online,",
            f"and {f(a['total_s'], 1)} versus {f(b['total_s'], 1)}\\,s including preprocessing.",
            r'\Cref{fig:packed-network} includes all network measurements, including $2^{12}$.',
            'At small sizes, extra dependency depth can offset online byte savings.']
    if a['online_s'] < b['online_s'] and abs(reduction(a['total_s'], b['total_s'])) < 1:
        net += [f"At $2^{{20}}$, online time is {f(reduction(a['online_s'], b['online_s']), 1)}\\% shorter,",
                f"while the totals differ by only {f(abs(reduction(a['total_s'], b['total_s'])), 1)}\\% in this single pair."]
    crossovers = [name for profile, name in (('lan', 'LAN'), ('wan', 'WAN'), ('slow', 'slow WAN'))
                  if row(12, profile=profile)['online_s'] >= row(12, 'Batcher', profile)['online_s']
                  and row(16, profile=profile)['online_s'] < row(16, 'Batcher', profile)['online_s']]
    if crossovers:
        names = ', '.join(crossovers)
        net += [f"On {names}, Batcher has the lower median online time at $2^{{12}}$,",
                r'whereas \ourprotocol\ has the lower median at $2^{16}$.',
                'These sampled sizes bracket a change in the faster protocol; we do not infer an exact break-even size.']
    network_swap = max(r['sampled_swap_gib'] for key, r in rows.items() if key[0] != 'local')
    if network_swap:
        net.append(f"Sampled network-process swap reached {f(network_swap, 2)}\\,GiB; the affected TCP timings include paging.")
    calibration = ROOT / 'docs/benchmarks/packed-network-calibration.jsonl'
    records = [json.loads(line) for line in calibration.read_text().splitlines()]
    goodput = {profile: [r['receiver']['goodput_mbit_s'] for r in records
                        if r['type'] == 'tcp_goodput' and r['profile'] == profile]
               for profile in ('lan', 'wan', 'slow')}
    rtts = {}
    for r in records:
        if r['type'] == 'unloaded_rtt':
            match = re.search(r'= [\d.]+/([\d.]+)/', r['ping_stdout'])
            if not match:
                raise ValueError('Cannot parse calibration RTT')
            rtts[r['profile']] = float(match.group(1))
    if set(rtts) != set(goodput) or any(len(v) != 2 for v in goodput.values()):
        raise ValueError('Incomplete bidirectional link calibration')
    net += ['A separate unloaded calibration measures mean RTTs of ' +
            ', '.join(f"{f(rtts[p], 2)}\\,ms" for p in ('lan', 'wan', 'slow')) +
            ' for LAN, WAN, and slow WAN.',
            'Forward/reverse TCP goodput lies in the respective ranges ' +
            ', '.join(f"{f(min(goodput[p]), 1)}--{f(max(goodput[p]), 1)}\\,Mbit/s" for p in ('lan', 'wan', 'slow')) + '.']
    (dest / 'packed-network-text.tex').write_text('\n'.join(net) + '\n')

    byte_wins = sum(row(e)['online_mib'] < row(e, 'Batcher')['online_mib'] for e in range(10, 21))
    time_wins = sum(row(e)['online_s'] < row(e, 'Batcher')['online_s'] for e in range(10, 21))
    wan_pi, wan_b = row(20, profile='wan'), row(20, 'Batcher', 'wan')
    intro = f"""# Optimized Pi-logstar: results and reproduction

The optimized implementation uses fewer online bytes at **{byte_wins}/11 sizes**,
covering every power of two from 2^10 through 2^20 keys per input list.
Each local point is the median of three verified real-cryptography executions.
At 2^20, it sends **{large['online_mib']:,.3f} MiB versus {large_b['online_mib']:,.3f} MiB**
online, a **{reduction(large['online_mib'], large_b['online_mib']):.1f}% reduction**.
Median local online time is **{large['online_s']:.3f} s versus {large_b['online_s']:.3f} s**.
Its median online time is lower at {time_wins}/11 sampled sizes; the plots show
the observed timing ranges, not confidence intervals.
At 2^20 the online ranges overlap, so the small median difference does not
establish a consistent local speed advantage.

The single 2^20 WAN pair (100 Mbit/s per direction, 40 ms RTT) took
**{wan_pi['online_s']:.2f} s versus {wan_b['online_s']:.2f} s online**. Including preprocessing,
the totals were **{wan_pi['total_s']:.2f} s versus {wan_b['total_s']:.2f} s**.
This largest network point is one execution per protocol, without a variability estimate.

Preprocessing is a separate tradeoff: at 2^20 it sends
{large['offline_mib']:,.3f} MiB versus {large_b['offline_mib']:,.3f} MiB, and takes
{large['offline_s']:.1f} s versus {large_b['offline_s']:.1f} s locally.
The online dependency bounds are {large['rounds']} versus {large_b['rounds']}.
The online-byte improvement does not imply a universal reduction in total
bandwidth or latency. The TCP table below includes preprocessing plus online time.

## What changed

The concrete path uses one partition and four-row terminal blocks. Compressed
block identifiers, parallel all-pairs base merges, a hybrid broadcast prefix,
direct global ranks, and stable extraction by joint shuffling remove repeated
indices, key swaps, and per-row rank conversion. See the
[algorithm and semi-honest security argument](packed-logstar.md).
The inherited biased 32-bit modulo permutation sampler is also replaced by
`std::shuffle` with the cryptographic PRNG's 64-bit interface. All new results
use this correction; earlier builds are retained only as development diagnostics.
This specialization targets concrete sizes; it makes no improved asymptotic
claim for a fixed block size. The general padded and recursive path remains available.

## Measurement procedure

- Lenovo 83DF, Intel Core i9-14900HX, approximately 32 GiB physical RAM;
  WSL2 exposes 32 logical CPUs and approximately 15 GiB RAM.
- GCC 13.3, CMake 3.28.3, C++20 Release, `-O3 -march=native`.
- Local: both parties on one OS thread. TCP: separate processes, each with
  two Boost.Asio I/O workers. Two concurrent correlation batches are coroutine
  overlap. CPU affinity and frequency are not fixed.
- Same GMW backend and stable gather output for both protocols; Batcher is a
  bitonic merge network with balanced comparisons and final-output projection.
- 32-bit random synthetic keys; matched input seeds, alternating protocol order,
  fresh OS-seeded cryptographic randomness, real OT/OLE and permutation protocols.
- Independent public cost selection among block sizes 2, 4, 8, 16 at every n.
  Block 4 minimizes the public byte-cost objective throughout the grid. This selection does not
  optimize separately for network latency. All candidates are archived.
- Matched 2^20-entry correlation batches. Separate batch-size pilots compare
  2^20 and 2^22 at 2^14 and 2^16; those pilots are excluded from the main figures.
- MiB = 2^20 bytes. Communication sums the parties' sent bytes once, including
  coproto framing and excluding TCP/IP headers. Setup and output verification
  are outside phase timings. TCP time uses the slower party for each phase.
  Local Batcher charges 24 bytes of session registration per party online;
  TCP initializes that session in its excluded configuration handshake.
  The raw transport counters are retained without normalization.
- Peak RSS: maximum across trials, both parties together locally and maximum
  per-party RSS for TCP. Process swap is sampled every 0.5 s; maximum observed
  process swap across the main runs is {max_swap:.3f} GiB.
- Isolated netns/veth links with `tc netem`: LAN 1 Gbit/s and 0.2 ms RTT,
  WAN 100 Mbit/s and 40 ms RTT, slow WAN 10 Mbit/s and 40 ms RTT.
  Three paired trials per profile at 2^12 and 2^16; one paired WAN trial at 2^20.
  The latter is a scale check without a variability estimate.

## Raw records and figures

The [raw-record index](benchmarks/README.md) describes the JSONL files,
candidate schedules, and unloaded link calibration. All main plotted records
use one executable hash. Earlier implementation results are retained separately.
The appended paper subsection and figures are in the accompanying paper repository,
under `evaluation.tex` and `plots/packed/`. The previous evaluation text is preserved.

"""
    tables = (dest / 'packed-results.md').read_text()
    reproduction = r'''
## Reproduce

Build and validation instructions are in [the API guide](pi-logstar.md).
From the repository root, use fresh output paths (existing measurement files
are never silently overwritten):

```sh
python3 scripts/evaluate_packed_logstar.py --trials 3 --output out/reproduce/packed-scaling.jsonl --schedule out/reproduce/packed-schedule.json
sudo python3 scripts/evaluate_packed_logstar.py --min-exp 12 --max-exp 12 --trials 3 --profiles lan,wan,slow --output out/reproduce/packed-network-12.jsonl --schedule out/reproduce/schedule-12.json
sudo python3 scripts/evaluate_packed_logstar.py --min-exp 16 --max-exp 16 --trials 3 --profiles lan,wan,slow --output out/reproduce/packed-network-16.jsonl --schedule out/reproduce/schedule-16.json
sudo python3 scripts/evaluate_packed_logstar.py --min-exp 20 --max-exp 20 --trials 1 --profiles wan --output out/reproduce/packed-network-20.jsonl --schedule out/reproduce/schedule-20.json
sudo python3 scripts/calibrate_logstar_network.py --output out/reproduce/packed-network-calibration.jsonl
python3 scripts/plot_packed_logstar.py out/reproduce/packed-scaling.jsonl out/reproduce/packed-network-12.jsonl out/reproduce/packed-network-16.jsonl out/reproduce/packed-network-20.jsonl --output out/reproduce/plots
```

Run experiments serially without overlapping compilation, plotting, or link
calibration. `--resume` resumes verified local trials from the same executable.
To regenerate the paper's numeric includes from the delivered complete records:

```sh
python3 scripts/write_packed_evaluation.py --paper ../64c0aeaf1c1f5473b45f1e06
```

The optional `--append` flag updates only this task's appended subsection after
checking the original evaluation's byte prefix and SHA-256 digest. It requires
the preserved snapshot at `out/evaluation-before-packed.tex`.
'''
    pilot_table = ['\n## Matched batch-size pilots\n',
                   'One execution per configuration; excluded from the main plots.\n',
                   '| n/list | Protocol | Batch entries | Offline MiB | Offline s | Peak RSS GiB |',
                   '|---:|---|---:|---:|---:|---:|']
    for (n, method, batch), r in sorted(pilots.items()):
        pilot_table.append(f"| {n:,} | {method} | {batch:,} | {r['offline_total_sent_bytes']/2**20:.3f} | "
                           f"{r['offline_ms']/1000:.3f} | {r['peak_rss_kib']/2**20:.2f} |")
    (ROOT / 'docs/packed-results.md').write_text(intro + tables.replace('# Optimized Pi-logstar measurements', '## Complete measurements', 1) +
                                               '\n'.join(pilot_table) + '\n' + reproduction)

    if args.append:
        original = (ROOT / 'out/evaluation-before-packed.tex').read_bytes()
        if len(original) != ORIGINAL_BYTES or hashlib.sha256(original).hexdigest() != ORIGINAL_SHA256:
            raise ValueError('Original evaluation snapshot does not match its recorded digest')
        path = args.paper / 'evaluation.tex'
        current = path.read_bytes()
        marker = b'% BEGIN OPTIMIZED PACKED PI-LOGSTAR EVALUATION'
        if current != original and not (current.startswith(original) and current[len(original):].lstrip().startswith(marker)):
            raise ValueError('The current evaluation contains unrelated edits; refusing to overwrite them')
        path.write_bytes(original + b'\n\n' + template.encode())
        assert path.read_bytes()[:ORIGINAL_BYTES] == original
    print(json.dumps(dict(binary_sha256=summary['binary_sha256'], groups=len(rows),
                          local_trials=66, tcp_party_records=76, source_line_counts=counts,
                          appended=args.append), indent=2))


if __name__ == '__main__':
    main()
