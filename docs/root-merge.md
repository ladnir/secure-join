# CubeRootMerge and SquareRootMerge

The current code is **implementation version 3**, with additional communication
and depth optimizations. The committed benchmark tables, plots, and paper still
describe version 1. They have not been rerun or regenerated. See
`root-merge-optimizations-v3.md` for the latest focused checks and differences;
`root-merge-optimizations.md` records the preceding version-2 checkpoint.

`secure-join/Sort/RootMerge.h` exposes `CubeRootMerge`, `SquareRootMerge`, and
`BatcherUnequalMerge`. These merge two sorted, unsigned XOR-shared lists of
lengths `m <= n` and return an XOR-shared **gather permutation** of `X || Y`.
Equal keys are stable: X precedes Y, and each list keeps its original order.
The library supports 1–256-bit keys. The synthetic benchmark CLI supports
1–64-bit keys, with additional 65/127/256-bit coverage in its self-test.

These are the paper's asymmetric protocols: the principal comparisons use
`m = ceil(cuberoot(n))` or `m = ceil(sqrt(n))`, respectively. They are not
balanced-list replacements for the recursively composed Pi-logstar protocol.
All three implementations also accept other public `m <= n` dimensions.

## Concrete implementation

Both protocols divide Y into `K = ceil(n / b)` blocks, for a public power-of-two
block size `b`. The final partial block is padded with an extra-bit positive
infinity, so the entire unsigned input domain remains valid. They compare each
X key with the maximum of every block except the last. This assigns keys to
the first block whose maximum is at least the key; keys above all Y values
go to the last block. This explicit convention handles equal keys and removes
the paper pseudocode's boundary ambiguities.

For each X key, a one-hot block map identifies whether it is the first X key
assigned to that block. The first such key tags its real block; other keys tag
their own dummy blocks. There are exactly `m` distinct nonzero tags and `K`
zero tags, regardless of the data. A fresh joint shuffle hides their source
positions before the tags are opened. This selects exactly one real or dummy
block per X key without revealing the number of distinct selected blocks.

* **CubeRootMerge** compares `X_i` only against selected block slots `j <= i`.
  Every later real slot is at or above `X_i`, and dummy slots are infinity,
  so the omitted upper triangle is publicly false. At most one real block
  contributes a partial offset; its low bits combine by XOR. The parity of
  full-block flags and the parity of group starts identify the single possible
  within-block overflow. Thus all offset counting is local, with no popcount.
  The scatter rank is `i + z_i*b + offset_i`. The two passes perform
  `m*(K-1) + m*(m+1)*b/2` key comparisons.
* **SquareRootMerge** uses a secret segmented prefix broadcast to replace
  dummy entries by copies of the preceding real block. Each X key is compared
  with only its own copied block. Its scatter rank is `i + z_i*b + offset_i`.
  A reversed segmented broadcast recovers Y insertion counts within each
  group of copied blocks. The two passes perform `m*(K-1) + m*b` comparisons.

Y ranks are recovered by unshuffling **count corrections**, which are narrower
than the 32-bit key blocks in this evaluation. A block's coarse count is the number of X keys
at or below its maximum (the last block uses `m`). In a selected real block,
the correction is its fine count **XOR** this coarse count. Unselected blocks
receive zero corrections. XORing the restored correction with the original
coarse count, then adding the original Y index, gives its scatter rank. The
forced last-block count cancels for a selected last block. If the last block
is unselected, all X keys precede it and its count is indeed `m`.

Finally, a separate fresh joint shuffle opens the scatter ranks and places
the still-shared original indices at those positions. The public original
indices are shuffled during preprocessing, and only ranks are shuffled online;
these applications consume disjoint fresh mask bytes. All rows are active,
so no flag-opening phase is needed. Rank shares are opened in byte-packed form.
A correct stable merge
has each rank `0..m+n-1` exactly once; the opened shuffled ranks are a uniform
permutation independent of the keys. The output is a secret gather permutation.
The benchmark opens this output **after** all measured phases to check it.

## Engineering choices and security scope

The protocols use the same real two-party GMW and OT/VOLE correlation backend
as Pi-logstar and Batcher. No trusted dealer, mock correlations, public
permutation seed, or revealed comparison result is used. The API rejects mock
and debug correlation generators, repeated preprocessing/preparation, and
reuse of a consumed instance. Its security target is semi-honest two-party
execution with sorted inputs; it does not prove that a malicious party supplied
sorted values or followed the protocol.

The forward block shuffle and its inverse share the same hidden permutation
but consume **disjoint fresh mask bytes**. A separate permutation hides final
ranks. Private permutations use the backend's corrected uniform sampler and
fresh OS randomness. The public seed controls synthetic benchmark keys only.

Rank addition exploits public row indices. Initial carry generate/propagate
bits are computed locally; a Sklansky prefix circuit handles only the secret
count width. Its final carry selects between two public high words locally.
Counts of sorted comparison bits use local XOR boundary encoding; no popcount
remains in either root. Tag generation combines one-hot map changes in one layer.
SquareRootMerge's suffix operation uses a reversed eight-leaf grouped prefix
broadcast, batched over Y positions. Every combine takes one AND layer. The
forward broadcast uses the same grouped schedule. Small lane sets still incur
GMW's 128-lane padding. Both roots and the unequal Batcher baseline use the
same smaller strict-comparison circuit; 32-bit comparison costs 58 ANDs at
depth six, down from 94 at depth six. Earlier Pi-logstar code is unchanged.
The code drops consumed GMW state and large temporary matrices between stages.

