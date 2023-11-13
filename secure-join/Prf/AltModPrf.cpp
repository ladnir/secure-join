

#include "AltModPrf.h"
#include "secure-join/AggTree/PerfectShuffle.h"

#define AltMod_NEW

namespace secJoin
{


    const std::array<block, 128> AltModPrf::mB = []() {
        std::array<block, 128> r;
        memset(&r, 0, sizeof(r));
        PRNG prng(block(2134, 5437));
        for (u64 i = 0; i < r.size(); ++i)
        {
            //*oc::BitIterator(r[i].mData[0].data(), i) = 1;
            r[i] = prng.get();
        }
        return r;
        }();


        F2LinearCode AltModPrf::mBCode = []() {

            oc::Matrix<u8> g(128, sizeof(block)), gt(128, sizeof(block));
            g.resize(128, sizeof(block));
            for (u64 i = 0; i < 128; ++i)
                memcpy(g[i], span<const block>(&mB[i], 1));
            oc::transpose(g, gt);
            F2LinearCode r;
            r.init(gt);
            return r;
            }();


            F3AccPermCode AltModPrf::mACode = []() {

                F3AccPermCode r;
                r.init(AltModPrf::KeySize, AltModPrf::MidSize);
                return r;
                }();

                //const std::array<block256, 128> AltModPrf::mBShuffled = []() {

                //    std::array<block256, 128> shuffled;
                //    for (u64 i = 0; i < shuffled.size(); ++i)
                //    {
                //        auto iter0 = oc::BitIterator((u8*)&mB[i].mData[0]);
                //        auto iter1 = oc::BitIterator((u8*)&mB[i].mData[1]);
                //        auto dest = oc::BitIterator((u8*)&shuffled[i]);
                //        for (u64 j = 0; j < 128; ++j)
                //        {
                //            *dest++ = *iter0++;
                //            *dest++ = *iter1++;
                //        }
                //    }
                //    return shuffled;
                //}();

