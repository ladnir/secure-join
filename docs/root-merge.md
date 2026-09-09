# CubeRootMerge and SquareRootMerge

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

* **CubeRootMerge** compares every X key with all selected real Y keys. Dummy
  comparisons are gated off. It keeps dummy holes in the selected array instead
  of compacting them. If `R_i` counts extracted real Y keys below `X_i`, `z_i`
  is its block index, and `g_i` is the number of selected distinct blocks up to
  that X key, its final scatter rank is `i + R_i + (z_i - g_i + 1) * b`.
  The two passes perform `m*(K-1) + m*m*b` key comparisons.
* **SquareRootMerge** uses a secret segmented prefix broadcast to replace
  dummy entries by copies of the preceding real block. Each X key is compared
  with only its own copied block. Its scatter rank is `i + z_i*b + offset_i`.
  A segmented suffix sum combines per-key Y insertion counts within each
  group of copied blocks. The two passes perform `m*(K-1) + m*b` comparisons.

Y ranks are recovered by unshuffling **count corrections**, which are narrower
than the 32-bit key blocks in this evaluation. A block's coarse count is the number of X keys
at or below its maximum (the last block uses `m`). In a selected real block,
the correction is its fine count minus this coarse count, modulo
`2^ceil(log2(m+1))`. Unselected blocks receive zero corrections. Adding the
restored count to each original Y index gives its final scatter rank. The
forced last-block count cancels for a selected last block. If the last block
is unselected, all X keys precede it and its count is indeed `m`.

Finally, a separate fresh joint shuffle opens the scatter ranks and places
the still-shared original indices at those positions. A correct stable merge
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

Boolean addition uses parallel-prefix carry circuits. Popcounts use balanced
trees with growing word widths. SquareRootMerge's suffix scan uses a Brent–Kung
tree, flattened across `(edge, count word)` to avoid padding a whole wide block
to 128 lanes at the top of the tree. The inherited prefix broadcast uses its
eight-leaf grouped schedule. Small lane sets still incur GMW's 128-lane padding.
The code drops consumed GMW state and large temporary matrices between stages.

The reported round bound sums executed GMW AND layers plus nine one-way steps
for forward block shuffling, opening tags, inverse block routing, and final
shuffled extraction. Word arithmetic is implemented by Boolean circuits;
these concrete counts are not the paper's idealized primitive-round counts.
The current implementation executes those stages sequentially. In particular,
SquareRootMerge's Boolean suffix additions can make its depth greater than
Batcher's even when it communicates fewer bytes.

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

The scaling study includes every long-list size `2^10` through `2^20`, with
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

The correctness suite includes 198 real-crypto runs with random XOR shares,
duplicates, both disjoint orders, maximum unsigned keys, irregular lengths,
block overrides, and wide keys. It also checks invalid dimensions and insecure
correlation modes. A separate plaintext oracle passes 13,504 root-protocol
cases and exhaustive unequal-Batcher binary inputs on the tested small sizes.
The measured large outputs are each checked against a stable plaintext merge.

See `benchmarks/root-merge-summary.md` for the measured comparison and
`benchmarks/root-merge-*.jsonl` for raw records. Timing ranges are observed
ranges over repetitions, not confidence intervals.
