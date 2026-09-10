#pragma once
#include "PiLogStar.h"

namespace secJoin
{
    enum class PiMedianLeaf { AllPairs, Batcher, Bitonic };

    struct PiMedianOptions
    {
        u64 baseCase = 16;
        PiMedianLeaf leaf = PiMedianLeaf::Batcher;
        // Batcher uses odd-even merging; Bitonic retains the alternative
        // regular network for public planning and computational comparisons.
        // The paper's concrete evaluation uses two alignment levels and a
        // Batcher leaf. Zero removes this cap and uses only baseCase to stop.
        u64 maxDepth = 2;
        // Recursion preserves each original source's real-row order,
        // so (key, source) suffices. Full indices remain secret payloads.
        // Batcher appends public leaf-local positions to stabilize its network.
        bool fullIndexComparisons = false;
        // Per-level child run lengths. Zero/missing entries select
        // 2^floor(2*log2(n)/3); positive entries must divide n and be < n.
        std::vector<u64> childSizes;
        // Per-level block size for the asymmetric cube merge. Zero/missing
        // entries use the median count k. Independent of childSizes.
        std::vector<u64> cubeBlocks;
        // Public resource guard, checked before registering correlations.
        u64 maxExpandedRows = u64(1) << 28;
    };

    struct PiMedianLevel
    {
        u64 batches = 0, halfSize = 0, childSize = 0, medians = 0, cubeBlock = 0;
        u64 comparisons = 0, gmwRounds = 0, paddedAnds = 0, onlineRoundBound = 0;
        u64 shufflePayloadBytes = 0, openingPayloadBytes = 0;
    };

    // Paper's recursive Pi-median: two sorted equal-length unsigned XOR-
    // shared lists -> XOR-shared gather permutation of X || Y. Stable on
    // original input position. Supports 1..256 key bits and irregular n via
    // explicit infinity padding. All instances at a level execute together.
    // Semi-honest, real two-party correlations only; single-use instance.
    class PiMedian
    {
    public:
        PiMedian();
        ~PiMedian();
        PiMedian(PiMedian&&) noexcept;
        PiMedian& operator=(PiMedian&&) noexcept;
        PiMedian(const PiMedian&) = delete;
        PiMedian& operator=(const PiMedian&) = delete;
        void init(u64 n, u64 keyBits, CorGenerator& cor, const PiMedianOptions& options = {});
        void preprocess();
        macoro::task<> prepare(coproto::Socket& sock, PRNG& privatePrng);
        macoro::task<> merge(const BinMatrix& x, const BinMatrix& y,
            AdditivePerm& output, coproto::Socket& sock, PRNG& privatePrng);
        const std::vector<PiMedianLevel>& levels() const;
        const std::vector<PiLogStarStage>& stages() const;
        u64 paddedSize() const;
        u64 expandedSize() const;
        u64 comparisons() const;
        u64 comparisonBits() const;
        u64 leafComparisonBits() const;
        u64 gmwRounds() const;
        u64 paddedAnds() const;
        // Boolean AND dependency layers + one-way shuffle/OT/opening steps.
        // Excludes preprocessing, input sharing and output reconstruction.
        u64 onlineRoundBound() const;
        // Both parties' application payload; excludes transport framing.
        u64 onlinePayloadBytes() const;
    private:
        struct Impl;
        std::unique_ptr<Impl> m;
    };
}
