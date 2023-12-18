#include "BinOleBatch.h"

namespace secJoin
{



    OleBatch::OleBatch(bool sender, oc::Socket&& s, PRNG&& p)
    {
        mSock = std::move(s);
        mPrng = std::move(p);
        if (sender)
        {
            mSendRecv.emplace<0>();
        }
        else
        {
            mSendRecv.emplace<1>();
        }
    }

    void OleBatch::getCor(Cor* c, u64 begin, u64 size)
    {
        if (c->mType != CorType::Ole)
            std::terminate();

        auto& d = *static_cast<BinOle*>(c);
        assert(begin % 128 == 0);
        assert(size % 128 == 0);
        d.mAdd = mAdd.subspan(begin / 128, size / 128);
        d.mMult = mMult.subspan(begin / 128, size / 128);
    }

    BaseRequest OleBatch::getBaseRequest()
    {
        BaseRequest r;

        mSendRecv | match{
            [&](SendBatch& send) {
                send.mSender.configure(mSize);
                r.mSendSize = send.mSender.silentBaseOtCount();
            },

            [&](RecvBatch& recv) {
                recv.mReceiver.configure(mSize);
                r.mChoice = recv.mReceiver.sampleBaseChoiceBits(mPrng);
            }
        };
        return r;
    }

    void OleBatch::setBase(span<oc::block> rMsg, span<std::array<oc::block, 2>> sMsg)
    {

        mSendRecv | match{
            [&](SendBatch& send) {
                if (rMsg.size())
                    std::terminate();
                send.mSender.setSilentBaseOts(sMsg);
            },
            [&](RecvBatch& recv) {
                if (sMsg.size())
                    std::terminate();
                recv.mReceiver.setSilentBaseOts(rMsg);
            }
        };
        mHaveBase.set();
    }

    macoro::task<> OleBatch::getTask()
    {
        return mSendRecv | match{
            [&](SendBatch& send) {
                   return send.sendTask(mIndex, mPrng, mSock, mAdd, mMult, mCorReady, mHaveBase);
            },
            [&](RecvBatch& recv) {
                  return  recv.recvTask(mIndex, mPrng, mSock, mAdd, mMult, mCorReady, mHaveBase);
            }
        };
    }

    void OleBatch::mock(u64 batchIdx)
    {
        mAdd.resize(mSize / 128);
        mMult.resize(mSize / 128);
        mSendRecv | match{
            [&](SendBatch& send) {
                send.mock(batchIdx, mAdd, mMult);
            },
            [&](RecvBatch& recv) {
                recv.mock(batchIdx, mAdd, mMult);
            }
        };

    }
    void OleBatch::SendBatch::mock(u64 batchIdx, span<oc::block> add, span<oc::block> mult)
    {
        //memset(add, 0);
        //memset(mult, 0);
        //return;

        auto m = add.size();
        auto m8 = m / 8 * 8;
        oc::block mm8(4532453452, 43254534);
        oc::block mm = oc::mAesFixedKey.ecbEncBlock(oc::block(batchIdx, 0));

        oc::block aa8(0, 43254534);
        oc::block aa(0, mm.get<u64>(0));
        u64 i = 0;
        while (i < m8)
        {
            mult.data()[i + 0] = mm;
            mult.data()[i + 1] = mm;
            mult.data()[i + 2] = mm;
            mult.data()[i + 3] = mm;
            mult.data()[i + 4] = mm;
            mult.data()[i + 5] = mm;
            mult.data()[i + 6] = mm;
            mult.data()[i + 7] = mm;
            add.data()[i + 0] = aa;
            add.data()[i + 1] = aa;
            add.data()[i + 2] = aa;
            add.data()[i + 3] = aa;
            add.data()[i + 4] = aa;
            add.data()[i + 5] = aa;
            add.data()[i + 6] = aa;
            add.data()[i + 7] = aa;
            mm += mm8;
            aa += aa8;
            i += 8;
        }
        for (; i < m; ++i)
        {
            mult[i] = oc::block(i, i);
            add[i] = oc::block(0, i);
        }
    }

