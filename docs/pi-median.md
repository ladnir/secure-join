# Pi-median implementation

`secure-join/Sort/PiMedian.h` implements the paper's recursive **Pi-median
merge**, not median selection. It merges two sorted, equally sized lists of
unsigned XOR-shared keys and returns an XOR-shared gather permutation of
`X || Y`: output row `i` comes from original input row `pi[i]`. Equal keys
preserve their original input order, with X before Y.

The library and public planner accept 1–256-bit keys. The synthetic runner accepts 1–64 bits;
its correctness suite also exercises 65-, 127- and 256-bit keys. Non-power-of-two
lengths are padded with an explicit infinity bit, leaving the entire unsigned
key domain available. `n` always means **the length of each input list**.

This is implementation work with correctness and development checks only.
No formal scaling benchmarks, network-emulation experiments, paper tables,
or paper plots have been produced or updated for Pi-median. Public cost plans
are calculated schedules, not runtime measurements.

## Relationship to the paper

The implementation follows `constructiontwo.tex`, `constructioncuberoot.tex`,
`blockalignment.tex`, and the `UpdatePermutation` procedure in
`subprotocolssubprocedures.tex` in the accompanying paper source. At each
level, with parent half-size `s`, choose `k` medians and child size `m=s/k`.
The medians are block **maxima**, at `(i+1)*m-1`, as in the formal protocol and
notation. The overview also contains a block-minimum convention; the
implementation consistently uses the formal block-maximum convention.

1. Find secret insertion counts for `med(X,k)` in Y, and for `med(Y,k)` in X.
2. Duplicate each median `m` times and set all inserted copies' real flags to
   zero. Original rows retain their flags, including earlier dummies.
3. Form the expanded scatter ranks directly. If `cX[i]` counts Y rows before
   median `i`, and `cY[j]` counts medians before Y row `j`, the ranks are
   `i*m+t+cX[i]` for copy `t`, and `j+m*cY[j]` for Y row `j`.
4. Apply those ranks to obtain both aligned length-`2s` lists. Publicly regroup
   their corresponding length-`m` blocks into `2k` child merge instances.
5. Merge all terminal instances, concatenate their outputs, and stably extract
   the known `2n` real rows.

The two directions and **every instance at a level** share SIMD GMW calls and
batch-wide shuffles. There is no sequential per-child protocol loop. For `B`
parent instances, the next level has `2Bk` instances and twice as many rows.
The default is `m=2^floor(2*log2(s)/3)`, with Batcher leaves after at most two
alignment levels, or earlier when `s<=16`. The **two-level Batcher default**
follows the paper's concrete-evaluation choice in `evaluation.tex`, which
deliberately stops earlier than the asymptotic presentation. Setting
`--max-depth 0 --leaf allpairs` instead follows recursion down to the base
threshold. Both retain cube-root recursion up to public rounding.
After `d` alignment levels, there are exactly `2*padded_n*2^d` rows.
The capped Batcher profile is a concrete optimization, not an `O(log log n)`
round claim for unbounded `n`. Uncapped recursion with a fixed all-pairs base
threshold follows the paper's asymptotic structure.

## Implementation optimizations

**Counts instead of intermediate permutations.** The internal batched cube
merge returns insertion counts. It omits the standalone RootMerge API's rank
additions and gather-permutation inversion, because alignment needs the counts
directly. The optimized comparator and public-index adder are shared with
RootMerge through `MergeInternal.h`.

**Two-pass asymmetric merge.** Divide the long list into blocks of public size
`b`. Compare each median with block maxima except the last. The final block
also receives values above all long-list keys. One-hot maps and one AND layer
tag the first median assigned to each block. Each later median in the same
block selects its own infinity dummy block. Thus exactly `k` distinct tags
and `s/b` zero tags exist per asymmetric instance, regardless of the keys.

The selected blocks are kept in median-slot order. Median `i` needs only
selected slots `j<=i`, removing the upper triangle of fine comparisons.
Adjacent XORs of sorted comparison bits encode counts locally. The parity of
full selected blocks and group starts recovers the within-block overflow.
No popcount, segmented prefix broadcast, or secret suffix carry circuit is
needed. Both directions together use exactly

```
2*B * (k*(s/b-1) + k*(k+1)*b/2)
```

