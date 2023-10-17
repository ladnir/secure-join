#pragma once
#include "secure-join/config.h"
#include "secure-join/Defines.h"
#include "secure-join/Prf/DarkMatter22Prf.h"

#include "cryptoTools/Common/BitIterator.h"
#include <bitset>
#include "libOTe/Tools/Tools.h"
#include "libOTe/TwoChooseOne/Silent/SilentOtExtSender.h"
#include "libOTe/TwoChooseOne/Silent/SilentOtExtReceiver.h"
#include "libOTe/TwoChooseOne/SoftSpokenOT/SoftSpokenShOtExt.h"
#include <numeric>
namespace secJoin
{


    inline oc::AlignedUnVector<u16> samplePerm(oc::block s, u64 n)
    {
        oc::AlignedUnVector<u16> pi(n);
        std::iota(pi.begin(), pi.end(), 0);

        PRNG prng(s);
        for (u64 i = 0; i < n; ++i)
        {
            auto j = prng.get<u64>() % (n - i) + i;
            std::swap(pi[i], pi[j]);
        }
        return pi;
    }

    class DarkMatter32Prf
    {
    public:
        block256 mKey;
        std::array<block256, 256> mKeyMask;

        void setKey(block256 k)
        {
            mKey = k;
            std::array<block256, 2> zeroOne;
            memset(&zeroOne[0], 0, sizeof(zeroOne[0]));
            memset(&zeroOne[1], -1, sizeof(zeroOne[1]));

            for (u64 i = 0; i < 256; ++i)
                mKeyMask[i] = zeroOne[*oc::BitIterator((u8*)&k, i)];
        }



        oc::block eval(block256 x)
        {
            block256m3 u;
            memset(&u, 0, sizeof(u));
            for (u64 i = 0; i < mKeyMask.size(); ++i)
            {
                auto xi = x.rotate(i) & mKeyMask[i];
                u ^= xi;
            }
            block256 w = u.mod2();
            return compress(w);
        }

        static inline oc::block compress(block256 w)
        {
            return DarkMatter22Prf::compress(w);
        }
    };

    class DarkMatter32PrfSender : public oc::TimerAdapter
    {
        std::vector<PRNG> mKeyOTs;
    public:
        bool mCompressed = true;
        block256 mKey;
#ifdef SECUREJOIN_DK_USE_SILENT
        oc::SilentOtExtSender mOtSender;
#else
        oc::SoftSpokenShOtSender<> mSoftSender;
#endif
        std::vector<block256> mU2;
        std::vector<std::array<u16, 256>> mU;

        oc::AlignedUnVector<u16> mPi;

        void setKeyOts(span<oc::block> ots)
        {
            if (ots.size() != 256)
                throw RTE_LOC;
            mKeyOTs.resize(256);
            for (u64 i = 0; i < 256; ++i)
            {
                mKeyOTs[i].SetSeed(ots[i]);
            }
        }

        void setKey(block256 k)
        {
            mKey = k;
        }


