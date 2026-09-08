# Security status of the Pi-logstar implementation

The `PiLogStar` library implements the complete merge functionality using real
two-party protocols. No missing component is replaced by a trusted dealer,
plaintext computation on reconstructed secrets, mocked preprocessing, or an
ideal-functionality stub in the active execution path.

| Component | Active implementation |
|---|---|
| Comparisons, conditional swaps, and interval masks | Boolean GMW with real binary OLE correlations |
| Batched segmented broadcast | GMW implementation of a Brent–Kung prefix scan |
| Block permutations | `AltModComposedPerm` and the repository's alternating-moduli PRF protocols |
| Stable extraction | One-bit `RadixSort`, real bit injection/OTs, and a correlated permutation |
| Preprocessing | `CorGenerator` with `mock=false`; real base OT and OT/OLE extension |
| Private randomness | Independent `oc::sysRandomSeed()` seeds for protocol PRNGs |
| Reuse protection | Single-use protocol state and correlations; lifecycle misuse is rejected |

`PiLogStar::init` rejects an uninitialized, mocked, or debug correlation generator.
The private radix component's `mInsecureMock` and `mDebug` flags remain false.
The permutation and GMW debugging paths are likewise disabled. The inherited
repository contains mock/test helpers, but their presence does not mean they are
used by this protocol.

The security claim is for the library's stated functionality and the inherited
semi-honest assumptions of its cryptographic components. The inputs are already
sorted XOR-shared unsigned lists with matching public configuration; the result
is an XOR-shared gather permutation. Secret key bits, masks, source tags, and the
output permutation are not opened inside this API. This is not an independent
security audit or a claim of malicious security, implementation side-channel
resistance, or bug-free dependencies.

## The executable is an evaluation harness

`frontend/logstar.cpp` generates public synthetic data and intentionally opens
the output permutation **after** the measured phases to check correctness.
Consequently, running this executable as-is is not a private-data application.
Use `PiLogStar::prepare` and `PiLogStar::merge` with application input shares and
retain the returned shares when privacy of the permutation is required.
There is currently no private-file ingestion frontend; this is an application
interface limitation, not a missing cryptographic subprotocol.

The public benchmark seed only selects synthetic data. It is not used as the
protocol's cryptographic randomness. Correctness tests also exercise random XOR
input shares, rather than only the benchmark's owner/zero sharing convention.

The main unpruned construction is implemented. The optional pruning extension
changes complexity and performance; its absence does not turn the implemented
main construction into an insecure or mocked execution.

See [the API and assumptions](pi-logstar.md) and the active source paths:
[PiLogStar](../secure-join/Sort/PiLogStar.cpp),
[benchmark frontend](../frontend/logstar.cpp), and
[correlation generator](../secure-join/CorGenerator/CorGenerator.cpp).
