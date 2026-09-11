#pragma once
#include "PiLogStar.h"
#include "BatcherMerge.h"
#include "RadixSort.h"
#include "StableSecretExtract.h"
#include "secure-join/AggTree/BatchPrefix.h"

namespace secJoin
{
    // One partition of PiLogStar, with block IDs shared once per block.
    // Only called for unpadded power-of-two inputs and a terminal block merge.
    class PackedLogStar
    {
    public:
        void init(u64 n, u64 keyBits, u64 block, CorGenerator& cor, bool optimized = true);
        void preprocess();
        macoro::task<> prepare(coproto::Socket& sock, PRNG& prng);
        macoro::task<> merge(const BinMatrix& x, const BinMatrix& y,
            AdditivePerm& output, coproto::Socket& sock, PRNG& prng);
        // Scalar key comparisons in the initialized circuit schedule.
        u64 comparisons() const { return mComparisons; }
        std::vector<PiLogStarStage> stats;
    private:
        u64 n = 0, bits = 0, block = 0, blocks = 0, idBits = 0, offsetBits = 0;
        u64 blockBits = 0, orderBits = 0, role = 0;
        bool optimized = true;
        u64 mComparisons = 0;
        BatcherMerge medians;
        BatchPrefix prefix;
        Gmw mask, recover, tinyMerge, blockRanks;
        AltModComposedPerm blockGen;
        ComposedPerm blockPerm;
        StableSecretExtract compact;
    };
}
