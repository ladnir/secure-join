# Pi-logstar implementation

`secJoin::PiLogStar` merges two equal-length sorted lists of XOR-shared unsigned
keys and returns an XOR-shared **gather permutation**:

```
sorted[i] = (X || Y)[permutation[i]]
```

The protocol never reconstructs the keys, source tags, or output permutation.
The optimized path opens only jointly shuffled validity flags and active ranks,
whose distribution is independent of the input given the public dimensions.
The benchmark reconstructs its synthetic result **after** the measured protocol
to check it against an independent stable merge. The library supports 1–256-bit
keys; all meaningful key bits are little-endian within each `BinMatrix` row.
The output uses the existing `AdditivePerm` type, which despite its name contains
XOR shares of `u32` indices, not arithmetic shares.

See [optimized measurements](packed-results.md) for the complete size sweep,
parameter selection, Batcher comparisons, and emulated LAN/WAN experiments.
The [earlier measurements](logstar-results.md) remain archived separately.

## Optimized concrete path

The current default also enables the size-independent improvements documented
in [the development audit](logstar-optimization-audit.md): comparison reuse,
omitted predecessor minima, local rank construction, and common comparator and
network improvements for both Pi-logstar and Batcher. `--optimized 0` restores
the archived circuit schedules. The existing paper measurements below describe
that earlier implementation; they have not been regenerated for this follow-up.

For unpadded power-of-two inputs with one partition and block size at most 16,
the default `packed` option uses compressed block IDs, a hybrid prefix, batched
all-pairs base merges, direct global ranks, and stable shuffled extraction.
See [the complete optimization and security argument](packed-logstar.md).
Set `--packed 0` to run the general construction below. `--plan` prints public
circuit counts without claiming an executed or verified benchmark.
The per-size evaluator considers blocks 2, 4, 8, and 16 independently; the
32-bit online-byte objective selects block 4 throughout 2^10--2^20.
Run that measured configuration with `--base 4 --block 4 --batch-size 1048576`.
The library's automatic block heuristic remains the general construction's
near-logarithmic choice; `packed=true` enables the specialization when its
public schedule meets the conditions above. Use the explicit selected parameters
to reproduce the optimized evaluation.

## General construction

This follows `constructionone.tex` in the accompanying paper: block merge,
transition-block broadcast, interval masking, parallel recursion, and final
in-order extraction. The practical construction expands the physical array by
two at each partition level. It does **not** implement the further subproblem
pruning described in `logstarextension.tex`. Thus its formal work bound is the
paper's unpruned `O(n * 2^(log* n))` word operations, rather than the extension's
`O(n log* n)`. With the default cutoff there are only one or two partition levels
at practical sizes. The benchmark exposes the actual expanded size.

1. Pad each input to a public power of two with an explicit infinity bit. Real
   maximum-valued keys remain distinct from padding. Order by `(infinity, key,
   original_index)` to make ties stable, including equal keys across both inputs.
2. Choose a power-of-two block size near `ceil(log2(n))`. Merge the two sorted
   lists of block minima with a Batcher bitonic **merge** network. The network
   carries block indices, not block contents. Apply its shared gather permutation
   to whole blocks with the repository's correlated permutation protocol.
3. Reset a source bit to identify the two **current** recursive inputs. A
   work-efficient Brent–Kung segmented prefix scan copies the predecessor at a
   source transition to all blocks in the next streak. The scan operates on
   pre-mask records. All subproblems at one recursion depth share each GMW layer.
   Small batches flatten independent bit products into packed SIMD lanes, avoiding
   a separate 128-lane padding penalty for every payload bit. Full batches retain
   the ordinary row-wise evaluator to avoid unnecessary packing work.
4. Keep original rows below the next block minimum. Keep copied strays in the
   half-open interval `[current_minimum, next_minimum)`. The final block has no
   upper restriction. Only the secret `isReal` flag changes.