        coproto::task<> evaluate(span<oc::block> y, coproto::Socket& sock, PRNG& prng)
        {
            static constexpr auto compSize = 256 / 4;

            MC_BEGIN(coproto::task<>, y, this, &sock, &prng,
                ui = oc::AlignedUnVector<u8>{},
                uu = oc::AlignedUnVector<u16>{},
                h = oc::AlignedUnVector<u16>{},
                f = oc::BitVector{},
                diff = oc::BitVector{},
                ots = oc::AlignedUnVector<std::array<oc::block, 2>>{},
                i = u64{});

            setTimePoint("DarkMatter.sender.begin");
            mU.resize(y.size());

            if (mCompressed)
            {
                //xk
                uu.resize(y.size() * 4);

                // y.size() rows, each of size 1024
                h.resize(uu.size() * 256);
                for (i = 0; i < 256; ++i)
                {
                    ui.resize(y.size()); // y.size() * 256 * 2 bits

                    MC_AWAIT(sock.recv(ui));

                    u8 ki = *oc::BitIterator((u8*)&mKey, i);
                    if (ki)
                    {
                        // ui = ui ^ H(mKeyOTs[i])
                        xorVector(ui, mKeyOTs[i]);
                        decompressMod3(uu, ui);
                    }
                    else
                    {
                        sampleMod3(mKeyOTs[i], uu);
                    }

                    for (u64 j = 0; j < y.size(); ++j)
                    {
                        for (u64 k = 0; k < 4; ++k)
                        {
                            h[j * 1024 + i * 4 + k] = uu[j * 4 + k];
                        }
                    }
                }

                if (mPi.size() == 0)
                {
                    mPi = samplePerm(oc::ZeroBlock, 1024);
                }

                for (u64 j = 0; j < y.size(); ++j)
                {
                    auto  hj = h.subspan(j * 1024, 1024);
                    for (u64 k = 1; k < 1024; ++k)
                    {
                        hj[k] += hj[k];
                    }

                    auto& uj = mU[j];
                    auto pik = mPi.data();
                    for (u64 k = 0; k < 256; ++k)
                    {
                        uj[k] =
                            hj[pik[0]] +
                            hj[pik[1]] +
                            hj[pik[2]] +
                            hj[pik[3]];
                        uj[k] %= 3;
                        pik += 4;
                    }
                }
            }
            else
            {

                uu.resize(y.size() * 256);//256
                for (i = 0; i < 256; ++i)
                {
                    ui.resize(y.size() * compSize); // y.size() * 256 * 2 bits

                    MC_AWAIT(sock.recv(ui));

                    u8 ki = *oc::BitIterator((u8*)&mKey, i);
                    if (ki)
                    {
                        // ui = ui ^ H(mKeyOTs[i])
                        xorVector(ui, mKeyOTs[i]);
                        decompressMod3(uu, ui);
                    }
                    else
                    {
                        sampleMod3(mKeyOTs[i], uu);
                    }

                    auto u = (oc::block*)mU.data();
                    auto ui = (oc::block*)uu.data();
                    for (u64 j = 0; j < y.size(); ++j)
                    {
                        for (u64 k = 0; k < 4; ++k)
                        {
                            u[0] = _mm_adds_epu16(u[0], ui[0]);
                            u[1] = _mm_adds_epu16(u[1], ui[1]);
                            u[2] = _mm_adds_epu16(u[2], ui[2]);
                            u[3] = _mm_adds_epu16(u[3], ui[3]);
                            u[4] = _mm_adds_epu16(u[4], ui[4]);
                            u[5] = _mm_adds_epu16(u[5], ui[5]);
                            u[6] = _mm_adds_epu16(u[6], ui[6]);
                            u[7] = _mm_adds_epu16(u[7], ui[7]);
                            u += 8;
                            ui += 8;
                        }
                    }
                }

                for (u64 j = 0; j < y.size(); ++j)
                {
                    for (u64 k = 0; k < 256; ++k)
                    {
                        mU[j][k] = mU[j][k] % 3;
                    }
                }
            }

            setTimePoint("DarkMatter.sender.kxMult");

            // mod 2

#ifdef SECUREJOIN_DK_USE_SILENT
            diff.resize(y.size() * 512);
            MC_AWAIT(mOtSender.silentSendInplace(prng.get(),diff.size(), prng, sock));
            MC_AWAIT(sock.recv(diff));
            setTimePoint("DarkMatter.sender.silent");
#else
            ots.resize(y.size() * 512);
            MC_AWAIT(mSoftSender.send(ots, prng, sock));
            setTimePoint("DarkMatter.sender.soft");
#endif

            {

                //mW.resize(y.size());

                f.resize(y.size() * 256 * 2);
                //auto mask = oc::AllOneBlock ^ oc::OneBlock;
                //auto uIter = oc::BitIterator((u8*)mU2.data());
                //auto u8Iter = (u8*)mW.data();
                //auto dIter = diff.begin();
                auto f16Iter = (u16*)f.data();
#if defined(SECUREJOIN_DK_USE_SILENT)
                auto d16Iter = (u16*)diff.data();
                auto bIter = mOtSender.mB.begin();

                //auto fIter = f.begin();
                //auto rIter = rKeys.begin();
                for (u64 i = 0; i < y.size(); ++i)
                {
                    block256 w;
                    auto u8Iter = (u8*)&w;

                    for (u64 j = 0; j < 256; )
                    {
                        *u8Iter = 0;
                        *f16Iter = 0;
                        auto di = *d16Iter++;
                        for (u64 k = 0; k < 8; ++k, ++j)
                        {

                            std::array<oc::block, 4> s;
                            s[0] = *bIter;
                            s[1] = *bIter; ++bIter;
                            s[2] = *bIter;
                            s[3] = *bIter; ++bIter;

                            auto d0 = ((di) ^ 1) & 1;
                            auto d1 = ((di >> 1) ^ 1) & 1;
                            di = di >> 2;

                            s[d0] = s[d0] ^ mOtSender.mDelta;
                            s[d1 + 2] = s[d1 + 2] ^ mOtSender.mDelta;

                            s[0] = s[0] & mask;
                            s[1] = s[1] & mask;
                            s[2] = s[2] & mask;
                            s[3] = s[3] & mask;

                            oc::mAesFixedKey.hashBlocks<4>(s.data(), s.data());

                            auto q0 = (s[0].get<u8>(1) ^ s[2].get<u8>(1)) & 1;
                            auto q1 = (s[1].get<u8>(1) ^ s[2].get<u8>(1)) & 1;
                            auto q2 = (s[0].get<u8>(1) ^ s[3].get<u8>(1)) & 1;

                            //  them       us
                            //         0   1   2
                            //        ___________
                            //  0    | 0   1   0
                            //  1    | 1   0   0
                            //  2    | 0   0   1

                            //  0   -> u==1
                            //  1   -> u==0
                            //  2   -> u==2

                            auto uij = mU.data()[i].data()[j];
                            auto t0 = q0 ^ (uij == 1);
                            auto t1 = t0 ^ (uij == 0);
                            auto t2 = t0 ^ (uij == 2);
                            assert(t0 < 2);
                            *u8Iter |= t0 << k;
                            //*uIter++ = t0;
                            //*fIter++ = q1 ^ t1;
                            //*fIter++ = q2 ^ t2;
                            auto b1 = q1 ^ t1;
                            auto b2 = (q2 ^ t2) << 1;
                            *f16Iter |= (b1 ^ b2) << (2 * k);

                        }

                        ++f16Iter;
                        ++u8Iter;
                    }

                    y[i] = DarkMatter22Prf::compress(w);
                }
#else
                auto bIter = ots.data();
                mU2.resize(y.size());

                for (u64 i = 0; i < y.size(); ++i)
                {

                    block256& w = mU2[i];
                    auto u8Iter = (u8*)&w;
                    for (u64 j = 0; j < 256; )
                    {
                        *u8Iter = 0;
                        *f16Iter = 0;
                        for (u64 k = 0; k < 8; ++k, ++j)
                        {
                            auto s = (oc::block*)bIter; bIter += 2;



                            auto q0 = (s[0].get<u8>(0) ^ s[2].get<u8>(0)) & 1;
                            auto q1 = (s[1].get<u8>(0) ^ s[2].get<u8>(0)) & 1;
                            auto q2 = (s[0].get<u8>(0) ^ s[3].get<u8>(0)) & 1;

                            //  them       us
                            //         0   1   2
                            //        ___________
                            //  0    | 0   1   0
                            //  1    | 1   0   0
                            //  2    | 0   0   1

                            //  0   -> u==1
                            //  1   -> u==0
                            //  2   -> u==2
                            auto uij = mU.data()[i].data()[j];
                            auto t0 = q0 ^ (uij == 1);
                            auto t1 = t0 ^ (uij == 0);
                            auto t2 = t0 ^ (uij == 2);

                            *u8Iter |= t0 << k;
                            auto b1 = q1 ^ t1;
                            auto b2 = (q2 ^ t2) << 1;
                            *f16Iter |= (b1 ^ b2) << (2 * k);

                        }

                        ++f16Iter;
                        ++u8Iter;
                    }


                    y[i] = DarkMatter22Prf::compress(w);
                }
#endif

            }
            MC_AWAIT(sock.send(std::move(f)));

            setTimePoint("DarkMatter.sender.derand");

            MC_END();
        }

    };