    void OleBatch::RecvBatch::mock(u64 batchIdx, span<oc::block> add, span<oc::block> mult)
    {

        //memset(add, 0);
        //memset(mult, 0);
        //return;

        auto m = add.size();
        auto m8 = m / 8 * 8;
        oc::block mm8(4532453452, 43254534);
        oc::block mm = oc::mAesFixedKey.ecbEncBlock(oc::block(batchIdx, 0));

        oc::block aa8(4532453452, 0);
        oc::block aa(mm.get<u64>(1), 0);
        u64 i = 0;
        while (i < m8)
        {
            mult.data()[i + 0] = mm;
            mult.data()[i + 1] = mm;
            mult.data()[i + 2] = mm;
            mult.data()[i + 3] = mm;
            mult.data()[i + 4] = mm;
            mult.data()[i + 5] = mm;
            mult.data()[i + 6] = mm;
            mult.data()[i + 7] = mm;
            add.data()[i + 0] = aa;
            add.data()[i + 1] = aa;
            add.data()[i + 2] = aa;
            add.data()[i + 3] = aa;
            add.data()[i + 4] = aa;
            add.data()[i + 5] = aa;
            add.data()[i + 6] = aa;
            add.data()[i + 7] = aa;
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


    macoro::task<>  OleBatch::RecvBatch::recvTask(
        u64 batchIdx,
        PRNG& prng, 
        oc::Socket& sock,
        oc::AlignedUnVector<oc::block>& add,
        oc::AlignedUnVector<oc::block>& mult,
        macoro::async_manual_reset_event& corReady,
        macoro::async_manual_reset_event& haveBase)
    {
        MC_BEGIN(macoro::task<>, this,batchIdx, &prng, &sock, &add, &mult, &corReady, &haveBase,
            mChoice = oc::BitVector{},
            mMsg = oc::AlignedUnVector<oc::block>{});

        MC_AWAIT(haveBase);
        mChoice.resize(mReceiver.mRequestedNumOts);
        mMsg.resize(mReceiver.mRequestedNumOts);
        assert(mReceiver.mGen.hasBaseOts());
        //std::cout << "recv OleBatch begin " << batchIdx << std::endl;
        MC_AWAIT(mReceiver.silentReceive(mChoice, mMsg, prng, sock));
        //std::cout << "recv OleBatch proto done " << batchIdx << std::endl;
        add.resize(oc::divCeil(mMsg.size(), 128));
        mult.resize(oc::divCeil(mMsg.size(), 128));
        compressRecver(mChoice, mMsg, add, mult);
        mChoice = {};
        mMsg = {};
        corReady.set();
        MC_END();
    }

    macoro::task<>  OleBatch::SendBatch::sendTask(
        u64 batchIdx,
        PRNG& prng,
        oc::Socket& sock,
        oc::AlignedUnVector<oc::block>& add,
        oc::AlignedUnVector<oc::block>& mult,
        macoro::async_manual_reset_event& corReady,
        macoro::async_manual_reset_event& haveBase)
    {
        MC_BEGIN(macoro::task<>, this, batchIdx, &prng, &sock, &add, &mult, &corReady, &haveBase,
            mMsg2 = oc::AlignedUnVector<std::array<oc::block, 2>>{});

        MC_AWAIT(haveBase);
        mMsg2.resize(mSender.mRequestNumOts);
        assert(mSender.mGen.hasBaseOts());
        //std::cout << "send OleBatch begin " << batchIdx << std::endl;
        MC_AWAIT(mSender.silentSend(mMsg2, prng, sock));
        //std::cout << "send OleBatch proto done " << batchIdx << std::endl;
        add.resize(oc::divCeil(mMsg2.size(), 128));
        mult.resize(oc::divCeil(mMsg2.size(), 128));
        compressSender(mMsg2, add, mult);
        corReady.set();
        mMsg2 = {};
        MC_END();
    }




    void OleBatch::RecvBatch::compressRecver(
        oc::BitVector& bv,
        span<oc::block> recvMsg,
        span<oc::block> add,
        span<oc::block> mult)
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


    void OleBatch::SendBatch::compressSender(span<std::array<oc::block, 2>> sendMsg, span<oc::block> add, span<oc::block> mult)
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


}