5. Recurse on all block/carry pairs in one batch. At the public cutoff, use a
   batched Batcher merge. Concatenate and compact with a **stable one-bit radix
   partition**, then apply its inverse/scatter ranks to the indices. Return the
   first public `2*n` indices. If there is no partition level, public infinity
   padding is already terminal and compaction is unnecessary.

### Physical-key invariant and paper edge cases

Every recursive input stays sorted in its physical keys: partitioning takes
contiguous blocks, broadcasting copies a sorted block, and masking never changes
keys. Therefore the first physical row is a valid block minimum even in an
all-dummy block. Representatives can be selected locally; the paper's interactive
first-real `Med` is unnecessary in this variant. In particular, an all-dummy block
does not acquire a synthetic zero minimum that could break the sorted-run
precondition of a merge network.

For any output block interval, only that block and the last preceding block of
the opposite source can contribute real rows. The prefix scan supplies precisely
that predecessor. The half-open masks assign each real row to one interval, so
the recursive real subsequence contains each input exactly once in sorted order.
`S_0` is a dummy copy of `B_0`, avoiding an additional negative-infinity encoding.

The paper's stray-mask pseudocode uses an inclusive upper endpoint in one place;
the implementation uses a strict upper endpoint, consistent with the interval
description, to avoid copying a boundary row twice. The Python reference tests
these invariants at every recursive return, including reversed tie orders between
equal block minima.

## Security and interface requirements

The target is two-party semi-honest security under the assumptions of the
repository's GMW, OT/OLE, and alternating-moduli permutation-correlation protocols.
The implementation uses those real protocols and rejects a mock or debug
correlation generator. It does not supply a malicious-security upgrade or a new
proof of the underlying cryptographic primitives.

Public parameters are the two equal lengths, key width, padding, recursion
schedule, cutoff, and block-size override. Inputs must already be sorted.
All addressing, control flow, message lengths, and batch sizes before extraction
depend on public parameters. Packed extraction also uses its input-independent
opened shuffled flags and ranks. Local comparisons of secret share values are
never used to choose branches.
Every invocation needs fresh private randomness and fresh, single-use
correlations. The public benchmark seed selects only the synthetic dataset;
protocol PRNGs must use independent operating-system entropy.

Initialize **every** stage before `CorGenerator::start()`, which consumes the
generator's request state. For one party of an established socket pair:

```cpp
PRNG prng(oc::sysRandomSeed());
CorGenerator cor;
cor.init(sock.fork(), prng, role, 2, 1 << 18, false);
PiLogStar protocol;
protocol.init(n, keyBits, cor, PiLogStarOptions{4, 4});
protocol.preprocess();
auto ready = co_await macoro::when_all_ready(
    cor.start(), protocol.prepare(sock, prng));
std::get<0>(ready).result();
std::get<1>(ready).result();
AdditivePerm gather;
co_await protocol.merge(xShare, yShare, gather, sock, prng);
```

The two parties must agree on public configuration before entering the protocol.
The API checks dimensions but does not open data to check sortedness. Instantiate
a new protocol for a second run. The base merge and prefix helpers also reject
correlation reuse. Applications retain output shares; only testing code should
open them.

## Communication and rounds

In the general construction, the median merge has `log2(2*n/m)` comparator layers; all same-depth subproblems
share each layer. The broadcast has at most `2*log2(2*n/m)-1` AND layers, regardless
of the number of subproblems. The base merge has `log2(2*m)` comparator layers.
Comparisons use a balanced tree with logarithmic AND depth in the encoded key
width, rather than a ripple comparator. Conditional swaps use one AND per row
bit and local XORs for both outputs. The final median and base merge layers
compute only the payload bits the caller needs, skipping swaps of discarded keys.
The packed path replaces the terminal network with parallel all-pairs comparisons
and uses the hybrid broadcast depth described in [the optimization notes](packed-logstar.md).

