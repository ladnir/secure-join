# Figure 2 constants: implementation audit (2026-09-17)

This note records the derivation approved for the paper's related-work Figure 2
(`fig:mergeasymptotics` in `relwork.tex`). It changes the four implemented rows;
the two BBDLO rows retain their previous estimates, formatted to two decimals.
The note is not included in the manuscript and adds no paper pages.

## Definitions and normalization

- Each input list has **n = 2^20 = 1,048,576** elements. Quicksort sorts their
  concatenation, **N = 2n = 2,097,152**.
- All displayed logarithms are base 2. `log2(n) = 20`,
  `log2(log2(n)) = 4.321928094887363`, and `log*(n) = 5`, where log* counts
  logarithms until the result is at most 1.
- `c = log(2)/log(3/2) = 1.7095112913514547`; calculations use this exact
  expression, not the rounded 1.71 shown in the asymptotic column.
- A comparison is one scalar secure key comparison, summed over logical SIMD
  lanes and subproblems. Semantic dummies count; unused SIMD padding does not.
  Reused comparison results count only once. The two parties do not duplicate
  the count.
- Comparison depth is the number of sequential key-comparison layers along the
  longest dependency path. Independent comparisons run in one layer. This is
  not Boolean AND depth or total communication rounds: selection, arithmetic,
  routing, and openings are excluded as operations, while dependencies through
  them still determine which key comparisons can run together.
- The factors divide concrete costs at the selected benchmark parameters by the
  displayed asymptotic expressions. They are finite-size normalized costs, not
  proven asymptotic leading constants. The tuned implementation schedules need
  not be the schedules used to establish the asymptotic theorems.

| Method | Comparison denominator | Depth denominator | Comparison factor | Depth factor | Figure cells |
| --- | --- | --- | ---: | ---: | --- |
| Logstar | n log*(n) = 5,242,880 | log2(n) = 20 | 2.100000190735 | 1 | 2.10 / 1.00 |
| Median | n (log2(n))^c = 175,681,237.21669838 | log2(log2(n)) | 0.322492446533 | 3.239294984237 | 0.32 / 3.24 |
| Shuffled quicksort | n log2(n) = 20,971,520 | log2(n) = 20 | 2.687535827140 (expected) | 2.45 (observed median) | 2.69 / 2.45 |
| Batcher | n log2(n) = 20,971,520 | log2(n) = 20 | 1.000000047684 | 1.05 | 1.00 / 1.05 |

Previously the pairs were Logstar 2.9/1.7, Median 0.3/3.9, quicksort 1.4/1.4,
and Batcher 1.0/1.0. The preserved BBDLO pairs are now written 25.50/19.20
(subprotocol) and 120.80/55.30 (full). These BBDLO estimates were not re-audited.

## Logstar

The active benchmark uses the optimized packed specialization with block size
b = 4. Let q = n/b = 262,144. The odd-even merge of block representatives costs
`q log2(q) + 1 = 4,718,593` comparisons. Each of the `2n/b` blocks performs
`b(b-1)` cross comparisons: `2n(b-1) = 6,291,456`. Comparisons with the first
opposite-source predecessor are implied by sorted order. Terminal merges reuse
these results and add no key comparisons.

Total: **11,010,049** comparisons. The representative merge has
`log2(q)+1 = 19` layers; cross comparisons add one layer. Total depth: **20**.

Sources: `secure-join/Sort/PackedLogStar.cpp`, functions `crossComparisons`,
`allPairs` (the reuse branch), and `PackedLogStar::init`; the odd-even stage
schedule in `secure-join/Sort/BatcherMerge.cpp`.

## Median

The active benchmark uses one alignment level, child-list length s = 2,048,
k = n/s = 512 representatives per input, cube-block size b = 64, and Batcher
leaves. Both alignment directions run in parallel. There are 2k = 1,024
independent leaf merges, each merging two lists of length s.

| Component | Formula | Comparisons |
| --- | --- | ---: |
| Boundary comparisons | 2k(n/b - 1) | 16,776,192 |
| Selected-block detail comparisons | k(k+1)b | 16,809,984 |
| Batcher leaves | 2k(s log2(s) + 1) | 23,069,696 |
| Total | Sum | **56,655,872** |

Boundary comparisons form one layer; detail comparisons depend on their
selection results and form the next. Batcher leaves add `log2(s)+1 = 12`
layers. Total comparison depth: **14**. Rank arithmetic and final compaction
add communication rounds, but not key comparisons.

Sources: `secure-join/Sort/PiMedian.cpp`, `CubeCounts::init`,
`CubeCounts::apply`, and `PiMedian::init`; active options in
`docs/benchmarks/paper-2026/implementation-comparison-counts.json`.

## Shuffled quicksort

The implementation uses one pivot and all-pairs terminal partitions of size at
most 8. Original-position tie breakers make records distinct. A uniform joint
shuffle therefore permits the uniform-pivot expectation recurrence, with
E_m counting key comparisons for m records:

```
E_m = m(m-1)/2                         for 0 <= m <= 8
E_m = m-1 + (2/m) sum_{j=0}^{m-1} E_j  for m > 8
```

