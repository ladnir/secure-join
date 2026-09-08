#pragma once
#include "BitInjection.h"
#include "secure-join/GMW/Gmw.h"
#include "secure-join/Perm/AltModComposedPerm.h"

namespace secJoin
{
    // Stable extraction with a PUBLIC number of active rows. Compute secret
    // ranks, jointly shuffle, open shuffled flags, then open ONLY active ranks.
    // The openings are a uniform placement of ranks [0, active), independent
    // of the input. Inactive ranks and all payloads remain secret shared.
    class StableSecretExtract
    {
    public:
        void init(u64 rows, u64 active, u64 payloadBits, CorGenerator& cor, bool suppliedRanks = false);
        void preprocess();
        macoro::task<> prepare(coproto::Socket& sock, PRNG& prng);
        macoro::task<> apply(const BinMatrix& activeFlags, const oc::Matrix<u32>& payload,
            AdditivePerm& output, coproto::Socket& sock);
        // Active ranks must be a secret-shared permutation of [0, active).
        macoro::task<> applyRanked(const BinMatrix& activeFlags, const BinMatrix& binaryRanks,
            const oc::Matrix<u32>& payload, AdditivePerm& output, coproto::Socket& sock);
        Gmw ranks;
    private:
        u64 rows = 0, active = 0, rankBits = 0, payloadBits = 0, role = 0;
        bool initialized = false, preprocessed = false, prepareStarted = false, prepared = false, used = false;
        bool suppliedRanks = false;
        BitInject inject;
        AltModComposedPerm gen;
        ComposedPerm shuffle;
        macoro::task<> finish(const BinMatrix& flags, const BinMatrix& binaryRanks,
            const oc::Matrix<u32>& payload, AdditivePerm& output, coproto::Socket& sock);
    };
}
