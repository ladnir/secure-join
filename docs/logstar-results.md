# Pi-logstar development results — September 7, 2026

The implementation runs with real two-party cryptography and returns the correct
secret-shared merge permutation. These are preliminary development measurements,
not a claim that Pi-logstar dominates a conventional merge network. Batcher remains
a strong baseline at the smaller sizes; preprocessing is a substantial part of
the total cost. See [the implementation notes](pi-logstar.md) for the protocol,
security assumptions, API, and the distinction between the implemented main
construction and the optional asymptotic pruning extension.

The final executable used for the results below has SHA-256
`8bd169cee4a74d6edcce0345dbd04a2fc0a7c3b3532988c532dd43dfeb93b0ce`.
The [raw JSONL records](benchmarks/README.md) accompany this report.
The machine has an Intel Core i9-14900HX, with 32 logical CPUs visible to Ubuntu
under WSL2 (Linux 6.6.87.1, glibc 2.39). Builds use GCC 13.3, CMake 3.28.3, C++20,
Release optimization, real libOTe, and Boost networking. Dependency revisions:
libOTe `657f6da90bff5774a2d01c824e997572d5e8ba00`, coproto `adcfcc47`, and
libsodium `778c9cf4`, with Boost 1.86.

Local runs put both parties in one process using `LocalAsyncSocket`; the network
runs use two independent processes. Preprocessing uses two concurrent correlation
batches. Timings are medians of three fresh executions in the scaling/network
tables, two executions in the key/batch sweep, and one execution in the large
case. CPU affinity and clock frequency were not locked. Every run checks the
reconstructed result against an independent stable merge *after* measurement.
The public data seeds select synthetic inputs; cryptographic randomness comes
from independent OS-seeded PRNGs.

Bytes sum both parties' transmitted bytes once, including coproto framing and
excluding TCP/IP headers. Phase counters are flushed; output opening, public
configuration negotiation, and harness barriers are excluded. Online rounds are
a conservative dependency-depth upper bound, including permutation and radix
conversion steps, not observed packet counts. MiB means 2^20 bytes. Here `n` is
the number of keys **per party/list**, so each merge has `2n` real keys.
Times are elapsed wall-clock measurements. Public circuit allocation/setup is
reported as `setup_ms` in the raw records and excluded from the offline/online
columns. Adding column medians does not give the median end-to-end runtime.

## Scaling with 32-bit keys

All rows use correlation batches of 262,144. `Default` is cutoff 16 with the
automatic block size (16 at these sizes). `Tuned` uses cutoff 16 and block size 8.
`Batcher` sets the cutoff above the input size, invoking the same backend's
standalone bitonic merge, including the same final-output projection optimization.

| n/list | Configuration | Offline MiB | Online MiB | Offline ms | Online ms | Online rounds ≤ |
|---:|---|---:|---:|---:|---:|---:|
| 1,024 | Default | 8.020 | 1.716 | 399.48 | 2.26 | 137 |
| 1,024 | Tuned | 7.675 | 1.551 | 382.52 | 2.42 | 139 |
| 1,024 | Batcher | 1.943 | 0.979 | 161.39 | 2.92 | 88 |
| 4,096 | Default | 28.326 | 7.026 | 1,311.84 | 8.06 | 157 |
| 4,096 | Tuned | 27.366 | 6.570 | 1,188.13 | 7.49 | 159 |
| 4,096 | Batcher | 9.332 | 4.795 | 648.11 | 6.09 | 104 |
| 16,384 | Default | 111.758 | 29.509 | 4,764.66 | 31.56 | 177 |
| 16,384 | Tuned | 108.487 | 27.832 | 4,716.36 | 32.85 | 179 |
| 16,384 | Batcher | 44.448 | 22.893 | 2,633.20 | 21.38 | 120 |
| 65,536 | Default | 465.754 | 126.872 | 20,221.70 | 144.04 | 198 |
| 65,536 | Tuned | 459.218 | 120.543 | 18,685.74 | 142.61 | 200 |
| 65,536 | Batcher | 215.494 | 111.192 | 13,118.50 | 102.96 | 136 |

The tuned Pi-logstar communication premium over Batcher falls from about 58%
at 1,024 keys/list to about 8.4% at 65,536 keys/list. The tuned case's peak process
RSS at 65,536 keys/list was approximately 1.35 GiB, including both parties.
Online elapsed time alone is not the total protocol cost: the separate preprocessing
column is much larger and requires fresh correlations for each invocation.

