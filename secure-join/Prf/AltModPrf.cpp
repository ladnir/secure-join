

#include "AltModPrf.h"
#include "secure-join/AggTree/PerfectShuffle.h"

#define AltMod_NEW

namespace secJoin
{


    const std::array<block256, 128> AltModPrf::mB = oc::PRNG(oc::block(2134, 5437)).get<std::array<block256, 128>>();
    const std::array<block256, 128> AltModPrf::mBShuffled = []() {

        std::array<block256, 128> shuffled;
        for (u64 i = 0; i < shuffled.size(); ++i)
        {
            auto iter0 = oc::BitIterator((u8*)&mB[i].mData[0]);
            auto iter1 = oc::BitIterator((u8*)&mB[i].mData[1]);
            auto dest = oc::BitIterator((u8*)&shuffled[i]);
            for (u64 j = 0; j < 128; ++j)
            {
                *dest++ = *iter0++;
                *dest++ = *iter1++;
            }
        }
        return shuffled;
    }();

    const std::array<std::array<u8, 256>, 128> AltModPrf::mBExpanded = []() {

        std::array<std::array<u8, 256>, 128> r;
        for (u64 i = 0; i < mB.size(); ++i)
        {
            auto iter0 = oc::BitIterator((u8*)&mB[i].mData[0]);
            for (u64 j = 0; j < r[i].size(); ++j)
                r[i][j] = *iter0++;
        }

        return r;
    }();

    // input v.
    // v will have m=256 rows. It will store the i'th value in
    // bit decomposed/transposed manner. That is, the j'th bit of the i'th value is
    // stored at v[j,i] where the indexing is into the bits of v.
    //
    // The result is written to y. y[i] will store the i'th output.
    // It will *not* be in transposed format
    //
    void compressB(
        u64 begin,
        u64 n,
        oc::MatrixView<oc::block> v,
        span<oc::block> y
    )
    {
        //auto n = y.size();
        assert(begin % 128 == 0);

        // the begin'th v value starts at block index begin128
        auto begin128 = oc::divCeil(begin, 128);
        
        // the number of 128 chunks
        auto n128 = oc::divCeil(n, 128);

        // the number of 128 chunks given that there are at least 8 more.
        auto n1024 = n128 / 8 * 8;


        oc::Matrix<oc::block> yt(128, n128);

        //auto B = AltModPrf::mB;
        assert(begin % 128 == 0);
        // assert(n % 128 == 0);
        assert(v.rows() == AltModPrf::MidSize);
        assert(v.cols() >= begin128 + n128);
        assert(y.size() >= begin + n);

        auto vStep = v.cols();
        auto ytIter = yt.data();
        auto ytstep = yt.cols();
        auto vSize = n128 * sizeof(oc::block);

        for (u64 i = 0; i < 128; ++i)
        {
            u64 j = 0;
            while (AltModPrf::mBExpanded[i][j] == 0)
                ++j;

            auto vIter = v.data() + begin128 + j * vStep;
            assert(yt[i].data() == ytIter);
            assert(v[j].subspan(begin128).data() == vIter);
            memcpy(ytIter, vIter, vSize);
            vIter += vStep;
            ++j;

            //memcpy(yt[i], v[j++].subspan(begin128, n128));
            while (j < 256)
            {
                if (AltModPrf::mBExpanded[i][j])
                {
                    assert(yt[i].data() == ytIter);
                    assert(vIter == v[j].data() + begin128);
                    oc::block* __restrict yti = ytIter;
                    oc::block* __restrict vj = vIter;
                    u64 k = 0;

                    for (; k < n1024; k += 8)
                    {
                        yti[k + 0] = yti[k + 0] ^ vj[k + 0];
                        yti[k + 1] = yti[k + 1] ^ vj[k + 1];
                        yti[k + 2] = yti[k + 2] ^ vj[k + 2];
                        yti[k + 3] = yti[k + 3] ^ vj[k + 3];
                        yti[k + 4] = yti[k + 4] ^ vj[k + 4];
                        yti[k + 5] = yti[k + 5] ^ vj[k + 5];
                        yti[k + 6] = yti[k + 6] ^ vj[k + 6];
                        yti[k + 7] = yti[k + 7] ^ vj[k + 7];

                    }
                    for (; k < n128; ++k)
                        yti[k] = yti[k] ^ vj[k];
                }
                vIter += vStep;
                ++j;
            }

            ytIter += ytstep;
        }

        oc::AlignedArray<oc::block, 128> tt;
        auto step = yt.cols();
        for (u64 i = 0, ii = 0; i < n; i += 128, ++ii)
        {
            auto offset = yt.data() + ii;
            for (u64 j = 0; j < 128; ++j)
            {
                assert(&yt(j, ii) == offset);
                tt[j] = *offset;
                offset += step;
            }

            oc::transpose128(tt.data());
            auto m = std::min<u64>(n - i, 128);

            memcpy(y.data() + i + begin, tt.data(), m * sizeof(oc::block));
            //if (m == 128)
            //{

            //    for (u64 j = 0; j < m; ++j)
            //    {
            //        y[i + j] = tt[j];
            //    }
            //}
            //else
            //{
            //    for (u64 j = 0; j < m; ++j)
            //    {
            //        y[i + j] = tt[j];
            //    }
            //}
        }
    }



    void compressB(
        oc::MatrixView<oc::block> v,
        span<oc::block> y
    )
    {
        u64 batch = 1ull << 12;
        auto n = y.size();
        for (u64 i = 0; i < n; i += batch)
        {
            auto m = std::min<u64>(batch, n - i);
            compressB(i, m, v, y);
        }
    }

    // z =  x + y mod 3
    void mod3Add(
        span<oc::block> z1, span<oc::block> z0,
        span<oc::block> x1, span<oc::block> x0,
        span<oc::block> y1, span<oc::block> y0)
    {
        assert(z1.size() == z0.size());
        assert(z1.size() == x0.size());
        assert(z1.size() == y0.size());
        assert(x1.size() == x0.size());
        assert(y1.size() == y0.size());

        //auto x1x0 = x1 ^ x0;
        //auto z1 = (1 ^ y0 ^ x0) * (x1x0 ^ y1);
        //auto z0 = (1 ^ x1 ^ y1) * (x1x0 ^ y0);
        //auto e = (x + y) % 3;
        for (u64 i = 0; i < z0.size(); ++i)
        {
            auto x1i = x1.data()[i];
            auto x0i = x0.data()[i];
            auto y1i = y1.data()[i];
            auto y0i = y0.data()[i];
            auto x1x0 = x1i ^ x0i;
            z1[i] = (y0i ^ x0i).andnot_si128(x1x0 ^ y1i);
            z0[i] = (x1i ^ y1i).andnot_si128(x1x0 ^ y0i);
        }
    }

