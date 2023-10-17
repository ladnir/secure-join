#pragma once
#include "secure-join/config.h"
#include "secure-join/Defines.h"
#include "cryptoTools/Common/BitIterator.h"
#include <bitset>
#include "libOTe/Tools/Tools.h"
#include "libOTe/TwoChooseOne/Silent/SilentOtExtSender.h"
#include "libOTe/TwoChooseOne/Silent/SilentOtExtReceiver.h"
#include "libOTe/TwoChooseOne/SoftSpokenOT/SoftSpokenShOtExt.h"
#include "cryptoTools/Crypto/PRNG.h"
#include "cryptoTools/Common/Timer.h"

namespace secJoin
{
    struct block256
    {
        std::array<oc::block, 2> mData;

        void operator^=(const block256& x)
        {
            mData[0] = mData[0] ^ x.mData[0];
            mData[1] = mData[1] ^ x.mData[1];
        }
        block256 operator&(const block256& x) const
        {
            block256 r;
            r.mData[0] = mData[0] & x.mData[0];
            r.mData[1] = mData[1] & x.mData[1];
            return r;
        }

        block256 operator^(const block256& x) const
        {
            auto r = *this;
            r ^= x;
            return r;
        }

        block256 rotate(u64 i) const
        {
            auto xx = *(std::bitset<256>*)this;
            auto low = xx >> i;
            auto hgh = xx << (256 - i);

            auto m = hgh ^ low;
            block256 r;
            memcpy(&r, &m, sizeof(r));
            return r;
        }

        bool operator==(const block256& x) const
        {
            return std::memcmp(this, &x, sizeof(x)) == 0;
        }
        bool operator!=(const block256& x) const
        {
            return std::memcmp(this, &x, sizeof(x)) != 0;
        }

        oc::block& operator[](u64 i) { return mData[i]; }
    };

    inline std::ostream& operator<<(std::ostream& o, const block256& x)
    {
        o << x.mData[1] << x.mData[0];
        return o;
    }

    struct block256m3
    {
        //std::array<oc::block, 2> mData;
        std::array<u8, 256> mData;
        void operator^=(const block256& x)
        {
            //oc::BitIterator iter((u8*)&x);
            //for (u64 i = 0; i < 256; ++i, ++iter)
            //{
            //    assert((mData[i] == 255 && *iter) == false);

            //    mData[i] += *iter;
            //}
            oc::block block1 = oc::block::allSame<u8>(1);
            oc::block X[8], v[8];
            for (u64 j = 0; j < 2; ++j)
            {

                X[0] = x.mData[j];
                X[1] = x.mData[j] >> 1;
                X[2] = x.mData[j] >> 2;
                X[3] = x.mData[j] >> 3;
                X[4] = x.mData[j] >> 4;
                X[5] = x.mData[j] >> 5;
                X[6] = x.mData[j] >> 6;
                X[7] = x.mData[j] >> 7;

                auto xIter = (u8*)X;
                u8* v8 = v[0].data();
                for (u64 t = 0; t < 8; ++t)
                {
                    for (u64 kk = 0; kk < 2; ++kk)
                    {

                        for (u64 k = 0; k < 8; ++k)
                        {
                            v8[k] = xIter[sizeof(oc::block) * k];
                            //if (v != *oc::BitIterator((u8*)&x, i))
                            //    throw RTE_LOC;
                            //mData[i] += v;
                        }
                        v8 += 8;
                        ++xIter;
                    }
                }
                auto d = (oc::block*)&mData[j * 128];

                v[0] = v[0] & block1;
                v[1] = v[1] & block1;
                v[2] = v[2] & block1;
                v[3] = v[3] & block1;
                v[4] = v[4] & block1;
                v[5] = v[5] & block1;
                v[6] = v[6] & block1;
                v[7] = v[7] & block1;

                d[0] = d[0] + v[0];
                d[1] = d[1] + v[1];
                d[2] = d[2] + v[2];
                d[3] = d[3] + v[3];
                d[4] = d[4] + v[4];
                d[5] = d[5] + v[5];
                d[6] = d[6] + v[6];
                d[7] = d[7] + v[7];


            }

            //u64* iter = (u64*)&x;
            //for (u64 i = 0; i < 256;)
            //{
            //    auto d = i + 64;
            //    auto xx = *iter;
            //    auto s = 0;
            //    while (i < d)
            //    {

            //        auto xd = xx >> s;
            //        mData[i] += (xd & 1);
            //        ++s;
            //        ++i;
            //    }
            //    ++iter;
            //}
        }

