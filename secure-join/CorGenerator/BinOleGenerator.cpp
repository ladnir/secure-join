#pragma once
#include "BinOleGenerator.h"
namespace secJoin
{

    void fakeFill(u64 m, BinOle& ole, bool sender, coproto::SessionID sid)
    {
        //mNumBinOle += m;
        assert(m % 128 == 0);
        m = m / 128;

        //oc::PRNG prng(oc::block(mCurSize++));
        ole.mAdd.resize(m);
        ole.mMult.resize(m);
        auto add = ole.mAdd.data();
        auto mult = ole.mMult.data();

        auto m8 = m / 8 * 8;
        oc::block mm8(4532453452, 43254534);
        oc::block mm;
        memcpy(mm.data(), sid.mVal, sizeof(oc::block));
        //oc::block mm(2342314, 213423);

        if (sender)
        {
            oc::block aa8(0, 43254534);
            //oc::block aa(0, 213423);
            oc::block aa(0, mm.get<u64>(0));
            u64 i = 0;
            while (i < m8)
            {
                mult[i + 0] = mm;
                mult[i + 1] = mm;
                mult[i + 2] = mm;
                mult[i + 3] = mm;
                mult[i + 4] = mm;
                mult[i + 5] = mm;
                mult[i + 6] = mm;
                mult[i + 7] = mm;
                add[i + 0] = aa;
                add[i + 1] = aa;
                add[i + 2] = aa;
                add[i + 3] = aa;
                add[i + 4] = aa;
                add[i + 5] = aa;
                add[i + 6] = aa;
                add[i + 7] = aa;
                mm += mm8;
                aa += aa8;
                i += 8;
            }
            for (; i < m; ++i)
            {
                //oc::block m0 = std::array<u32, 4>{i, i, i, i};// prng.get();
                //oc::block m1 = std::array<u32, 4>{i, i, i, i};//prng.get();
                //oc::block a0 = std::array<u32, 4>{0, i, 0, i};//prng.get();
                //auto a1 = std::array<u32, 4>{i, 0, i, 0};;// m0& m1^ a0;
                mult[i] = oc::block(i, i);
                add[i] = oc::block(0, i);
            }
        }
        else
        {
            oc::block aa8(4532453452, 0);
            //oc::block aa(2342314, 0);
            oc::block aa(mm.get<u64>(1), 0);
            u64 i = 0;
            while (i < m8)
            {
                //oc::block mm(i, i);
                //oc::block aa(i, 0);
                mult[i + 0] = mm;
                mult[i + 1] = mm;
                mult[i + 2] = mm;
                mult[i + 3] = mm;
                mult[i + 4] = mm;
                mult[i + 5] = mm;
                mult[i + 6] = mm;
                mult[i + 7] = mm;
                add[i + 0] = aa;
                add[i + 1] = aa;
                add[i + 2] = aa;
                add[i + 3] = aa;
                add[i + 4] = aa;
                add[i + 5] = aa;
                add[i + 6] = aa;
                add[i + 7] = aa;
                mm += mm8;
                aa += aa8;
                i += 8;
            }
            for (; i < m; ++i)
            {
                mult[i] = oc::block(i, i);
                add[i] = oc::block(i, 0);
            }
        }
    }

    void BinOleGenerator::Impl::init(u64 size, coproto::Socket& sock, oc::PRNG& prng)
    {
        mSize = size;
        mSock = sock.fork();
        mPrng = prng.get<oc::block>();
    }

    void BinOleGenerator::Impl::init(u64 size, coproto::Socket& sock, oc::PRNG& prng, SendBase& base)
    {
        init(size, sock, prng);
        mReceiver.setBaseOts(base.get());
        mReceiver.configure(size);
        mT = recvTask();
    }

    void BinOleGenerator::Impl::init(u64 size, coproto::Socket& sock, oc::PRNG& prng, RecvBase& base)
    {
        init(size, sock, prng);
        mSender.configure(size);
        mSender.setBaseOts(base.get(), base.mChoice);
        mT = sendTask();
    }