    // (ab) += y0 mod 3
    // we treat binary x1 as the MSB and binary x0 as lsb.
    // That is, for bits, x1 x0 y0, we sets 
    //   t = x1 * 2 + x0 + y0
    //   x1 = t / 2
    //   x0 = t % 2
    void mod3Add(
        span<oc::block> z1, span<oc::block> z0,
        span<oc::block> x1, span<oc::block> x0,
        span<oc::block> y0)
    {
        //auto z1 = x1 ^ (x1 ^ x0) * y0;
        //auto z0 = x0 ^ (1 ^ x1) * y0;
        assert(z1.size() == z0.size());
        assert(z1.size() == x0.size());
        assert(z1.size() == y0.size());
        assert(x1.size() == x0.size());


        for (u64 i = 0; i < x1.size(); ++i)
        {
            auto ab = x1.data()[i] ^ x0.data()[i];
            auto abc = ab & y0.data()[i];

            auto zz1 = x1.data()[i] ^ abc;

            auto nac = x1.data()[i].andnot_si128(y0.data()[i]);
            auto zz0 = x0.data()[i] ^ nac;

            z1[i] = zz1;
            z0[i] = zz0;

        }
    }



    inline void sampleMod3(oc::PRNG& prng, span<u8> mBuffer)
    {
        auto n = mBuffer.size();
        auto dst = mBuffer.data();
        oc::block m[8], t[8], eq[8];
        oc::block allOne = oc::AllOneBlock;
        oc::block block1 = oc::block::allSame<u16>(1);
        oc::block block3 = oc::block::allSame<u16>(3);

        static constexpr int batchSize = 16;
        std::array<std::array<oc::block, 8>, 64> buffer;
        std::array<u8* __restrict, 64> iters;

        for (u64 i = 0; i < n;)
        {
            for (u64 j = 0; j < 64; ++j)
                iters[j] = (u8*)buffer[j].data();

            for (u64 bb = 0; bb < batchSize; ++bb)
            {
                prng.mAes.ecbEncCounterMode(prng.mBlockIdx, 8, m);
                prng.mBlockIdx += 8;


                for (u64 j = 0; j < 8; ++j)
                {
                    if (j)
                    {
                        m[0] = m[0] >> 2;
                        m[1] = m[1] >> 2;
                        m[2] = m[2] >> 2;
                        m[3] = m[3] >> 2;
                        m[4] = m[4] >> 2;
                        m[5] = m[5] >> 2;
                        m[6] = m[6] >> 2;
                        m[7] = m[7] >> 2;
                    }

                    t[0] = m[0] & block3;
                    t[1] = m[1] & block3;
                    t[2] = m[2] & block3;
                    t[3] = m[3] & block3;
                    t[4] = m[4] & block3;
                    t[5] = m[5] & block3;
                    t[6] = m[6] & block3;
                    t[7] = m[7] & block3;

                    eq[0] = _mm_cmpeq_epi16(t[0], block3);
                    eq[1] = _mm_cmpeq_epi16(t[1], block3);
                    eq[2] = _mm_cmpeq_epi16(t[2], block3);
                    eq[3] = _mm_cmpeq_epi16(t[3], block3);
                    eq[4] = _mm_cmpeq_epi16(t[4], block3);
                    eq[5] = _mm_cmpeq_epi16(t[5], block3);
                    eq[6] = _mm_cmpeq_epi16(t[6], block3);
                    eq[7] = _mm_cmpeq_epi16(t[7], block3);

                    eq[0] = eq[0] ^ allOne;
                    eq[1] = eq[1] ^ allOne;
                    eq[2] = eq[2] ^ allOne;
                    eq[3] = eq[3] ^ allOne;
                    eq[4] = eq[4] ^ allOne;
                    eq[5] = eq[5] ^ allOne;
                    eq[6] = eq[6] ^ allOne;
                    eq[7] = eq[7] ^ allOne;

                    eq[0] = eq[0] & block1;
                    eq[1] = eq[1] & block1;
                    eq[2] = eq[2] & block1;
                    eq[3] = eq[3] & block1;
                    eq[4] = eq[4] & block1;
                    eq[5] = eq[5] & block1;
                    eq[6] = eq[6] & block1;
                    eq[7] = eq[7] & block1;

                    auto t16 = (u16 * __restrict)t;
                    auto e16 = (u16 * __restrict)eq;
                    for (u64 j = 0; j < 64; ++j)
                    {
                        iters[j][0] = t16[j];
                        iters[j] += e16[j];
                    }
                }
            }

            for (u64 j = 0; j < 64 && i < n; ++j)
            {
                auto b = (u8*)buffer[j].data();

                auto size = iters[j] - b;
                auto min = std::min<u64>(size, n - i);
                if (min)
                {
                    memcpy(dst + i, b, min);
                    i += min;
                }
            }
        }
    }

