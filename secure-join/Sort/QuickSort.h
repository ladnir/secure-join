#pragma once

#include "secure-join/Perm/AdditivePerm.h"
#include "secure-join/CorGenerator/CorGenerator.h"
#include <memory>

namespace secJoin
{
    struct QuickSortOptions
    {
        // Compare all unordered pairs in terminal partitions, batched together
        // with the other partitions' pivot comparisons. 2 recovers ordinary
        // single-pivot quicksort. Public range: [2,32].
        u64 terminalSize = 8;
        // SIMD comparison lanes to reserve offline, including padding.
        // 0 chooses an O(n log n) reserve. Exhaustion triggers fresh REAL
        // preprocessing; it never aborts a valid sort or reuses an OLE.
        u64 reserveComparisons = 0;
        // 3 partitions into four buckets in a single comparison/opening batch,
        // including ordering the pivots. Trades bandwidth for lower depth.
        u64 pivotCount = 1;
    };

    struct QuickSortPlan
    {
        u64 rows = 0, keyBits = 0, indexBits = 0, comparisonBits = 0;
        u64 terminalSize = 0, reservedComparisonLanes = 0;
        u64 andsPerComparison = 0, roundsPerComparison = 0;
        u64 reservedBinaryOle = 0, shufflePayloadBytes = 0;
        u64 pivotCount = 1;
        // Expectation for the chosen terminal schedule on a random permutation.
        // Neither this nor 1.44*n*log2(n) is a depth bound.
        // -1 when no exact expectation is calculated (three pivots, n>t).
        double expectedComparisons = 0;
        double leadingComparisonEstimate = 0;
    };

    struct QuickSortStats
    {
        u64 comparisons = 0, comparisonBatches = 0, paddedAnds = 0;
        u64 comparisonRounds = 0, openingRounds = 0, shuffleRounds = 0;
        u64 openingPayloadBytes = 0, onlinePayloadBytes = 0;
        u64 refills = 0, refillSentBytes = 0, refillReceivedBytes = 0;
        u64 extraReservedBinaryOle = 0;
        double refillMilliseconds = 0;
        // Complete online depth only when refills == 0; otherwise excludes
        // correlation generation on refill. Transport framing is excluded.
        u64 onlineRoundsWithoutRefills() const
        { return comparisonRounds + openingRounds + shuffleRounds; }
    };

    // Two-party semi-honest shuffle-then-quicksort on unsigned little-endian
    // XOR shares. Appends unique original positions BEFORE the hidden joint
    // shuffle, mandatory for duplicate privacy. Returns an XOR-shared gather
    // permutation: sorted[i] = input[pi[i]]. Only comparisons of distinct
    // shuffled records are opened; original positions and keys stay shared.
    class QuickSort
    {
    public:
        QuickSort();
        ~QuickSort();
        QuickSort(const QuickSort&) = delete;
        QuickSort& operator=(const QuickSort&) = delete;
        QuickSort(QuickSort&&) noexcept;
        QuickSort& operator=(QuickSort&&) noexcept;

        // Pure public calculation: no crypto requests or execution.
        static QuickSortPlan plan(u64 rows, u64 keyBits,
            const QuickSortOptions& options = {});
        // Register in an initialized REAL generator. Both parties must use
        // identical public parameters. Each initialization executes once.
        void init(u64 rows, u64 keyBits, CorGenerator& cor,
            const QuickSortOptions& options = {});
        void preprocess();
        // Run concurrently with cor.start(); await both before sort().
        // prng must contain private, cryptographically seeded randomness.
        macoro::task<> prepare(coproto::Socket& sock, PRNG& prng);
        macoro::task<> sort(const BinMatrix& keys, AdditivePerm& output,
            coproto::Socket& sock);
        // Sort X||Y. Presorted runs are accepted but not required. Ties retain
        // concatenation order; rows passed to init must equal |X|+|Y|.
        macoro::task<> merge(const BinMatrix& x, const BinMatrix& y,
            AdditivePerm& output, coproto::Socket& sock);

        const QuickSortPlan& plan() const;
        const QuickSortStats& stats() const;
    private:
        struct Impl;
        std::unique_ptr<Impl> m;
    };
}
