# Shuffled quicksort

`Sort/QuickSort.{h,cpp}` implements the manuscript's shuffle-then-sort baseline
with real two-party semi-honest cryptography. It returns the same XOR-shared
**gather permutation** as the merge implementations, including stable ties.
Formal paper benchmarks have not been run.

The construction follows Protocol 1 and Sections 3.2–3.3 of
[Hamada et al., *Practically Efficient Multi-party Sorting Protocols from
Comparison Sort Algorithms*](https://www.acsu.buffalo.edu/~mblanton/cse715/sorting-2.pdf).
The initial shuffle uses the repository's alternating-moduli permutation
correlations from [Peceny et al., 2024/547](https://eprint.iacr.org/2024/547).
These references are available online; no additional upload is required.

## Interface and ordering

`QuickSort::init(N, keyBits, cor, options)` registers correlations in an
initialized real `CorGenerator`. Call `preprocess()`, then run `cor.start()`
concurrently with `prepare(socket, privatePrng)` and await both. Call `sort()`
once with an `N`-row `BinMatrix`. Reinitialize with fresh correlations before
reuse. Mock/debug generators and invalid lifecycle transitions are rejected.

Keys are unsigned little-endian bit strings, with 1–4096 bits. The implementation
supports empty inputs, singleton inputs, and arbitrary list lengths below the
repository permutation limit (`2^32-1`). Unused high bits in the input byte are
ignored. `merge(X,Y,...)` accepts two runs whose total length is `N`; it sorts
their concatenation and does not exploit prior ordering. On equal keys it
preserves original order, with X before Y. The returned `AdditivePerm` satisfies
`sorted[i] = input[pi[i]]` after reconstructing its XOR shares.

Before shuffling, each record is extended to `(key, originalPosition)`, with
the position in the least significant `ceil(log2(N))` bits. This makes every
comparison value distinct, including for all-equal keys. The indices remain
secret after shuffling and are returned as shares; they are never opened.
The library sorts indices rather than wide payloads, allowing its output to be
used with the repository's existing permutation application facilities.

## Execution and optimizations

1. Jointly shuffle the extended records with independent private permutations
   and fresh masks. The repository's `Perm::randomize` uses `std::shuffle`
   with the cryptographic PRNG's unbiased uniform generator interface.
2. Keep the shuffled records stationary. Maintain an array of public handles
   into **shuffled** positions and public active partition ranges.
3. Batch all comparisons in a wave into one SIMD GMW evaluation. Open one
   packed result bit per actual comparison. Locally partition the handles.
4. Finish small partitions with all unordered pairs, in the same batch as
   the larger partitions' pivot comparisons. Determine their ranks locally.
5. Gather the secret original-position fields in sorted order.

Single-pivot mode (`pivotCount=1`, default) selects the last shuffled handle in
each partition. Stable partitioning preserves the random relative order in
each child. `terminalSize=8` saves small-subtree dependency levels for a linear
comparison overhead; `terminalSize=2` selects ordinary quicksort throughout.
Values up to 32 are supported, with explicit quadratic terminal work.

Three-pivot mode (`pivotCount=3`) orders the three sampled pivots and compares
each other row to all three **in the same GMW batch**. The opened results place
rows into four buckets. There is no separate secure pivot-selection phase.
Partitions too small to use three pivots use fewer. This reduces partition
depth at increased bandwidth; the default retains single-pivot communication.
No measured claim of globally optimal parameters is made.

The comparison circuit uses equality of high segments to select the ordering
of high or low segments. Since full values are guaranteed distinct, the low
segment's output can be arbitrary on equality. Omitting its strict-equality
correction gives fewer than `2*w` ANDs and exactly `ceil(log2(w))` nonlinear
layers, where `w = keyBits + ceil(log2(N))`. Padded SIMD lanes need not be
distinct: their outputs are never opened or used.

Input matrices are passed through GMW's `MatrixView` interface to avoid the
copying `BinMatrix` convenience overload. Row-major scratch is released after
transposition and before circuit evaluation. Only public 32-bit handles move
between waves; keys and arbitrary payloads are not repeatedly rearranged.

## Preprocessing and accounting

Adaptive partition sizes cannot be known before the shuffle. A single
`O(N log N)` reserve of binary OLEs is requested offline, then divided into
disjoint requests for the actual SIMD batch sizes. This avoids preprocessing
at every partition wave and avoids evaluating a full `N` lanes at every depth.
The small new GMW overload accepts such a preallocated request.

The pool consumes pairs of 128-bit OLE blocks. An odd final block in a backend
segment is discarded, never reused; this handles preceding unrelated requests
that end at a 128-bit boundary. The default reserve is a practical heuristic,
not a claimed tail bound. `reserveComparisons` overrides its padded lane count.
If a public random-order transcript exhausts it, the protocol generates fresh
real correlations. It neither fails on valid inputs nor substitutes mock
cryptography. Refill communication and time are counted in online execution
and reported separately. Used and unused reserve material is released after
the sort. Correlation batch size must be a multiple of 256.

For wave comparison counts `c_j`, comparator AND count `A(w)`, and depth `d(w)`:

```
padded ANDs = A(w) * sum_j 128*ceil(c_j/128)
online dependent steps without refills = 2 + sum_j (d(w) + 1)
application bytes, both parties, without refills =
    2*N*ceil(w/8) + padded_ANDs/2 + 2*sum_j ceil(c_j/8)
```

The first two steps and first byte term are the composed shuffle. Each wave
adds one packed opening round after comparison. Empty/singleton inputs have
zero online communication. Application payload excludes transport framing,
initial preprocessing, and output reconstruction. The runner reports actual
sent bytes and offline/online time separately. When `refills>0`, the round
formula excludes the refill protocol's dependency depth and **must not be
reported as complete online rounds**. Actual online bytes/time include it.

The public `plan()` function constructs only a small comparison circuit and
calculates resource counts; it does not register correlations, generate
cryptography, or choose a private permutation. The single-pivot comparison
expectation follows the recurrence

```
E(n) = n*(n-1)/2                         for n <= terminalSize
E(n) = n-1 + (2/n)*sum_{k=0}^{n-1} E(k)  otherwise.
```

The planner evaluates its closed form using harmonic numbers. The leading
term is `2*N*ln(N) = 1.38629...*N*log2(N)`, consistent with the manuscript's
approximate 1.44 comparison factor. Three-pivot mode's leading term is
`(36/13)*N*ln(N)`: its four random spacings have expected entropy
`H_4-1 = 13/12`, and each nonpivot participates in three comparisons. The
planner marks its nonterminal exact expectation unavailable (`-1`) and
reports the leading estimate separately. These leading terms are not bounds.

Comparison **work** constants do not give the maximum recursion **depth**.
The manuscript's 1.44 depth estimate must not be read as an implementation
guarantee. Actual dependent waves are measured, and rounds also include the
index suffix, packed openings, and shuffle. Worst-case quicksort remains
quadratic; the random shuffle gives expected `O(N log N)` comparisons and
`O(log N)` waves. Reserve exhaustion has a correct fallback rather than an
assumed high-probability cutoff. No formal benchmark numbers are extrapolated
from small development tests.

## Validation and commands

In the repository's configured Linux/WSL environment:

```sh
cmake -S . -B out/build/linux
cmake --build out/build/linux --target shuffledquicksort -j4
ctest --test-dir out/build/linux -R shuffled_quicksort_real_crypto --output-on-failure
python3 tests/shuffled_quicksort_reference.py
python3 scripts/check_shuffled_quicksort.py

# Arithmetic/circuit planning only, including for paper-sized inputs.
out/build/linux/frontend/shuffledquicksort --n 2097152 --bits 128 --plan

# Small synthetic development execution, with stable output verification.
out/build/linux/frontend/shuffledquicksort --n 257 --bits 128 --pattern duplicates
out/build/linux/frontend/shuffledquicksort --n 257 --left 65 --pivots 3
```

Unlike the symmetric merge runners, `--n` here means **total rows**. To compare
against a merge of two size-`n` runs later, pass `--n 2n --left n`.
`--party 0|1 --address HOST:PORT` supports two-process TCP development tests;
both parties exchange every public protocol/data-generation parameter before
preprocessing, rejecting mismatches. Public synthetic data seeds are never
used for shuffle masks, private permutations, or correlation randomness.

The real-crypto self-test verifies random XOR shares, stable duplicates,
maximum values, odd widths, high-bit comparisons up to 256 bits, terminal
boundaries, both pivot modes, unequal/empty merge runs, lifecycle failures,
forced refills, and misaligned preceding OLE requests. An independent plaintext
oracle checks stable output and exhaustively compares whole transcript
distributions across input multisets through seven rows. These finite checks
supplement the shuffle-then-sort security argument; they are not a new formal
proof or a malicious-security claim. No input-dependent property besides the
randomized total-order comparison trace is intentionally opened.

Validation at this implementation checkpoint: **all four CTest suites passed**,
including 111 new real-crypto quicksort cases, 212 Pi-median cases, and 255
asymmetric-merge cases. The independent oracle passed **142,536 plaintext
cases**, including exhaustive transcript distributions for both pivot modes.
The bounded checker passed six large public-only plans, three small local
executions, three TCP executions (including refills and three pivots), and
configuration mismatch rejection. No new-source compiler warnings remained;
the build still emits pre-existing dependency deprecation warnings.

Two additional single development invocations sorted 1,024 random 128-bit keys
with terminal size 8 and correlation batch size 65,536. Each used its own fresh
private shuffle; these are noisy observations, not controlled benchmarks:

| Pivots | Comparisons | Dependent online steps | Application bytes, both parties | Online ms | Offline ms |
|---:|---:|---:|---:|---:|---:|
| 1 | 12,006 | 164 | 1,748,684 | 2.018 | 483.541 |
| 3 | 15,747 | 83 | 2,210,986 | 2.135 | 593.705 |

Both outputs were verified and neither required a refill. The single-pivot
public expectation at this size is 11,921.759 comparisons. The three-pivot run
illustrates the intended latency/bandwidth tradeoff, with roughly half the
dependent steps and 26% more application bytes in these two executions.
These tests do not establish WAN speedups or paper-scale runtime/memory usage.
