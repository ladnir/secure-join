# Third optimization checkpoint (implementation version 4)

This pass reduces online communication and depth further at the same public
block sizes. It includes public cost calculations and focused correctness
checks only. No scaling sweep, parameter search, emulated-network experiments,
or paper edits were performed. The committed paper evaluation remains version 1.

## Fixed-parameter costs

Payload sums both parties' application bytes and excludes transport framing.
The large-instance entries are **calculated costs, not new measurements**.
All entries use 32-bit keys and the previously selected block sizes.

| Method | n | m | b | Version-3 payload → version-4 payload (bytes) | Round bound |
|---|---:|---:|---:|---:|---:|
| Cube | 4,096 | 16 | 16 | 276,736 → 268,936 | 25 → 24 |
| Square | 4,096 | 64 | 32 | 506,688 → 503,392 | 44 → 43 |
| Cube | 1,048,576 | 102 | 128 | 74,076,496 → 71,747,528 | 25 → 24 |
| Square | 1,048,576 | 1,024 | 512 | 145,704,000 → 141,886,528 | 53 → 52 |

At `n=2^20`, these are further payload reductions of **3.1%** for Cube and
**2.6%** for Square relative to version 3. Padded online AND counts fall from
101,553,024 to 98,571,776 and from 231,528,576 to 225,500,288, respectively.
The round metric remains the longest dependency path described in
`root-merge.md`; it does not count all independent GMW circuits sequentially.

The shared comparison improvement also applies to unequal Batcher. For the
same two large input shapes, its round bound drops from **168 to 147**.
Its payload increases slightly: 334,122,496 → 336,375,872 bytes for `m=102`,
and 470,074,880 → 473,222,720 bytes for `m=1024`, about **0.7%** each.
This is a deliberate depth/AND tradeoff in the common comparator, not a
reduction in every cost metric. Even against the lower version-3 Batcher
payloads, the new roots use 78.5% and 69.8% fewer bytes. No runtime speedup
is claimed from these public plans.

## Implementation changes

**Public-index carry specialization.** For `w`-bit count `a` and public row
index `i`, the circuit needs carries only in the low `w` bits. For each
public residue `k = i mod 2^w`, a leaf propagates when `a_j XOR k_j` is one.
When it does not propagate, its outgoing carry equals public bit `k_j`.
The carry value may therefore be arbitrary when the leaf propagates, except
at bit zero where it must encode input carry zero. Combining a higher and
lower segment selects the lower value if the higher segment propagates,
and selects the higher value otherwise. This preserves the segment invariant;
prefixes reaching bit zero give the actual carries.

Specializing these selectors removes gates whenever their public alternatives
coincide or are opposite constants. A Sklansky schedule computes all prefixes.
All `2^w` residues share one circuit, with `ceil(n/2^w)` SIMD lanes, so the
residue groups do not introduce sequential protocol steps. Padding rows are
ignored, and the cost comparison includes GMW's 128-lane rounding. Only the
selected circuit requests correlations. The implementation considers this
choice through `w=11`, bounds circuit construction size, and selects it only
when padded ANDs decrease without added depth. Other sizes use the generic
version-3 adder. Both choices depend only on public dimensions.

The large Cube and Square plans select 128 and 2,048 residues, respectively.
At `n=4096`, Cube selects 32; Square retains the generic adder because the
specialized circuit's SIMD padding would outweigh its gate savings. The
public JSON field `rank_adder_residues` records the choice, with zero denoting
the generic circuit. The specialized path also avoids allocating and filling
the generic propagate/generate input matrices.

**Width-balanced comparison.** The strict comparator now recursively splits
the actual bit width in half. This avoids hanging a short high tail above a
complete power-of-two tree. The 32-bit comparator remains 58 ANDs at depth
six. The 33-bit detail comparator becomes 60 ANDs at depth six, instead of
59 at depth seven. The additional bit represents infinity padding. Both roots
save one dependent layer. Unequal Batcher uses the same comparator builder
for its key-plus-index comparisons, accounting for its depth/byte tradeoff
above. Stable tie handling and the comparison relation are unchanged.

**Bit-packed openings.** Shuffled block tags now use exactly
`ceil(log2(m+1))` bits per value, replacing 32-bit slots. Shuffled rank
openings likewise pack values across byte boundaries rather than rounding
every rank to whole bytes. Each message has at most seven trailing padding
bits. This changes the encoding of the already-opened values only: the tag
multiset is fixed by public dimensions, and the shuffled ranks form a uniform
permutation. Shuffle records and their fresh masks remain byte aligned.
The payload model accounts for both packing changes and still understands
historical versions 1–3. The TCP handshake rejects mixed protocol versions.

The security target remains semi-honest two-party execution on sorted inputs.
No new secret-dependent quantity is revealed, no mask or correlation is reused,
and no work is replaced with mock preprocessing. Reduced AND counts also
reduce their correlation requests; offline performance was not benchmarked.

## Focused validation

- The release build and **255 real-crypto correctness cases** passed with
  random XOR shares and fresh correlations. Coverage includes duplicate and
  maximum keys, both disjoint orders, irregular lengths, 65/127/256-bit keys,
  and full/partial blocks. Twelve added cases exercise specialized adders with
  4, 16, and 128 residues, including partial final groups. The existing cases
  also cover the generic fallback and carries through an 11-bit count word.
- Four small before/after probes at `n=4096` verified their outputs. Actual
  online sent bytes fell **277,200 → 269,384** for Cube and
  **507,456 → 504,144** for Square. The measured differences match the payload
  model plus transport framing; these are single byte/correctness checks,
  not replicated timing experiments.
- Both roots passed two-process loopback TCP checks at `m=17, n=65, b=8`.
  A mixed version-3/version-4 pair rejected the configuration before protocol
  execution. No link emulation was used.
- Plans, commands, executable hashes, and small probes are retained in
  `out/root-optimization-v4-final-check.json`. Build and self-test output are
  in `out/root-optimization-v4-final-build.log` and
  `out/root-optimization-v4-final-selftest.jsonl`; TCP records are in
  `out/root-optimization-v4-tcp.json`. These are local ignored artifacts.
- The version-3 executable is preserved at `out/root-optimization-v3/rootmerge`,
  SHA256 `4cb0333f719b788fcf929aa081d5cc6523058af528869fa099e0faeedb9b70a5`.
  Version-1 and version-2 snapshots remain unchanged.
- Checked version-4 executable: `out/build/linux/frontend/rootmerge`, SHA256
  `b1b59597c093c60ed0de5dfaa31bddafcb0275865474b1a0c8267ee42c2d3334`.

Reproduce the focused comparison using a fresh output path:

```sh
cmake --build out/build/linux --target rootmerge -j4
out/build/linux/frontend/rootmerge --self-test
python3 scripts/check_root_merge_optimization.py \
  --before out/root-optimization-v3/rootmerge \
  --probe --out out/fresh-v4-check.json
```

Omit `--probe` for public plans only. Full experiments and paper updates remain
pending the user's instruction.