The reported round bound is the longest dependency path, including eight
one-way shuffle/opening steps. X-rank addition overlaps suffix recovery,
inverse block routing, and Y-rank addition on a separate logical channel.
Specifically the bound is the sum of executed GMW layers plus eight, minus
`min(X-add layers, suffix layers + Y-add layers + 2)` for that overlap.
The separately reported GMW-layer sum and stage counters still count all
executed circuits. These concrete Boolean-circuit bounds are distinct from
the paper's idealized primitive-round counts. The archived version 1 executed
the phases sequentially and used Boolean suffix additions; version 2 removed
those suffix carry circuits, and version 3 adds this overlap.

The baseline is Batcher's **odd-even merge network on the exact unequal list
lengths**, with empty recursive branches removed publicly. It does not pad
both lists to `n` and does not sort an initially unsorted concatenation.
Comparisons use the key followed by the original index for identical stability
semantics. The last comparator layer retains only the index bits. This baseline
differs from the balanced bitonic network in the earlier Pi-logstar tables;
those earlier measurements remain separate.

## Parameter selection and measurement

For each public size and protocol, the runner considers `b0/4`, `b0/2`, `b0`,
and `2*b0`, where `b0` is the smallest power of two at least `m` (with valid
endpoint clipping). It minimizes estimated online application payload bytes,
including padded GMW ANDs, block movement, and rank inversion; ties use the
round bound. Transport framing is omitted from this selection model. Every
candidate schedule and selected block size is saved. Selection uses no timing
sample or secret key. The chosen parameters are held fixed across repetitions
and network profiles.

The **archived version-1** scaling study includes every long-list size `2^10` through `2^20`, with
three fresh runs per method and shape. Root and Batcher inputs have identical
dimensions, 32-bit keys, and public input seed within each repetition. Protocol
order alternates. Both use correlation batches of `2^20` and concurrency two.
The local transport schedules both parties on one OS thread. TCP uses two
processes, each with two Asio I/O workers and a main thread; no GMW worker pool
is enabled. Preprocessing, online time, communication, and setup are recorded
separately. Communication sums both parties' sent bytes. TCP phase time is the
slower endpoint; offline-plus-online time sums these two phase maxima.
Raw counters retain transport framing. Local Batcher pays 48 additional online
bytes for Coproto's first-use main-channel metadata (24 bytes per party); TCP
establishes this channel in its preceding public-configuration handshake. Root
protocols use the main channel during preparation in both transports. The
summary validates this exact difference and keeps the measured counts.

The hardware and isolated `tc netem` link construction match the earlier
evaluation: Lenovo 83DF laptop, Intel Core i9-14900HX, 32 logical cores, about
32 GiB physical RAM, Windows with Ubuntu under WSL2 (about 15 GiB guest RAM).
No CPU affinity or fixed-frequency policy is imposed. LAN is 1 Gbit/s and
0.2 ms RTT; WAN is 100 Mbit/s and 40 ms RTT, per-direction full-duplex rate caps.
Both endpoints run on this laptop. `benchmark_logstar.trial` samples RSS and
swap every 0.5 seconds and rejects swapped trials in this study. Compiler and
executable/source provenance are recorded with the artifacts.

## Reproduction

In an application, initialize a fresh instance with a real `CorGenerator`,
call `preprocess()`, then execute `cor.start()` concurrently
with `prepare(socket, privatePrng)`. After both complete, call
`merge(xShares, yShares, outputPermutation, socket, privatePrng)` once. Both
parties must agree on the public dimensions, key width, and block size. The
benchmark driver includes a TCP configuration handshake and an example of this
coroutine scheduling. The returned permutation's `i`th reconstructed entry is
the index in `X || Y` of output row `i`.

From the repository in the configured Linux/WSL build environment:

```sh
cmake --build out/build/linux --target rootmerge -j4
ctest --test-dir out/build/linux -R asymmetric_merge_real_crypto --output-on-failure
python3 scripts/test_root_merge_model.py

# Fresh path required; existing raw files are never overwritten.
python3 scripts/benchmark_root_merge.py --out out/root-scaling.jsonl

# An individual matched pair, with a public override for the root protocol.
out/build/linux/frontend/rootmerge --method cube --m 102 --n 1048576 --block 128
out/build/linux/frontend/rootmerge --method batcher --m 102 --n 1048576

# Reuse the saved public choices on an isolated emulated WAN (requires root).
sudo python3 scripts/benchmark_root_merge.py --profiles wan --min-log 20 --max-log 20 \
  --parameters out/root-scaling.parameters.json --out out/root-wan-20.jsonl
```

The current correctness suite includes 243 real-crypto runs with random XOR shares,
duplicates, both disjoint orders, maximum unsigned keys, irregular lengths,
block overrides, and wide keys. It also checks invalid dimensions and insecure
correlation modes. It includes a full 8-bit-domain merge, plus carries at and
beyond the 11-bit count boundary. A separate plaintext oracle covers 40,512 root-protocol
cases, exhaustive unequal-Batcher binary inputs on the tested small sizes,
and 33,410 unary counts split into random XOR shares.
The measured large outputs are each checked against a stable plaintext merge.

See `benchmarks/root-merge-summary.md` for the measured comparison and
`benchmarks/root-merge-*.jsonl` for raw records. Timing ranges are observed
ranges over repetitions, not confidence intervals.