    macoro::task<> BinOleGenerator::Impl::recvTask()
    {
        MC_BEGIN(macoro::task<>, this);
        mChoice.resize(mReceiver.mRequestedNumOts);
        mMsg.resize(mReceiver.mRequestedNumOts);

        MC_AWAIT(mReceiver.silentReceive(mChoice, mMsg, mPrng, mSock));
        mAdd.resize(oc::divCeil(mMsg.size(), 128));
        mMult.resize(oc::divCeil(mMsg.size(), 128));
        compressRecver(mChoice, mMsg, mAdd, mMult);
        MC_END();
    }

    macoro::task<> BinOleGenerator::Impl::sendTask()
    {
        MC_BEGIN(macoro::task<>, this);


        mMsg2.resize(mSender.mRequestNumOts);
        MC_AWAIT(mSender.silentSend(mMsg2, mPrng, mSock));
        mAdd.resize(oc::divCeil(mMsg2.size(), 128));
        mMult.resize(oc::divCeil(mMsg2.size(), 128));
        compressSender(mMsg2, mAdd, mMult);
        MC_END();
    }

    macoro::task<> BinOleGenerator::Impl::task() {
        if (!mT.handle())
            throw RTE_LOC;
        return std::move(mT);
    }

    void BinOleGenerator::Impl::compressRecver(oc::BitVector& bv, span<oc::block> recvMsg, span<oc::block> add, span<oc::block> mult)
    {
        auto aIter16 = (u16*)add.data();
        auto bIter16 = (u16*)mult.data();

        if (bv.size() != recvMsg.size())
            throw RTE_LOC;
        if (add.size() * 128 != recvMsg.size())
            throw RTE_LOC;
        if (mult.size() * 128 != recvMsg.size())
            throw RTE_LOC;
        using block = oc::block;

        auto shuffle = std::array<block, 16>{};
        memset(shuffle.data(), 1 << 7, sizeof(*shuffle.data()) * shuffle.size());
        for (u64 i = 0; i < 16; ++i)
            shuffle[i].set<u8>(i, 0);

        memcpy(bIter16, bv.data(), bv.sizeBytes());

        for (u64 i = 0; i < recvMsg.size(); i += 16)
        {
            // _mm_shuffle_epi8(a, b): 
            //     FOR j := 0 to 15
            //         i: = j * 8
            //         IF b[i + 7] == 1
            //             dst[i + 7:i] : = 0
            //         ELSE
            //             index[3:0] : = b[i + 3:i]
            //             dst[i + 7:i] : = a[index * 8 + 7:index * 8]
            //         FI
            //     ENDFOR

            // _mm_sll_epi16 : shifts 16 bit works left
            // _mm_movemask_epi8: packs together the MSG

            block a00 = _mm_shuffle_epi8(recvMsg[i + 0], shuffle[0]);
            block a01 = _mm_shuffle_epi8(recvMsg[i + 1], shuffle[1]);
            block a02 = _mm_shuffle_epi8(recvMsg[i + 2], shuffle[2]);
            block a03 = _mm_shuffle_epi8(recvMsg[i + 3], shuffle[3]);
            block a04 = _mm_shuffle_epi8(recvMsg[i + 4], shuffle[4]);
            block a05 = _mm_shuffle_epi8(recvMsg[i + 5], shuffle[5]);
            block a06 = _mm_shuffle_epi8(recvMsg[i + 6], shuffle[6]);
            block a07 = _mm_shuffle_epi8(recvMsg[i + 7], shuffle[7]);
            block a08 = _mm_shuffle_epi8(recvMsg[i + 8], shuffle[8]);
            block a09 = _mm_shuffle_epi8(recvMsg[i + 9], shuffle[9]);
            block a10 = _mm_shuffle_epi8(recvMsg[i + 10], shuffle[10]);
            block a11 = _mm_shuffle_epi8(recvMsg[i + 11], shuffle[11]);
            block a12 = _mm_shuffle_epi8(recvMsg[i + 12], shuffle[12]);
            block a13 = _mm_shuffle_epi8(recvMsg[i + 13], shuffle[13]);
            block a14 = _mm_shuffle_epi8(recvMsg[i + 14], shuffle[14]);
            block a15 = _mm_shuffle_epi8(recvMsg[i + 15], shuffle[15]);

            a00 = a00 ^ a08;
            a01 = a01 ^ a09;
            a02 = a02 ^ a10;
            a03 = a03 ^ a11;
            a04 = a04 ^ a12;
            a05 = a05 ^ a13;
            a06 = a06 ^ a14;
            a07 = a07 ^ a15;

            a00 = a00 ^ a04;
            a01 = a01 ^ a05;
            a02 = a02 ^ a06;
            a03 = a03 ^ a07;

            a00 = a00 ^ a02;
            a01 = a01 ^ a03;

            a00 = a00 ^ a01;

            a00 = _mm_slli_epi16(a00, 7);

            u16 ap = _mm_movemask_epi8(a00);

            *aIter16++ = ap;
        }
    }


