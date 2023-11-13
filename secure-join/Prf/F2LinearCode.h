#pragma once

#include "secure-join/Defines.h"
#include "cryptoTools/Common/Matrix.h"
#include "cryptoTools/Crypto/PRNG.h"
#include <string>
#include <vector>

namespace secJoin
{
    // represents a binary matrix G. The encode function on input x
    // returns G * x. This is done by preprocessing subcodes and 
    // implementing G * x as a lookup table for each subcode.
    class F2LinearCode
    {
    public:
        // the maximum number of subcodes / input bytes that is 
        // currently supported. Can be increases if needed.
        static constexpr u16 sLinearCodePlainTextMaxSize = 64;

        F2LinearCode() = default;
        F2LinearCode(const F2LinearCode& cp) = default;


        void init(oc::Matrix<u8> g);

        void generateSubcodes();

        u64 mInputByteSize = 0;

        // The code
        oc::Matrix<u8> mG;

        // every 256 rows is a subcode.
        oc::Matrix<oc::block> mSubcodes;

        inline void encode(u8* input, u8* codeword)
        {

            // highlevel idea: For each byte of the input, we have preprocessed 
            // the corresponding partial code. That is, we have many sub-codes
            // each with input size 8. And these subcodes are precomputed
            // in a lookup table called mSubcodes. Each sub-code takes up 256 * codeSize;

            //const u64 rowSize8 = rowSize * 8;
            //const u64 superRowCount = (mU8RowCount + 7) / 8;

#ifndef NDEBUG
            const u64 codeSize = mSubcodes.cols();
            if (codeSize != 1)
                throw RTE_LOC;// not impl.
            if (mInputByteSize != sizeof(block))
                throw RTE_LOC;//not impl
#endif
            const auto mInputByteSize_ = 16;
            const auto codeSize_ = 1; // block
            const u64 subcodeSize = 256 * codeSize_;

            // in some cases below, the input array must be a
            // multiple of 8 in length...
            u8 _byteView[sLinearCodePlainTextMaxSize];
            u8* byteView = _byteView;
            memcpy(byteView, input, mInputByteSize_);


            // create a local to store the partial codeword
            // and zero it out.
            oc::AlignedArray<block, 8> c;
            c[0] = c[0] ^ c[0];
            c[1] = c[1] ^ c[1];
            c[2] = c[2] ^ c[2];
            c[3] = c[3] ^ c[3];
            c[4] = c[4] ^ c[4];
            c[5] = c[5] ^ c[5];
            c[6] = c[6] ^ c[6];
            c[7] = c[7] ^ c[7];

            // for performance reasons, we have multiplt implementations, one for
            // each size under 9 blocks wide. There is a general case at the end.
            //switch (codeSize)
            //{
            //case 1:
            //{
            // this case has been optimized and we lookup 8 sub-codes at a time.
            static const u64 byteStep = 8;
            block* __restrict  T0 = mSubcodes.data() + subcodeSize * 0;
            block* __restrict  T1 = mSubcodes.data() + subcodeSize * 1;
            block* __restrict  T2 = mSubcodes.data() + subcodeSize * 2;
            block* __restrict  T3 = mSubcodes.data() + subcodeSize * 3;
            block* __restrict  T4 = mSubcodes.data() + subcodeSize * 4;
            block* __restrict  T5 = mSubcodes.data() + subcodeSize * 5;
            block* __restrict  T6 = mSubcodes.data() + subcodeSize * 6;
            block* __restrict  T7 = mSubcodes.data() + subcodeSize * 7;

            u64 step = subcodeSize * byteStep;

            for (u64 i = 0; i < mInputByteSize_; i += byteStep)
            {
                c[0] = c[0] ^ T0[byteView[i + 0]];
                c[1] = c[1] ^ T1[byteView[i + 1]];
                c[2] = c[2] ^ T2[byteView[i + 2]];
                c[3] = c[3] ^ T3[byteView[i + 3]];
                c[4] = c[4] ^ T4[byteView[i + 4]];
                c[5] = c[5] ^ T5[byteView[i + 5]];
                c[6] = c[6] ^ T6[byteView[i + 6]];
                c[7] = c[7] ^ T7[byteView[i + 7]];

                T0 += step;
                T1 += step;
                T2 += step;
                T3 += step;
                T4 += step;
                T5 += step;
                T6 += step;
                T7 += step;
            }

            c[0] = c[0] ^ c[4];
            c[1] = c[1] ^ c[5];
            c[2] = c[2] ^ c[6];
            c[3] = c[3] ^ c[7];

            c[0] = c[0] ^ c[2];
            c[1] = c[1] ^ c[3];

            c[0] = c[0] ^ c[1];

            memcpy(codeword, c.data(), codeSize_ * sizeof(oc::block));

            //    break;
            //}
            //case 2:
            //{
            //    // this case has been optimized and we lookup 4 sub-codes at a time.
            //    static const u64 byteStep = 4;

            //    u64 kStop = (mG8.size() / 8) * 8;
            //    u64 kStep = rowSize * byteStep;
            //    for (u64 k = 0, i = 0; k < kStop; i += byteStep, k += kStep)
            //    {
            //        block* g0 = mG8.data() + k + byteView[i + 0] * codeSize + rowSize * 0;
            //        block* g1 = mG8.data() + k + byteView[i + 1] * codeSize + rowSize * 1;
            //        block* g2 = mG8.data() + k + byteView[i + 2] * codeSize + rowSize * 2;
            //        block* g3 = mG8.data() + k + byteView[i + 3] * codeSize + rowSize * 3;

            //        c[0] = c[0] ^ g0[0];
            //        c[1] = c[1] ^ g0[1];

            //        c[2] = c[2] ^ g1[0];
            //        c[3] = c[3] ^ g1[1];

            //        c[4] = c[4] ^ g2[0];
            //        c[5] = c[5] ^ g2[1];

            //        c[6] = c[6] ^ g3[0];
            //        c[7] = c[7] ^ g3[1];
            //    }

            //    c[0] = c[0] ^ c[4];
            //    c[1] = c[1] ^ c[5];
            //    c[2] = c[2] ^ c[6];
            //    c[3] = c[3] ^ c[7];

            //    c[0] = c[0] ^ c[2];
            //    c[1] = c[1] ^ c[3];

            //    memcpy(codeword, c, codewordU8Size());

            //    break;
            //}
            //case 3:
            //case 4:
            //{

            //    // this case has been optimized and we lookup 2 sub-codes at a time.
            //    static const u64 byteStep = 2;

            //    i32 kStop = static_cast<i32>(mG8.size() / 8) * 8;
            //    i32 kStep = static_cast<i32>(rowSize * byteStep);

            //    block* gg0 = mG8.data();
            //    block* gg1 = mG8.data() + rowSize;
            //    for (i32 k = 0; k < kStop; byteView += byteStep, k += kStep)
            //    {
            //        auto g0 = gg0 + byteView[0] * 4;
            //        auto g1 = gg1 + byteView[1] * 4;
            //        gg0 += kStep;
            //        gg1 += kStep;

            //        c[0] = c[0] ^ g0[0];
            //        c[1] = c[1] ^ g0[1];
            //        c[2] = c[2] ^ g0[2];
            //        c[3] = c[3] ^ g0[3];

            //        c[4] = c[4] ^ g1[0];
            //        c[5] = c[5] ^ g1[1];
            //        c[6] = c[6] ^ g1[2];
            //        c[7] = c[7] ^ g1[3];
            //    }

            //    c[0] = c[0] ^ c[4];
            //    c[1] = c[1] ^ c[5];
            //    c[2] = c[2] ^ c[6];
            //    c[3] = c[3] ^ c[7];

            //    memcpy(codeword, c, codewordU8Size());


            //    break;
            //}
            //case 5:
            //case 6:
            //case 7:
            //case 8:
            //{

            //    // this case has been optimized and we lookup 1 sub-codes at a time.
            //    static const u64 byteStep = 1;

            //    u64 kStop = (mG8.size() / 8) * 8;
            //    u64 kStep = rowSize * byteStep;

            //    for (u64 k = 0, i = 0; k < kStop; i += byteStep, k += kStep)
            //    {
            //        block* g0 = mG8.data() + k + byteView[i] * codeSize;

            //        c[0] = c[0] ^ g0[0];
            //        c[1] = c[1] ^ g0[1];
            //        c[2] = c[2] ^ g0[2];
            //        c[3] = c[3] ^ g0[3];
            //        c[4] = c[4] ^ g0[4];
            //        c[5] = c[5] ^ g0[5];
            //        c[6] = c[6] ^ g0[6];
            //        c[7] = c[7] ^ g0[7];
            //    }

            //    memcpy(codeword, c, codewordU8Size());


            //    break;
            //}
            //default:
            //{

            //    // general case when the code word is wide than 8;

            //    block* g0 = mG8.data() + rowSize * 0;
            //    block* g1 = mG8.data() + rowSize * 1;
            //    block* g2 = mG8.data() + rowSize * 2;
            //    block* g3 = mG8.data() + rowSize * 3;
            //    block* g4 = mG8.data() + rowSize * 4;
            //    block* g5 = mG8.data() + rowSize * 5;
            //    block* g6 = mG8.data() + rowSize * 6;
            //    block* g7 = mG8.data() + rowSize * 7;

            //    for (u64 j = 0; j < codeSize; ++j)
            //    {
            //        c[0] = c[0] ^ c[0];
            //        c[1] = c[1] ^ c[1];
            //        c[2] = c[2] ^ c[2];
            //        c[3] = c[3] ^ c[3];
            //        c[4] = c[4] ^ c[4];
            //        c[5] = c[5] ^ c[5];
            //        c[6] = c[6] ^ c[6];
            //        c[7] = c[7] ^ c[7];

            //        auto bv = byteView;

            //        auto gg0 = g0;
            //        auto gg1 = g1;
            //        auto gg2 = g2;
            //        auto gg3 = g3;
            //        auto gg4 = g4;
            //        auto gg5 = g5;
            //        auto gg6 = g6;
            //        auto gg7 = g7;

            //        for (u64 i = 0; i < superRowCount; ++i, bv += 8)
            //        {
            //            c[0] = c[0] ^ gg0[bv[0] * codeSize];
            //            c[1] = c[1] ^ gg1[bv[1] * codeSize];
            //            c[2] = c[2] ^ gg2[bv[2] * codeSize];
            //            c[3] = c[3] ^ gg3[bv[3] * codeSize];
            //            c[4] = c[4] ^ gg4[bv[4] * codeSize];
            //            c[5] = c[5] ^ gg5[bv[5] * codeSize];
            //            c[6] = c[6] ^ gg6[bv[6] * codeSize];
            //            c[7] = c[7] ^ gg7[bv[7] * codeSize];

            //            gg0 += rowSize8;
            //            gg1 += rowSize8;
            //            gg2 += rowSize8;
            //            gg3 += rowSize8;
            //            gg4 += rowSize8;
            //            gg5 += rowSize8;
            //            gg6 += rowSize8;
            //            gg7 += rowSize8;

            //        }

            //        c[0] = c[0] ^ c[1];
            //        c[2] = c[2] ^ c[3];
            //        c[4] = c[4] ^ c[5];
            //        c[6] = c[6] ^ c[7];
            //        c[0] = c[0] ^ c[2];
            //        c[4] = c[4] ^ c[6];
            //        c[0] = c[0] ^ c[4];

            //        memcpy(
            //            (codeword + sizeof(block) * j),
            //            c,
            //            std::min<u64>(sizeof(block), codewordU8Size() - j * sizeof(block)));

            //        ++g0;
            //        ++g1;
            //        ++g2;
            //        ++g3;
            //        ++g4;
            //        ++g5;
            //        ++g6;
            //        ++g7;
            //    }
            //    break;
            //}
            //}
        }

