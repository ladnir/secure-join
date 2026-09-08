# Concrete Pi-logstar optimization

The `optimize-pi-logstar` branch specializes one partition of the main protocol
for equal power-of-two input lengths. It preserves the stable XOR-shared gather
permutation interface, with terminal block sizes at most 16. The general recursive implementation remains available
for padded inputs, deeper schedules, and the `--packed 0` ablation.

## Block representation and comparisons

Let each list contain `n` keys and let the public power-of-two block size be `m`.
Every initial block contains consecutive original indices. Store one block ID,
one validity bit, and `m` keys, rather than repeating an index and validity bit
beside every key. The source list is the high bit of the block ID. Permuting and
broadcasting blocks preserves this representation.

Representatives are ordered by `(key, block ID)`. They contain no padding, so
no infinity bit is needed. The representative network carries its own block ID
as the permutation payload; a second 32-bit copy is unnecessary.

For an ordered block `B`, its copied predecessor `S`, and the next representative
`H`, compare only `(key, original source)` in the upper interval masks. The upper test retains a row whose tag is
less than or equal to `H`'s tag. An equal same-source tag is safe: the row belongs
to an earlier block of that sorted source. Different-source equal keys have
different tags. The initial copied block has validity zero. These facts give the
same active rows as full original-index comparisons with half-open intervals.

Each terminal merge compares all cross-pairs by `(key, original source)` in
parallel. Sorted inputs make these comparisons monotone along each row and column.
Adjacent XORs give one-hot insertion positions. Public offsets and source choices
are accumulated with XORs; only the validity bit needs a secret product per
candidate. Copied rows with zero predecessors from `B` are inactive, implementing
the lower interval mask without another key comparison. An absent copied block
contributes zero cross-comparison bits and follows every original row.
The output carries only the local offset, the choice of original/copied block,
and validity. One batched secret multiplexer then selects
the block ID for each output. Concatenating that ID and the offset reconstructs
the original index. No input-dependent addressing is used during these stages.

The broadcast reduces groups of eight leaves with Brent--Kung, applies a
Sklansky prefix to group endpoints, and distributes prefixes within groups.
For `L>8` blocks, this uses `log2(L)+3` AND layers, instead of `2*log2(L)-1`.
This concrete depth/work tradeoff increases the broadcast's gate count slightly.

For every active output in a block pair, its global rank is the sum of the two
within-list block-start indices and its local merged position. Rows retained
in the interval cannot have predecessors in a later opposite-source block.
The formula therefore counts every preceding input row exactly once.
When no opposite predecessor exists, its dummy start is zero and its comparison
bits are forced to zero. Two batched additions per block compute the high rank
bits for the two halves of the local output; low bits are public positions.
The additions run in parallel using `~(~a+~b)=a+b+1` modulo the rank word size.
This removes per-row prefix conversion and all extraction bit-injection OTs.

## Stable extraction by a joint shuffle

The extraction primitive receives `N` XOR-shared validity bits and payloads.
The number `K` of valid rows is public; here `N=4n` and `K=2n`. It returns the
valid payloads, still XOR-shared, in their original relative order.

Its general interface obtains ranks as follows; the packed protocol instead
supplies the direct ranks above and omits steps 1 and 2.

1. Convert each validity bit to an arithmetic sharing using real OT. Each party
   computes its share of the inclusive prefix count minus one locally.
2. Convert the counts to XOR sharings with a batched addition circuit modulo
   `2^ceil(log2(K))`. Every valid rank lies in `[0,K)`; inactive ranks are unused.
3. Apply a fresh jointly random composed permutation to records containing the
   validity bit, rank, and payload. Each party contributes a private permutation.
4. Open the shuffled validity bits. Open ranks only at shuffled valid positions.
   Place each surviving payload share at its opened rank locally.

The protocol never opens a payload or an inactive rank. All correlations are
single-use, and the shuffle uses independent private randomness.

The inherited `Perm::randomize` previously reduced 32-bit random words modulo
the remaining Fisher--Yates length. That introduces modulo bias and does not
satisfy the uniform-shuffle premise below. The implementation now uses
`std::shuffle` with the cryptographic PRNG's 64-bit UniformRandomBitGenerator
interface, which uses unbiased bounded sampling. This shared correction also
applies to the block-permutation masks. Measurements made before this correction
are development diagnostics, not evidence for the corrected security claim.

To justify the openings, work in the hybrid where the cryptographic PRNG supplies
uniform random words and the inherited OT, GMW, and permutation protocols are
their semi-honest ideal functionalities. For either corrupted
party, the honest party's private uniform permutation hides the original row
positions. For any input containing exactly `K` valid rows, the revealed result
is a uniform placement of the distinct labels `0,...,K-1` among `N` positions,
with all other positions labeled inactive. This distribution depends only on
`N,K`. A simulator samples that placement and simulates the shared payloads
consistently with the output shares. Composition and PRNG security then reduce
security to the inherited primitive assumptions. The implementation checks the public
cardinality and that the revealed ranks form a permutation of `[0,K)`.

Revealing inactive ranks would expose prefix-count information and invalidate
this argument. The implementation deliberately omits those ranks from both
parties' messages. A random shuffle followed by opening ranks at every position
would not be an equivalent protocol.

Compared with general radix compaction, this avoids ranking inactive rows,
the two-bin secret rank selection, and permutation derandomization. Online
non-GMW depth is bounded by nine one-way dependency steps: five for block
permutation derandomization/application, two for the
joint shuffle, and two for the openings. Add the reported GMW AND depth.

## Validation and measurements

`tests/packed_logstar_reference.py` independently checks 8,642 stable merges,
including exhaustive duplicate-key cases and random inputs. It also enumerates
all shuffle transcripts for every placement of two valid rows among four.
This exhaustive check illustrates the distribution argument; it does not
replace the argument or the inherited cryptographic assumptions.

The C++ self-test exercises random XOR shares, all six input patterns, block
sizes 2, 4, 8, and 16, and 1-, 32-, 64-, 127-, and 256-bit keys. It also retains
the general path's non-power-of-two and deeper-recursion cases. Separate real
cryptographic tests cover general stable extraction, including one active row,
all active rows, 32-bit payloads, and rejection of correlation reuse. The prefix
test covers five core-group settings, boundary sizes, and exhaustive controls.

`--plan` reports a public circuit schedule without preprocessing or execution.
Its records are labeled `public_schedule`; they are not measurements or verified
protocol runs. Benchmark records are emitted only after a real execution passes
the independent stable-gather check. Per-size tuning and measured comparisons
are recorded separately from these development notes.