key comparisons per level. With `k` and `b` near the cube root of `s`, this is
linear in the level's input size. Returning Y counts moves narrow XOR count
corrections through the inverse block shuffle, rather than moving keys back.

**Narrow stable comparisons.** `(key, original source)`
is enough: each subtree has real rows from one original source in each input;
alignment only inserts dummies into the other input, and never changes the
relative order of that input's real rows. Public splitting and concatenation
preserve this order inductively. All-pairs ranks preserve each input's order
at the leaves. Batcher leaves additionally attach public leaf-local positions
before invoking the network. These make each input strictly ordered and
stabilize the network. Source breaks cross-input key ties. Consequently global indices
remain payloads and do not increase comparison width with `n`. The comparator
uses `keyBits+2` bits, including source and infinity. `--full-index` retains
full index tie-breaking as a diagnostic alternative. The plan reports the
alignment comparison width and leaf comparison width separately.

**Narrow rank arithmetic and overlapping communication.** Multiplication by
`m` is a local bit shift. Y-rank addition operates only on the `log2(k)+1`
high bits; its low `log2(m)` bits are public offsets. Public batch offsets are
also inserted locally. X-rank construction overlaps reverse block routing and
Y-rank construction on a separate logical channel. Global ranks are shuffled
with the payloads and opened only after the shuffle. Rank and tag openings
pack consecutive values across byte boundaries.

Public-index adders specialize only the residues that their public index
period actually uses. A candidate is selected only if its padded AND count
is smaller and its depth is no greater. An inexpensive lower bound on the
candidate's nonlinear carries rejects impossible wins before constructing
the circuit. This avoids paying setup time for large, heavily padded candidates.
Unary counts telescope the adjacent-XOR encoding so each comparison share is
loaded once. Alignment scatter writes directly into the next recursion's
public block layout, removing a full expanded-row buffer and copy.

**Leaf input reuse.** For small all-pairs leaves, one circuit contains every
comparison in a leaf pair and shares its `2m` input keys internally. It emits
the binary counts using XOR boundary encoding. This avoids materializing
`2m^2` repeated keys in row-major input matrices. The implementation selects
this layout only through `m=64` and only when its 128-lane padding does not
increase the AND count; otherwise it uses the comparison-lane layout.
Leaves route only original indices and real flags. Batcher retains only those
fields in its last comparator layer and opts into the same width-balanced
selector comparison used by the cube merge. This option leaves existing
Pi-logstar circuit schedules unchanged. Consumed GMW state and temporary matrices
are released between stages, and rank packing processes bytes rather than
individual bits.

The first network layer compares only the key/source/infinity field: its
left public position always precedes its right public position. Position-bit
differences are also known (`m` in odd-even, `2m-1` in bitonic), so those swaps
use only local XORs. Later layers retain the full position tie-breaker.
This optimization is used only for freshly attached public leaf positions;
the full-index diagnostic keeps comparisons of its secret global indices.

The default Batcher leaf uses odd-even merging: `m*log2(m)+1` comparisons per
pair of length-`m` runs, compared with bitonic's `m*(log2(m)+1)`, at the same
`log2(m)+1` network layers. Each layer derives its wire pairs arithmetically;
there is no stored per-comparator schedule. Already-final boundary rows retain
their payload during the last layer's projection. `--leaf bitonic` keeps the
alternative available. The leaf takes ownership of its input buffer and fully
overwritten input scratch space is allocated without clearing it first.

## Public parameters

| Library option | Runner flag | Meaning |
|---|---|---|
| `baseCase` | `--base-case` | Stop recursion at or below this power-of-two half-size. Default 16. |
| `maxDepth` | `--max-depth` | Maximum alignment levels. Default 2, matching the paper's concrete evaluation; 0 removes the cap. |
| `childSizes` | `--children` | Comma-separated child sizes, from outermost level inward. Each positive value must be a power of two smaller than its parent. |
| `cubeBlocks` | `--cube-blocks` | Independently choose the long-list block size of the asymmetric merge at each level. |
| `leaf` | `--leaf allpairs\|batcher\|bitonic` | All-pairs comparisons, Batcher odd-even (default), or the alternative bitonic network. |
| `fullIndexComparisons` | `--full-index` | Force global-index comparisons throughout, as a diagnostic alternative. |
| `maxExpandedRows` | `--max-expanded-rows` | Reject an oversized public recursion schedule before registering correlations. Default `2^28`. |
| CorGenerator batch size | `--batch-size` | Correlation batch size; default `2^20`. |
| CorGenerator concurrency | `--concurrency` | Backend correlation-generation concurrency; default 2. |

