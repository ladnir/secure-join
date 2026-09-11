# Unified paper benchmark, September 2026

This study replaces the accumulated analytical estimates and measurements from
older executables in the paper's evaluation section. Only records from the
common `paper_benchmark` executable belong to this study.

## Experiment matrix

- Balanced: Logstar, Median, odd-even Batcher merge, and shuffled quicksort;
  both input lists have length `n`.
- Cube-root shape: CubeRootMerge and both baselines; lengths
  `ceil(n^(1/3))` and `n`.
- Square-root shape: SquareRootMerge and both baselines; lengths
  `ceil(sqrt(n))` and `n`.
- Every power of two `n=2^8,...,2^20`, for local in-memory transport, LAN
  (1 Gbit/s per direction, 0.2 ms RTT), and WAN (100 Mbit/s, 40 ms RTT).
- Three independent executions through `2^16`, one at each larger size:
  930 timed executions covering 390 configurations, plus 130 separate round
  audits. Both endpoints of a TCP execution share one `trial_id`.

The published results require this matrix to be complete. The summarizer's
`--allow-incomplete` option is for diagnostics only.

The host is an Intel Core i9-14900HX laptop with 32 GiB RAM and Ubuntu 24.04
in WSL2. With authorization, the Linux memory cap was raised to 26 GiB. Every
final-study execution uses that cap. Each environment record includes the
actual `/proc/meminfo`. Preliminary runs with an older harness are preserved
under `superseded/` and are not pooled with the final study.
The original WSL configuration was absent. After all experiments and artifact
checks, the unchanged temporary configuration was removed and WSL was shut
down, restoring the original defaults and stopped state.

## Implementation and accounting

All methods use the same real two-party semi-honest cryptographic backend,
32-bit unsigned keys, stable original-position tie breaking, and a shared
gather-permutation output. Quicksort sorts the concatenation after a fresh
joint shuffle. Its terminal partitions have at most eight rows and use
all-pairs comparison. Batcher uses an unequal-length odd-even network and
exploits the sorted input lists. The random synthetic seed is identical across
matched methods; private cryptographic randomness comes from the OS.

The common C++ runner uses exactly one OS thread per TCP party, including its
Boost.Asio callbacks. Local runs execute both parties on one OS thread.
Correlation batches have `2^20` entries and up to **two batches in flight**, uniformly
for every method. This is coroutine concurrency, not additional OS threads.
Correlation generation and protocol messages use separate data connections;
harness configuration, barriers, and verification use a third control
connection. All connections share the same shaped interface and rate cap.
Separating streams prevents a receive not yet posted on one logical channel
from blocking correlation messages needed to reach that receive. The older
shared-stream harness stalled on the `n=2^18` LAN Median case. The corrected
harness passed that case and the small local/buffered/TCP probes; final timings
were collected again with one corrected executable. This study uses no thread pool.

Offline communication and time include fresh correlation generation; online
measurements start when correlations are ready. Public circuit construction
and allocation (`setup_ms`), input generation, configuration exchange, phase
barriers, and output reconstruction/verification are excluded. Offline time
therefore does not include all possible local setup work. `total_seconds` is
offline plus online, not complete process wall time.
The CSV also gives public construction/allocation time in `setup_seconds`.

Bytes sum **sent** counters once across both parties and both data streams,
include coproto framing, and exclude TCP/IP headers. Do not also add received
bytes. The separate harness control connection is excluded. Deterministic
protocols have matching local and TCP byte counts in the validation probes.
MiB means `2^20` bytes. Each TCP phase time is the maximum of its two endpoint
times; medians are taken after pairing. Per-sample totals are formed before
taking their median. Peak RSS is both parties together locally and the larger
per-party peak for TCP, so those memory columns have different scopes.

Online round counts are complete implementation dependency bounds, not packet
counts or comparison counts. They include comparison/selection AND layers,
rank arithmetic, and routing. Quicksort's actual random partition tree supplies
its bound. Its comparison reserve is prepared before timing the online phase;
the runner rejects any main-study record with a refill rather than hiding
preprocessing in online time. This is not a claim that refills are impossible.

Offline round audits run **fresh real cryptography** on `BufferingSocket`.
Both parties' available output messages on both data streams are captured
before any is delivered. One such exchange is one one-way synchronous message
wave. This batches independent sends, includes the configured correlation
scheduler, and excludes phase barriers and verification. The audit also
records actual online waves, for comparison with the online dependency bound.
Its buffered-transport timings are not included in any timing table.

Runs are serial. TCP peers execute simultaneously; different protocol trials
never overlap. Timed records reject observed Linux process swapping. Explicitly
marked untimed audits may page; their elapsed times are always excluded. Large timings have
one sample, so no confidence interval or variability claim is made for them.
The smaller-size CSV columns contain observed minima and maxima, not confidence
intervals. These are laptop/WSL experiments, not measurements on remote hosts.

## Revised presentation of network-independent costs

The paper's `evaluation.tex` replaces its implementation/evaluation section
and is included by both `main_llncs.tex` and `main_cic.tex`. The standalone
`benchmark-section.tex` reads that same section, including the public
parameter table; it is only a preview wrapper.

Online communication uses a zero-based linear y axis. A second, also linear,
panel reports Batcher bytes divided by each protocol's bytes; values above
one indicate a communication reduction. Input size remains on a logarithmic
axis. Online rounds have one common panel rather than separate network panels.

For every deterministic method, all observed online bytes and dependency
bounds are identical across transports and repeats. The summarizer now checks
this invariant. Quicksort's fresh private shuffle changes its partition tree,
comparison count, and depth between executions. The public input seed does not
fix the private shuffle. Differences between its former network panels were
independent-run variation, not a bandwidth or latency effect.