                const std::array<std::array<u8, 128>, 128> AltModPrf::mBExpanded = []() {

                    std::array<std::array<u8, 128>, 128> r;
                    for (u64 i = 0; i < mB.size(); ++i)
                    {
                        auto iter0 = oc::BitIterator((u8*)&mB[i]);
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
                        oc::MatrixView<block> v,
                        span<block> y
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


                        oc::Matrix<block> yt(128, n128);

                        //auto B = AltModPrf::mB;
                        assert(begin % 128 == 0);
                        // assert(n % 128 == 0);
                        assert(v.rows() == AltModPrf::MidSize);
                        assert(v.cols() >= begin128 + n128);
                        assert(y.size() >= begin + n);

                        auto vStep = v.cols();
                        auto ytIter = yt.data();
                        auto ytstep = yt.cols();
                        auto vSize = n128 * sizeof(block);

                        for (u64 i = 0; i < 128; ++i)
                        {
                            //while (AltModPrf::mBExpanded[i][j] == 0)
                            //    ++j;

                            auto vIter = v.data() + begin128 + i * vStep;
                            assert(yt[i].data() == ytIter);
                            assert(v[i].subspan(begin128).data() == vIter);
                            memcpy(ytIter, vIter, vSize);
                            //vIter += vStep;
                            //++j;

                            //memcpy(yt[i], v[j++].subspan(begin128, n128));
                            u64 j = 128;
                            vIter = v.data() + begin128 + j * vStep;
                            while (j < 256)
                            {
                                if (AltModPrf::mBExpanded[i][j - 128])
                                {
                                    assert(yt[i].data() == ytIter);
                                    assert(vIter == v[j].data() + begin128);
                                    block* __restrict yti = ytIter;
                                    block* __restrict vj = vIter;
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

                        oc::AlignedArray<block, 128> tt;
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

                            memcpy(y.data() + i + begin, tt.data(), m * sizeof(block));
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
                        oc::MatrixView<block> v,
                        span<block> y
                    )
                    {
                        if (1)
                        {
                            // rownd down
                            auto n = y.size();

                            oc::AlignedArray<block, 128> tt, yy;
                            //auto v1 = v[1];
                            block* v0Iter = v[0].data();
                            block* v1Iter = v[128].data();
                            auto vStep = v.cols();
                            u64 i = 0, ii = 0;

                            for (; i < n; i += 128, ++ii)
                            {
                                for (u64 j = 0; j < 128; ++j)
                                {
                                    yy[j] = v0Iter[j * vStep];
                                }
                                ++v0Iter;

                                oc::transpose128(yy);

                                for (u64 j = 0; j < 128; ++j)
                                {
                                    tt[j] = v1Iter[j * vStep];
                                }
                                ++v1Iter;

                                oc::transpose128(tt.data());

                                if (i + 128 < n)
                                {
                                    auto yIter = y.data() + i;
                                    for (u64 j = 0; j < 128; ++j)
                                    {
                                        AltModPrf::mBCode.encode((u8*)(tt.data() + j), (u8*)(tt.data() + j));
                                        yIter[j] = yy[j] ^ tt[j];
                                    }
                                }
                                else
                                {
                                    auto m = n - i;
                                    auto yIter = y.data() + i;
                                    for (u64 j = 0; j < m; ++j)
                                    {
                                        AltModPrf::mBCode.encode((u8*)(tt.data() + j), (u8*)(tt.data() + j));
                                        yIter[j] = yy[j] ^ tt[j];
                                    }
                                }
                            }


                        }
                        else
                        {

                            u64 batch = 1ull << 12;
                            auto n = y.size();
                            for (u64 i = 0; i < n; i += batch)
                            {
                                auto m = std::min<u64>(batch, n - i);
                                compressB(i, m, v, y);
                            }
                        }
                    }

                    // z =  x + y mod 3
                    void mod3Add(
                        span<block> z1, span<block> z0,
                        span<block> x1, span<block> x0,
                        span<block> y1, span<block> y0)
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
                            z1.data()[i] = (y0i ^ x0i).andnot_si128(x1x0 ^ y1i);
                            z0.data()[i] = (x1i ^ y1i).andnot_si128(x1x0 ^ y0i);
                        }
                    }

                    // (ab) += y0 mod 3
                    // we treat binary x1 as the MSB and binary x0 as lsb.
                    // That is, for bits, x1 x0 y0, we sets 
                    //   t = x1 * 2 + x0 + y0
                    //   x1 = t / 2
                    //   x0 = t % 2
                    void mod3Add(
                        span<block> z1, span<block> z0,
                        span<block> x1, span<block> x0,
                        span<block> y0)
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

                            z1.data()[i] = zz1;
                            z0.data()[i] = zz0;

                        }
                    }