At 262,144 keys/list, a single larger run with correlation batches of 1,048,576
shows an online communication crossover. This is a byte-count improvement, not
an overall runtime improvement; Pi-logstar still needs substantially more
preprocessing traffic and a higher online dependency depth.

| n/list | Configuration | Offline MiB | Online MiB | Offline ms | Online ms | Online rounds ≤ |
|---:|---|---:|---:|---:|---:|---:|
| 262,144 | Tuned | 1,021.829 | 506.045 | 84,822.39 | 731.20 | 220 |
| 262,144 | Batcher | 286.967 | 512.267 | 65,584.85 | 484.02 | 152 |

The tuned online byte count is about 1.2% lower. Its physical recursion contains
1,048,576 rows, compared with 524,288 for the pure Batcher merge.

## Emulated networks

These runs use independent TCP processes, 4,096 keys/list, 32-bit keys,
correlation batches of 262,144, and three fresh trials per configuration.
Each direction has a separate `tc netem` egress rate limit and half of the
configured RTT. Links are isolated veth pairs on this machine; CPU affinity,
packet offloads, and TCP behavior are not controlled as on dedicated lab hosts.
Bandwidth and RTT labels below describe the configured profiles.

| Link configuration | Configuration | Offline ms | Online ms | Offline MiB | Online MiB | Online rounds ≤ |
|---|---|---:|---:|---:|---:|---:|
| 1 Gbit/s, 0.2 ms RTT | Tuned | 1,201.67 | 63.87 | 27.366 | 6.570 | 159 |
| 1 Gbit/s, 0.2 ms RTT | Batcher | 646.30 | 44.46 | 9.332 | 4.795 | 104 |
| 100 Mbit/s, 40 ms RTT | Tuned | 2,697.31 | 3,526.11 | 27.366 | 6.570 | 159 |
| 100 Mbit/s, 40 ms RTT | Batcher | 1,598.70 | 2,362.35 | 9.332 | 4.795 | 104 |
| 10 Mbit/s, 40 ms RTT | Tuned | 19,641.39 | 6,339.20 | 27.366 | 6.570 | 159 |
| 10 Mbit/s, 40 ms RTT | Batcher | 8,716.50 | 4,331.60 | 9.332 | 4.795 | 104 |

Batcher wins at this input size on all tested links. Reducing bit-level comparison
depth and preprocessing costs remains valuable: batching subproblems avoids
serializing recursion, but does not eliminate the dependency depth of GMW gates.
The online byte counts agree with the local runs; network shaping changes elapsed
time, not the protocol's public communication schedule.

A separate idle-link calibration measured the following application goodput
and unloaded ping RTT. Each direction used one fresh connection, a 1 MiB warmup,
then a transfer lasting at least two seconds including receiver drain.

| Profile | Goodput forward / reverse (Mbit/s) | Mean unloaded RTT (ms) |
|---|---:|---:|
| LAN | 956.674 / 956.678 | 0.279 |
| WAN | 95.718 / 95.709 | 40.388 |
| Slow WAN | 9.570 / 9.571 | 40.264 |