At N = 2n, this gives **56,361,711.34958 expected comparisons**, or
**2.68753582714** after normalization by n log2(n). The audit script evaluates
the recurrence independently using prefix sums. It agrees with the
harmonic-number closed form in `QuickSort::plan` (floating-point differences
are far below the displayed precision).

The familiar approximately 1.386 N log2(N) leading work term uses **total**
input size N. It cannot be applied directly as 1.4 n log2(n) for two length-n
inputs, and a work coefficient is not a comparison-depth coefficient.
For reference, the standard leading work term is 2 N ln(N):
https://algs4.cs.princeton.edu/23quicksort/

The existing three timed executions at this size give:

| Profile | Comparisons | Comparison layers | Total online rounds |
| --- | ---: | ---: | ---: |
| Local | 54,222,420 | 46 | 324 |
| LAN | 55,895,051 | 50 | 352 |
| WAN | 55,570,321 | 49 | 345 |

These are independent random shuffles, not an effect of network latency on
comparison work. Deduplicate TCP endpoints by `trial_id`; exclude the separate
untimed round audit. The observed median work is **55,570,321**, or
**2.64979939461** after normalization. Figure 2 uses the expectation instead,
which is less dependent on three random executions.

The recorded total online rounds equal `2 + (6+1)D`: two shuffle rounds, then
six Boolean comparison rounds and one opening round per batch. Comparators
operate on 32 key bits plus 21 original-index bits; their nonlinear depth is
`ceil(log2(53)) = 6`. There were no preprocessing refills. Thus D is exactly
46, 50, and 49 in the recorded executions. Median D = **49**, giving **2.45**;
observed normalized range **2.30-2.50**. This is an empirical three-run median,
not an expected-depth calculation or a worst-case bound.

Sources: `secure-join/Sort/QuickSort.cpp`, `QuickSort::plan` and
`QuickSort::sort`; `QuickSortStats::onlineRoundsWithoutRefills` in
`secure-join/Sort/QuickSort.h`; `docs/benchmarks/paper-2026/measurements.jsonl`.

## Batcher

For the implemented odd-even merge of two equal power-of-two lists,
`C(1)=1`, `C(n)=2C(n/2)+n-1`, hence `C(n)=n log2(n)+1`.
Similarly `D(1)=1`, `D(n)=D(n/2)+1`, hence `D(n)=log2(n)+1`.
At n = 2^20: **20,971,521 comparisons and 21 layers**.

Source: `secure-join/Sort/RootMerge.cpp`, `UnequalBatcher::merge` and
`UnequalBatcher::comparator`. This is the actual benchmark baseline; it exploits
both sorted inputs. It is not a full sorting network.

## Bitlength and communication rounds

These comparison-level units deliberately abstract away element bitlength.
They do not claim comparisons account for all MPC communication. In particular,
it is incorrect to recover comparison depth by dividing every protocol's total
online rounds by a common log(bitlength): protocols use different comparison
widths and also perform other interactive operations.

The published total online rounds at this size remain Logstar 174, Median 119,
quicksort median 345, and Batcher 147. The comparison-count archive separately
contains conservative constructor round bounds (175 for Logstar, 121 for
Median); neither those bounds nor the measured totals are Figure 2 depths.

## Reproduction and provenance

From the implementation repository, using Python 3.10 or later:

```sh
python3 scripts/audit_figure2_constants.py
```

This read-only script prints the factors, formatted Figure 2 cells, quicksort
observations, and provenance hashes as JSON. It checks the archived source
hashes and parameter manifests, and independently recomputes **all 26** archived
Logstar/Median comparison totals (n = 2^8 through 2^20), not only the final size.
No cryptographic benchmark needs to be rerun. If an archived source hash no
longer matches, review/regenerate the comparison archive before reusing it.

- Implementation revision inspected: `2145cee` (the working-tree README had an
  unrelated edit, which is excluded from this audit).
- Manuscript revision before this update: `19284f3`.
- `implementation-comparison-counts.json` was created 2026-09-11 and records
  exact public circuit counts, source hashes, commands, and parameter checks.
- Frozen timed executable SHA-256:
  `d1dbb7f48e5e1f989814742eb666b75c6f1d8f50fce3c952e8248f4e6dc7c79e`.
- Active Median parameters are selected by `active-median-revision.json`;
  the audit verifies its hash against the comparison-count archive.
- The manuscript changes are confined to Figure 2's constants/caption and a
  Figure 2 cross-reference in the existing AI disclosure. This audit does not
  change the implementation, recorded benchmarks, or asymptotic claims. Adding trailing zeroes to BBDLO values does not add precision to
  those retained estimates.

## Submission layout verification

The submission build retains a **27-page main body**, including the disclosure.
References start on page 28; the complete PDF, including references and
supplementary material, remains 56 pages. Figure 2 is on page 7. The revised
figure and final main-body page were rendered and visually inspected. There
are no unresolved citations/references and no new overfull-box warnings
relative to a fresh build of the preceding manuscript.
