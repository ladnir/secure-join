#pragma once
#include "PiLogStar.h"

namespace secJoin
{
    enum class RootMergeKind { CubeRoot, SquareRoot, Batcher };

    // Two-party semi-honest stable merge of m <= n unsigned XOR-shared keys.
    // Returns the XOR-shared gather permutation of X || Y; equal X keys precede
    // equal Y keys and each input's original order is preserved. Only tags and
    // ranks behind fresh joint shuffles are opened. Every instance is single-use.
    class RootMerge
    {
    public:
        explicit RootMerge(RootMergeKind kind);
        virtual ~RootMerge();
        RootMerge(RootMerge&&) noexcept;
        RootMerge& operator=(RootMerge&&) noexcept;
        RootMerge(const RootMerge&) = delete;
        RootMerge& operator=(const RootMerge&) = delete;
        void init(u64 m, u64 n, u64 keyBits, CorGenerator& cor, u64 blockSize = 0);
        void preprocess();
        macoro::task<> prepare(coproto::Socket& sock, PRNG& prng);
        macoro::task<> merge(const BinMatrix& x, const BinMatrix& y,
            AdditivePerm& output, coproto::Socket& sock, PRNG& prng);
        const std::vector<PiLogStarStage>& stages() const;
        u64 blockSize() const;
        u64 gmwRounds() const;
        u64 onlineRoundBound() const;
        u64 paddedAnds() const;
        u64 comparisons() const;
    private:
        struct Impl;
        std::unique_ptr<Impl> mImpl;
    };

    class CubeRootMerge : public RootMerge
    { public: CubeRootMerge() : RootMerge(RootMergeKind::CubeRoot) {} };
    class SquareRootMerge : public RootMerge
    { public: SquareRootMerge() : RootMerge(RootMergeKind::SquareRoot) {} };
    class BatcherUnequalMerge : public RootMerge
    { public: BatcherUnequalMerge() : RootMerge(RootMergeKind::Batcher) {} };
}