                    inline void sampleMod3(PRNG& prng, span<u8> mBuffer)
                    {
                        auto n = mBuffer.size();
                        auto dst = mBuffer.data();
                        block m[8], t[8], eq[8];
                        block allOne = oc::AllOneBlock;
                        block block1 = block::allSame<u16>(1);
                        block block3 = block::allSame<u16>(3);

                        static constexpr int batchSize = 16;
                        std::array<std::array<block, 8>, 64> buffer;
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

                    void sampleMod3(PRNG& prng, span<block> msb, span<block> lsb, oc::AlignedUnVector<u8>& b)
                    {
                        b.resize(msb.size() * 128);
                        sampleMod3(prng, b);
                        block block1 = block::allSame<u8>(1);
                        block block2 = block::allSame<u8>(2);

                        for (u64 i = 0; i < msb.size(); ++i)
                        {
                            auto bb = (block*)&b.data()[i * 128];
                            assert((u64)bb % 16 == 0);

                            block tt[8];
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

                    // generated using buildMod3Tabel. Each of the first 243=3^5 positions
                    // contains 5 mod-3 values. the first byte of each entry contains the 
                    //5 lsb. the second byte contains the 5 msb. The third byte contains a 
                    // zero-one flag indicating if this sample less that 243 ad therefore
                    // valid. The idea of the sample is that it takes as input a random byte 
                    // and returns 5 random mod3 values or bot.
                    static const std::array<u32, 256> mod3Table =
                    { {
                        0x10000, 0x10001, 0x10100, 0x10002, 0x10003, 0x10102, 0x10200, 0x10201,
                        0x10300, 0x10004, 0x10005, 0x10104, 0x10006, 0x10007, 0x10106, 0x10204,
                        0x10205, 0x10304, 0x10400, 0x10401, 0x10500, 0x10402, 0x10403, 0x10502,
                        0x10600, 0x10601, 0x10700, 0x10008, 0x10009, 0x10108, 0x1000a, 0x1000b,
                        0x1010a, 0x10208, 0x10209, 0x10308, 0x1000c, 0x1000d, 0x1010c, 0x1000e,
                        0x1000f, 0x1010e, 0x1020c, 0x1020d, 0x1030c, 0x10408, 0x10409, 0x10508,
                        0x1040a, 0x1040b, 0x1050a, 0x10608, 0x10609, 0x10708, 0x10800, 0x10801,
                        0x10900, 0x10802, 0x10803, 0x10902, 0x10a00, 0x10a01, 0x10b00, 0x10804,
                        0x10805, 0x10904, 0x10806, 0x10807, 0x10906, 0x10a04, 0x10a05, 0x10b04,
                        0x10c00, 0x10c01, 0x10d00, 0x10c02, 0x10c03, 0x10d02, 0x10e00, 0x10e01,
                        0x10f00, 0x10010, 0x10011, 0x10110, 0x10012, 0x10013, 0x10112, 0x10210,
                        0x10211, 0x10310, 0x10014, 0x10015, 0x10114, 0x10016, 0x10017, 0x10116,
                        0x10214, 0x10215, 0x10314, 0x10410, 0x10411, 0x10510, 0x10412, 0x10413,
                        0x10512, 0x10610, 0x10611, 0x10710, 0x10018, 0x10019, 0x10118, 0x1001a,
                        0x1001b, 0x1011a, 0x10218, 0x10219, 0x10318, 0x1001c, 0x1001d, 0x1011c,
                        0x1001e, 0x1001f, 0x1011e, 0x1021c, 0x1021d, 0x1031c, 0x10418, 0x10419,
                        0x10518, 0x1041a, 0x1041b, 0x1051a, 0x10618, 0x10619, 0x10718, 0x10810,
                        0x10811, 0x10910, 0x10812, 0x10813, 0x10912, 0x10a10, 0x10a11, 0x10b10,
                        0x10814, 0x10815, 0x10914, 0x10816, 0x10817, 0x10916, 0x10a14, 0x10a15,
                        0x10b14, 0x10c10, 0x10c11, 0x10d10, 0x10c12, 0x10c13, 0x10d12, 0x10e10,
                        0x10e11, 0x10f10, 0x11000, 0x11001, 0x11100, 0x11002, 0x11003, 0x11102,
                        0x11200, 0x11201, 0x11300, 0x11004, 0x11005, 0x11104, 0x11006, 0x11007,
                        0x11106, 0x11204, 0x11205, 0x11304, 0x11400, 0x11401, 0x11500, 0x11402,
                        0x11403, 0x11502, 0x11600, 0x11601, 0x11700, 0x11008, 0x11009, 0x11108,
                        0x1100a, 0x1100b, 0x1110a, 0x11208, 0x11209, 0x11308, 0x1100c, 0x1100d,
                        0x1110c, 0x1100e, 0x1100f, 0x1110e, 0x1120c, 0x1120d, 0x1130c, 0x11408,
                        0x11409, 0x11508, 0x1140a, 0x1140b, 0x1150a, 0x11608, 0x11609, 0x11708,
                        0x11800, 0x11801, 0x11900, 0x11802, 0x11803, 0x11902, 0x11a00, 0x11a01,
                        0x11b00, 0x11804, 0x11805, 0x11904, 0x11806, 0x11807, 0x11906, 0x11a04,
                        0x11a05, 0x11b04, 0x11c00, 0x11c01, 0x11d00, 0x11c02, 0x11c03, 0x11d02,
                        0x11e00, 0x11e01, 0x11f00, 0x0, 0x0, 0x0, 0x0, 0x0,
                        0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0
                } };


                    void buildMod3Tabel()
                    {
                        std::array<u32, 256> ret;
                        auto m = 243;
                        auto s = 5;
                        u32 vals[5];
                        for (u64 j = 0; j < s; ++j)
                            vals[j] = 0;
                        for (u64 i = 0; i < 256; ++i)
                        {
                            if (i < m)
                            {
                                u32 lsb = 0;
                                u32 msb = 0;

                                for (u64 j = 0; j < s; ++j)
                                {
                                    auto lsbj = vals[j] & 1;
                                    auto msbj = (vals[j] >> 1) & 1;
                                    lsb |= lsbj << j;
                                    msb |= msbj << j;
                                    //std::cout << vals[j] << "(" <<msbj<<"" << lsbj << "), ";
                                }
                                ret[i] = ((1 << 16) + (msb << 8) + lsb);

                                ++vals[0];
                                for (u64 j = 0; j < s; ++j)
                                {
                                    if (vals[j] == 3 && j != s - 1)
                                        vals[j + 1]++;
                                    vals[j] = vals[j] % 3;
                                }

                            }
                            else
                            {
                                ret[i] = 0;
                                //std::cout << "0," << std::endl;
                            }

                            std::cout << "0x" << std::hex << ret[i] << ",";
                            if (i % 8 == 0)
                                std::cout << std::endl;
                        }
                        //return ret;
                    };


                    void sampleMod3Lookup(PRNG& prng, span<block> msb, span<block> lsb)
                    {
                        u64 n = msb.size() * 128;
                        auto msbIter = (u64*)msb.data();
                        auto lsbIter = (u64*)lsb.data();
                        span<u8> rands = prng.getBufferSpan(256);
                        u64 rIdx = 0;
                        u64 e = rands.size();
                        for (u64 i = 0; i < n; i += 64)
                        {
                            u64 lsb = 0, msb = 0;
                            u64 j = 0;
                            while (j < 64)
                            {
                                if (rIdx == e)
                                {
                                    rands = prng.getBufferSpan(256);
                                    rIdx = 0;
                                    e = rands.size();
                                }
                                auto b = rands.data()[rIdx++];
                                //auto b = prng.get<u8>();
                                auto v = mod3Table[b];
                                auto lsbj = v & 255ull;
                                auto msbj = (v >> 8) & 255ull;
                                auto flag = v >> 16;
                                //__assume(flag <= 1);
                                lsb |= lsbj << j;
                                msb |= msbj << j;
                                j += flag * 5;
                            }
                            *lsbIter++ = lsb;
                            *msbIter++ = msb;
                        }
                    }
                    //while (j < 64)
                    //{
                    //    auto r = prng.get<u32>();
                    //    u8 bs[4], flags[4];
                    //    u32 vs[4], lsbjs[4], msbjs[4];

                    //    for (u64 k = 0;k < 4; ++k)
                    //    {
                    //        bs[k] = (r >> (8 * k)) & 255ull;
                    //        vs[k] = mod3Table[bs[k]];
                    //        lsbjs[k] = vs[k] & 255ull;
                    //        msbjs[k] = (vs[k] >> 8) & 255ull;
                    //        flags[k] = vs[0] >> 16;
                    //    }

                    //    auto lsbj = lsbjs[0] | (lsbjs[1] << 8) | (lsbjs[2] << 16) | (lsbjs[3] << 24);
                    //    auto msbj = msbjs[0] | (msbjs[1] << 8) | (msbjs[2] << 16) | (msbjs[3] << 24);

                    //    auto flag = flags[0] + flags[1] + flags[2] + flags[3];

                    //    lsb |= lsbj << j;
                    //    msb |= msbj << j;
                    //    j += flag * 5;
                    //}


                    void compare(span<block> m1, span<block> m0, span<u16> u, bool verbose = false)
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
                    void compare(span<block> m1, span<block> m0,
                        span<block> u1, span<block> u0)
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



                    // v = u + PRNG()
                    inline void xorVectorOne(span<oc::block> v, span<const oc::block> u, PRNG& prng)
                    {
                        oc::block m[8];
                        auto vIter = v.data();
                        auto uIter = u.data();
                        auto n = v.size();
                        assert(u64(v.data()) % 16 == 0);

                        auto j = 0ull;
                        auto main = n / 8 * 8;
                        for (; j < main; j += 8)
                        {
                            prng.mAes.ecbEncCounterMode(prng.mBlockIdx, 8, m);
                            prng.mBlockIdx += 8;

                            vIter[0] = uIter[0] ^ m[0];
                            vIter[1] = uIter[1] ^ m[1];
                            vIter[2] = uIter[2] ^ m[2];
                            vIter[3] = uIter[3] ^ m[3];
                            vIter[4] = uIter[4] ^ m[4];
                            vIter[5] = uIter[5] ^ m[5];
                            vIter[6] = uIter[6] ^ m[6];
                            vIter[7] = uIter[7] ^ m[7];
                            vIter += 8;
                            uIter += 8;
                        }
                        for (; j < n; ++j)
                        {
                            auto m = prng.mAes.ecbEncBlock(oc::toBlock(prng.mBlockIdx++));
                            //oc::block m = prng.get();

                            *vIter = *uIter ^ m;
                            ++vIter;
                            ++uIter;
                        }
                        assert(vIter == v.data() + v.size());
                    }



                    // out = (hi0, hi1) ^ prng()
                    inline void xorVectorOne(
                        span<block> out1,
                        span<block> out0,
                        span<block> m1,
                        span<block> m0,
                        PRNG& prng)
                    {

                        xorVectorOne(out1, m1, prng);
                        xorVectorOne(out0, m0, prng);
                    }

                    // out = (hi0, hi1) ^ prng()
                    inline void xorVectorOne(
                        span<block> out,
                        span<block> m1,
                        span<block> m0,
                        PRNG& prng)
                    {
                        xorVectorOne(out.subspan(m0.size()), m1, prng);
                        xorVectorOne(out.subspan(0, m0.size()), m0, prng);
                    }

                    void  AltModPrf::setKey(AltModPrf::KeyType k)
                    {
                        mExpandedKey = k;
                        //mExpandedKey[1] = oc::mAesFixedKey.hashBlock(k);
                        //mExpandedKey[2] = oc::mAesFixedKey.hashBlock(k ^ oc::block(4234473534532, 452878778345324));
                        //mExpandedKey[3] = oc::mAesFixedKey.hashBlock(k ^ oc::block(35746745624534, 876876787665423));

                        //static_assert(KeyType{}.size() == 4, "assumed");
                    }

                    void  AltModPrf::mtxMultA(const std::array<u16, KeySize>& hj, block256m3& uj)
                    {
                        std::array<u8, KeySize> h;
                        for (u64 i = 0; i < KeySize; ++i)
                            h[i] = hj[i];
                        mACode.encode<u8>(h, uj.mData);
                    }


                    void AltModPrf::expandInput(span<block> x, oc::MatrixView<block> xt)
                    {
                        auto n = x.size();
                        for (u64 i = 0, k = 0; i < n; ++k)
                        {
                            static_assert(AltModPrf::KeySize % 128 == 0);
                            auto m = std::min<u64>(128, n - i);
                            auto xIter = x.data() + k * 128;

                            for (u64 q = 0; q < AltModPrf::KeySize / 128; ++q)
                            {
                                auto tweak = block(q, q);
                                oc::AlignedArray<block, 128> t;
                                if (q == 0)
                                {
                                    for (u64 j = 0;j < m; ++j)
                                    {
                                        t[j] = xIter[j];
                                    }
                                }
                                else
                                {
                                    for (u64 j = 0;j < m; ++j)
                                    {
                                        t[j] = xIter[j] ^ tweak;
                                    }
                                    oc::mAesFixedKey.hashBlocks(t, t);
                                }

                                oc::transpose128(t.data());

                                auto xtk = &xt(q * 128, k);
                                auto step = xt.cols();
                                for (u64 j = 0;j < 128; ++j)
                                {
                                    assert(xtk == &xt(q * 128 + j, k));
                                    *xtk = t[j];
                                    xtk += step;
                                }
                            }


                            i += 128;
                        }


                    }

                    void AltModPrf::expandInput(block x, KeyType& X)
                    {
                        X[0] = x;
                        for (u64 i = 1; i < X.size(); ++i)
                            X[i] = x ^ block(i, i);

                        constexpr const auto rem = KeyType{}.size() - 1;
                        if (rem)
                            oc::mAesFixedKey.hashBlocks<rem>(X.data() + 1, X.data() + 1);
                    }

                    block  AltModPrf::eval(block x)
                    {
                        std::array<u16, KeySize> h;
                        std::array<block, KeySize / 128> X;

                        expandInput(x, X);

                        auto kIter = oc::BitIterator((u8*)mExpandedKey.data());
                        auto xIter = oc::BitIterator((u8*)X.data());
                        for (u64 i = 0; i < KeySize; ++i)
                        {
                            h[i] = *kIter & *xIter;
                            ++kIter;
                            ++xIter;
                        }

                        block256m3 u;
                        mtxMultA(h, u);

                        block256 w;
                        for (u64 i = 0; i < u.mData.size(); ++i)
                        {
                            *oc::BitIterator((u8*)&w, i) = u.mData[i] % 2;
                        }
                        return compress(w);
                    }

                    void AltModPrf::eval(span<block> x, span<block> y)
                    {
                        //for (u64 i = 0; i < x.size(); ++i)
                        //    y[i] = eval(x[i]);
                        //return;

                        oc::Matrix<block> xt, xk0, xk1, u0, u1;

                        // we need x in a transformed format so that we can do SIMD operations.
                        xt.resize(AltModPrf::KeySize, oc::divCeil(y.size(), 128));
                        AltModPrf::expandInput(x, xt);

                        xk0.resize(AltModPrf::KeySize, oc::divCeil(x.size(), 128), oc::AllocType::Uninitialized);
                        xk1.resize(AltModPrf::KeySize, oc::divCeil(x.size(), 128), oc::AllocType::Uninitialized);
                        for (u64 i = 0; i < KeySize; ++i)
                        {
                            if (bit(mExpandedKey, i))
                            {
                                memcpy(xk0[i], xt[i]);
                            }
                            else
                                memset(xk0[i], 0);

                            memset(xk1[i], 0);
                        }

                        u0.resize(AltModPrf::MidSize, oc::divCeil(x.size(), 128), oc::AllocType::Uninitialized);
                        u1.resize(AltModPrf::MidSize, oc::divCeil(x.size(), 128), oc::AllocType::Uninitialized);

                        AltModPrf::mACode.encode(xk1, xk0, u1, u0);

                        compressB(u0, y);

                    }



                    block  AltModPrf::compress(block256& w)
                    {
                        return compress(w, mB);
                    }


                    block  AltModPrf::compress(block256& w, const std::array<block, 128>& B)
                    {
                        oc::AlignedArray<block, 128> bw;

                        for (u64 i = 0; i < 128; ++i)
                        {
                            //bw[0][i] = B[i].mData[0] & w.mData[0];
                            bw[i] = B[i] & w.mData[1];
                        }
                        oc::transpose128(bw.data());
                        //oc::transpose128(bw[1].data());

                        block r = w[0];
                        //memset(&r, 0, sizeof(r));
                        //for (u64 i = 0; i < 128; ++i)
                        //    r = r ^ bw[0][i];
                        for (u64 i = 0; i < 128; ++i)
                            r = r ^ bw[i];

                        return r;
                    }



                    void mtxMultA(
                        oc::Matrix<block>&& v1,
                        oc::Matrix<block>&& v0,
                        oc::Matrix<block>& u1,
                        oc::Matrix<block>& u0
                    )
                    {
                        AltModPrf::mACode.encode(v1, v0, u1, u0);
                        v0 = {};
                        v1 = {};
                    }


                    void AltModPrfSender::setKeyOts(AltModPrf::KeyType k, span<block> ots)
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

                    void mod3BitDecompostion(oc::MatrixView<u16> u, oc::MatrixView<block> u0, oc::MatrixView<block> u1)
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
                        span<block> y,
                        coproto::Socket& sock,
                        PRNG& prng,
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
                        span<block> y,
                        coproto::Socket& sock,
                        PRNG& prng)
                    {

                        MC_BEGIN(coproto::task<>, y, this, &sock,
                            buffer = oc::AlignedUnVector<u8>{},
                            f = oc::BitVector{},
                            diff = oc::BitVector{},
                            ots = oc::AlignedUnVector<std::array<block, 2>>{},
                            i = u64{},
                            ole = BinOleRequest{},
                            u0 = oc::Matrix<block>{},
                            u1 = oc::Matrix<block>{},
                            uu0 = oc::Matrix<block>{},
                            uu1 = oc::Matrix<block>{},
                            v = oc::Matrix<block>{},
                            xk0 = oc::Matrix<block>{},
                            xk1 = oc::Matrix<block>{},
                            msg = oc::Matrix<block>{},
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
                            if (oc::divCeil(otRecv.mChoice.size(), 8) != sizeof(AltModPrf::KeyType))
                                throw RTE_LOC;
                            auto k = otRecv.mChoice.getSpan<AltModPrf::KeyType>()[0];
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
                                auto ki = *oc::BitIterator((u8*)&mPrf.mExpandedKey, i);
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
                        xk0.resize(AltModPrf::KeySize, oc::divCeil(y.size(), 128), oc::AllocType::Uninitialized);
                        xk1.resize(AltModPrf::KeySize, oc::divCeil(y.size(), 128), oc::AllocType::Uninitialized);
                        for (i = 0; i < AltModPrf::KeySize;)
                        {
                            msg.resize(StepSize, xk0.cols() * 2); // y.size() * 256 * 2 bits
                            MC_AWAIT(sock.recv(msg));
                            for (u64 k = 0; k < StepSize; ++i, ++k)
                            {
                                u8 ki = *oc::BitIterator((u8*)&mPrf.mExpandedKey, i);

                                auto lsbShare = xk0[i];
                                auto msbShare = xk1[i];
                                if (ki)
                                {
                                    auto msbMsg = msg[k].subspan(0, msbShare.size());
                                    auto lsbMsg = msg[k].subspan(msbShare.size(), lsbShare.size());

                                    // ui = (hi1,hi0)                                   ^ G(OT(i,1))
                                    //    = { [ G(OT(i,0))  + x  mod 3 ] ^ G(OT(i,1)) } ^ G(OT(i,1))
                                    //    =     G(OT(i,0))  + x  mod 3 
                                    xorVectorOne(msbShare, msbMsg, mKeyOTs[i]);
                                    xorVectorOne(lsbShare, lsbMsg, mKeyOTs[i]);
                                }
                                else
                                {
                                    // ui = (hi1,hi0)         
                                    //    = G(OT(i,0))  mod 3 
                                    sampleMod3Lookup(mKeyOTs[i], msbShare, lsbShare);
                                }
                            }
                        }

                        if (mDebug)
                        {
                            mDebugXk0 = xk0;
                            mDebugXk1 = xk1;
                        }

                        // Compute u = H * xkShare mod 3
                        buffer = {};
                        u0.resize(AltModPrf::MidSize, oc::divCeil(y.size(), 128), oc::AllocType::Uninitialized);
                        u1.resize(AltModPrf::MidSize, oc::divCeil(y.size(), 128), oc::AllocType::Uninitialized);
                        mtxMultA(std::move(xk1), std::move(xk0), u1, u0);

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

                    void AltModPrfReceiver::setKeyOts(span<std::array<block, 2>> ots)
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
                        span<block> x,
                        span<block> y,
                        coproto::Socket& sock,
                        PRNG& prng,
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
                        span<block> x,
                        span<block> y,
                        coproto::Socket& sock,
                        PRNG& prng)
                    {
                        MC_BEGIN(coproto::task<>, x, y, this, &sock,
                            xt = oc::Matrix<block>{},
                            buffer = oc::AlignedUnVector<u8>{},
                            gx = oc::AlignedUnVector<u16>{},
                            i = u64{},
                            baseOts = oc::AlignedUnVector<std::array<block, 2>>{},
                            u0 = oc::Matrix<block>{},
                            u1 = oc::Matrix<block>{},
                            uu0 = oc::Matrix<block>{},
                            uu1 = oc::Matrix<block>{},
                            xk0 = oc::Matrix<block>{},
                            xk1 = oc::Matrix<block>{},
                            msg = oc::Matrix<block>{},
                            v = oc::Matrix<block>{},
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
                        xt.resize(AltModPrf::KeySize, oc::divCeil(y.size(), 128));
                        AltModPrf::expandInput(x, xt);

                        static_assert(AltModPrf::KeySize % StepSize == 0, "we dont handle remainders. Should be true.");

                        // for each bit of the key, perform an OT derandomization where we get a share
                        // of the input x times the key mod 3. We store the LSB and MSB of the share separately.
                        // Hence we need 2 * AltModPrf::KeySize rows in xkShares
                        xk0.resize(AltModPrf::KeySize, oc::divCeil(x.size(), 128), oc::AllocType::Uninitialized);
                        xk1.resize(AltModPrf::KeySize, oc::divCeil(x.size(), 128), oc::AllocType::Uninitialized);
                        for (i = 0; i < AltModPrf::KeySize;)
                        {
                            assert(AltModPrf::KeySize % StepSize == 0);
                            msg.resize(StepSize, xk0.cols() * 2);

                            for (u64 k = 0; k < StepSize; ++i, ++k)
                            {
                                // we store them in swapped order to negate the value.
                                auto msbShare = xk0[i];
                                auto lsbShare = xk1[i];
                                sampleMod3Lookup(mKeyOTs[i][0], msbShare, lsbShare);

                                auto msbMsg = msg[k].subspan(0, msbShare.size());
                                auto lsbMsg = msg[k].subspan(msbShare.size(), lsbShare.size());

                                // # hi = G(OT(i,0)) + x mod 3
                                mod3Add(
                                    msbMsg, lsbMsg,
                                    msbShare, lsbShare,
                                    xt[i]);

                                // ## msg = m ^ G(OT(i,1))
                                //xorVector(msg[k], mKeyOTs[i][1]);
                                xorVector(msbMsg, mKeyOTs[i][1]);
                                xorVector(lsbMsg, mKeyOTs[i][1]);
                            }

                            MC_AWAIT(sock.send(std::move(msg)));
                        }
                        if (mDebug)
                        {
                            mDebugXk0 = xk0;
                            mDebugXk1 = xk1;
                        }

                        // Compute u = H * xkShare mod 3
                        buffer = {};
                        u0.resize(AltModPrf::MidSize, oc::divCeil(x.size(), 128), oc::AllocType::Uninitialized);
                        u1.resize(AltModPrf::MidSize, oc::divCeil(x.size(), 128), oc::AllocType::Uninitialized);
                        mtxMultA(std::move(xk1), std::move(xk0), u1, u0);

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
                        oc::MatrixView<block> u0,
                        oc::MatrixView<block> u1,
                        oc::MatrixView<block> out,
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
                            buff = oc::AlignedUnVector<block>{},
                            outIter = (block*)nullptr,
                            u0Iter = (block*)nullptr,
                            u1Iter = (block*)nullptr
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

                                    block m0, m1, m2, t0, t1, t2;
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
                        oc::MatrixView<block> u0,
                        oc::MatrixView<block> u1,
                        oc::MatrixView<block> out,
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
                            buff = oc::AlignedUnVector<block>{},
                            ww = oc::AlignedUnVector<block256>{},
                            add = span<block>{},
                            mlt = span<block>{},
                            outIter = (block*)nullptr,
                            u0Iter = (block*)nullptr,
                            u1Iter = (block*)nullptr
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
                                    block w =
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