The paper treats comparisons and fixed-word arithmetic as primitives. Actual
GMW rounds include their bit-level depth, and the actual byte counts include
the tie index and infinity/dummy metadata. Consequently the implementation should
not be described as attaining the paper's exact constant factors or treating a
comparison as one round.

Reported measurements distinguish:

- **Offline:** real OT/OLE generation and permutation correlations, before inputs
  enter the merge. It includes base cryptography and is not free or mocked.
- **Online:** the merge alone, with per-stage bytes and time. Counts cover both
  directions using the socket's counters, without double-counting the peer.
  They do not include Ethernet/IP/TCP headers.
  Phase totals are recorded after flushing buffered sends. Per-stage attribution
  is approximate because sends may finish during the next stage.
- **GMW rounds:** interactive AND layers, excluding local XOR-only levels.
- **Online round bound:** The packed path adds nine one-way steps to its GMW
  depth (block permutation and shuffled extraction). The general path adds
  five one-way dependency steps per
  derandomized permutation and four for stable radix bit/rank conversion. This
  is a conservative protocol-depth accounting, not a count of TCP packets or
  socket `send()` calls. Preprocessing and verification are excluded.
- **Padded ANDs:** actual SIMD gate count including rounding each GMW batch to
  128 lanes. Binary OLE consumption is twice this count; permutation and OT
  conversion costs are measured separately in bytes.

## Build and run

Linux with C++20, CMake, Python, Git, and the usual autotools prerequisites:

```sh
cmake -S . -B out/build/linux -DCMAKE_BUILD_TYPE=Release \
  -DFETCH_AUTO=ON -DSECUREJOIN_ENABLE_BOOST=ON -DSUDO_FETCH=OFF
cmake --build out/build/linux --target logstar secJoinfrontend -j4
out/build/linux/frontend/logstar --self-test
ctest --test-dir out/build/linux -R pi_logstar_real_crypto --output-on-failure
out/build/linux/frontend/secJoinfrontend -u BatchPrefix_Test
out/build/linux/frontend/secJoinfrontend -u StableSecretExtract_Test
out/build/linux/frontend/secJoinfrontend -u plaintext_perm_test ComposedPerm_apply_test ComposedPerm_compose_test AltModPerm_setup_test AltModComposedPerm_setup_test
python3 tests/logstar_reference.py
python3 tests/packed_logstar_reference.py
out/build/linux/frontend/logstar --n 4096 --bits 32 --base 4 --block 4 --batch-size 1048576
```

Use `--base` equal to or larger than the padded list size for a pure Batcher
merge baseline. `--block` overrides only the outermost partition block size and
must be a power of two smaller than the padded input length. `--block 0` chooses
automatically. Compare cutoffs, blocks, and correlation batches using:

```sh
python3 scripts/benchmark_logstar.py --sizes 1024,4096 --bases 8,16,4096 \
  --blocks 0 --trials 3 --output out/logstar/local.jsonl
```

The harness also runs separate TCP processes in two fresh Linux network
namespaces connected by a veth pair. Both egress directions receive half the
requested RTT, with a rate limit in each direction. Existing host interfaces,
routes, and qdiscs are left alone; only resources created by this run are cleaned
up. Running tc profiles needs Linux root:

```sh
sudo python3 scripts/benchmark_logstar.py --sizes 4096 --bases 8,16,4096 \
  --profiles tcp,lan,metro,wan,slow --trials 3 --output out/logstar/network.jsonl
```

Profiles are unshaped TCP; LAN at 1 Gbit/s and 0.2 ms RTT; metro at 100 Mbit/s and
10 ms; WAN at 100 Mbit/s and 40 ms; and slow WAN at 10 Mbit/s and 40 ms. They are
emulated links on one machine, not measurements between geographically separate
hosts. Results depend on CPU, scheduling, transport buffering, and the backend's
preprocessing batch size. Preserve raw JSONL and report online and offline costs
separately.
