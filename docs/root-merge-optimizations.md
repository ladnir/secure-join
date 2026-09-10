# Root-merge optimization checkpoint (implementation version 2)

Historical checkpoint: the subsequent implementation-version-3 changes and
focused checks are recorded in `root-merge-optimizations-v3.md`.

Implemented and checked on 2026-09-09, after commit `4bda6a8`. This is a
focused optimization checkpoint, **not a new paper evaluation**. No scaling
sweep, network emulation, parameter retuning, or paper edits were performed.
All comparisons here keep the previously selected block sizes and 32-bit keys.

## Changes

1. **Local unary counting.** For a sorted comparison vector `11...100...0`,
   `q[i] XOR q[i+1]` (with final zero) encodes a unique count boundary. XORing
   public integers `i+1` under these bits gives a shared binary count without
   communication. This replaces coarse counts in both roots, within-block X
   counts in SquareRootMerge, and Y counts in CubeRootMerge.
2. **Cube offsets.** Every selected real block before X's target is entirely
   below X, every later block is entirely at or above X, and at most the target
   block is partial. Its low offset bits can therefore be XORed across all
   selected blocks; only one full-block flag per block needs a popcount.
   The generic `m*b`-bit popcount becomes an `m`-bit popcount for each X.
3. **Square suffix ranks without addition.** For fixed copied Y position `t`,
   let `a_i=[X_i <= Y_t]`, and `c_{i+1}` indicate that the next X belongs to
   the same block group. Its suffix rank obeys
   `rank_i = (a_i AND c_{i+1}) ? rank_{i+1} : i+a_i`.
   `i+a_i` is a local selection between public integers `i` and `i+1`.
   The recurrence is evaluated by a reversed segmented broadcast, with one
   AND layer per combine, replacing the previous carry-propagating suffix sums.
   At the first X in a group it gives the global X insertion count for Y;
   only these real-block entries are routed back to Y.
4. **XOR correction routing.** Route `fine_count XOR coarse_count` instead of
   their arithmetic difference. XOR with the original coarse count after the
   inverse permutation recovers the fine count; unselected blocks retain their
   coarse count. This removes two arithmetic circuits and keeps the same narrow
   correction width and fresh forward/inverse mask separation.
5. **Smaller comparisons and records.** A balanced comparison tree selects the
   most significant differing bit of the right input. Only the least-significant
   leaf must explicitly encode strict less-than; other equal-segment values are
   immaterial. At 32 bits this costs 58 rather than 94 ANDs, with the same six
   AND layers. **The unequal Batcher baseline gets this comparator too.**
   Boundary comparisons omit the unused infinity bit. Selected dummy keys are
   already infinity, so CubeRootMerge's extra validity AND is unnecessary.
   Block records omit unused block IDs and the now-unnecessary group-start index.

All operations remain over secret XOR shares. No comparison, group boundary,
or count is newly opened. The only openings remain fixed-multiset tags and
final ranks under fresh joint shuffles. Inputs must remain sorted, as in the
original semi-honest two-party contract. The eight-leaf grouped broadcast,
fresh correlation generation, and single-use checks remain in force. Earlier
Pi-logstar implementation files were not changed.

## Fixed-parameter public cost checks

These are exact circuit schedules and application-payload calculations,
**not measured large-instance runtime or network traffic**. Payload sums both
parties' sent bytes and excludes transport framing. The round bound sums
interactive GMW layers plus the same nine shuffle/opening steps as before.

| Protocol | n | m | b | Payload bytes, before → after | Round bound, before → after |
|---|---:|---:|---:|---:|---:|
| Cube | 4,096 | 16 | 16 | 692,644 → 451,844 | 75 → 50 |
| Square | 4,096 | 64 | 32 | 920,016 → 607,696 | 125 → 56 |
| Cube | 1,048,576 | 102 | 128 | 195,360,818 → 138,825,242 | 109 → 64 |
| Square | 1,048,576 | 1,024 | 512 | 288,614,272 → 185,346,368 | 210 → 67 |

At `n=2^20`, this is **28.9% less payload and 41.3% fewer rounds for Cube**,
and **35.8% less payload and 68.1% fewer rounds for Square**, compared with
their previous implementations. Parameters have not yet been retuned.

For the same large shapes, the improved Batcher plans cost 334,122,496 bytes
(`m=102`) and 470,074,880 bytes (`m=1024`), both with a bound of 168 rounds.
The optimized roots therefore still use about 58.5% and 60.6% less application
payload than their respective **also-optimized** Batcher baselines. These are
cost comparisons, not claims about measured speedup.

## Validation and provenance

- Release build succeeded. Only inherited deprecation warnings were emitted.
- All **239 real-crypto correctness cases** passed, including random XOR shares,
  duplicate/all-equal/max keys, 1-bit and 65/127/256-bit keys, irregular lengths,
  blocks larger than the unpadded Y list, and single-use/insecure-mode rejection.
  One case checks all **65,536 pairs of 8-bit keys** in the actual GMW detail
  matrix, including strict equality behavior.
- The plaintext oracle passed **27,008 old/new root cases**, exhaustive small
  unequal-Batcher binary checks, and **33,410 random-share unary-count checks**.
- Only **four small performance/byte probes** were executed: one old and one
  new run per root at `n=4096`. All reconstructed outputs were correct. Measured
  online sent bytes fell from 693,796 to 452,596 for Cube, and from 921,968 to
  608,544 for Square. Each is a single check, not a timing estimate; no timing
  conclusions are drawn. Every other before/after comparison was `--plan` only.
- Preserved version-1 executable: `out/rootmerge-evaluated`, SHA256
  `73debf57d6fcfaecacd7f4da243a3015ee5b53516f42bfa624d8fc03b79c89fb`.
- Checked version-2 executable: `out/build/linux/frontend/rootmerge`, SHA256
  `a9196de63ae51ac1b67b69e2fba970a64e9422211d2b5458b3081a85d3cbc3b0`.
- Local check records: `out/root-optimization-check.json` and
  `out/root-optimization-selftest.jsonl`; build log:
  `out/root-optimization-build.log`. These are separate from paper benchmarks.

The frontend now emits `implementation_version: 2`, and its TCP configuration
magic changes so old and new binaries reject mismatched versions. The parameter
selection payload model understands both record layouts; archived JSON without
a version field is treated as version 1. To reproduce only the focused check:

```sh
python3 scripts/check_root_merge_optimization.py --out out/fresh-root-plan-check.json
# Optional four small correctness/byte probes, using a different fresh output:
python3 scripts/check_root_merge_optimization.py --probe --out out/fresh-root-probe-check.json
```

Full benchmarking and paper changes await the user's next instruction.
