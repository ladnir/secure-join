# Optimized Pi-logstar: results and reproduction

The optimized implementation uses fewer online bytes at **11/11 sizes**,
covering every power of two from 2^10 through 2^20 keys per input list.
Each local point is the median of three verified real-cryptography executions.
At 2^20, it sends **1,337.504 MiB versus 2,331.071 MiB**
online, a **42.6% reduction**.
Median local online time is **2.405 s versus 2.438 s**.
Its median online time is lower at 11/11 sampled sizes; the plots show
the observed timing ranges, not confidence intervals.
At 2^20 the online ranges overlap, so the small median difference does not
establish a consistent local speed advantage.

The single 2^20 WAN pair (100 Mbit/s per direction, 40 ms RTT) took
**73.88 s versus 120.65 s online**. Including preprocessing,
the totals were **439.83 s versus 440.78 s**.
This largest network point is one execution per protocol, without a variability estimate.

Preprocessing is a separate tradeoff: at 2^20 it sends
3,618.914 MiB versus 1,305.796 MiB, and takes
262.0 s versus 283.6 s locally.
The online dependency bounds are 207 versus 168.
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
  process swap across the main runs is 0.000 GiB.
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

## Complete measurements

Executable SHA-256: `769ba0491bdd740616f500dcd0f1acab78dfdb944ed777cc3bbcf9953287c0b5`.

All rows are verified real executions. MiB = 2^20 bytes; n is per input list.

| n/list | Protocol | m | Batch | Online MiB | Offline MiB | Online s | Offline s | Depth | Peak RSS GiB | Trials |
|---:|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| 1,024 | Pi-logstar | 4 | 1048576 | 0.923 | 4.768 | 0.0017 | 0.395 | 115 | 0.27 | 3 |
| 1,024 | Batcher | 0 | 1048576 | 0.979 | 0.565 | 0.0036 | 0.203 | 88 | 0.20 | 3 |
| 2,048 | Pi-logstar | 4 | 1048576 | 1.920 | 8.128 | 0.0031 | 0.645 | 125 | 0.32 | 3 |
| 2,048 | Batcher | 0 | 1048576 | 2.174 | 1.266 | 0.0040 | 0.392 | 96 | 0.20 | 3 |
| 4,096 | Pi-logstar | 4 | 1048576 | 3.968 | 14.735 | 0.0057 | 1.103 | 134 | 0.32 | 3 |
| 4,096 | Batcher | 0 | 1048576 | 4.795 | 2.787 | 0.0099 | 0.784 | 104 | 0.21 | 3 |
| 8,192 | Pi-logstar | 4 | 1048576 | 8.200 | 28.395 | 0.0111 | 2.095 | 143 | 0.39 | 3 |
| 8,192 | Batcher | 0 | 1048576 | 10.506 | 5.979 | 0.0129 | 1.494 | 112 | 0.23 | 3 |
| 16,384 | Pi-logstar | 4 | 1048576 | 16.962 | 55.829 | 0.0219 | 3.899 | 152 | 0.44 | 3 |
| 16,384 | Batcher | 0 | 1048576 | 22.893 | 12.889 | 0.0234 | 3.139 | 120 | 0.28 | 3 |
| 32,768 | Pi-logstar | 4 | 1048576 | 35.301 | 110.957 | 0.0465 | 7.668 | 161 | 0.57 | 3 |
| 32,768 | Batcher | 0 | 1048576 | 51.519 | 28.966 | 0.0470 | 6.881 | 128 | 0.37 | 3 |
| 65,536 | Pi-logstar | 4 | 1048576 | 72.982 | 221.818 | 0.0984 | 15.154 | 170 | 0.82 | 3 |
| 65,536 | Batcher | 0 | 1048576 | 111.192 | 62.325 | 0.1036 | 14.911 | 136 | 0.57 | 3 |
| 131,072 | Pi-logstar | 4 | 1048576 | 150.983 | 445.010 | 0.1998 | 31.411 | 179 | 1.58 | 3 |
| 131,072 | Batcher | 0 | 1048576 | 239.071 | 134.018 | 0.2216 | 31.758 | 144 | 1.00 | 3 |
| 262,144 | Pi-logstar | 4 | 1048576 | 312.261 | 894.463 | 0.4459 | 65.467 | 188 | 3.36 | 3 |
| 262,144 | Batcher | 0 | 1048576 | 512.267 | 286.967 | 0.5120 | 67.497 | 152 | 1.91 | 3 |
| 524,288 | Pi-logstar | 4 | 1048576 | 648.129 | 1798.636 | 1.0260 | 128.685 | 198 | 6.59 | 3 |
| 524,288 | Batcher | 0 | 1048576 | 1094.284 | 612.993 | 1.1404 | 143.638 | 160 | 3.86 | 3 |
| 1,048,576 | Pi-logstar | 4 | 1048576 | 1337.504 | 3618.914 | 2.4047 | 261.953 | 207 | 12.94 | 3 |
| 1,048,576 | Batcher | 0 | 1048576 | 2331.071 | 1305.796 | 2.4384 | 283.620 | 168 | 8.00 | 3 |

| Profile | n/list | Protocol | Online s | Offline s | Total s | Trials |
|---|---:|---|---:|---:|---:|---:|
| lan | 4,096 | Pi-logstar | 0.045 | 1.006 | 1.051 | 3 |
| lan | 4,096 | Batcher | 0.044 | 0.692 | 0.736 | 3 |
| wan | 4,096 | Pi-logstar | 2.899 | 1.820 | 4.718 | 3 |
| wan | 4,096 | Batcher | 2.353 | 1.154 | 3.507 | 3 |
| slow | 4,096 | Pi-logstar | 4.643 | 8.805 | 13.441 | 3 |
| slow | 4,096 | Batcher | 4.318 | 3.008 | 7.321 | 3 |
| lan | 65,536 | Pi-logstar | 0.462 | 15.543 | 16.009 | 3 |
| lan | 65,536 | Batcher | 0.583 | 14.570 | 15.153 | 3 |
| wan | 65,536 | Pi-logstar | 7.042 | 22.690 | 29.723 | 3 |
| wan | 65,536 | Batcher | 8.032 | 16.185 | 24.222 | 3 |
| slow | 65,536 | Pi-logstar | 38.167 | 135.069 | 173.647 | 3 |
| slow | 65,536 | Batcher | 52.788 | 61.723 | 114.511 | 3 |
| wan | 1,048,576 | Pi-logstar | 73.879 | 365.951 | 439.830 | 1 |
| wan | 1,048,576 | Batcher | 120.652 | 320.123 | 440.775 | 1 |

## Matched batch-size pilots

One execution per configuration; excluded from the main plots.

| n/list | Protocol | Batch entries | Offline MiB | Offline s | Peak RSS GiB |
|---:|---|---:|---:|---:|---:|
| 16,384 | Batcher | 1,048,576 | 12.889 | 3.055 | 0.28 |
| 16,384 | Batcher | 4,194,304 | 3.662 | 3.356 | 0.82 |
| 16,384 | Pi-logstar | 1,048,576 | 55.829 | 3.910 | 0.44 |
| 16,384 | Pi-logstar | 4,194,304 | 45.366 | 4.489 | 1.22 |
| 65,536 | Batcher | 1,048,576 | 62.325 | 14.589 | 0.57 |
| 65,536 | Batcher | 4,194,304 | 17.790 | 16.203 | 1.04 |
| 65,536 | Pi-logstar | 1,048,576 | 221.818 | 15.014 | 0.82 |
| 65,536 | Pi-logstar | 4,194,304 | 178.441 | 16.542 | 1.57 |

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