    inline void sampleMod3(oc::PRNG& prng, span<oc::block> msb, span<oc::block> lsb, oc::AlignedUnVector<u8>& b)
    {
        b.resize(msb.size() * 128);
        sampleMod3(prng, b);
        oc::block block1 = oc::block::allSame<u8>(1);
        oc::block block2 = oc::block::allSame<u8>(2);

        for (u64 i = 0; i < msb.size(); ++i)
        {
            auto bb = (oc::block*)&b.data()[i * 128];
            assert((u64)bb % 16 == 0);

            oc::block tt[8];
            tt[0] = bb[0] & block1;
            tt[1] = bb[1] & block1;
            tt[2] = bb[2] & block1;
            tt[3] = bb[3] & block1;
            tt[4] = bb[4] & block1;
            tt[5] = bb[5] & block1;
            tt[6] = bb[6] & block1;
            tt[7] = bb[7] & block1;

            lsb[i] =
                tt[0] << 0 ^
                tt[1] << 1 ^
                tt[2] << 2 ^
                tt[3] << 3 ^
                tt[4] << 4 ^
                tt[5] << 5 ^
                tt[6] << 6 ^
                tt[7] << 7;


            tt[0] = bb[0] & block2;
            tt[1] = bb[1] & block2;
            tt[2] = bb[2] & block2;
            tt[3] = bb[3] & block2;
            tt[4] = bb[4] & block2;
            tt[5] = bb[5] & block2;
            tt[6] = bb[6] & block2;
            tt[7] = bb[7] & block2;

            msb[i] =
                tt[0] >> 1 ^
                tt[1] << 0 ^
                tt[2] << 1 ^
                tt[3] << 2 ^
                tt[4] << 3 ^
                tt[5] << 4 ^
                tt[6] << 5 ^
                tt[7] << 6;


        }

    }
    void compare(span<oc::block> m1, span<oc::block> m0, span<u16> u, bool verbose = false)
    {
        for (u64 i = 0; i < u.size(); ++i)
        {
            if (verbose && i < 10)
            {
                std::cout << i << ": " << u[i] << " ~ (" << bit(m1[0], i) << " " << bit(m0[0], i) << std::endl;
            }

            assert(u[i] < 3);
            if (*oc::BitIterator((u8*)m1.data(), i) != (u[i] >> 1))
            {
                std::cout << "msb " << i << ": " << u[i] << " -> (" << *oc::BitIterator((u8*)m1.data(), i) << " " << *oc::BitIterator((u8*)m0.data(), i) << " )" << std::endl;
                throw RTE_LOC;
            }
            if (*oc::BitIterator((u8*)m0.data(), i) != (u[i] & 1))
            {
                std::cout << "lsb " << i << ": " << u[i] << " -> (" << *oc::BitIterator((u8*)m1.data(), i) << " " << *oc::BitIterator((u8*)m0.data(), i) << " )" << std::endl;
                throw RTE_LOC;;
            }
        }
    }
    void compare(span<oc::block> m1, span<oc::block> m0,
        span<oc::block> u1, span<oc::block> u0)
    {
        assert(m1.size() == u1.size());
        assert(m0.size() == u1.size());
        assert(u0.size() == u1.size());

        for (u64 i = 0; i < u0.size(); ++i)
        {
            if (m1[i] != u1[i] || m0[i] != u0[i])
            {
                std::cout << "bad compare " << std::endl;
                for (u64 j = i * 128; j < i * 128 + 128; ++j)
                {
                    std::cout << j << ": (" <<
                        bit(m1[i], j % 128) << " " << bit(m0[i], j % 128) << ") vs (" <<
                        bit(u1[i], j % 128) << " " << bit(u0[i], j % 128) << ")" << std::endl;
                }
                throw RTE_LOC;

            }
        }
    }
    // out = (hi0, hi1) ^ prng()
    inline void xorVector(
        span<oc::block> out1,
        span<oc::block> out0,
        span<oc::block> m1,
        span<oc::block> m0,
        oc::PRNG& prng)
    {

        xorVector(out1, m1, prng);
        xorVector(out0, m0, prng);
        //assert(out1.size() == hi1.size());
        //assert(out0.size() == hi1.size());

        //prng.get(out0);
        //for (u64 i = 0; i < hi0.size(); ++i)
        //    out0[i] = out0[i] ^ hi0[i];

        //prng.get(out1);
        //for (u64 i = 0; i < out1.size(); ++i)
        //    out1[i] = out1[i] ^ hi1[i];
    }

    // out = (hi0, hi1) ^ prng()
    inline void xorVector(
        span<oc::block> out,
        span<oc::block> m1,
        span<oc::block> m0,
        oc::PRNG& prng)
    {
        xorVector(out.subspan(m0.size()), m1, prng);
        xorVector(out.subspan(0, m0.size()), m0, prng);

        //assert(out.size() == hi1.size() + hi0.size());

        //prng.get(out);
        //for (u64 i = 0; i < hi0.size(); ++i)
        //    out[i] = out[i] ^ hi0[i];
        //for (u64 i = hi0.size(), j = 0; i < out.size(); ++i, ++j)
        //    out[i] = out[i] ^ hi1[j];
    }

    void  AltModPrf::setKey(oc::block k)
    {
        mKey[0] = k;
    }

    void  AltModPrf::compressH(const std::array<u16, KeySize>& hj, block256m3& uj)
    {
        //for (u64 k = 0; k < KeySize; ++k)
        //{
        //    uj.mData[k] = hj[k];
        //}


        //assert(mPi.size() != 0);
        if constexpr (KeySize == 128)
        {
            uj.mData[0] = hj[0];
            for (u64 k = 1; k < KeySize; ++k)
            {
                uj.mData[k] = hj[k] + uj.mData[k - 1];
                uj.mData[k] = uj.mData[k] % 3;
            }

            for (u64 k = 128; k < 256; ++k)
            {
                //uj.mData[k] = hj[k-128] + uj.mData[k-128];
                uj.mData[k] = uj.mData[k - 128];
            }

        }
        else if constexpr (KeySize == 256)
        {


            uj.mData[0] = hj[0];
            for (u64 k = 1; k < KeySize; ++k)
            {
                uj.mData[k] = hj[k] + uj.mData[k - 1];
                uj.mData[k] = uj.mData[k] % 3;
            }
        }
        else if constexpr (KeySize == 512)
        {
            assert(0);
            //auto pik = mPi.data();
            //for (u64 k = 0; k < 256; ++k)
            //{
            //    uj.mData[k] = (
            //        hj[pik[0]] +
            //        hj[pik[1]]
            //        ) % 3;
            //    pik += 2;
            //}
        }
        else
        {
            assert(0);
        }
    }

    oc::block  AltModPrf::eval(oc::block x)
    {
        std::array<u16, KeySize> h;
        std::array<oc::block, KeySize / 128> X;
        if constexpr (AltModPrf::KeySize / 128 > 1)
        {

            for (u64 i = 0; i < X.size(); ++i)
                X[i] = x ^ oc::block(i, i);
            oc::mAesFixedKey.hashBlocks<X.size()>(X.data(), X.data());
        }
        else
            X[0] = x;

        auto kIter = oc::BitIterator((u8*)mKey.data());
        auto xIter = oc::BitIterator((u8*)X.data());
        for (u64 i = 0; i < KeySize; ++i)
        {
            h[i] = *kIter & *xIter;

            //if (i < 20)
            //    std::cout << "h[" << i << "] = " << h[i] 
            //    << " = " << *kIter 
            //    <<" & " << *xIter <<std::endl;

            ++kIter;
            ++xIter;
        }

        block256m3 u;
        compressH(h, u);

        block256 w;
        for (u64 i = 0; i < u.mData.size(); ++i)
        {
            //if (i < 10)
            //    std::cout << "u[" << i << "] = " << (int)u.mData[i] << std::endl;

            *oc::BitIterator((u8*)&w, i) = u.mData[i] % 2;
        }
        return compress(w);
    }

