# Pi-logstar development optimization audit

This follow-up changes no paper tables or benchmark records. Development checks
use small inputs; the large-size comparisons below are public circuit schedules,
not executions. Block size four is held fixed. No per-size parameter search is
performed.

## Size-independent changes

The packed protocol still merges block representatives, permutes blocks,
broadcasts the preceding opposite-source block, solves the small block merges,
and extracts the shared gather permutation through a joint shuffle. It retains
the existing one-partition specialization and general recursive fallback.

1. Use the repository's width-balanced selector comparator, odd-even Batcher
   network, and public initial-position optimization for representatives.
2. Reuse neighboring terminal comparisons for the interval masks, removing the
   separate interval key-comparison circuit.
3. Omit comparisons against the copied block's minimum, whose order is known.
   Omit that key from the prefix broadcast as well.
4. Compute global ranks using a public block position and one shared bit,
   removing the two secret block-ID additions.
5. Apply the same comparator/network improvements to the Batcher baseline.
   For an unpadded pure merge, its records contain exactly key and index bits;
   infinity, validity, and byte-padding bits do not require compare-swaps.

These changes depend on public dimensions and protocol invariants, not key
distribution, a selected input size, or a newly tuned block/batch parameter.
`PiLogStarOptions::optimized` defaults to true. `--optimized 0` retains the
previous circuit schedules for an ablation; `--packed 0` still selects the
general construction, with the common circuit optimizations unless disabled.

## Why comparison reuse is correct

Write `B_i` for the ith block in representative order, `S_i` for its preceding
opposite-source block, and `v_i` for whether that predecessor exists. Compare
keys lexicographically by `(key, original source)`, with X preceding Y on ties.
Let

```
L_i[j,k] = v_i AND (S_i[k] < B_i[j]).
t_i      = source(B_i) XOR source(B_(i+1)).
```

For a present predecessor, `S_i[0] < B_i[0] <= B_i[j]`, including duplicate
keys, so `L_i[j,0] = v_i`. This is a shared wire copy, not a comparison. No
later optimized operation needs the omitted `S_i[0]` key.

If `t_i=1`, the next copied block is `S_(i+1)=B_i`. Therefore the next row
`L_(i+1)[0,j]` gives B's upper-interval test. Every S key is below the next
representative: S and that representative are distinct ordered blocks from
the same source. If `t_i=0`, every B key is below the next same-source block
in the full stable order, and `S_(i+1)=S_i`; the same comparison row gives S's
upper test. Consequently the upper validity factors are

```
upper_B[j] = NOT t_i OR L_(i+1)[0,j]
upper_S[k] = v_i AND (t_i OR L_(i+1)[0,k]).
```

At the last block both upper restrictions are absent, implemented by a public
all-one next row. Copied rows below B's minimum still have zero B predecessors;
the existing omission of their insertion-position-zero candidate applies the
lower mask. All absent-predecessor comparisons are zero. The existing one-hot
insertion encoding and conditional validity products then produce exactly the
same active rows as explicit interval masking.

## Why global ranks need no secret addition

Let `a` and `b` be the source-local block IDs of B and S. Exactly `i` block
representatives precede B. With a predecessor, these comprise `a` same-source
blocks and `b+1` opposite-source blocks. Without one, `a=i` and the dummy
predecessor is a copy of the first block, with local ID zero. Thus

```
a + b = i - v_i
rank  = m * (i - v_i) + local_merged_position.
```

For either half of a `2m` terminal output, its high rank bits select between
two public constants. If these constants are A (absent) and P (present),
each XOR share is computed locally as `A_share XOR (v_share AND (A XOR P))`.
The AND has a public operand and communicates nothing. Low rank bits remain
the public within-half position. Inactive ranks may wrap and remain unopened.

## Security and fairness

No additional values are opened, and no access depends on a secret. Stable
duplicate handling, arbitrary XOR input shares, single-use correlations, real
preprocessing, and the shared gather-permutation interface are preserved.
The existing joint-shuffle argument applies because the active ranks and
payloads are unchanged. The proof above supplements that argument; correctness
tests alone are not a security proof.

The earlier 2^20 result compared 1,337.504 MiB for Pi-logstar with 2,331.071 MiB
for the previous bitonic Batcher implementation, about 1.74x. A fair new ratio
must use the refreshed Batcher implementation too. Common backend gains cannot
be credited exclusively to Pi-logstar. Online bandwidth, round bounds,
preprocessing cost, and computation must be reported separately.