    void BinOleGenerator::Impl::compressSender(span<std::array<oc::block, 2>> sendMsg, span<oc::block> add, span<oc::block> mult)
    {

        auto bIter16 = (u16*)add.data();
        auto aIter16 = (u16*)mult.data();

        if (add.size() * 128 != sendMsg.size())
            throw RTE_LOC;
        if (mult.size() * 128 != sendMsg.size())
            throw RTE_LOC;
        using block = oc::block;

        auto shuffle = std::array<block, 16>{};
        memset(shuffle.data(), 1 << 7, sizeof(*shuffle.data()) * shuffle.size());
        for (u64 i = 0; i < 16; ++i)
            shuffle[i].set<u8>(i, 0);

        for (u64 i = 0; i < sendMsg.size(); i += 16)
        {
            block a00 = _mm_shuffle_epi8(sendMsg[i + 0][0], shuffle[0]);
            block a01 = _mm_shuffle_epi8(sendMsg[i + 1][0], shuffle[1]);
            block a02 = _mm_shuffle_epi8(sendMsg[i + 2][0], shuffle[2]);
            block a03 = _mm_shuffle_epi8(sendMsg[i + 3][0], shuffle[3]);
            block a04 = _mm_shuffle_epi8(sendMsg[i + 4][0], shuffle[4]);
            block a05 = _mm_shuffle_epi8(sendMsg[i + 5][0], shuffle[5]);
            block a06 = _mm_shuffle_epi8(sendMsg[i + 6][0], shuffle[6]);
            block a07 = _mm_shuffle_epi8(sendMsg[i + 7][0], shuffle[7]);
            block a08 = _mm_shuffle_epi8(sendMsg[i + 8][0], shuffle[8]);
            block a09 = _mm_shuffle_epi8(sendMsg[i + 9][0], shuffle[9]);
            block a10 = _mm_shuffle_epi8(sendMsg[i + 10][0], shuffle[10]);
            block a11 = _mm_shuffle_epi8(sendMsg[i + 11][0], shuffle[11]);
            block a12 = _mm_shuffle_epi8(sendMsg[i + 12][0], shuffle[12]);
            block a13 = _mm_shuffle_epi8(sendMsg[i + 13][0], shuffle[13]);
            block a14 = _mm_shuffle_epi8(sendMsg[i + 14][0], shuffle[14]);
            block a15 = _mm_shuffle_epi8(sendMsg[i + 15][0], shuffle[15]);

            block b00 = _mm_shuffle_epi8(sendMsg[i + 0][1], shuffle[0]);
            block b01 = _mm_shuffle_epi8(sendMsg[i + 1][1], shuffle[1]);
            block b02 = _mm_shuffle_epi8(sendMsg[i + 2][1], shuffle[2]);
            block b03 = _mm_shuffle_epi8(sendMsg[i + 3][1], shuffle[3]);
            block b04 = _mm_shuffle_epi8(sendMsg[i + 4][1], shuffle[4]);
            block b05 = _mm_shuffle_epi8(sendMsg[i + 5][1], shuffle[5]);
            block b06 = _mm_shuffle_epi8(sendMsg[i + 6][1], shuffle[6]);
            block b07 = _mm_shuffle_epi8(sendMsg[i + 7][1], shuffle[7]);
            block b08 = _mm_shuffle_epi8(sendMsg[i + 8][1], shuffle[8]);
            block b09 = _mm_shuffle_epi8(sendMsg[i + 9][1], shuffle[9]);
            block b10 = _mm_shuffle_epi8(sendMsg[i + 10][1], shuffle[10]);
            block b11 = _mm_shuffle_epi8(sendMsg[i + 11][1], shuffle[11]);
            block b12 = _mm_shuffle_epi8(sendMsg[i + 12][1], shuffle[12]);
            block b13 = _mm_shuffle_epi8(sendMsg[i + 13][1], shuffle[13]);
            block b14 = _mm_shuffle_epi8(sendMsg[i + 14][1], shuffle[14]);
            block b15 = _mm_shuffle_epi8(sendMsg[i + 15][1], shuffle[15]);

            a00 = a00 ^ a08;
            a01 = a01 ^ a09;
            a02 = a02 ^ a10;
            a03 = a03 ^ a11;
            a04 = a04 ^ a12;
            a05 = a05 ^ a13;
            a06 = a06 ^ a14;
            a07 = a07 ^ a15;

            b00 = b00 ^ b08;
            b01 = b01 ^ b09;
            b02 = b02 ^ b10;
            b03 = b03 ^ b11;
            b04 = b04 ^ b12;
            b05 = b05 ^ b13;
            b06 = b06 ^ b14;
            b07 = b07 ^ b15;

            a00 = a00 ^ a04;
            a01 = a01 ^ a05;
            a02 = a02 ^ a06;
            a03 = a03 ^ a07;

            b00 = b00 ^ b04;
            b01 = b01 ^ b05;
            b02 = b02 ^ b06;
            b03 = b03 ^ b07;

            a00 = a00 ^ a02;
            a01 = a01 ^ a03;

            b00 = b00 ^ b02;
            b01 = b01 ^ b03;

            a00 = a00 ^ a01;
            b00 = b00 ^ b01;

            a00 = _mm_slli_epi16(a00, 7);
            b00 = _mm_slli_epi16(b00, 7);

            u16 ap = _mm_movemask_epi8(a00);
            u16 bp = _mm_movemask_epi8(b00);

            assert(aIter16 < (u16*)(mult.data() + mult.size()));
            assert(bIter16 < (u16*)(add.data() + add.size()));

            *aIter16++ = ap ^ bp;
            *bIter16++ = ap;
        }
    }


