# Second optimization checkpoint (implementation version 3)

This is a historical checkpoint. The current implementation and latest focused
checks are described in `root-merge-optimizations-v4.md`; the numbers below
remain specific to version 3. Its executable is now preserved at
`out/root-optimization-v3/rootmerge`.

This pass follows the version-2 checkpoint in `root-merge-optimizations.md`.
It changes the implementation and focused correctness checks only. No scaling
sweep, network experiments, parameter retuning, or paper edits were performed.
The committed evaluation still describes version 1.

## Fixed-parameter cost results

Application payload sums both parties' sent bytes, excluding transport framing.
Large-instance entries below are public circuit/payload calculations, **not new
large-instance measurements**. All use the previous block choices and 32-bit keys.

| Method | n | m | b | Version-2 payload → version-3 payload (bytes) | Round bound |
|---|---:|---:|---:|---:|---:|
| Cube | 4,096 | 16 | 16 | 451,844 → 276,736 | 50 → 25 |
| Square | 4,096 | 64 | 32 | 607,696 → 506,688 | 56 → 44 |
| Cube | 1,048,576 | 102 | 128 | 138,825,242 → 74,076,496 | 64 → 25 |
| Square | 1,048,576 | 1,024 | 512 | 185,346,368 → 145,704,000 | 67 → 53 |

At `n=2^20`, this pass removes a further **46.6%** of Cube's online payload and
**21.4%** of Square's, relative to the already-optimized version 2. Current
payloads are 70.64 MiB and 138.95 MiB. Relative to version 1's payload model,
the cumulative reductions are about 62.1% and 49.5%; round bounds went from
109 to 25 and from 210 to 53.

The Batcher comparison circuit from version 2 remains in place unchanged.
Its two matched large-shape plans still cost 334,122,496 and 470,074,880 bytes,
both at 168 rounds. The current roots use 77.8% and 69.0% less application
payload than those also-optimized baselines. None of these cost reductions
is asserted to be a measured runtime speedup.

## Why the reductions are valid

**Cube's upper comparison triangle is unnecessary.** If selected slot `j>i`
is real, it is the first X in a target block strictly after `X_i`'s target.
Every key there is at or above `X_i`. If it is dummy, every padded key is
infinity. Therefore `[Y_(j,t) < X_i]` is publicly false in either case.
Only slots `j<=i` are compared, reducing detail comparisons from `m*m*b` to
`m*(m+1)*b/2`. Omitted comparisons are reconstructed as public zeros for
Y-count recovery. Equal keys still place X before Y.

**Cube's remaining popcounts disappear.** Let `G_i` be the number of real
selected groups through X position i, and `F_i` the number of selected full
blocks below `X_i`. Every previous real group contributes a full block, and
the current group contributes either a partial block or one full block:
`F_i = G_i - 1 + own_full_i`. Consequently
`own_full_i = parity(F_i) XOR parity(G_i) XOR 1`.
Both parities are local XORs of existing shared bits. At most one selected
block contributes nonzero low offset bits, so those also combine by XOR.
This removes both of the remaining arithmetic popcounts, rather than merely
changing their circuits.

**Block start and offset combine locally.** In either root, an offset equal
to `b` is possible only in the forced final target block, whose ID is `K-1`.
Thus the block-ID carry is
`new_id = id XOR (own_full * ((K-1) XOR K))`.
This selects between public constants under one shared bit and requires no
interactive multiplication. Concatenating `new_id` and the low offset bits
gives the Y insertion count; only addition of the public X index remains.

**Public-index addition uses fewer ANDs.** For a secret count `a` and public
row index `i`, initial carry signals `p=a XOR i` and `g=a AND i` are local.
A Sklansky prefix computes carries only across the count's secret word width.
The carry into the public high index word selects locally between its public
old value and successor. The Y count has `ceil(log2(m+1))` bits, often much
smaller than a full output rank. This removes the generic secret-secret
initial carry layer and unnecessary high-word gates.

**Tagging uses one layer.** A first-occupant map is
`map[i,j] AND NOT map[i-1,j]`, with the first row copied locally. One-hot maps
and nondecreasing target IDs make its row parity the first-in-group flag.
This replaces a separate same-group product and subsequent tag product.

