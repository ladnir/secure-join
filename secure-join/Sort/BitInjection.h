#pragma once
#include "macoro/macros.h"
#include "coproto/coproto.h"
#include "secure-join/Util/Matrix.h"
#include "secure-join/CorGenerator/CorGenerator.h"

namespace secJoin
{

    struct BitInject
    {

        // The correlated randomness used when the receiver.
        OtRecvRequest mRecvReq;

        // The correlated randomness used when the sender.
        OtSendRequest mSendReq;

        // True if preprocess has been called.
        bool mHasPreprocessing = false;

        // True if request has been called.
        bool mRequested = false;

        // The number of rows the input will have
        u64 mRowCount = 0;

        // The bit count of each input row.
        u64 mInBitCount = 0;

        // Should be zero or one. Controls the role of this party.
        u64 mRole = -1;

        // True if preprocess has been called.
        bool hasPreprocessing() { return mHasPreprocessing; }

        // True if request has been called.
        bool hasRequest() { return mRequested; }

        // n =  the number of rows the input will have.
        // inBitCount = the number of bit injections per row.
        void init(u64 n, u64 inBitCount)
        {
            mRowCount = n;
            mInBitCount = inBitCount;
        }

        // request the correlated randomness. Call preprocess to start the generation.
        void request(CorGenerator& gen);


        macoro::task<> preprocess();

        // convert each bit of the binary secret sharing `in`
        // to integer Z_{2^outBitCount} arithmetic sharings.
        // Each row of `in` should have `inBitCount` bits.
        // out will therefore have dimension `in.rows()` rows 
        // and `inBitCount` columns.
        macoro::task<> bitInjection(
            const oc::Matrix<u8>& in,
            u64 outBitCount,
            oc::Matrix<u32>& out,
            coproto::Socket& sock);
    };


} // namespace secJoin