        template<int N>
        void encodeN(block* inout)
        {
#ifndef NDEBUG
            const u64 codeSize = mSubcodes.cols();
            if (codeSize != 1)
                throw RTE_LOC;// not impl.
            if (mInputByteSize != sizeof(block))
                throw RTE_LOC;//not impl
#endif
            // const auto mInputByteSize_ = 16;
            // const auto codeSize_ = 1; // block
            // const u64 subcodeSize = 256 * codeSize_;

            static_assert(N % 8 == 0);
            // create a local to store the partial codeword
            // and zero it out.
            oc::AlignedArray<block, N> c;

            block* __restrict  T = mSubcodes.data();
            // u8* inPtr = inout->data();

            for (u64 i = 0; i < N; i += 8)
            {
                c[i + 0] = T[inout[i + 0].data()[0]];
                c[i + 1] = T[inout[i + 1].data()[0]];
                c[i + 2] = T[inout[i + 2].data()[0]];
                c[i + 3] = T[inout[i + 3].data()[0]];
                c[i + 4] = T[inout[i + 4].data()[0]];
                c[i + 5] = T[inout[i + 5].data()[0]];
                c[i + 6] = T[inout[i + 6].data()[0]];
                c[i + 7] = T[inout[i + 7].data()[0]];
            }

            // u64 step = subcodeSize;

            for (u64 j = 1; j < 128; ++j)
            {
                for (u64 i = 0; i < N; i += 8)
                {
                    c[i + 0] = c[i + 0] ^ T[inout[i + 0].data()[j]];
                    c[i + 1] = c[i + 1] ^ T[inout[i + 1].data()[j]];
                    c[i + 2] = c[i + 2] ^ T[inout[i + 2].data()[j]];
                    c[i + 3] = c[i + 3] ^ T[inout[i + 3].data()[j]];
                    c[i + 4] = c[i + 4] ^ T[inout[i + 4].data()[j]];
                    c[i + 5] = c[i + 5] ^ T[inout[i + 5].data()[j]];
                    c[i + 6] = c[i + 6] ^ T[inout[i + 6].data()[j]];
                    c[i + 7] = c[i + 7] ^ T[inout[i + 7].data()[j]];
                }
            }

            memcpy(inout, c.data(), sizeof(c));
        }

    };

}