    oc::block  AltModPrf::compress(block256& w)
    {
        return compress(w, mB);
    }

    oc::block  AltModPrf::shuffledCompress(block256& w)
    {
        return compress(w, mBShuffled);
    }

    oc::block  AltModPrf::compress(block256& w, const std::array<block256, 128>& B)
    {
        alignas(32) std::array<std::array<oc::block, 128>, 2> bw;

        for (u64 i = 0; i < 128; ++i)
        {
            bw[0][i] = B[i].mData[0] & w.mData[0];
            bw[1][i] = B[i].mData[1] & w.mData[1];
        }
        oc::transpose128(bw[0].data());
        oc::transpose128(bw[1].data());

        oc::block r;
        memset(&r, 0, sizeof(r));
        for (u64 i = 0; i < 128; ++i)
            r = r ^ bw[0][i];
        for (u64 i = 0; i < 128; ++i)
            r = r ^ bw[1][i];

        return r;
    }



    template<int keySize>
    inline void compressH(
        oc::Matrix<u16>&& mH,
        oc::Matrix<u16>& mU
    )
    {
        if constexpr (keySize == 128)
        {

            auto n = mH.size() / keySize;
            auto& h2 = mH;

            for (u64 i = 1; i < keySize; ++i)
            {
                auto h0 = h2.data() + n * (i - 1);
                auto h1 = h2.data() + n * (i);
                for (u64 j = 0; j < n; ++j)
                {
                    auto h0j = h0[j];
                    auto h1j = h1[j];
                    //__assume(h0j < 3);
                    //__assume(h1j < 3);
                    // 000 0
                    // 001 1
                    // 010 2
                    // 011 3
                    // 100 4

                    auto s = (h0j + h1j);
                    // __assume(s < 5);
                    auto q = s == 3 || s == 4;
                    h1[j] = s - 3 * q;
                    assert(h1[j] == (h0j + h1j) % 3);
                }
            }

            for (u64 i = 0; i < keySize; ++i)
            {
                auto hi = mH[i];
                for (u64 j = 0; j < n; ++j)
                {
                    mU[j][i] = hi[j];
                    mU[j][i + keySize] = hi[j];
                }
            }

        }
        else
        {
            assert(0);
        }

        mH = {};
    }

    void compressH2(
        oc::Matrix<oc::block>&& mH,
        oc::Matrix<oc::block>& u1,
        oc::Matrix<oc::block>& u0
    )
    {
        auto keySize = 128;
        assert(mH.rows() == 2 * keySize);
        assert(u1.rows() == 2 * keySize);
        assert(u0.rows() == 2 * keySize);
        assert(mH.cols() == u0.cols());
        assert(mH.cols() == u1.cols());

        // u[0  ] = h[0]
        // u[128] = h[0]
        memcpy(u0[0], mH[0]);
        memcpy(u1[0], mH[1]);
        memcpy(u0[0 + 128], mH[0]);
        memcpy(u1[0 + 128], mH[1]);

        for (u64 i = 1; i < keySize; ++i)
        {
            auto h0lsb = mH[(i - 1) * 2 + 0];
            auto h0msb = mH[(i - 1) * 2 + 1];
            auto h1lsb = mH[i * 2 + 0];
            auto h1msb = mH[i * 2 + 1];

            // h[i] += h[i-1];
            mod3Add(h1msb, h1lsb, h0msb, h0lsb, h1msb, h1lsb);

            // u[i      ] = h[i]
            // u[i + 128] = h[i]
            memcpy(u0[i], h1lsb);
            memcpy(u1[i], h1msb);
            memcpy(u0[i + 128], h1lsb);
            memcpy(u1[i + 128], h1msb);
        }

        mH = {};
    }


    void AltModPrfSender::setKeyOts(oc::block k, span<oc::block> ots)
    {
        if (ots.size() != AltModPrf::KeySize)
            throw RTE_LOC;

        mPrf.setKey(k);
        mKeyOTs.resize(AltModPrf::KeySize);
        for (u64 i = 0; i < AltModPrf::KeySize; ++i)
        {
            mKeyOTs[i].SetSeed(ots[i]);
        }
        mHasKeyOts = true;
        mDoKeyGen = false;
    }

    void mod3BitDecompostion(oc::MatrixView<u16> u, oc::MatrixView<oc::block> u0, oc::MatrixView<oc::block> u1)
    {
        if (u.rows() != u0.rows())
            throw RTE_LOC;
        if (u.rows() != u1.rows())
            throw RTE_LOC;

        if (oc::divCeil(u.cols(), 128) != u0.cols())
            throw RTE_LOC;
        if (oc::divCeil(u.cols(), 128) != u1.cols())
            throw RTE_LOC;

        u64 n = u.rows();
        u64 m = u.cols();

        oc::AlignedUnVector<u8> temp(oc::divCeil(m * 2, 8));
        for (u64 i = 0; i < n; ++i)
        {
            auto iter = temp.data();

            assert(m % 4 == 0);

            for (u64 k = 0; k < m; k += 4)
            {
                assert(u[i][k + 0] < 3);
                assert(u[i][k + 1] < 3);
                assert(u[i][k + 2] < 3);
                assert(u[i][k + 3] < 3);

                // 00 01 10 11 20 21 30 31
                *iter++ =
                    (u[i][k + 0] << 0) |
                    (u[i][k + 1] << 2) |
                    (u[i][k + 2] << 4) |
                    (u[i][k + 3] << 6);
            }

            span<u8> out0((u8*)u0.data(i), temp.size() / 2);
            span<u8> out1((u8*)u1.data(i), temp.size() / 2);
            perfectUnshuffle(temp, out0, out1);

#ifndef NDEBUG
            for (u64 j = 0; j < out0.size(); ++j)
            {
                if (out0[j] & out1[j])
                    throw RTE_LOC;
            }
#endif
        }
    }


    coproto::task<> AltModPrfSender::evaluate(
        span<oc::block> y,
        coproto::Socket& sock,
        oc::PRNG& prng,
        CorGenerator& gen)
    {
        // init has not been called, call it
        if (mInputSize == 0)
            init(y.size());

        // request the required correlated randomness if needed.
        if (mOleReq.size() == 0)
            request(gen);

        // perform the main protocol.
        return evaluate(y, sock, prng);
    }