Zero or missing per-level entries select defaults. Extra entries for levels
that are never reached are rejected rather than silently ignored. Both parties
must agree on every public parameter. The TCP runner checks the complete public
configuration before generating correlations.

The resource guard bounds rows, not peak RAM. Deeper recursion doubles dummy
storage per level; larger all-pairs leaves increase comparisons quadratically.
Correlation preprocessing also occupies memory. Choosing fewer levels, a
different terminal merge, and smaller correlation batches changes these costs.
Full-size runtime and peak memory remain to be measured before selecting the
paper's final parameters; no setting here is claimed to be globally optimal.

## Round and communication accounting

The public plan reports comparisons, padded ANDs, stage dimensions, registered
offline requests, estimated application payload bytes, and online dependency
depth. It distinguishes the **sum** of all executed GMW AND layers from the
longest dependent path. For one alignment level, if `D` is the cube count
circuits' total AND depth and `Ax,Ay` are the two adder depths, the online bound is

```
D + 6 + max(Ax, 2 + Ay)
```

The six steps are forward block shuffle (2), tag opening (1), final alignment
shuffle (2), and rank opening (1). The two inverse-routing steps lie on the Y
path. All-pairs leaves add comparison depth, rank-adder depth, and three
shuffle/opening steps. Final compaction adds its conversion-circuit depth and
six OT/shuffle/opening steps. Batcher leaves add their network's AND depth.

These are concrete Boolean-backend bounds. The paper counts comparison and
arithmetic primitives as constant-round operations. The implementation avoids
serializing the recursive fan-out, but its concrete layer counts are not the
paper's idealized primitive counts. Correlation generation, input sharing,
transport setup, output reconstruction, and local work are excluded from the
online depth.

Application payload sums both parties' sent bytes: `padded_ANDs/2`, two row
payloads per composed-shuffle application, packed openings, and compaction OT
updates. Transport framing is excluded. Runtime JSON keeps actual sent and
received counters, preprocessing and online phases separately, and per-stage
counts. Synthetic output verification occurs after phase accounting.

For the default `n=2^20` schedule, the public plan has two alignment levels
with half-sizes `2^20 -> 8192 -> 256`, cube blocks `128,32`, and `8n` expanded
rows. It uses **50,609,920 comparisons**, versus approximately 54.3 million in
the paper's concrete estimate. Odd-even leaves remove 4,177,920 comparisons
from the previous implementation's 54,787,840. The following are **calculated
plans only**:

| Key bits | Alignment / leaf comparison bits | Full online dependency bound | Application payload bytes, both parties |
|---:|---:|---:|---:|
| 32 | 34 / 43 | 123 | 3,889,367,360 |
| 128 | 130 / 139 | 149 | 10,574,522,944 |

With the same recursion parameters at every `n=2^10,...,2^20`, implementation
version 2 reduces calculated application payload by **8.69–12.11% for 32-bit
keys** and **8.38–12.12% for 128-bit keys**, compared with version 1. Every
dependency bound is unchanged. These reductions come from the network and
circuit changes, with no selection of parameters for individual list sizes.

Three small local development checks used the same default recursion and
`--batch-size 65536`, with one invocation per version and configuration:

| n / key bits / pattern | Online ms, v1 → v2 | Preprocessing ms, v1 → v2 |
|---|---:|---:|
| 33 / 7 / equal | 0.316 → 0.360 | 132.438 → 124.874 |
| 257 / 32 / duplicates | 3.036 → 2.846 | 444.442 → 419.821 |
| 1024 / 64 / random | 8.395 → 7.618 | 946.453 → 842.457 |

All outputs were verified. These are noisy development observations, without
repetitions or parameter tuning; they do not establish a runtime speedup for
all sizes. The communication reductions above follow from public circuits.