**Known output labels move offline.** Original indices `0..N-1` are public,
so their secret shuffled shares are prepared before inputs arrive. Online,
the same fresh joint permutation is applied only to scatter ranks, consuming
disjoint fresh mask bytes. Every row is active, so an active-flag opening is
unnecessary. Rank shares are opened in byte-packed form instead of 32-bit
slots. The opened ranks remain a uniform permutation independent of the
secret merge. Original indices remain secret-shared at the output.

This last change deliberately moves one public-label shuffle into preprocessing.
It does not eliminate that work or reuse randomness. Two byte-aligned fields
can require more mask padding than one combined record for some smaller rank
widths. At the two reported large shapes, the rank-field mask width stays six
bytes per row; preprocessing additionally communicates the shuffled labels.
The large reduction in GMW gates also reduces binary-OLE requests, but no
offline-time claim is made without the later evaluation.

**Independent rank calculations overlap.** X-rank addition runs on a separate
logical socket while the other branch performs suffix recovery, inverse block
routing, and Y-rank addition. Both branches must finish before final inversion.
The GMW-layer metric continues to sum all executed circuits; the online round
bound follows the longest dependency path:

```text
sum(GMW layers) + 8
  - min(X-add layers, suffix layers + Y-add layers + 2)
```

The eight extra steps are two forward block-shuffle steps, tag opening, two
inverse block-shuffle steps, two final rank-shuffle steps, and rank opening.
The socket fork adds framing bytes but no dependent acknowledgement step.

## Validation and retained artifacts

- Release build passed. All **243 real-crypto correctness cases** passed with
  fresh correlations and random XOR shares, including duplicates, both
  disjoint orders, maximum keys, 65/127/256-bit keys, irregular dimensions,
  full/partial final blocks, and API misuse checks. Added cases exercise
  carries at and beyond an 11-bit count word. The full 8-bit-domain case now
  uses boundary and triangular detail comparisons; version 3 intentionally
  does not evaluate the redundant upper triangle.
- **40,512 plaintext root cases** passed across versions 1, 2, and 3, along
  with exhaustive small unequal-Batcher binary checks and **33,410** unary
  counts split into random XOR shares. Version-3 checks cover triangle
  omission, first-occupant maps, parity recovery, and the last-block ID carry.
- Only four small before/after probes ran, at `n=4096`, one per root/version.
  All outputs verified. Measured online sent bytes fell from **452,596 to
  277,200** for Cube and **608,544 to 507,456** for Square. These are single
  correctness/byte checks, not replicated timing estimates.
- Two additional tiny loopback TCP correctness cases passed, one per root,
  exercising the overlapping logical channels with separate processes. A
  mixed version-2/version-3 pair rejected the configuration before protocol
  execution. These checks used no network emulation; their records are in
  `out/root-optimization-v3-tcp.json`.
- `out/root-optimization-v3-check.json` retains the exact commands, public
  plans, small probes, and executable hashes. Correctness output is in
  `out/root-optimization-v3-selftest.jsonl`; build output is in
  `out/root-optimization-v3-build.log`.
- Version-2 executable is preserved at `out/root-optimization-v2/rootmerge`,
  SHA256 `a9196de63ae51ac1b67b69e2fba970a64e9422211d2b5458b3081a85d3cbc3b0`.
  The original version-1 executable remains at `out/rootmerge-evaluated`.
- Checked version-3 executable: `out/build/linux/frontend/rootmerge`, SHA256
  `4cb0333f719b788fcf929aa081d5cc6523058af528869fa099e0faeedb9b70a5`.

The frontend emits `implementation_version: 3` and rejects mixed-version TCP
peers through its configuration magic. The payload model understands all three
versions. The focused comparison can be reproduced with a fresh output path:

```sh
python3 scripts/check_root_merge_optimization.py \
  --before out/root-optimization-v2/rootmerge \
  --out out/fresh-v3-plan-check.json
# Add --probe only to include the four small correctness/byte checks.
```

The security target remains sorted-input, semi-honest two-party execution.
No new secret-dependent value is opened, no correlation is reused, and no
mock/dealer randomness is introduced. Full experiment reruns and paper changes
await the user's next instruction.