    coproto::task<> AltModPrfSender::evaluate(
        span<oc::block> y,
        coproto::Socket& sock,
        oc::PRNG& prng)
    {

        MC_BEGIN(coproto::task<>, y, this, &sock, &prng,
            buffer = oc::AlignedUnVector<u8>{},
            f = oc::BitVector{},
            diff = oc::BitVector{},
            ots = oc::AlignedUnVector<std::array<oc::block, 2>>{},
            i = u64{},
            ole = BinOleRequest{},
            u0 = oc::Matrix<oc::block>{},
            u1 = oc::Matrix<oc::block>{},
            uu0 = oc::Matrix<oc::block>{},
            uu1 = oc::Matrix<oc::block>{},
            v = oc::Matrix<oc::block>{},
            xkShares = oc::Matrix<oc::block>{},
            msg = oc::Matrix<oc::block>{},
            pre = macoro::eager_task<>{},
            otRecv = OtRecv{}
        );

        if (mOleReq.size() != oc::roundUpTo(y.size(), 128) * AltModPrf::MidSize * 2)
            throw std::runtime_error("do not have enough preprocessing. Call request(...) first. " LOCATION);

        // If no one has started the preprocessing, then lets start it.
        if (mHasPrepro == false)
            pre = preprocess() | macoro::make_eager();

        // if we are doing the key gen, get the results.
        if (mKeyReq.size())
        {
            MC_AWAIT(mKeyReq.get(otRecv));
            auto k = otRecv.mChoice.getSpan<oc::block>()[0];
            setKeyOts(k, otRecv.mMsg);
        }

        // make sure we have a key.
        if (!mHasKeyOts)
            throw std::runtime_error("AltMod was called without a key and keyGen was not requested. " LOCATION);

        // debugging, make sure we have the correct key OTs.
        if (mDebug)
        {
            ots.resize(mKeyOTs.size());
            MC_AWAIT(sock.recv(ots));
            for (u64 i = 0; i < ots.size(); ++i)
            {
                auto ki = *oc::BitIterator((u8*)&mPrf.mKey, i);
                if (ots[i][ki] != mKeyOTs[i].getSeed())
                {
                    std::cout << "bad key ot " << i << "\nki=" << ki << " " << mKeyOTs[i].getSeed() << " vs \n"
                        << ots[i][0] << " " << ots[i][1] << std::endl;
                    throw RTE_LOC;
                }
            }
        }

        setTimePoint("DarkMatter.sender.begin");

        // for each bit of the key, perform an OT derandomization where we get a share
        // of the input x times the key mod 3. We store the LSB and MSB of the share separately.
        // Hence we need 2 * AltModPrf::KeySize rows in xkShares
        xkShares.resize(2 * AltModPrf::KeySize, oc::divCeil(y.size(), 128));
        for (i = 0; i < AltModPrf::KeySize;)
        {
            msg.resize(StepSize, xkShares.cols() * 2); // y.size() * 256 * 2 bits
            MC_AWAIT(sock.recv(msg));
            for (u64 k = 0; k < StepSize; ++i, ++k)
            {
                u8 ki = *oc::BitIterator((u8*)&mPrf.mKey, i);

                auto msbShare = xkShares[i * 2 + 1];
                auto lsbShare = xkShares[i * 2 + 0];
                if (ki)
                {
                    auto shares = span<oc::block>(lsbShare.data(), lsbShare.size() * 2);
                    assert(shares.data() + shares.size() == msbShare.data() + msbShare.size());

                    // ui = (hi1,hi0)                                   ^ G(OT(i,1))
                    //    = { [ G(OT(i,0))  + x  mod 3 ] ^ G(OT(i,1)) } ^ G(OT(i,1))
                    //    =     G(OT(i,0))  + x  mod 3 
                    xorVector(shares, msg[k], mKeyOTs[i]);
                }
                else
                {
                    // ui = (hi1,hi0)         
                    //    = G(OT(i,0))  mod 3 
                    sampleMod3(mKeyOTs[i], msbShare, lsbShare, buffer);
                }
            }
        }

        if (mDebug)
        {
            mDebugXkShares = xkShares;
        }

        // Compute u = H * xkShare mod 3
        buffer = {};
        u0.resize(AltModPrf::MidSize, oc::divCeil(y.size(), 128), oc::AllocType::Uninitialized);
        u1.resize(AltModPrf::MidSize, oc::divCeil(y.size(), 128), oc::AllocType::Uninitialized);
        compressH2(std::move(xkShares), u1, u0);

        if (mDebug)
        {
            mDebugU0 = u0;
            mDebugU1 = u1;
        }

        // Compute v = u mod 2
        v.resize(AltModPrf::MidSize, u0.cols());
        MC_AWAIT(mod2(u0, u1, v, sock));

        if (mDebug)
        {
            mDebugV = v;
        }

        // Compute y = B * v
        compressB(v, y);

        // cleanup
        if (pre.handle())
            MC_AWAIT(pre);
        mHasPrepro = false;
        mOleReq = {};
        mKeyReq = {};
        mInputSize = 0;

        MC_END();
    }

    void AltModPrfReceiver::setKeyOts(span<std::array<oc::block, 2>> ots)
    {
        if (ots.size() != AltModPrf::KeySize)
            throw RTE_LOC;
        mKeyOTs.resize(AltModPrf::KeySize);
        for (u64 i = 0; i < AltModPrf::KeySize; ++i)
        {
            mKeyOTs[i][0].SetSeed(ots[i][0]);
            mKeyOTs[i][1].SetSeed(ots[i][1]);
        }
        mHasKeyOts = true;
        mDoKeyGen = false;
    }

    coproto::task<> AltModPrfReceiver::evaluate(
        span<oc::block> x,
        span<oc::block> y,
        coproto::Socket& sock,
        oc::PRNG& prng,
        CorGenerator& gen)
    {
        // init has not been called, call it
        if (mInputSize == 0)
            init(y.size());

        // request the required correlated randomness if needed.
        if (mOleReq.size() == 0)
            request(gen);

        // perform the main protocol.
        return evaluate(x, y, sock, prng);
    }

