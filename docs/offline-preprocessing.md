# Preprocessing: implementation audit and batch tuning

The implementation prioritizes online communication and latency. Its offline
phase already uses batched correlation generation, Silent OT/VOLE, AES/SIMD
kernels, and reused temporary buffers. It is not a naive implementation with a
public-key OT for every Boolean gate. We have not optimized the complete
preprocessing pipeline or established that its current cost is near optimal.

## What is generated

Preprocessing depends on public sizes and the selected circuit, not on the
sorted key values. It generates fresh correlated randomness for one merge:

* **GMW multiplication correlations.** `Gmw::init` requests two binary OLE
  correlations per padded Boolean AND. These provide the product masks used
  when evaluating secret-shared comparisons, additions, and selections.
* **Block-permutation correlations.** The packed path prepares a composition
  of two private permutations, one belonging to each party. A basic correlation
  relates a random vector and masks by `Delta XOR B = pi(A)`: the permutation
  holder has `pi, Delta`, and the other party has `A, B`.
* **Extraction-permutation correlations.** A second composed permutation
  shuffles the expanded output before opening the masked selection information.
  Both permutation generators use the inherited alternating-moduli PRF protocol.
  Its main auxiliary request is 1-out-of-4 OT of bits; a small regular-OT request
  supports key multiplication.

These objects are held in memory. The benchmark includes generating them from
fresh OS randomness and exchanging their setup messages. It does not load a
precomputed file or use a trusted dealer. Consumed masks and correlations must
not be reused for another merge.

At `n=2^20` keys per list, 32-bit keys, terminal block size four, and `2^20`-entry
correlation batches, the public schedule requests, **per party**:

| Request | Logical entries |
|---|---:|
| Padded online AND evaluations | 2,616,131,584 |
| Binary OLE | 5,232,263,168 |
| 1-out-of-4 OT of bits | 2,684,354,560 |
| Regular random OT | 2,048 |
| Trit OT | 0 |
| Correlation batches | 7,552 |

Logical entries are expanded in batches; they are neither independent
public-key operations nor a count of transmitted 128-bit blocks. The new
`--plan` output exposes the actual registered correlation requests.

The two permutation generators account for 10,485,760 joint PRF evaluations.
The block permutation has `2n/4` rows and requires two 128-bit PRF outputs per
row and direction. Extraction has `4n` rows and requires one output per row and
direction. Each generator has two directions. The resulting `10n` PRF
evaluations consume 256 F4-bit-OT entries each. This is substantial additional
preprocessing that Batcher's GMW-only merge does not require.

## Measured batch tuning

The accompanying report `benchmarks/offline-batch-summary.md` compares batch
sizes `2^20` and `2^22` at `n=2^16`, using three fresh executions of each protocol
and configuration in both local and WAN settings. WAN is an isolated Linux
network-namespace/veth link with `tc netem`, 100 Mbit/s per direction and 40 ms
RTT. The laptop, backend, online circuit, and executable are those used in the
main evaluation. Runs are serial; protocol order alternates between repetitions
and configuration order rotates. Local execution uses one OS thread for both
parties. Each TCP party has two Asio I/O workers, as in the main evaluation.

The executable SHA-256 is
`769ba0491bdd740616f500dcd0f1acab78dfdb944ed777cc3bbcf9953287c0b5`.
No worker-pool experiment is included in these measurements. Phase accounting
and output reconstruction follow the main evaluation. The TCP time for each
phase is the slower party's time; total time adds those two phase times.
Communication sums sent bytes once over both parties. MiB means `2^20` bytes.
The report shows medians and observed ranges, rather than confidence intervals.
RSS is both parties together locally and the maximum per-party RSS for TCP.

Larger batches amortize setup and lower communication at the expense of larger
temporary buffers and potentially more CPU time. The same tuning is measured
for Batcher. It does not change the online circuit, correlation request counts,
online communication, or online round bound. The main online plots retain their
original, uniformly matched `2^20` batch setting.

