#pragma once

#include "secure-join/GMW/Gmw.h"
#include <memory>
#include <vector>

namespace secJoin
{
    // Append unsigned little-endian left < right to a circuit. The bundles
    // must have the same positive width. Returns one temporary output wire;
    // AND depth is logarithmic in the width. Does not levelize the circuit.
    u32 logstarLessThan(BetaCircuit& cir, const BetaBundle& left,
        const BetaBundle& right);

    // Merge batches of two ascending, equally sized runs of XOR-shared rows.
    // The first orderBits bits of a row are an unsigned little-endian key;
    // all rowBits bits, including the key, travel together. The schedule is
    // public and every comparator in one network layer is one SIMD GMW call.
    // Keys should include a tie-breaker when stable ordering is required:
    // individual comparisons do not swap equal keys, but the network is not
    // a stable merge because its initial reversal is public.
    class BatcherMerge
    {
    public:
        BatcherMerge() = default;
        BatcherMerge(const BatcherMerge&) = delete;
        BatcherMerge& operator=(const BatcherMerge&) = delete;
        BatcherMerge(BatcherMerge&&) noexcept = default;
        BatcherMerge& operator=(BatcherMerge&&) noexcept = default;

        // Register all correlations before starting cor. batches and halfSize
        // must be positive; halfSize must be a power of two. Each instance
        // executes once, or can be initialized again with fresh correlations.
        // An optional nonempty finalOutputBits selects original row-bit positions
        // to retain in the result; every other output position becomes public
        // zero. This saves final-layer swaps of fields the caller will discard.
        // The selection is public and must contain unique positions < rowBits.
        // Empty (the default) returns every row bit as usual.
        void init(u64 batches, u64 halfSize, u64 orderBits, u64 rowBits,
            CorGenerator& cor, const std::vector<u64>& finalOutputBits = {});

        // Start every layer's correlation request ahead of the online phase.
        void preprocess();

        // input has batches * 2 * halfSize rows, grouped as [run0 || run1].
        // output is resized and may alias input. No input or result is opened.
        macoro::task<> apply(const BinMatrix& input, BinMatrix& output,
            coproto::Socket& sock);

        // GMW levels containing nonlinear gates, summed over network layers;
        // excludes the final local XOR level, offline correlations and framing.
        u64 numRounds() const { return mNumRounds; }

        // Number of evaluated AND-equivalent gates including 128-lane SIMD
        // padding, over every layer. Binary OLE consumption is twice this.
        u64 numAnds() const { return mNumAnds; }

    private:
        u64 mBatches = 0;
        u64 mHalfSize = 0;
        u64 mRowBits = 0;
        u64 mRows = 0;
        u64 mNumRounds = 0;
        u64 mNumAnds = 0;
        bool mUsed = false;
        std::vector<std::unique_ptr<Gmw>> mStages;
        std::vector<u64> mFinalOutputBits;

        static BetaCircuit compareSwapCircuit(u64 orderBits, u64 rowBits,
            const std::vector<u64>& outputBits = {});
    };
}