    coproto::task<> AltModPrfReceiver::evaluate(
        span<oc::block> x,
        span<oc::block> y,
        coproto::Socket& sock,
        oc::PRNG& prng)
    {
        MC_BEGIN(coproto::task<>, x, y, this, &sock, &prng,
            xt = oc::Matrix<oc::block>{},
            buffer = oc::AlignedUnVector<u8>{},
            gx = oc::AlignedUnVector<u16>{},
            i = u64{},
            baseOts = oc::AlignedUnVector<std::array<oc::block, 2>>{},
            u0 = oc::Matrix<oc::block>{},
            u1 = oc::Matrix<oc::block>{},
            uu0 = oc::Matrix<oc::block>{},
            uu1 = oc::Matrix<oc::block>{},
            xkShares = oc::Matrix<oc::block>{},
            msg = oc::Matrix<oc::block>{},
            v = oc::Matrix<oc::block>{},
            pre = macoro::eager_task<>{},
            otSend = OtSend{}
        );

        if (x.size() != y.size())
            throw std::runtime_error("input output lengths do not match. " LOCATION);

        if (mOleReq.size() != oc::roundUpTo(y.size(), 128) * AltModPrf::MidSize * 2)
            throw std::runtime_error("do not have enough preprocessing. Call request(...) first. " LOCATION);

        // If no one has started the preprocessing, then lets start it.
        if (mHasPrepro == false)
            pre = preprocess() | macoro::make_eager();

        // if we are doing the key gen, get the results.
        if (mKeyReq.size())
        {
            MC_AWAIT(mKeyReq.get(otSend));
            setKeyOts(otSend.mMsg);
        }

        // make sure we have a key.
        if (!mHasKeyOts)
            throw std::runtime_error("AltMod was called without a key and keyGen was not requested. " LOCATION);

        // debugging, make sure we have the correct key OTs.
        if (mDebug)
        {
            baseOts.resize(mKeyOTs.size());
            for (u64 i = 0; i < baseOts.size(); ++i)
            {
                baseOts[i][0] = mKeyOTs[i][0].getSeed();
                baseOts[i][1] = mKeyOTs[i][1].getSeed();
            }
            MC_AWAIT(sock.send(std::move(baseOts)));
        }

        setTimePoint("DarkMatter.recver.begin");

        // we need x in a transformed format so that we can do SIMD operations.
        xt.resize(128, oc::divCeil(y.size(), 128));
        for (u64 i = 0, k = 0; i < y.size(); ++k)
        {
            auto m = std::min<u64>(128, y.size() - i);
            oc::AlignedArray<oc::block, 128> t;
            for (u64 j = 0;j < m; ++j, ++i)
            {
                t[j] = x.data()[i];
            }

            oc::transpose128(t.data());

            auto xtk = &xt(0, k);
            auto step = xt.cols();
            for (u64 j = 0;j < 128; ++j)
            {
                assert(xtk == &xt(j, k));
                *xtk = t[j];
                xtk += step;
            }
        }

        static_assert(AltModPrf::KeySize % StepSize == 0, "we dont handle remainders. Should be true.");

        // for each bit of the key, perform an OT derandomization where we get a share
        // of the input x times the key mod 3. We store the LSB and MSB of the share separately.
        // Hence we need 2 * AltModPrf::KeySize rows in xkShares
        xkShares.resize(2 * AltModPrf::KeySize, oc::divCeil(x.size(), 128));
        for (i = 0; i < AltModPrf::KeySize;)
        {
            assert(AltModPrf::KeySize % StepSize == 0);
            msg.resize(StepSize, xkShares.cols() * 2);

            for (u64 k = 0; k < StepSize; ++i, ++k)
            {
                // we store them in swapped order to negate the value.
                auto msbShare = xkShares[i * 2];
                auto lsbShare = xkShares[i * 2 + 1];
                sampleMod3(mKeyOTs[i][0], msbShare, lsbShare, buffer);

                // # hi = G(OT(i,0)) + x mod 3
                mod3Add(
                    msg[k].subspan(msbShare.size()),
                    msg[k].subspan(0, msbShare.size()),
                    msbShare, lsbShare,
                    xt[i]);

                // ## msg = m ^ G(OT(i,1))
                xorVector(msg[k], mKeyOTs[i][1]);
            }

            MC_AWAIT(sock.send(std::move(msg)));
        }
        if (mDebug)
        {
            mDebugXkShares = xkShares;
        }

        // Compute u = H * xkShare mod 3
        buffer = {};
        u0.resize(AltModPrf::MidSize, oc::divCeil(x.size(), 128));
        u1.resize(AltModPrf::MidSize, oc::divCeil(x.size(), 128));
        compressH2(std::move(xkShares), u1, u0);

        if (mDebug)
        {
            mDebugU0 = u0;
            mDebugU1 = u1;
        }

        // Compute v = u mod 2
        v.resize(AltModPrf::MidSize, u0.cols());
        MC_AWAIT(mod2(u0, u1, v, sock));

        if (mDebug)
        {
            mDebugV = v;
        }

        // Compute y = B * v
        compressB(v, y);

        // cleanup
        if (pre.handle())
            MC_AWAIT(pre);
        mHasPrepro = false;
        mOleReq = {};
        mKeyReq = {};
        mInputSize = 0;

        MC_END();
    }