        block256 mod2()
        {
            block256 r;
            //oc::BitIterator iter((u8*)&r);

            //
            //for (u64 i = 0; i < 256; ++i)
            //{
            //    mData[i] %= 3;
            //    *iter = mData[i] % 2;
            //}

            u64* iter = (u64*)&r;
            for (u64 i = 0; i < 256;)
            {
                auto d = i + 64;
                auto s = 0;
                *iter = 0;
                while (i < d)
                {

                    mData[i] %= 3;
                    *iter |= u64(mData[i] % 2) << s;
                    ++i;
                    ++s;
                }
                ++iter;
            }
            return r;
        }
    };


    class DarkMatter22Prf
    {
    public:
        block256 mKey;

        std::array<block256, 256> mKeyMask;

        static const std::array<block256, 128> mB;

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
            block256 v;
            block256m3 u;
            memset(&v, 0, sizeof(v));
            memset(&u, 0, sizeof(u));
            for (u64 i = 0; i < mKeyMask.size(); ++i)
            {
                auto xi = x.rotate(i) & mKeyMask[i];
                v ^= xi;
                u ^= xi;
            }

            block256 u2 = u.mod2();
            block256 w = v ^ u2;

            return compress(w);
        }

        static inline oc::block compress(block256 w)
        {

            alignas(32) std::array<std::array<oc::block, 128>, 2> bw;

            for (u64 i = 0; i < 128; ++i)
            {
                bw[0][i] = DarkMatter22Prf::mB[i].mData[0] & w.mData[0];
                bw[1][i] = DarkMatter22Prf::mB[i].mData[1] & w.mData[1];
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
    };


    inline void sampleMod3(PRNG& prng, span<u16> mBuffer)
    {
        auto n = mBuffer.size();
        auto dst = mBuffer.data();
        oc::block m[8], t[8], eq[8];
        oc::block allOne = oc::AllOneBlock;
        oc::block block1 = std::array<u16, 8>{1, 1, 1, 1, 1, 1, 1, 1};
        oc::block block3 = std::array<u16, 8>{3, 3, 3, 3, 3, 3, 3, 3};

        static constexpr int batchSize = 16;
        std::array<std::array<oc::block, batchSize>, 64> buffer;
        std::array<u16* __restrict, 64> iters;

        for (u64 i = 0; i < n;)
        {
            for (u64 j = 0; j < 64; ++j)
                iters[j] = (u16*)buffer[j].data();


            for (u64 bb = 0; bb < batchSize; ++bb)
            {

                prng.mAes.ecbEncCounterMode(prng.mBlockIdx, 8, m);
                prng.mBlockIdx += 8;
                //for (auto k = 0; k < 16 && i < n; ++k)
                //{
                //    u64 t = ((u64*)m)[k];
                //    auto min = std::min<u64>(32, n - i);
                //    for (u64 j = 0; j < min; ++j)
                //    {
                //        auto b = t & 3;
                //        dst[i] = b;
                //        i += (b != 3);
                //        t >>= 2;
                //    }
                //}
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
                auto b = (u16*)buffer[j].data();

                auto size = iters[j] - b;
                auto min = std::min<u64>(size, n - i);
                if (min)
                {
                    memcpy(dst + i, b, min * 2);
                    i += min;
                }
            }
        }
    }

    inline oc::AlignedUnVector<u16> sampleMod3(PRNG& prng, u64 n)
    {
        oc::AlignedUnVector<u16> mBuffer(n);
        sampleMod3(prng, mBuffer);
        return mBuffer;
    }

    inline void compressMod3(span<u8> dst, span<const u16> src)
    {
        assert(oc::divCeil(src.size(), 4) == dst.size());

        u64* d = (u64*)dst.data();
        auto s = src.data();
        auto n = src.size();
        auto n32 = n / 32 * 32;
        u64 i = 0;
        for (; i < n32;)
        {
            assert(d < (u64*)(dst.data() + dst.size()));

            u64 x[4];
            u8* x8 = (u8*)x;
            //auto si = s + i;
            for (u64 j = 0; j < 8; ++j)
            {
                assert(s + 4 <= src.data() + src.size());
                x8[0 + j] = *s++;
                x8[8 + j] = *s++;
                x8[16 + j] = *s++;
                x8[24 + j] = *s++;
                //si += 4;
            }
            x[1] <<= 2;
            x[2] <<= 4;
            x[3] <<= 6;

            *d++ = x[0] ^ x[1] ^ x[2] ^ x[3];

            assert(s <= src.data() + src.size());
            i += 32;
        }


        s = src.data();
        while (i < n)
        {
            auto& d8 = dst[i / 4];
            d8 = 0;
            for (u64 j = 0; i < n && j < 8; j += 2, ++i)
            {
                assert(s[i] < 3);
                d8 |= s[i] << j;
            }
        }
    }

    inline void decompressMod3(span<u16> dst, span<const u8> src)
    {
        assert(oc::divCeil(dst.size(), 4) == src.size());

        const u64* s = (const u64*)src.data();
        auto d = dst.data();
        u64 i = 0, main = dst.size() / 32 * 32;
        for (; i < main;)
        {
            auto ss = *s;
            for (u64 j = 0; j < 32; ++j, ++i)
            {
                assert(s < (u64*)(src.data() + src.size()));
                assert(i < dst.size());
                auto& dsti = d[i];
                dsti = ss & 3;
                assert(dsti < 3);
                ss >>= 2;
            }
            ++s;
        }
        for (; i < dst.size();)
        {
            auto ss = src[i/4];
            auto rem = std::min<u64>(4, dst.size() - i);
            for (u64 j = 0; j < rem; ++j, ++i)
            {
                auto& dsti = d[i];
                dsti = ss & 3;
                assert(dsti < 3);
                ss >>= 2;
            }
        }
    }


    inline void xorVector(span<oc::block> v, PRNG& prng)
    {
        oc::block m[8];
        auto vIter = v.data();
        auto n = v.size();
        assert(u64(v.data()) % 16 == 0);

        auto j = 0ull;
        auto main = n / 8 * 8;
        for (; j < main; j += 8)
        {
            prng.mAes.ecbEncCounterMode(prng.mBlockIdx, 8, m);
            prng.mBlockIdx += 8;
            //m[0] = prng.get();
            //m[1] = prng.get();
            //m[2] = prng.get();
            //m[3] = prng.get();
            //m[4] = prng.get();
            //m[5] = prng.get();
            //m[6] = prng.get();
            //m[7] = prng.get();

            vIter[0] = vIter[0] ^ m[0];
            vIter[1] = vIter[1] ^ m[1];
            vIter[2] = vIter[2] ^ m[2];
            vIter[3] = vIter[3] ^ m[3];
            vIter[4] = vIter[4] ^ m[4];
            vIter[5] = vIter[5] ^ m[5];
            vIter[6] = vIter[6] ^ m[6];
            vIter[7] = vIter[7] ^ m[7];
            vIter += 8;
        }
        for (; j < n; ++j)
        {
            auto m = prng.mAes.ecbEncBlock(oc::toBlock(prng.mBlockIdx++));
            //oc::block m = prng.get();
            *vIter = *vIter ^ m;
            ++vIter;
        }
        assert(vIter == v.data() + v.size());
    }

    inline void xorVector(span<u8> ui, PRNG& prng)
    {
        auto nBlk = ui.size() / sizeof(oc::block);
        assert((u64)ui.data() % 16 == 0);
        auto uiBlk = span<oc::block>((oc::block*)ui.data(), nBlk);
        xorVector(uiBlk, prng);

        for (u64 i = nBlk * sizeof(oc::block); i < ui.size(); ++i)
        {
            auto r = prng.get<u8>();
            ui[i] ^= r;
        }
    }

    inline void xorVector(span<block256> v, PRNG& prng)
    {
        auto vv = span<oc::block>((oc::block*)v.data(), v.size() * 2);
        xorVector(vv, prng);
    }


    // v = v + u + PRNG()
    inline void xorVectorAdd(span<oc::block> v, span<const oc::block> u, PRNG& prng)
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
            //m[0] = prng.get();
            //m[1] = prng.get();
            //m[2] = prng.get();
            //m[3] = prng.get();
            //m[4] = prng.get();
            //m[5] = prng.get();
            //m[6] = prng.get();
            //m[7] = prng.get();

            vIter[0] = vIter[0] ^ uIter[0] ^ m[0];
            vIter[1] = vIter[1] ^ uIter[1] ^ m[1];
            vIter[2] = vIter[2] ^ uIter[2] ^ m[2];
            vIter[3] = vIter[3] ^ uIter[3] ^ m[3];
            vIter[4] = vIter[4] ^ uIter[4] ^ m[4];
            vIter[5] = vIter[5] ^ uIter[5] ^ m[5];
            vIter[6] = vIter[6] ^ uIter[6] ^ m[6];
            vIter[7] = vIter[7] ^ uIter[7] ^ m[7];
            vIter += 8;
            uIter += 8;
        }
        for (; j < n; ++j)
        {
            auto m = prng.mAes.ecbEncBlock(oc::toBlock(prng.mBlockIdx++));
            //oc::block m = prng.get();

            *vIter = *vIter ^ *uIter ^ m;
            ++vIter;
            ++uIter;
        }
        assert(vIter == v.data() + v.size());
    }

    // v = v + u + PRNG()
    inline void xorVectorAdd(span<block256> v, span<const block256> u, PRNG& prng)
    {
        auto vv = span<oc::block>(v[0].mData.data(), v.size() * 2);
        auto uu = span<const oc::block>(u[0].mData.data(), u.size() * 2);
        xorVectorAdd(vv, uu, prng);
    }

    class DarkMatter22PrfSender : public oc::TimerAdapter
    {
        std::vector<PRNG> mKeyOTs;
    public:
        block256 mKey;
#ifdef SECUREJOIN_DK_USE_SILENT
        oc::SilentOtExtSender mOtSender;
#else
        oc::SoftSpokenShOtSender<> mSoftSender;
#endif
        std::vector<block256> mV, mU2, mW;
        std::vector<std::array<u16, 256>> mU;

        void setKeyOts(span<oc::block> ots)
        {
            if (ots.size() != 256)
                throw RTE_LOC;
            mKeyOTs.resize(256);
            for (u64 i = 0; i < 256; ++i)
            {
                //mKeyOTs[i].mAes.setKey(ots[i]);
                //mKeyOTs[i].mBlockIdx = 0;
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
                vi = oc::AlignedUnVector<block256>{},
                ui = oc::AlignedUnVector<u8>{},
                uui = oc::AlignedUnVector<u16>{},
                f = oc::BitVector{},
                diff = oc::BitVector{},
                ots = oc::AlignedUnVector<std::array<oc::block, 2>>{},
                i = u64{});

            setTimePoint("DarkMatter.sender.begin");
            mV.resize(y.size());
            mU.resize(y.size());
            uui.resize(y.size() * 256);
            for (i = 0; i < 256; ++i)
            {
                vi.resize(y.size()); // y.size() * 256 bits
                ui.resize(y.size() * compSize); // y.size() * 256 * 2 bits

                MC_AWAIT(sock.recv(vi));
                MC_AWAIT(sock.recv(ui));

                u8 ki = *oc::BitIterator((u8*)&mKey, i);
                if (ki)
                {
                    // mV = mV ^ vi ^ H(mKeyOTs[i])
                    xorVectorAdd(mV, vi, mKeyOTs[i]);

                    // ui = ui ^ H(mKeyOTs[i])
                    xorVector(ui, mKeyOTs[i]);

                    decompressMod3(uui, ui);
                }
                else
                {
                    sampleMod3(mKeyOTs[i], uui);

                    xorVector(mV, mKeyOTs[i]);
                    //for (u64 j = 0; j < y.size(); ++j)
                    //{
                    //    mV[j] = mV[j] ^ mKeyOTs[i].get<block256>();
                    //}
                }

                auto u = (oc::block*)mU.data();
                auto ui = (oc::block*)uui.data();
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
                    //auto uij = uui.subspan(j * 256, 256);
                    //for (u64 k = 0; k < 256; ++k)
                    //{
                    //    mU[j][k] += uij[k];
                    //}
                }
            }

            for (u64 j = 0; j < y.size(); ++j)
            {
                for (u64 k = 0; k < 256; ++k)
                {
                    mU[j][k] = mU[j][k] % 3;
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

                mU2.resize(y.size());
                mW.resize(y.size());
                f.resize(y.size() * 256 * 2);
                //auto mask = oc::AllOneBlock ^ oc::OneBlock;
                //auto uIter = oc::BitIterator((u8*)mU2.data());
                auto u8Iter = (u8*)mU2.data();
                //auto dIter = diff.begin();
                auto f16Iter = (u16*)f.data();
#ifdef SECUREJOIN_DK_USE_SILENT
                auto d16Iter = (u16*)diff.data();
                auto bIter = mOtSender.mB.begin();

                //auto fIter = f.begin();
                //auto rIter = rKeys.begin();
                for (u64 i = 0; i < y.size(); ++i)
                {
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


                    auto w = mU2[i] ^ mV[i];

                    y[i] = DarkMatter22Prf::compress(w);
                }
#else
                auto bIter = ots.data();

                for (u64 i = 0; i < y.size(); ++i)
                {
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


                    auto w = mU2[i] ^ mV[i];

                    y[i] = DarkMatter22Prf::compress(w);
                }
#endif

            }
            MC_AWAIT(sock.send(std::move(f)));

            setTimePoint("DarkMatter.sender.derand");

            MC_END();
        }

    };

    class DarkMatter22PrfReceiver : public oc::TimerAdapter
    {
        std::vector<std::array<PRNG, 2>> mKeyOTs;
    public:
        oc::SilentOtExtReceiver mOtReceiver;
        oc::SoftSpokenShOtReceiver<> mSoftReceiver;

        std::vector<block256> mV, mU2, mW;
        std::vector<std::array<u16, 256>> mU;


        void setKeyOts(span<std::array<oc::block, 2>> ots)
        {
            if (ots.size() != 256)
                throw RTE_LOC;
            mKeyOTs.resize(256);
            for (u64 i = 0; i < 256; ++i)
            {
                //mKeyOTs[i][0].mAes.setKey(ots[i][0]);
                //mKeyOTs[i][1].mAes.setKey(ots[i][1]);
                //mKeyOTs[i][0].mBlockIdx = 0;
                //mKeyOTs[i][1].mBlockIdx = 0;
                mKeyOTs[i][0].SetSeed(ots[i][0]);
                mKeyOTs[i][1].SetSeed(ots[i][1]);
            }
        }


        coproto::task<> evaluate(span<block256> x, span<oc::block> y, coproto::Socket& sock, PRNG& prng)
        {
            MC_BEGIN(coproto::task<>, x, y, this, &sock, &prng,
                X = oc::AlignedUnVector<std::array<u16, 512>>{},
                vi = oc::AlignedUnVector<block256>{},
                ui = oc::AlignedUnVector<u8>{},
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
            mV.resize(x.size());
            mU.resize(x.size());
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
                vi.resize(x.size()); // x.size() * 256 bits
                ui.resize(x.size() * 256 / 4); // x.size() * 256 * 2 bits

                sampleMod3(mKeyOTs[i][0], mod3);
                mod3i = mod3.data();

                //for (u64 j = 0; j < x.size(); ++j)
                //{
                //    auto xji = x[j].rotate(i);
                //    auto vij0 = mKeyOTs[i][0].get<block256>();
                //    mV[j] = mV[j] ^ vij0;
                //    vi[j] = vij0 ^ xji ^ mKeyOTs[i][1].get<block256>();
                //}
                {
                    u64 j = 0;
                    auto main = x.size() / 4 * 4;
                    block256 xji[4];
                    block256* xj = x.data();
                    //oc::block m0[8];
                    oc::block m1[8];
                    auto vIter = (oc::block*)mV.data();
                    auto viIter = (oc::block*)vi.data();
                    for (; j < main; j += 4)
                    {
                        xji[0] = xj[0].rotate(i);
                        xji[1] = xj[1].rotate(i);
                        xji[2] = xj[2].rotate(i);
                        xji[3] = xj[3].rotate(i);
                        xj += 4;

                        mKeyOTs[i][0].mAes.ecbEncCounterMode(mKeyOTs[i][0].mBlockIdx, 8, viIter);
                        mKeyOTs[i][0].mBlockIdx += 8;
                        //viIter[0] = mKeyOTs[i][0].get();
                        //viIter[1] = mKeyOTs[i][0].get();
                        //viIter[2] = mKeyOTs[i][0].get();
                        //viIter[3] = mKeyOTs[i][0].get();
                        //viIter[4] = mKeyOTs[i][0].get();
                        //viIter[5] = mKeyOTs[i][0].get();
                        //viIter[6] = mKeyOTs[i][0].get();
                        //viIter[7] = mKeyOTs[i][0].get();

                        vIter[0] = vIter[0] ^ viIter[0];
                        vIter[1] = vIter[1] ^ viIter[1];
                        vIter[2] = vIter[2] ^ viIter[2];
                        vIter[3] = vIter[3] ^ viIter[3];
                        vIter[4] = vIter[4] ^ viIter[4];
                        vIter[5] = vIter[5] ^ viIter[5];
                        vIter[6] = vIter[6] ^ viIter[6];
                        vIter[7] = vIter[7] ^ viIter[7];

                        mKeyOTs[i][1].mAes.ecbEncCounterMode(mKeyOTs[i][1].mBlockIdx, 8, m1);
                        mKeyOTs[i][1].mBlockIdx += 8;

                        //m1[0] = mKeyOTs[i][1].get();
                        //m1[1] = mKeyOTs[i][1].get();
                        //m1[2] = mKeyOTs[i][1].get();
                        //m1[3] = mKeyOTs[i][1].get();
                        //m1[4] = mKeyOTs[i][1].get();
                        //m1[5] = mKeyOTs[i][1].get();
                        //m1[6] = mKeyOTs[i][1].get();
                        //m1[7] = mKeyOTs[i][1].get();

                        auto xji128 = (oc::block*)xji;
                        viIter[0] = viIter[0] ^ xji128[0] ^ m1[0];
                        viIter[1] = viIter[1] ^ xji128[1] ^ m1[1];
                        viIter[2] = viIter[2] ^ xji128[2] ^ m1[2];
                        viIter[3] = viIter[3] ^ xji128[3] ^ m1[3];
                        viIter[4] = viIter[4] ^ xji128[4] ^ m1[4];
                        viIter[5] = viIter[5] ^ xji128[5] ^ m1[5];
                        viIter[6] = viIter[6] ^ xji128[6] ^ m1[6];
                        viIter[7] = viIter[7] ^ xji128[7] ^ m1[7];

                        vIter += 8;
                        viIter += 8;

                        //auto vij0 = mKeyOTs[i][0].get<block256>();
                        //mV[j] = mV[j] ^ vij0;
                        //vi[j] = vij0 ^ xji ^ mKeyOTs[i][1].get<block256>();
                    }
                }


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
                        //assert(*mod3i < 3);
                        //uj[k] += mod3i[k];
                        //mod3i[k] = (mod3i[k] + xij[k]);
                        //mod3i[k] *= (mod3i[k] != 3);

                        //uj128[k] = _mm_adds_epu16(uj128[k], mod3i128[k]);
                        //mod3i128[k] = _mm_adds_epu16(mod3i128[k], xij128[k]);
                        //auto eq = _mm_cmpeq_epi16(mod3i128[k], block3);
                        //mod3i128[k] = _mm_andnot_si128(eq, mod3i128[k]);

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
                    //for (u64 k = 0; k < 256; ++k)
                    //{
                    //    assert(*mod3i < 3);
                    //    auto& uijk = *mod3i++;
                    //    mU[j][k] += uijk;
                    //    auto xijk = X[j][u8(i + k)];
                    //    uijk = (uijk + xijk);
                    //    uijk *= (uijk != 3);
                    //}
                }
                compressMod3(ui, mod3);


                //ui = ui ^ H(mKeyOTs[i][1])
                xorVector(ui, mKeyOTs[i][1]);
                //for (u64 j = 0; j < ui.size(); ++j)
                //{
                //    ui[j] = ui[j] ^ mKeyOTs[i][1].get<u8>();
                //}

                MC_AWAIT(sock.send(std::move(vi)));
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
                        break;
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
                mW.resize(x.size());
                auto uIter = oc::BitIterator((u8*)mU2.data());
                auto fIter = oc::BitIterator((u8*)ui.data());
                auto rIter = rKeys.begin();
                for (i = 0; i < x.size(); ++i)
                {
                    auto uij = mU[i].data();

                    for (u64 j = 0; j < 256; ++j, ++uij)
                    {

                        auto u = (rIter++->get<u8>(0) & 1);
                        if (*uij)
                        {
                            u ^= *(fIter + (*uij - 1));
                        }
                        fIter = fIter + 2;
                        *uIter++ = u;
                        //switch (mU[i][j])
                        //{
                        //case 0:
                        //{
                        //    // u mod 2 = lsb(*rIter++) = q0
                        //    break;
                        //}
                        //case 1:
                        //    // u mod 2 = lsb(*rIter++) ^ *fIter

                        //    break;
                        //case 2:
                        //    // u mod 2 = lsb(*rIter++) ^ *(fIter + 1)

                        //    break;
                        //default:
                        //    __assume(0);
                        //    break;
                        //}
                    }

                    auto w = mU2[i] ^ mV[i];

                    alignas(32) std::array<std::array<oc::block, 128>, 2> bw;

                    for (u64 i = 0; i < 128; ++i)
                    {
                        bw[0][i] = DarkMatter22Prf::mB[i].mData[0] & w.mData[0];
                        bw[1][i] = DarkMatter22Prf::mB[i].mData[1] & w.mData[1];
                    }
                    oc::transpose128(bw[0].data());
                    oc::transpose128(bw[1].data());

                    oc::block& r = y[i];
                    memset(&r, 0, sizeof(r));
                    for (u64 i = 0; i < 128; ++i)
                        r = r ^ bw[0][i];
                    for (u64 i = 0; i < 128; ++i)
                        r = r ^ bw[1][i];
                }


            }
            setTimePoint("DarkMatter.recver.derand");

            MC_END();
        }

    };
}