The raw calibration records include qdisc statistics and offload settings:
TSO/GSO were enabled and GRO disabled. These results confirm plausible bulk
rates, not precise emulation of short TCP exchanges. The
[netem documentation](https://www.man7.org/linux/man-pages/man8/netem.8.html)
warns that sender-egress emulation can interact with TCP Small Queues; receiver
ingress shaping and dedicated hosts would strengthen publication measurements.
All temporary benchmark/calibration namespaces were removed after use.

## Key width and correlation batches

These runs use 4,096 keys/list, cutoff 16, block size 8, and two fresh trials.
Larger correlation batches reduce repeated setup traffic but need not reduce
elapsed time. The default 262,144 is a compromise; the batch size is public and
does not change the online circuit or its communication.

| Key bits | Correlation batch | Offline MiB | Online MiB | Offline ms | Online ms | Online rounds ≤ |
|---:|---:|---:|---:|---:|---:|---:|
| 32 | 65,536 | 65.432 | 6.570 | 1,233.16 | 8.42 | 159 |
| 32 | 262,144 | 27.366 | 6.570 | 1,197.44 | 7.53 | 159 |
| 32 | 1,048,576 | 15.967 | 6.570 | 1,319.53 | 7.51 | 159 |
| 64 | 65,536 | 91.343 | 10.186 | 1,510.38 | 11.38 | 174 |
| 64 | 262,144 | 35.755 | 10.186 | 1,663.13 | 10.42 | 174 |
| 64 | 1,048,576 | 19.303 | 10.186 | 1,788.77 | 9.06 | 174 |

## Optimizations and parameter choices

- Preserving physical sorted keys removes the interactive first-real median
  computation and resolves all-dummy block ordering.
- The prefix scan batches all same-depth subproblems and flattens small bit
  products into SIMD lanes. A 128-leaf, 896-bit prefix needs 222,208 padded ANDs
  instead of 1,491,840, with the same 13 interactive layers. These are exact
  circuit counts, not an isolated elapsed-time comparison.
- The last network layer swaps only needed index/flag bits. Ordinary full-row
  merge callers retain their original behavior.
- Cutoff 16 avoids an extra recursive doubling for automatic 16-element blocks.
  In the initial implementation, increasing the cutoff from 8 to 16 reduced
  online communication at 4,096 keys/list from 14.417 to 7.802 MiB, before the
  packing/projection improvements.
- The optimized block sweep at 16,384 keys/list measured 27.784, 27.832, and
  29.509 MiB online for blocks 4, 8, and 16 respectively. Block 8 had the lowest
  preprocessing traffic. The default block 16 uses two fewer online dependency
  steps than block 8; the explicit block override exposes this tradeoff.

## Validation

`ctest -R pi_logstar_real_crypto` passes with real OT/OLE and no mock mode.
It covers 8,532 comparator circuit cases, batched payload-preserving merges,
projected output bits, 127/256-bit keys, duplicate/maximal keys, non-power-of-two
input lengths, multiple recursion levels, random XOR input shares, invalid
parameters, mock rejection, and correlation reuse rejection. The independent
`BatchPrefix_Test` passes, including 127/128/129-batch boundaries and partial-byte
values. The plaintext reference passes 23,270 exhaustive/random cases with
`--trials 1000`, checking physical sortedness and exact-once real indices at
every recursive return. Independent TCP processes also verify the result.
The archived final matrix contains 68 fresh protocol trials (86 local/TCP
records). Every trial uses the recorded final binary, verifies successfully,
and has matching peer send/receive counters in both measured phases.

## Reproducing measurements

From the repository root, after building as described in the implementation notes:

```sh
python3 scripts/benchmark_logstar.py --sizes 1024,4096,16384,65536 \
  --bases 16 --blocks 0,8 --trials 3 --output out/logstar/scaling-pi.jsonl
python3 scripts/benchmark_logstar.py --sizes 1024,4096,16384,65536 \
  --bases 65536 --blocks 0 --trials 3 --output out/logstar/scaling-batcher.jsonl
python3 scripts/summarize_logstar.py out/logstar/scaling-pi.jsonl \
  out/logstar/scaling-batcher.jsonl
python3 scripts/benchmark_logstar.py --sizes 4096 --bits 32,64 \
  --bases 16 --blocks 8 --batch-sizes 65536,262144,1048576 --trials 2 \
  --output out/logstar/parameters.jsonl
python3 scripts/benchmark_logstar.py --sizes 262144 --bases 16 --blocks 8 \
  --batch-sizes 1048576 --trials 1 --output out/logstar/large-pi.jsonl
python3 scripts/benchmark_logstar.py --sizes 262144 --bases 262144 \
  --batch-sizes 1048576 --trials 1 --output out/logstar/large-batcher.jsonl
sudo python3 scripts/benchmark_logstar.py --sizes 4096 --bases 16 --blocks 8 \
  --profiles lan,wan,slow --trials 3 --output out/logstar/network-pi.jsonl
sudo python3 scripts/benchmark_logstar.py --sizes 4096 --bases 4096 \
  --profiles lan,wan,slow --trials 3 --output out/logstar/network-batcher.jsonl
sudo python3 scripts/calibrate_logstar_network.py --profiles lan,wan,slow \
  --output out/logstar/network-calibration.jsonl
```

The harness records the executable hash, CPU, public parameters, and a unique
trial ID. The summarizer refuses to combine different executable builds or
incomplete TCP party pairs. It does not group by machine or profile definition;
keep different environments separate even when their executable hashes match.
The source remains a research implementation with
the unpruned main recursion: the paper's optional `O(n log* n)` pruning extension
is not implemented, and comparisons have actual bit-level GMW depth rather than
unit-cost theoretical rounds.