For Pi-logstar, the larger batch cuts offline communication from 221.818 to
178.441 MiB (19.6%), but increases median local preprocessing from 15.014 to
16.878 seconds and WAN preprocessing from 23.160 to 26.266 seconds. Its local
peak RSS rises from 0.819 to 1.569 GiB. Batcher saves 71.5% of its offline bytes
but also takes longer. No process swap was observed in these samples.

We also piloted smaller batches, `2^18` and `2^19`, once per protocol locally.
The `2^19` setting was selected for a separate comparison against `2^20`, with
three fresh repetitions per method and configuration. Its results are in
`benchmarks/offline-half-batch-summary.md`; the earlier pilot is excluded from
those medians. This tuning exchanges more bandwidth for a possible modest
reduction in local preprocessing time.

In that follow-up, Pi-logstar's median changes from 15.648 to 15.180 seconds
(3.0% lower), but the observed ranges overlap and one of three matched
repetitions is slower with the smaller batch. Offline traffic increases from
221.818 to 274.165 MiB (23.6%). Batcher's median increases from 14.235 to
14.602 seconds. These measurements do not establish a consistent preprocessing
speedup; the original default remains appropriate for the reported comparisons.

## Further work and its limits

* **PRF tiling and memory traffic.** `AltModPrfProto.h` explicitly identifies
  batching large input sets for data locality as unfinished work. Tiling PRF
  evaluation and reducing temporary matrix traffic are concrete implementation
  opportunities. Their speedup has not been measured.
* **Parallel preprocessing.** The correlation backend exposes a worker-pool
  hook. A prototype with separate offline socket executors passed small tests
  but stalled at `n=2^16`; multiple workers also stalled a small test. The option
  was removed, and its timings are excluded. Robust parallel integration needs
  further scheduler work, so it is not an available optimization in this release.
* **Persistent setup across merges.** Reusing supported base-OT setup state
  could amortize startup across many merges, while deriving fresh correlations
  for each merge. This requires a stateful integration with the backend and its
  security conditions. It does not eliminate the large expansion and PRF costs.

The evidence supports saying that preprocessing has further optimization
opportunities. It does not support a numerical prediction of a large speedup
from these unimplemented changes.

## Reproduce

Build instructions are in `pi-logstar.md`. Use fresh output paths:

```sh
sudo python3 scripts/evaluate_offline_logstar.py \
  --exe out/build/linux/frontend/logstar \
  --out out/reproduce/offline-batch-n16.jsonl
python3 scripts/summarize_offline_logstar.py \
  out/reproduce/offline-batch-n16.jsonl \
  --out out/reproduce/offline-batch-summary
python3 scripts/evaluate_offline_logstar.py \
  --exe out/build/linux/frontend/logstar \
  --profiles local --configs inline,half-batch \
  --out out/reproduce/offline-half-batch-n16.jsonl
python3 scripts/summarize_offline_logstar.py \
  out/reproduce/offline-half-batch-n16.jsonl \
  --out out/reproduce/offline-half-batch-summary
out/build/linux/frontend/logstar --plan --n 1048576 --bits 32 \
  --base 4 --block 4 --batch-size 1048576 --concurrency 2
```

The batch study records its executable hash. Rebuilding the frontend to include
the new plan counters produces a different executable; those future timings
must remain separately identified. The counters do not change the protocol.

The paper's appended subsection is sourced from `docs/offline-evaluation.tex`.
After regenerating summaries from the delivered raw records, copy that template
to the paper's `plots/packed/packed-offline-evaluation.tex`, the large-batch
summary table to `packed-offline-table.tex`, and the two generated findings
files to `packed-offline-findings.tex` and `packed-offline-half-findings.tex`.
The paper already includes this subsection from `evaluation.tex`. Its earlier
evaluation text and online figures are preserved. The packed-evaluation writer
also preserves separately appended material after its own end marker.

Implementation sources: `secure-join/GMW/Gmw.cpp`,
`secure-join/CorGenerator/{CorGenerator.cpp,BinOleBatch.cpp,Correlations.h}`,
`secure-join/Perm/{AltModPerm.h,AltModComposedPerm.cpp,PermCorrelation.h}`,
`secure-join/Prf/AltModPrfProto.h`, and `secure-join/Sort/PackedLogStar.cpp`.
