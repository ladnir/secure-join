#pragma once

#include "secure-join/GMW/Gmw.h"
#include <memory>
#include <vector>

namespace secJoin
{
    // Work-efficient segmented prefix broadcast for independent equal-sized
    // batches of XOR-shared rows. For each batch, y[0] = values[0] and
    // y[i] = controls[i] ? y[i-1] : values[i]. No control or value is opened.
    // The public batch boundaries are never crossed by the scan.
    class BatchPrefix
    {
    public:
        BatchPrefix() = default;
        BatchPrefix(const BatchPrefix&) = delete;
        BatchPrefix& operator=(const BatchPrefix&) = delete;
        BatchPrefix(BatchPrefix&&) noexcept = default;
        BatchPrefix& operator=(BatchPrefix&&) noexcept = default;

        // Register all GMW correlations before starting cor. leaves must be a
        // positive power of two. Each initialized instance can execute once.
        // coreGroup=0 uses Brent--Kung. A positive power of two first reduces
        // groups of that size, scans their endpoints with Sklansky, then expands.
        void init(u64 batches, u64 leaves, u64 valueBits, CorGenerator& cor, u64 coreGroup = 0);
        void preprocess();

        // values has batches*leaves rows of valueBits bits; controls has the
        // same rows and one bit per row. Each first control is public zero
        // regardless of its supplied share. output may alias either input.
        macoro::task<> apply(const BinMatrix& values, const BinMatrix& controls,
            BinMatrix& output, coproto::Socket& sock);

        // Interactive AND layers, excluding correlation generation. For
        // default coreGroup and leaves > 1: 2*log2(leaves)-1, independently of batches.
        u64 numRounds() const { return mNumRounds; }

        // AND-equivalent gates including the GMW backend's 128-lane padding.
        // Binary OLE consumption is twice this value.
        u64 numAnds() const { return mNumAnds; }

    private:
        struct Stage
        {
            u64 stride = 0;
            u64 perBatch = 0;
            bool upward = true;
            bool flat = false;
            std::vector<std::pair<u64, u64>> nodes;
            std::unique_ptr<Gmw> gmw;
        };
        u64 mBatches = 0;
        u64 mLeaves = 0;
        u64 mValueBits = 0;
        u64 mRows = 0;
        u64 mNumRounds = 0;
        u64 mNumAnds = 0;
        bool mUsed = false;
        std::vector<Stage> mStages;

        static BetaCircuit combineCircuit(u64 valueBits, bool upward);
        static BetaCircuit scalarAndCircuit();
    };
}
