#pragma once
#include "secure-join/Defines.h"
#include "secure-join/Perm/Permutation.h"
//#include "libOTe/Tools/LDPC/Mtx.h"

namespace secJoin
{

    inline void mod3Add(
        span<block> z1, span<block> z0,
        span<block> y1, span<block> y0)
    {
        assert(z1.size() == z0.size());
        assert(z1.size() == y0.size());
        assert(y1.size() == y0.size());

        block* __restrict z1d = z1.data();
        block* __restrict z0d = z0.data();
        block* __restrict y1d = y1.data();
        block* __restrict y0d = y0.data();

        //auto x1x0 = x1 ^ x0;
        //auto z1 = (1 ^ y0 ^ x0) * (x1x0 ^ y1);
        //auto z0 = (1 ^ x1 ^ y1) * (x1x0 ^ y0);
        //auto e = (x + y) % 3;
        for (u64 i = 0; i < z0.size(); ++i)
        {
            auto x1i = z1d[i];
            auto x0i = z0d[i];
            auto y1i = y1d[i];
            auto y0i = y0d[i];
            auto x1x0 = x1i ^ x0i;
            z1d[i] = (y0i ^ x0i).andnot_si128(x1x0 ^ y1i);
            z0d[i] = (x1i ^ y1i).andnot_si128(x1x0 ^ y0i);
        }
    }


    // a compressing "linear code" that is comprised 
    // p iterations of accumulate and permute.
    // Loosely based on https://www.cs.yale.edu/homes/spielman/Research/tc.pdf
    // The assumption/conjecture is that this code 
    // will have high minimum distance.
    struct F3AccPermCode
    {
        // the code/output size
        u64 mN = 0;

        // the message/input size
        u64 mK = 0;

        // the permutation for each iteration.
        std::vector<Perm> mPerms;

        // construct a k -> n code with p iterations. Sample 
        // the permutations using seed.
        void init(u64 k, u64 n, u64 p = 0, block seed = oc::CCBlock)
        {
            if (k <= n)
                throw std::runtime_error("message size must be larger than the code size. " LOCATION);

            if (p == 0)
                p = std::max<u64>(7, oc::log2ceil(n));

            PRNG prng(seed);
            mPerms.resize(p);
            mN = n;
            mK = k;

            for (u64 i = 0; i < mPerms.size(); ++i)
                mPerms[i] = Perm(k, prng);
        }

        template<typename T>
        void encode(span<T> in, span<T> out)
        {
            if (in.size() != mK)
                throw RTE_LOC;
            if (out.size() != mN)
                throw RTE_LOC;

            for (u64 i = 1; i < mK; ++i)
                in[i] = (in[i] + in[i - 1]) % 3;

            for (u64 j = 0;j < mPerms.size() - 1; ++j)
            {

                for (u64 i = 1; i < mK; ++i)
                {
                    auto pi = mPerms[j][i];
                    auto pi1 = mPerms[j][i - 1];

                    in[pi] = (in[pi] + in[pi1]) % 3;
                }
            }

            for (u64 i = 0; i < mN; ++i)
                out[i] = in[mPerms.back()[i]];
        }

        // encode n instances.
        void encode(
            oc::MatrixView<block> msb, oc::MatrixView<block> lsb,
            oc::MatrixView<block> outMsb, oc::MatrixView<block> outLsb,
            u64 batchSize = 1ull << 10)
        {
            ;
            auto len = lsb.cols();
            if (lsb.rows() != mK)
                throw RTE_LOC;
            if (msb.rows() != mK)
                throw RTE_LOC;
            if (outLsb.rows() != mN)
                throw RTE_LOC;
            if (outMsb.rows() != mN)
                throw RTE_LOC;
            if (lsb.cols() != len)
                throw RTE_LOC;
            if (msb.cols() != len)
                throw RTE_LOC;
            if (outLsb.cols() != len)
                throw RTE_LOC;
            if (outMsb.cols() != len)
                throw RTE_LOC;

            auto bb = oc::divCeil(len, batchSize);
            for (u64 b = 0; b < bb; ++b)
            {
                auto begin = b * batchSize;
                auto s = std::min<u64>(batchSize, len - begin);

                for (u64 i = 1; i < mK; ++i)
                {
                    mod3Add(
                        msb[i].subspan(begin, s), 
                        lsb[i].subspan(begin, s), 
                        msb[i - 1].subspan(begin, s), 
                        lsb[i - 1].subspan(begin, s));
                }

                for (u64 j = 0;j < mPerms.size() - 1; ++j)
                {

                    for (u64 i = 1; i < mK; ++i)
                    {
                        auto pi = mPerms[j][i];
                        auto pi1 = mPerms[j][i - 1];

                        mod3Add(
                            msb[pi].subspan(begin, s), 
                            lsb[pi].subspan(begin, s), 
                            msb[pi1].subspan(begin, s), 
                            lsb[pi1].subspan(begin, s));
                    }
                }

                for (u64 i = 0; i < mN; ++i)
                {
                    auto pi = mPerms.back()[i];
                    memcpy(
                        outLsb[i].subspan(begin, s), 
                        lsb[pi].subspan(begin, s));

                    memcpy(
                        outMsb[i].subspan(begin, s),
                        msb[pi].subspan(begin, s));
                }
            }
        }

        oc::Matrix<u8> getMatrix()
        {
            oc::Matrix<u8> state(mK, mK);
            for (u64 i = 0; i < mK; ++i)
                state(i, i) = 1;

            auto addRow = [&](u64 d, u64 s)
            {
                for (u64 i = 0; i < mK; ++i)
                {
                    state(d, i) = (state(d, i) + state(s, i)) % 3;
                }
            };


            for (u64 i = 1; i < mK; ++i)
                addRow(i, i - 1);

            for (u64 j = 0;j < mPerms.size() - 1; ++j)
            {

                for (u64 i = 1; i < mK; ++i)
                {
                    auto pi = mPerms[j][i];
                    auto pi1 = mPerms[j][i - 1];
                    addRow(pi, pi1);
                }
            }

            oc::Matrix<u8> ret(mN, mK);
            for (u64 i = 0; i < mN; ++i)
            {
                for (u64 j = 0; j < mK; ++j)
                    ret(i, j) = state(mPerms.back()[i], j);
            }

            return ret;
        }
    };

    inline std::ostream& operator<<(std::ostream& o, F3AccPermCode& c)
    {
        auto m = c.getMatrix();
        for (u64 i = 0; i < m.rows(); ++i)
        {
            for (u64 j = 0; j < m.cols(); ++j)
                o << (int)m(i, j) << " ";
            o << "\n";
        }
        o << std::flush;
        return o;
    }
}