    // The parties input a sharing of the u=(u0,u1) such that 
    // 
    //   u = u0 + u1 mod 3
    // 
    // When looking at the truth table of u mod 2 we have
    // 
    //           u1
    //          0 1 2
    //         ________
    //      0 | 0 1 0
    //   u0 1 | 1 0 0 
    //      2 | 0 0 1
    //   
    // Logically, what we are going to do is a 1-out-of-3
    // OT. The PrfSender with u0 will use select the row of
    // this table based on u0. For example, if u0=0 then the 
    // truth table reduces to
    // 
    //   0 1 0
    // 
    // where the PrfReceiver should use the OT to pick up
    // the element indexed bu u1. For example, they should pick up 
    // 1 iff u1 = 1.
    // 
    // To maintain security, we need to give the PrfReceiver a sharing
    // of this value, not the value itself. The PrfSender will pick a random mask
    // 
    //   r
    // 
    // and then the PrfReceiver should learn that table above XOR r.
    // 
    // We can build a 1-out-of-3 OT from OLE. Each mod2 / 1-out-of-3 OT consumes 2 
    // binary OLE's. A single OLE consists of PrfSender holding 
    // 
    //     x0, y0
    // 
    // the PrfReceiver holding
    // 
    //     x1, y1
    // 
    // such that (x0+x1) = (y0*y1). We will partially derandomize these
    // by allowing the PrfReceiver to change their y1 to be a chosen
    // value, c. That is, the parties want to hold correlation
    // 
    //    (x0',y0',x1', c)
    // 
    // where x0,x1,y0 are random and c is chosen. This is done by sending
    // 
    //   d = (y1+c)
    // 
    // and the PrfSender updates their share as
    // 
    //   x0' = x0 + y0 * d
    // 
    // The parties output (x0',y0,x1,c). Observe parties hold the correlation
    // 
    //   x1 + x0'                   = y0 * c
    //   x1 + x0 + y0 * d           = y0 * c
    //   x1 + x0 + y0 * (y1+c)      = y0 * c
    //   x1 + x0 + y0 * y1 + y0 * c = y0 * c
    //                       y0 * c = y0 * c
    //
    // Ok, now let us perform the mod 2 operations. As state, this 
    // will consume 2 OLEs. Let these be denoted as
    // 
    //   (x0,x1,y0,y1), (x0',x1',y0',y1')
    //
    // These have not been derandomized yet. To get an 1-out-of-3 OT, we
    // will derandomize them using the PrfReceiver's u1. This value is 
    // represented using two bits (u10,u11) such that u1 = u10 + 2 * u11
    // 
    // We will derandomize (x0,x1,y0,y1) using u10 and (x0',x1',y0',y1') 
    // using u11. Let us redefine these correlations as 
    // 
    //   (x0,x1,y0,u10), (x0',x1',y0',u11)
    // 
    // That is, we also redefined x0,x0' accordingly.
    // 
    // We will define the random OT strings (in tms case x1 single bit)
    // as 
    //    
    //    m0 = x0      + x0'
    //    m1 = x0 + y0 + x0'
    //    m2 = x0      + x0' + y0'
    // 
    // Note that 
    //  - when u10 = u11 = 0, then x1=x0, x1'=x0' and therefore
    //    the PrfReceiver knows m0. m1,m2 is uniform due to y0, y0' being 
    //    uniform, respectively. 
    // - When u10 = 1, u11 = 0, then x1 = x0 + y0 and x1' = x0 and therefore 
    //   PrfReceiver knows m1 = x1 + x1' = (x0 + y0) + x0'. m0 is uniform because 
    //   x0 = x1 + y0 and y0 is uniform given x1. m2 is uniform because y0' 
    //   is uniform given x1. 
    // - Finally, when u10 = 0, u11 = 1, the same basic case as above applies.
    // 
    // 
    // these will be used to mask the truth table T. That is, the PrfSender
    // will sample x1 mask r and send
    // 
    //   t0 = m0 + T[u0, 0] + r
    //   t1 = m1 + T[u0, 1] + r
    //   t2 = m2 + T[u0, 2] + r
    // 
    // The PrfReceiver can therefore compute
    // 
    //   z1 = t_u1 + m_u1
    //      = T[u1, u1] + r
    //      = (u mod 2) + r
    // 
    // and the PrfSender can compute
    //  
    //   z0 = r
    // 
    // and therefor we have z0+z1 = u mod 2.
    // 
    // As an optimization, we dont need to send t0. The idea is that if
    // the receiver want to learn T[u0, 0], then they can set their share
    // as m0. That is,
    // 
    //   z1 = (u1 == 0) ? m0 : t_u1 + m_u1
    //      = u10 * t_u1 + mu1
    // 
    // The sender now needs to define r appropriately. In particular, 
    // 
    //   r = m0 + T[u0, 0]
    // 
    // In the case of u1 = 0, z1 = m0, z0 = r + T[u0,0], and therefore we 
    // get the right result. The other cases are the same with the randomness
    // of the mast r coming from m0.
    // 
    //
    // u has 256 rows
    // each row holds 2bit values
    // 
    // out will have 256 rows.
    // each row will hold packed bits.
    // 
    macoro::task<> AltModPrfSender::mod2(
        oc::MatrixView<oc::block> u0,
        oc::MatrixView<oc::block> u1,
        oc::MatrixView<oc::block> out,
        coproto::Socket& sock)
    {
        MC_BEGIN(macoro::task<>, this, u0, u1, out, &sock,
            triple = BinOle{},
            tIter = std::vector<BinOle>::iterator{},
            tIdx = u64{},
            tSize = u64{},
            i = u64{},
            j = u64{},
            rows = u64{},
            cols = u64{},
            step = u64{},
            end = u64{},
            buff = oc::AlignedUnVector<oc::block>{},
            outIter = (oc::block*)nullptr,
            u0Iter = (oc::block*)nullptr,
            u1Iter = (oc::block*)nullptr
        );

        if (out.rows() != u0.rows())
            throw RTE_LOC;
        if (out.rows() != u1.rows())
            throw RTE_LOC;
        if (out.cols() != u0.cols())
            throw RTE_LOC;
        if (out.cols() != u1.cols())
            throw RTE_LOC;

        tIdx = 0;
        tSize = 0;
        rows = u0.rows();
        cols = u0.cols();

        outIter = out.data();
        u0Iter = u0.data();
        u1Iter = u1.data();
        for (i = 0; i < rows; ++i)
        {
            for (j = 0; j < cols; )
            {

                if (tIdx == tSize)
                {
                    MC_AWAIT(mOleReq.get(triple));

                    tSize = triple.mAdd.size();
                    tIdx = 0;
                    buff.resize(tSize);
                    MC_AWAIT(sock.recv(buff));
                }


                // we have (cols - j) * 128 elems left in the row.
                // we have (tSize - tIdx) * 128 oles left
                // each elem requires 2 oles
                // 
                // so the amount we can do this iteration is 
                step = std::min<u64>(cols - j, (tSize - tIdx) / 2);
                end = step + j;
                for (; j < end; j += 1, tIdx += 2)
                {
                    // we have x1, y1, s.t. x0 + x1 = y0 * y1
                    auto x = &triple.mAdd.data()[tIdx];
                    auto y = &triple.mMult.data()[tIdx];
                    auto d = &buff.data()[tIdx];

                    // x1[0] = x1[0] ^ (y1[0] * d[0])
                    //       = x1[0] ^ (y1[0] * (u0[0] ^ y0[0]))
                    //       = x1[0] ^ (y[0] * u0[0])
                    for (u64 k = 0; k < 2; ++k)
                    {
                        x[k] = x[k] ^ (y[k] & d[k]);
                    }

                    oc::block m0, m1, m2, t0, t1, t2;
                    m0 = x[0] ^ x[1];
                    m1 = m0 ^ y[0];
                    m2 = m0 ^ y[1];

                    //           u1
                    //          0 1 2
                    //         ________
                    //      0 | 0 1 0
                    //   u0 1 | 1 0 0 
                    //      2 | 0 0 1
                    //t0 = hi0 + T[u0,0] 
                    //   = hi0 + u0(i,j)  

                    assert(u0Iter == &u0(i, j));
                    assert(u1Iter == &u1(i, j));
                    assert((u0(i, j) & u1(i, j)) == oc::ZeroBlock);

                    t0 = m0 ^ *u0Iter;
                    t1 = t0 ^ m1 ^ *u0Iter ^ *u1Iter ^ oc::AllOneBlock;
                    t2 = t0 ^ m2 ^ *u1Iter;

                    ++u0Iter;
                    ++u1Iter;

                    //if (mDebug)// && i == mPrintI && (j == (mPrintJ / 128)))
                    //{
                    //    auto mPrintJ = 0;
                    //    auto bitIdx = mPrintJ % 128;
                    //    std::cout << j << " m  " << bit(m0, bitIdx) << " " << bit(m1, bitIdx) << " " << bit(m2, bitIdx) << std::endl;
                    //    std::cout << j << " u " << bit(u1(i, j), bitIdx) << bit(u0(i, j), bitIdx) << " = " <<
                    //        (bit(u1(i, j), bitIdx) * 2 + bit(u0(i, j), bitIdx)) << std::endl;
                    //    std::cout << j << " r  " << bit(m0, bitIdx) << std::endl;
                    //    std::cout << j << " t  " << bit(t0, bitIdx) << " " << bit(t1, bitIdx) << " " << bit(t2, bitIdx) << std::endl;
                    //}

                    // r
                    assert(outIter == &out(i, j));
                    *outIter++ = t0;
                    d[0] = t1;
                    d[1] = t2;
                }

                if (tIdx == tSize)
                {
                    MC_AWAIT(sock.send(std::move(buff)));
                }
            }
        }

        assert(buff.size() == 0);

        MC_END();
    }