    template<typename Base>
    void BinOleGenerator::init(
        u64 n,
        u64 batchSize,
        coproto::Socket& sock,
        oc::PRNG& prng,
        Base& base,
        bool mock)
    {
        mMock = mock;
        mSize = n;
        mCorrelations.reserve(oc::divCeil(n, batchSize));
        for (u64 i = 0; i < n; i += batchSize)
        {
            u64 size = std::min(n - i * batchSize, batchSize);
            mCorrelations.emplace_back();
            mCorrelations.back().init(size, sock, prng, base);
        }

        mRole = std::is_same_v<Base, SendBase>;
    }

    template void BinOleGenerator::init<SendBase>(u64, u64, coproto::Socket&, oc::PRNG&, SendBase&, bool);
    template void BinOleGenerator::init<RecvBase>(u64, u64, coproto::Socket&, oc::PRNG&, RecvBase&, bool);


    macoro::task<> BinOleGenerator::task()
    {
        MC_BEGIN(macoro::task<>, this, i = u64{});
        if (mMock)
            MC_RETURN_VOID();

        TODO("ex handling");
        for (i = 0; i < mCorrelations.size(); ++i)
            mCorrelations[i].mTask = mCorrelations[i].task() | macoro::make_eager();


        for (i = 0; i < mCorrelations.size(); ++i)
        {
            MC_AWAIT(mCorrelations[i].mTask);
            mCorrelations[i].mDone.set();
        }

        MC_END();
    }

    macoro::task<> BinOleGenerator::get(BinOle& d)
    {
        MC_BEGIN(macoro::task<>, this, &d);

        if (mIdx >= mCorrelations.size())
            throw RTE_LOC;


        if (mMock)
        {
            auto s = mCorrelations[mIdx].mSize;
            auto r = mRole;
            fakeFill(s, d, r,  mCorrelations[mIdx].mSock.mId);
        }
        else
        {

            MC_AWAIT(mCorrelations[mIdx].mDone);

            d.mMult = std::move(mCorrelations[mIdx].mMult);
            d.mAdd = std::move(mCorrelations[mIdx].mAdd);
        }
        ++mIdx;

        MC_END();
    }


}