`protocol-costs.json` and `protocol-costs.csv` pool the individual executions
across all three transports: nine samples per point through `2^16`, three
above. They contain medians and observed minima/maxima. Pooling is performed
before summarization, never as a median of per-profile medians. Shading in the
quicksort panels shows the observed range, not a confidence interval. Offline
round counts still come from the separate fresh-cryptography audit.

Network-specific runtimes and complete per-profile tables retain the observations
for their respective transports. The active Median revision replaces only Median
rows in `all-results.csv`, `summary.json`, and pooled costs. Main tables show
common costs once and separate timing columns. The original raw experiment
provenance is retained; `presentation-provenance.json` identifies the active
Median manifest and authoring scripts.

## Public parameter tuning

`parameters.json` records the original candidates and schedules. Logstar and
the root methods retain their original objective: online payload plus **250,000 bytes per
dependent step**, with ties broken by payload then depth. This is a fixed
tradeoff weight, not a fitted runtime model. Parameters vary with public input
size and are held fixed across network profiles and test seeds.

- Logstar considers terminal blocks 2, 4, 8, and 16 and uses its exact packed
  one-partition specialization and direct-rank extraction.
- The active Median revision screens up to four alignment levels and chooses
  the fewest online rounds within max(3 times Batcher bytes, 8 MiB), breaking
  ties by payload. The largest two sizes use one level to limit memory. Its
  immutable schedules and exact public search are in `median-retune-20260911/`.
  At least one alignment is required; the method cannot be relabeled pure Batcher.
- Root methods consider five block scales around the corresponding root.
  Short-list sizes are computed with integer ceiling roots, not floating-point
  rounding. Baselines use the same actual unequal lengths.

The public planners and actual executions both use correlation concurrency
two. This field does not enter their online payload/depth calculations. The
non-Median online circuits are unchanged. No claim of global optimality is made.

## Files and reproduction

- `parameters.json`: frozen public candidates and selected schedules.
- `validation.jsonl`: local, buffered, and two-process TCP correctness probes.
- `calibration.jsonl`: unloaded ping RTT, bidirectional goodput, offload flags,
  and exact `tc` configuration/counters.
- `calibration-26gb.jsonl`: recalibration after the memory change and before
  the TCP sweep (LAN mean RTT 0.264 ms, WAN mean RTT 40.197 ms).
- `measurements.jsonl`: frozen original raw records, executable hashes, and commands.
- `active-median-revision.json`: validated manifest selecting the complete Median replacement.
- `median-retune-20260911/`: revised Median parameters, raw observations, memory logs, and validation.
- `resources.jsonl`: optional 15-second snapshots begun during the larger LAN
  cases, recording combined process RSS, Linux memory availability, swap use,
  and observed party thread counts. The main runner independently samples each
  party's memory every 0.5 seconds throughout every trial.
- `provenance.json`: exact source/dependency/compiler/executable identification.
- `artifact-verification.json`: final PDF hashes, visual checks, and restoration.
- `STATUS.md`: completed matrix and machine restoration record.
- `evaluation-before-rewrite.tex`: preserved original paper section.
- `*-before-benchmark-rewrite.tex`: original abstract, introduction, related
  work, and overview before aligning their old benchmark claims and references.
- Paper `plots/benchmark/all-results.csv`: complete phase costs and ranges.
- Paper `plots/benchmark/summary.json`: the same validated aggregate data.
- Workspace `output/pdf/main_llncs.pdf`: the actual paper with the replacement
  implementation/evaluation section.
- Workspace `output/pdf/benchmark-section.pdf`: preview of that same section
  and its selected parameters.
- Workspace `output/pdf/benchmark-full-results.pdf`: tables for every size/profile
  and runtime curves. Figures also have vector PDF and PNG versions in the paper's
  `plots/benchmark` directory.

To reproduce the original sweep, use the commands below from the repository
root in the configured Ubuntu/WSL environment. For the current Median schedules
and composite paper regeneration, follow `median-retune-20260911/README.md`:

```sh
cmake -S . -B out/build/linux -DCMAKE_BUILD_TYPE=Release \
  -DFETCH_AUTO=ON -DSECUREJOIN_ENABLE_BOOST=ON
cmake --build out/build/linux --target paper_benchmark -j4
# Use fresh output paths when reproducing; do not append to delivered records.
python3 scripts/plan_paper_benchmarks.py --out out/reproduce-parameters.json
sudo python3 scripts/run_paper_benchmarks.py --validate --out out/reproduce-validation.jsonl
sudo python3 scripts/calibrate_logstar_network.py --profiles lan,wan --trials 2 \
  --output out/reproduce-calibration.jsonl
sudo python3 scripts/run_paper_benchmarks.py --parameters out/reproduce-parameters.json \
  --out out/reproduce-measurements.jsonl --skip-audits
python3 scripts/run_paper_benchmarks.py --parameters out/reproduce-parameters.json \
  --out out/reproduce-measurements.jsonl --audit-only
python3 scripts/summarize_paper_benchmarks.py out/reproduce-measurements.jsonl \
  --parameters out/reproduce-parameters.json --out out/reproduce-figures
```

Network namespaces and veth devices use fresh generated names and are removed
after each trial, including on errors. Host interfaces and qdiscs are not
modified. Repeating the sweep command resumes verified completed records only
when the executable and parameter hashes match. Errors are retained separately.

## Completed Median revision (2026-09-11)

All 93 replacement Median timings and 13 audits are complete, including n = 2^20 on all three transports. The active manifest and strict validator select only this complete replacement. All original raw records, non-Median observations, root figures, and the BBDLO analytical subsection are preserved. See [the completed rerun](median-retune-20260911/README.md) for parameter selection, audit-only paging, and reproduction. The updated paper is 51 pages, the section preview 6 pages, and the full results 13 pages.