    macoro::task<> AltModPrfReceiver::mod2(
        oc::MatrixView<oc::block> u0,
        oc::MatrixView<oc::block> u1,
        oc::MatrixView<oc::block> out,
        coproto::Socket& sock)
    {
        MC_BEGIN(macoro::task<>, this, u0, u1, out, &sock,
            triple = std::vector<BinOle>{},
            tIter = std::vector<BinOle>::iterator{},
            tIdx = u64{},
            tSize = u64{},
            i = u64{},
            j = u64{},
            step = u64{},
            rows = u64{},
            end = u64{},
            cols = u64{},
            buff = oc::AlignedUnVector<oc::block>{},
            ww = oc::AlignedUnVector<block256>{},
            add = span<oc::block>{},
            mlt = span<oc::block>{},
            outIter = (oc::block*)nullptr,
            u0Iter = (oc::block*)nullptr,
            u1Iter = (oc::block*)nullptr
        );

        triple.reserve(mOleReq.batchCount());
        tIdx = 0;
        tSize = 0;
        
        // the format of u is that it should have AltModPrf::MidSize rows.
        rows = u0.rows();

        // cols should be the number of inputs.
        cols = u0.cols();

        // we are performing mod 2. u0 is the lsb, u1 is the msb. these are packed into 128 bit blocks. 
        // we then have a matrix of these with `rows` rows and `cols` columns. We mod requires
        // 2 OLEs. So in total we need rows * cols * 128 * 2 OLEs.
        assert(mOleReq.size() == rows * cols * 128 * 2);

        u0Iter = u0.data();
        u1Iter = u1.data();
        for (i = 0; i < rows; ++i)
        {
            for (j = 0; j < cols;)
            {

                if (tSize == tIdx)
                {

                    triple.emplace_back();
                    MC_AWAIT(mOleReq.get(triple.back()));

                    tSize = triple.back().mAdd.size();
                    tIdx = 0;
                    buff.resize(tSize);
                }

                step = std::min<u64>(cols - j, (tSize - tIdx) / 2);
                end = step + j;

                for (; j < end; ++j, tIdx += 2)
                {
                    auto y = &triple.back().mMult.data()[tIdx];
                    auto b = &buff.data()[tIdx];
                    assert(u0Iter == &u0(i, j));
                    assert(u1Iter == &u1(i, j));
                    b[0] = *u0Iter ^ y[0];
                    b[1] = *u1Iter ^ y[1];

                    ++u0Iter;
                    ++u1Iter;
                }

                if (tSize == tIdx)
                {
                    MC_AWAIT(sock.send(std::move(buff)));
                    TODO("clear mult");
                    //triple.back().clear();
                }
            }
        }

        if (buff.size())
            MC_AWAIT(sock.send(std::move(buff)));

        tIdx = 0;
        tSize = 0;
        tIter = triple.begin();
        outIter = out.data();
        u0Iter = u0.data();
        u1Iter = u1.data();
        for (i = 0; i < rows; ++i)
        {
            for (j = 0; j < cols; )
            {

                if (tIdx == tSize)
                {

                    tIdx = 0;
                    tSize = tIter->mAdd.size();
                    add = tIter->mAdd;
                    ++tIter;
                    buff.resize(tSize);
                    MC_AWAIT(sock.recv(buff));
                }

                step = std::min<u64>(cols - j, (tSize - tIdx) / 2);
                end = step + j;

                for (; j < end; ++j, tIdx += 2)
                {

                    assert((u0(i, j) & u1(i, j)) == oc::ZeroBlock);
                    assert(u0Iter == &u0(i, j));
                    assert(u1Iter == &u1(i, j));

                    // if u = 0, w = hi0
                    // if u = 1, w = hi1 + t1
                    // if u = 2, w = m2 + t2
                    oc::block w =
                        (*u0Iter++ & buff.data()[tIdx + 0]) ^ // t1
                        (*u1Iter++ & buff.data()[tIdx + 1]) ^ // t2
                        add.data()[tIdx + 0] ^ add.data()[tIdx + 1];// m_u

                    //if (mDebug)// && i == mPrintI && (j == (mPrintJ / 128)))
                    //{
                    //    auto mPrintJ = 0;
                    //    auto bitIdx = mPrintJ % 128;
                    //    std::cout << j << " u " << bit(u1(i, j), bitIdx) << bit(u0(i, j), bitIdx) << " = " <<
                    //        (bit(u1(i, j), bitIdx) * 2 + bit(u0(i, j), bitIdx)) << std::endl;
                    //    std::cout << j << " t  _ " << bit(buff[tIdx + 0], bitIdx) << " " << bit(buff[tIdx + 1], bitIdx) << std::endl;
                    //    std::cout << j << " w  " << bit(w, bitIdx) << std::endl;
                    //}

                    assert(outIter == &out(i, j));
                    *outIter++ = w;
                }
            }
        }

        MC_END();
    }
}