The paper's 128-bit table estimates 118 rounds. The implemented full bound is
higher: it includes tie and infinity bits, compare-exchange payload selection,
rank arithmetic, block routing, and final compaction. In particular, comparison
widths above 128 incur another Boolean layer. Similar comparison counts
do not imply equality of complete communication or round costs. These plans
do not establish large-run runtime or memory usage.

## Use and validation

Initialize a fresh `PiMedian` with an initialized real `CorGenerator`. Call
`preprocess()` to start every registered request. Run `cor.start()` concurrently
with `prepare(socket, privatePrng)`, wait for both, then call `merge()` once.
The library never reconstructs keys or the returned gather permutation.

In the configured Linux/WSL build environment:

```sh
cmake -S . -B out/build/linux
cmake --build out/build/linux --target pimedian -j4
ctest --test-dir out/build/linux -R pi_median_real_crypto --output-on-failure
python3 tests/pi_median_reference.py
python3 scripts/check_pi_median.py

# Plan only: no preprocessing or online execution, including at n=2^20.
out/build/linux/frontend/pimedian --n 1048576 --plan

# The asymptotic recursion with all-pairs leaves, also plan only.
out/build/linux/frontend/pimedian --n 1048576 --max-depth 0 --leaf allpairs --plan

# Example public override; not a recommendation derived from measurements.
out/build/linux/frontend/pimedian --n 1024 --base-case 8 \
  --children 64,8 --cube-blocks 16,8 --plan

# Small development execution with correctness reconstruction.
out/build/linux/frontend/pimedian --n 65 --leaf allpairs --base-case 8 \
  --pattern duplicates --batch-size 16384
```

`scripts/plan_pi_median.py` can later compare a public candidate set for every
`n=2^10..2^20`, recording each candidate and the selection in a **new** JSON
file. It only invokes `pimedian --plan`; it cannot launch a benchmark sweep.
It varies floor/ceiling recursion rounding, base thresholds, leaf backend,
two-level versus uncapped recursion, and cube-block scale. The objective is payload plus a configurable byte weight
per dependent step, optionally under a round cap. This is a public cost model,
not timing-based optimization. Each saved command still ends in `--plan`.

```sh
python3 scripts/plan_pi_median.py --min-log 10 --max-log 20 \
  --bases 8,16,32,64 --leaves allpairs,batcher --out out/median-plans.json
```

The independent plaintext oracle checks both full-index and source-only
comparisons, repeated equal dummy medians, stable ties, padding, alignment
maxima, and custom recursion schedules. The real-crypto suite uses random
XOR shares, all three leaf backends, narrow and wide keys, disjoint and reversed
ranges, maximum keys, partial padding, multiple recursion levels, block sizes
one and the full parent size, packed and unpacked leaves, invalid parameters,
and single-use lifecycle checks. The focused development checker inspects
public schedules for all eleven requested sizes, checks small local/TCP
outputs and byte accounting, and rejects mismatched TCP configurations.

## Security scope

Validation at this implementation checkpoint: all three CTest suites passed,
including 212 Pi-median real-crypto cases and 255 asymmetric-merge regression
cases; the independent plaintext oracle passed 64,008 protocol cases and
4,590 stable leaf-network cases, including the public-position simplification.
The focused checker passed 22 large public schedules (odd-even and bitonic for
each requested size), six small local/TCP executions, parameter/backend
mismatch rejection, and selection/accounting checks. An eighteen-candidate
public-only planner smoke check also completed. No formal benchmarks were run.

The target is two-party semi-honest execution on sorted inputs, matching the
repository. Mock/debug correlations and reuse of a consumed instance are
rejected. All private randomness comes from the real backend and private PRNGs;
the runner's public seed controls only its synthetic test keys.

Block tags are opened only behind a fresh joint shuffle, and their multiset
is fixed independently of inputs. The same hidden block permutation is reused
only to return corrections, consuming **disjoint fresh mask bytes**. Alignment
and leaf ranks are full permutations of fixed public ranges and are opened
behind separate fresh joint shuffles. Final extraction opens shuffled flags
with a fixed real count and then only active ranks. Inactive ranks, keys,
payload indices, comparison results, and unshuffled block assignments remain
shared. The shuffle openings therefore disclose fixed input-independent
multisets. This is an implementation argument and test coverage, not a new
formal security proof or malicious-security claim.