    class DarkMatter32PrfReceiver : public oc::TimerAdapter
    {
        std::vector<std::array<PRNG, 2>> mKeyOTs;
    public:
        oc::SilentOtExtReceiver mOtReceiver;
        oc::SoftSpokenShOtReceiver<> mSoftReceiver;

        std::vector<block256> mU2;
        std::vector<std::array<u16, 256>> mU;

        bool mCompressed = true;
        oc::AlignedUnVector<u16> mPi;

        void setKeyOts(span<std::array<oc::block, 2>> ots)
        {
            if (ots.size() != 256)
                throw RTE_LOC;
            mKeyOTs.resize(256);
            for (u64 i = 0; i < 256; ++i)
            {
                mKeyOTs[i][0].SetSeed(ots[i][0]);
                mKeyOTs[i][1].SetSeed(ots[i][1]);
            }
        }


        coproto::task<> evaluate(span<block256> x, span<oc::block> y, coproto::Socket& sock, PRNG& prng)
        {
            MC_BEGIN(coproto::task<>, x, y, this, &sock, &prng,
                X = oc::AlignedUnVector<std::array<u16, 512>>{},
                ui = oc::AlignedUnVector<u8>{},
                h = oc::AlignedUnVector<u16>{},
                rKeys = oc::AlignedUnVector<oc::block>{},
                mod3 = oc::AlignedUnVector<u16>{},
                mod3i = (u16*)nullptr,
                diff = oc::BitVector{},
                i = u64{},
                ots = oc::AlignedUnVector<oc::block>{},
                block3 = oc::block{}
            );
            setTimePoint("DarkMatter.recver.begin");

            block3 = std::array<u16, 8>{3, 3, 3, 3, 3, 3, 3, 3};
            mU.resize(x.size());


            if (mCompressed)
            {
                X.resize(x.size());
                for (u64 j = 0; j < x.size(); ++j)
                {
                    auto iter = oc::BitIterator((u8*)&x[j]);
                    for (i = 0; i < 256; ++i)
                    {
                        X[j][i] = *iter++;
                        X[j][i + 256] = X[j][i];
                    }
                }
                ////xk
                //uu.resize(y.size() * 4);

                //// y.size() rows, each of size 1024
                //h.resize(uu.size() * 256);
                mod3.resize(y.size() * 4);
                h.resize(mod3.size() * 256);
                for (i = 0; i < 256; ++i)
                {
                    ui.resize(y.size());
                    sampleMod3(mKeyOTs[i][0], mod3);

                    for (u64 j = 0; j < y.size(); ++j)
                    {
                        for (u64 k = 0; k < 4; ++k)
                        {
                            h[j * 1024 + i * 4 + k] = mod3[j * 4 + k];
                            mod3[j * 4 + k] += X[j][(i * 4 + k) % 512];
                            mod3[j * 4 + k] %= 3;
                        }
                    }

                    compressMod3(ui, mod3);
                    xorVector(ui, mKeyOTs[i][1]);

                    MC_AWAIT(sock.send(std::move(ui)));
                    //    ui.resize(y.size()); // y.size() * 256 * 2 bits

                    //    MC_AWAIT(sock.recv(ui));

                    //    u8 ki = *oc::BitIterator((u8*)&mKey, i);
                    //    if (ki)
                    //    {
                    //        // ui = ui ^ H(mKeyOTs[i])
                    //        xorVector(ui, mKeyOTs[i]);
                    //        decompressMod3(uu, ui);
                    //    }
                    //    else
                    //    {
                    //        sampleMod3(mKeyOTs[i], uu);
                    //    }
                }


                if (mPi.size() == 0)
                {
                    mPi = samplePerm(oc::ZeroBlock, 1024);
                }

                for (u64 j = 0; j < y.size(); ++j)
                {
                    auto  hj = h.subspan(j * 1024, 1024);
                    for (u64 k = 1; k < 1024; ++k)
                    {
                        hj[k] += hj[k];
                    }

                    auto& uj = mU[j];
                    auto pik = mPi.data();
                    for (u64 k = 0; k < 256; ++k)
                    {
                        uj[k] =
                            hj[pik[0]] +
                            hj[pik[1]] +
                            hj[pik[2]] +
                            hj[pik[3]];
                        uj[k] %= 3;
                        pik += 4;
                    }
                }
            }
            else
            {
                mod3.resize(x.size() * 256);
                X.resize(x.size());
                for (u64 j = 0; j < x.size(); ++j)
                {
                    auto iter = oc::BitIterator((u8*)&x[j]);
                    for (i = 0; i < 256; ++i)
                    {
                        X[j][i] = *iter++;
                        X[j][i + 256] = X[j][i];
                    }
                }
                for (i = 0; i < 256; ++i)
                {
                    ui.resize(x.size() * 256 / 4); // x.size() * 256 * 2 bits

                    sampleMod3(mKeyOTs[i][0], mod3);
                    mod3i = mod3.data();

                    for (u64 j = 0; j < x.size(); ++j)
                    {
                        oc::AlignedArray<oc::block, 32> xij;
                        memcpy(&xij, &X[j][i], sizeof(xij));
                        auto uj = mU[j].data();
                        auto xij128 = (oc::block*)&xij;
                        auto uj128 = (oc::block*)uj;
                        auto mod3i128 = (oc::block*)mod3i;
                        assert(u64(xij128) % 16 == 0);
                        assert(u64(uj128) % 16 == 0);
                        assert(u64(mod3i128) % 16 == 0);
                        for (u64 k = 0; k < 4; ++k)
                        {
                            uj128[0] = _mm_adds_epu16(uj128[0], mod3i128[0]);
                            uj128[1] = _mm_adds_epu16(uj128[1], mod3i128[1]);
                            uj128[2] = _mm_adds_epu16(uj128[2], mod3i128[2]);
                            uj128[3] = _mm_adds_epu16(uj128[3], mod3i128[3]);
                            uj128[4] = _mm_adds_epu16(uj128[4], mod3i128[4]);
                            uj128[5] = _mm_adds_epu16(uj128[5], mod3i128[5]);
                            uj128[6] = _mm_adds_epu16(uj128[6], mod3i128[6]);
                            uj128[7] = _mm_adds_epu16(uj128[7], mod3i128[7]);

                            mod3i128[0] = _mm_adds_epu16(mod3i128[0], xij128[0]);
                            mod3i128[1] = _mm_adds_epu16(mod3i128[1], xij128[1]);
                            mod3i128[2] = _mm_adds_epu16(mod3i128[2], xij128[2]);
                            mod3i128[3] = _mm_adds_epu16(mod3i128[3], xij128[3]);
                            mod3i128[4] = _mm_adds_epu16(mod3i128[4], xij128[4]);
                            mod3i128[5] = _mm_adds_epu16(mod3i128[5], xij128[5]);
                            mod3i128[6] = _mm_adds_epu16(mod3i128[6], xij128[6]);
                            mod3i128[7] = _mm_adds_epu16(mod3i128[7], xij128[7]);

                            auto eq0 = _mm_cmpeq_epi16(mod3i128[0], block3);
                            auto eq1 = _mm_cmpeq_epi16(mod3i128[1], block3);
                            auto eq2 = _mm_cmpeq_epi16(mod3i128[2], block3);
                            auto eq3 = _mm_cmpeq_epi16(mod3i128[3], block3);
                            auto eq4 = _mm_cmpeq_epi16(mod3i128[4], block3);
                            auto eq5 = _mm_cmpeq_epi16(mod3i128[5], block3);
                            auto eq6 = _mm_cmpeq_epi16(mod3i128[6], block3);
                            auto eq7 = _mm_cmpeq_epi16(mod3i128[7], block3);

                            mod3i128[0] = _mm_andnot_si128(eq0, mod3i128[0]);
                            mod3i128[1] = _mm_andnot_si128(eq1, mod3i128[1]);
                            mod3i128[2] = _mm_andnot_si128(eq2, mod3i128[2]);
                            mod3i128[3] = _mm_andnot_si128(eq3, mod3i128[3]);
                            mod3i128[4] = _mm_andnot_si128(eq4, mod3i128[4]);
                            mod3i128[5] = _mm_andnot_si128(eq5, mod3i128[5]);
                            mod3i128[6] = _mm_andnot_si128(eq6, mod3i128[6]);
                            mod3i128[7] = _mm_andnot_si128(eq7, mod3i128[7]);

                            xij128 += 8;
                            uj128 += 8;
                            mod3i128 += 8;
                        }
                        mod3i += 256;
                    }
                    compressMod3(ui, mod3);


                    //ui = ui ^ H(mKeyOTs[i][1])
                    xorVector(ui, mKeyOTs[i][1]);

                    MC_AWAIT(sock.send(std::move(ui)));
                }

                X = {};
                setTimePoint("DarkMatter.recver.xkMult");

                for (u64 j = 0; j < x.size(); ++j)
                {
                    auto ujk = mU[j].data();
                    for (u64 k = 0; k < 256; ++k, ++ujk)
                    {
                        *ujk = *ujk % 3;

                        //bool(mU[j][k]) * (i64(1 - mU[j][k]) * 2) + 1);
                        switch (*ujk)
                        {
                        case 1:
                            *ujk = 2;
                            break;
                        case 2:
                            *ujk = 1;
                            break;
                        default:
                            assert(*ujk == 0);
                            break;
                        }
                    }
                }
            }

            setTimePoint("DarkMatter.recver.mod2");

            // mod 2
            diff.resize(x.size() * 512);
            rKeys.resize(x.size() * 256);

#ifdef SECUREJOIN_DK_USE_SILENT
            MC_AWAIT(mOtReceiver.silentReceiveInplace(diff.size(), prng, sock, oc::ChoiceBitPacking::True));
            setTimePoint("DarkMatter.recver.silent");

            {
                auto mask = oc::AllOneBlock ^ oc::OneBlock;
                auto dIter = diff.begin();
                auto aIter = mOtReceiver.mA.begin();
                auto rIter = rKeys.begin();
                for (u64 i = 0; i < x.size(); ++i)
                {
                    for (u64 j = 0; j < 256; ++j)
                    {
                        auto uij = mU.data()[i][j];
                        auto a0 = uij & 1;
                        auto a1 = (uij >> 1);
                        assert(a1 < 2);

                        auto h0 = oc::mAesFixedKey.hashBlock(aIter[0] & mask);
                        auto h1 = oc::mAesFixedKey.hashBlock(aIter[1] & mask);

                        *rIter++ = h0 ^ h1;// (aIter[0] ^ aIter[1])& mask;

                        *dIter++ = ((*aIter++).get<u8>(0) ^ a0) & 1;
                        *dIter++ = ((*aIter++).get<u8>(0) ^ a1) & 1;
                    }
                }
            }
            MC_AWAIT(sock.send(std::move(diff)));

#else
            setTimePoint("DarkMatter.recver.choice");

            {
                auto dIter = diff.begin();
                for (u64 i = 0; i < x.size(); ++i)
                {
                    for (u64 j = 0; j < 256; ++j)
                    {
                        auto uij = mU.data()[i][j];
                        auto a0 = uij & 1;
                        auto a1 = (uij >> 1);
                        assert(a1 < 2);

                        *dIter++ = a0;
                        *dIter++ = a1 & 1;
                    }
                }
            }

            ots.resize(diff.size());
            MC_AWAIT(mSoftReceiver.receive(diff, ots, prng, sock));

            for (u64 i = 0; i < rKeys.size(); ++i)
            {
                rKeys[i] = ots[i * 2] ^ ots[i * 2 + 1];
            }
            setTimePoint("DarkMatter.recver.soft");

#endif

            //oc::mAesFixedKey.hashBlocks(rKeys, rKeys);
            ui.resize(0);
            ui.resize(x.size() * 256 / 4);

            MC_AWAIT(sock.recv(ui));

            {
                mU2.resize(x.size());
                auto fIter = oc::BitIterator((u8*)ui.data());
                auto rIter = rKeys.begin();
                for (i = 0; i < x.size(); ++i)
                {
                    auto uij = mU[i].data();
                    block256& w = mU2[i];
                    auto uIter = oc::BitIterator((u8*)&w);

                    for (u64 j = 0; j < 256; ++j, ++uij)
                    {
                        auto u = (rIter++->get<u8>(0) & 1);
                        assert(*uij < 3);

                        if (*uij)
                        {
                            u ^= *(fIter + (*uij - 1));
                        }

                        fIter = fIter + 2;
                        *uIter++ = u;
                    }

                    y[i] = DarkMatter22Prf::compress(w);
                }


            }
            setTimePoint("DarkMatter.recver.derand");

            MC_END();
        }

    };
}
