#pragma once
#include "macoro/macros.h"
#include "coproto/coproto.h"
#include "secure-join/Util/Matrix.h"
#include "secure-join/OleGenerator.h"

namespace secJoin
{

    struct BitInject
    {

        Request<OtRecv> mRecvReq;
        Request<OtSend> mSendReq;
        bool mHasPreprocessing = false;

        u64 mRole = -1;

        bool hasPreprocessing() { return mHasPreprocessing; }


        macoro::task<> preprocess(
            u64 n,
            u64 inBitCount,
            OleGenerator& gen,
            coproto::Socket& sock
        );

        // convert each bit of the binary secret sharing `in`
        // to integer Z_{2^outBitCount} arithmetic sharings.
        // Each row of `in` should have `inBitCount` bits.
        // out will therefore have dimension `in.rows()` rows 
        // and `inBitCount` columns.
        macoro::task<> bitInjection(
            u64 inBitCount,
            const oc::Matrix<u8>& in,
            u64 outBitCount,
            oc::Matrix<u32>& out,
            OleGenerator& gen,
            coproto::Socket& sock);



        // convert each bit of the binary secret sharing `in`
        // to integer Z_{2^outBitCount} arithmetic sharings.
        // Each row of `in` should have `inBitCount` bits.
        // out will therefore have dimension `in.rows()` rows 
        // and `inBitCount` columns.
        macoro::task<> bitInjection(
            u64 inBitCount,
            const oc::Matrix<u8>& in,
            u64 outBitCount,
            oc::Matrix<u32>& out,
            coproto::Socket& sock);
    };

    // convert each bit of the binary secret sharing `in`
    // to integer Z_{2^outBitCount} arithmetic sharings.
    // Each row of `in` should have `inBitCount` bits.
    // out will therefore have dimension `in.rows()` rows 
    // and `inBitCount` columns.
    //macoro::task<> bitInjection(
    //    u64 inBitCount,
    //    const oc::Matrix<u8>& in,
    //    u64 outBitCount,
    //    oc::Matrix<u32>& out,
    //    OleGenerator& gen,
    //    coproto::Socket& sock);

} // namespace secJoin