## Development findings, 2026-09-10

At **n=2^20 per list, 32-bit keys, fixed m=4**, the public schedule gives:

| Implementation | Online payload MiB, projected | Online round bound |
|---|---:|---:|
| Previous Pi-logstar | 1,337.469 | 207 |
| New Pi-logstar | **722.853** | **175** |
| Previous bitonic Batcher | 2,331.000 | 168 |
| Improved matched odd-even Batcher | **1,482.509** | **147** |

The fair new online ratio is **2.051x**, and the new Pi-logstar payload is
45.95% below its previous schedule. This makes a 2x online communication
advantage at this endpoint plausible without size-specific retuning. It is a
circuit/payload projection, not a measured large execution or a speedup claim.
The counts include actual 128-lane circuit padding. Framing is excluded from
all four projected rows, explaining their small difference from archived
measurements. The margin over 2x is modest; future evaluation should retain the
improved baseline and report the measured ratio, not round it up to a target.

Pi-logstar's round bound improves by 32 steps (15.5%) but is still 28 steps above
the improved baseline. These changes do not establish a round advantage, a
2x advantage at every smaller size, or a 2x end-to-end speedup.

Three serial **n=512-only** development executions per variant used matched
32-bit random datasets, fixed block four, matched 2^20 correlation batches,
alternating execution order, and fresh cryptographic randomness:

| Implementation | Measured online bytes | Median online ms | Observed range, ms |
|---|---:|---:|---:|
| Previous Pi-logstar schedule | 468,456 | 1.052 | 1.004--1.135 |
| New Pi-logstar | 225,528 | 0.815 | 0.762--0.982 |
| Previous Batcher schedule | 458,288 | 2.000 | 1.706--3.288 |
| Improved matched Batcher | 269,456 | 1.599 | 1.509--1.943 |

The byte model differs from these measurements by 1,640, 1,336, 1,328, and
1,168 framing bytes respectively. New Pi-logstar's measured online traffic is
51.9% below its earlier schedule, and its median online time is 22.5% lower.
These tiny local runs are sanity checks on computation and accounting, not
estimates of million-key timings, network behavior, or statistical confidence.
The final executable SHA-256 is
`be16a218df2b3d8d6f3b6b9cf45b4515a26756eb6bb334fd2cabf42b89f0d16e`.
The [development records](development/logstar-optimization-20260910.json)
retain the public schedules and small executions separately.

No online work was moved into preprocessing. At n=2^20, binary OLE requests
per party fall from 5,232,263,168 to 2,654,376,704; the 2,684,354,560 F4-bit OT
requests and 2,048 random OT requests remain unchanged. Request types have
different costs and must not be summed as a bandwidth estimate. The small new
Pi-logstar executions still send 2,984,080 bytes offline, versus 249,088 for the
improved Batcher baseline. Large offline bandwidth, runtime, and memory usage
remain to be measured. The expensive permutation preprocessing remains a
limitation despite the online improvement.

Validation includes 8,642 packed reference cases, 3,470 general recursive
reference cases, 17,576 comparator circuit cases, and the real-cryptography
self-test covering random XOR shares, duplicates, maximum keys, 1--256-bit
keys, all supported terminal block sizes, padding, deeper recursion, and
correlation-reuse rejection. The reference tests now explicitly check both
new algebraic identities and retain the exhaustive shuffled-transcript check.
Four additional tiny separate-process TCP merges passed (new packed, previous
packed, improved Batcher, and general recursive); mismatched optimization flags
were rejected before preprocessing on both endpoints.
Existing paper benchmark records and paper sources were not changed.

## Reproduction without a paper benchmark

```
cmake --build out/build/linux --target logstar -j4
out/build/linux/frontend/logstar --self-test
python3 tests/packed_logstar_reference.py
python3 tests/logstar_reference.py
python3 scripts/check_logstar_optimizations.py --execute-small --output out/logstar-dev/audit.json
```

The audit script refuses to overwrite its output. It emits public schedules
only at n=512 and n=2^20 and, when requested, three executions per variant at
n=512 only. It never executes a merge in the paper's 2^10--2^20 range.
Public `online_payload_bytes` include GMW, block permutation, joint shuffle,
flag openings, and active-rank openings, but exclude socket framing. Schedule
counts are not timing measurements or verified large protocol executions.
To reproduce an archived timing measurement, use its archived source/executable;
`--optimized 0` restores the earlier circuit schedule within the current build.
