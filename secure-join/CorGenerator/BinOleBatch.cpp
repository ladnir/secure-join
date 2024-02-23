#include "BinOleBatch.h"
#include "CorGenerator.h"
#include "secure-join/Util/match.h"
namespace secJoin
{



    OleBatch::OleBatch(GenState* state, bool sender, oc::Socket&& s, PRNG&& p)
        : Batch(state, std::move(s), std::move(p))
    {
        if (sender)
            mSendRecv.emplace<0>();
        else
            mSendRecv.emplace<1>();
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

        if (mGenState->mMock)
            return r;

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

        if (mGenState->mMock)
            return;

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
    }

    macoro::task<> OleBatch::getTask()
    {
        return mSendRecv | match{
            [&](SendBatch& send) {
                   return send.sendTask(mGenState, mIndex, mSize, mPrng, mSock, mAdd, mMult, mCorReady);
            },
            [&](RecvBatch& recv) {
                  return  recv.recvTask(mGenState,mIndex, mSize, mPrng, mSock, mAdd, mMult, mCorReady);
            }
        };
    }

    //void OleBatch::mock(u64 batchIdx)
    //{

    //    mCorReady.set();

    //}
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
            mult.data()[i + 1] = mm >> 1 | mm << 1;
            mult.data()[i + 2] = mm >> 2 | mm << 2;
            mult.data()[i + 3] = mm >> 3 | mm << 3;
            mult.data()[i + 4] = mm >> 4 | mm << 4;
            mult.data()[i + 5] = mm >> 5 | mm << 5;
            mult.data()[i + 6] = mm >> 6 | mm << 6;
            mult.data()[i + 7] = mm >> 7 | mm << 7;
            add.data()[i + 0] = aa;
            add.data()[i + 1] = aa >> 1 | aa << 1;
            add.data()[i + 2] = aa >> 2 | aa << 2;
            add.data()[i + 3] = aa >> 3 | aa << 3;
            add.data()[i + 4] = aa >> 4 | aa << 4;
            add.data()[i + 5] = aa >> 5 | aa << 5;
            add.data()[i + 6] = aa >> 6 | aa << 6;
            add.data()[i + 7] = aa >> 7 | aa << 7;
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
            mult.data()[i + 1] = mm >> 1 | mm << 1;
            mult.data()[i + 2] = mm >> 2 | mm << 2;
            mult.data()[i + 3] = mm >> 3 | mm << 3;
            mult.data()[i + 4] = mm >> 4 | mm << 4;
            mult.data()[i + 5] = mm >> 5 | mm << 5;
            mult.data()[i + 6] = mm >> 6 | mm << 6;
            mult.data()[i + 7] = mm >> 7 | mm << 7;
            add.data()[i + 0] = aa;
            add.data()[i + 1] = aa >> 1 | aa << 1;
            add.data()[i + 2] = aa >> 2 | aa << 2;
            add.data()[i + 3] = aa >> 3 | aa << 3;
            add.data()[i + 4] = aa >> 4 | aa << 4;
            add.data()[i + 5] = aa >> 5 | aa << 5;
            add.data()[i + 6] = aa >> 6 | aa << 6;
            add.data()[i + 7] = aa >> 7 | aa << 7;
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
        GenState* state,
        u64 batchIdx,
        u64 size,
        PRNG& prng,
        oc::Socket& sock,
        oc::AlignedUnVector<oc::block>& add,
        oc::AlignedUnVector<oc::block>& mult,
        macoro::async_manual_reset_event& corReady)
    {
        MC_BEGIN(macoro::task<>, this, state, batchIdx, size, &prng, &sock, &add, &mult, &corReady,
            baseSend = std::vector<std::array<block, 2>>{});

        add.resize(oc::divCeil(size, 128));
        mult.resize(oc::divCeil(size, 128));
        if (state->mMock)
        {
            mock(batchIdx, add, mult);
        }
        else
        {

            if (state->mDebug)
            {
                baseSend.resize(mReceiver.silentBaseOtCount());
                MC_AWAIT(sock.recv(baseSend));

                {
                    for (u64 i = 0; i < baseSend.size(); ++i)
                    {
                        if (mReceiver.mGen.mBaseOTs(i) != baseSend[i][mReceiver.mGen.mBaseChoices(i)])
                        {
                            std::cout << "OleBatch::getTask() base OTs do not match. " << LOCATION << std::endl;
                            std::terminate();
                        }
                    }
                }
            }

            assert(mReceiver.mGen.hasBaseOts());
            mReceiver.mMultType = oc::MultType::Tungsten;
            MC_AWAIT(mReceiver.silentReceiveInplace(mReceiver.mRequestNumOts, prng, sock, oc::ChoiceBitPacking::True));
            compressRecver(mReceiver.mA, add, mult);
            mReceiver.clear();
        }

        corReady.set();
        MC_END();
    }

    macoro::task<>  OleBatch::SendBatch::sendTask(
        GenState* state,
        u64 batchIdx,
        u64 size,
        PRNG& prng,
        oc::Socket& sock,
        oc::AlignedUnVector<oc::block>& add,
        oc::AlignedUnVector<oc::block>& mult,
        macoro::async_manual_reset_event& corReady)
    {
        MC_BEGIN(macoro::task<>, this, state, batchIdx, size, &prng, &sock, &add, &mult, &corReady);


        add.resize(oc::divCeil(size, 128));
        mult.resize(oc::divCeil(size, 128));
        if (state->mMock)
        {
            mock(batchIdx, add, mult);
        }
        else
        {

            if (state->mDebug)
            {
                MC_AWAIT(sock.send(coproto::copy(mSender.mGen.mBaseOTs)));
            }

            assert(mSender.mGen.hasBaseOts());
            mSender.mMultType = oc::MultType::Tungsten;
            MC_AWAIT(mSender.silentSendInplace(prng.get(), mSender.mRequestNumOts, prng, sock));
            compressSender(mSender.mDelta, mSender.mB, add, mult);
            mSender.clear();
        }


        corReady.set();
        MC_END();
    }



    // the LSB of A is the choice bit of the OT.

    void OleBatch::RecvBatch::compressRecver(
        span<oc::block> A,
        span<oc::block> add,
        span<oc::block> mult)
    {
        auto aIter16 = (u16*)add.data();
        auto bIter8 = (u8*)mult.data();


        if (add.size() * 128 != A.size())
            throw RTE_LOC;
        if (mult.size() * 128 != A.size())
            throw RTE_LOC;

        auto shuffle = std::array<block, 16>{};
        memset(shuffle.data(), 1 << 7, sizeof(*shuffle.data()) * shuffle.size());
        for (u64 i = 0; i < 16; ++i)
            shuffle[i].set<u8>(i, 0);

        auto OneBlock = block(1);
        auto AllOneBlock = block(~0ull, ~0ull);
        block mask = OneBlock ^ AllOneBlock;

        auto m = &A[0];

        for (u64 i = 0; i < A.size(); i += 16)
        {
            for (u64 j = 0; j < 2; ++j)
            {
                // extract the choice bit from the LSB of m
                u32 b0 = m[0].testc(OneBlock);
                u32 b1 = m[1].testc(OneBlock);
                u32 b2 = m[2].testc(OneBlock);
                u32 b3 = m[3].testc(OneBlock);
                u32 b4 = m[4].testc(OneBlock);
                u32 b5 = m[5].testc(OneBlock);
                u32 b6 = m[6].testc(OneBlock);
                u32 b7 = m[7].testc(OneBlock);

                // pack the choice bits.
                *bIter8++ =
                    b0 ^
                    (b1 << 1) ^
                    (b2 << 2) ^
                    (b3 << 3) ^
                    (b4 << 4) ^
                    (b5 << 5) ^
                    (b6 << 6) ^
                    (b7 << 7);

                // mask of the choice bit which is stored in the LSB
                m[0] = m[0] & mask;
                m[1] = m[1] & mask;
                m[2] = m[2] & mask;
                m[3] = m[3] & mask;
                m[4] = m[4] & mask;
                m[5] = m[5] & mask;
                m[6] = m[6] & mask;
                m[7] = m[7] & mask;

                oc::mAesFixedKey.hashBlocks<8>(m, m);

                m += 8;
            }
            m -= 16;

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

            block a00 = _mm_shuffle_epi8(m[0], shuffle[0]);
            block a01 = _mm_shuffle_epi8(m[1], shuffle[1]);
            block a02 = _mm_shuffle_epi8(m[2], shuffle[2]);
            block a03 = _mm_shuffle_epi8(m[3], shuffle[3]);
            block a04 = _mm_shuffle_epi8(m[4], shuffle[4]);
            block a05 = _mm_shuffle_epi8(m[5], shuffle[5]);
            block a06 = _mm_shuffle_epi8(m[6], shuffle[6]);
            block a07 = _mm_shuffle_epi8(m[7], shuffle[7]);
            block a08 = _mm_shuffle_epi8(m[8], shuffle[8]);
            block a09 = _mm_shuffle_epi8(m[9], shuffle[9]);
            block a10 = _mm_shuffle_epi8(m[10], shuffle[10]);
            block a11 = _mm_shuffle_epi8(m[11], shuffle[11]);
            block a12 = _mm_shuffle_epi8(m[12], shuffle[12]);
            block a13 = _mm_shuffle_epi8(m[13], shuffle[13]);
            block a14 = _mm_shuffle_epi8(m[14], shuffle[14]);
            block a15 = _mm_shuffle_epi8(m[15], shuffle[15]);

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
            m += 16;

        }
    }


    void OleBatch::SendBatch::compressSender(
        block delta,
        span<oc::block> B,
        span<oc::block> add,
        span<oc::block> mult)
    {

        auto bIter16 = (u16*)add.data();
        auto aIter16 = (u16*)mult.data();

        if (add.size() * 128 != B.size())
            throw RTE_LOC;
        if (mult.size() * 128 != B.size())
            throw RTE_LOC;
        using block = oc::block;

        auto shuffle = std::array<block, 16>{};
        memset(shuffle.data(), 1 << 7, sizeof(*shuffle.data()) * shuffle.size());
        for (u64 i = 0; i < 16; ++i)
            shuffle[i].set<u8>(i, 0);

        std::array<block, 16> sendMsg;
        auto m = B.data();

        auto OneBlock = block(1);
        auto AllOneBlock = block(~0ull, ~0ull);
        block mask = OneBlock ^ AllOneBlock;
        delta = delta & mask;

        for (u64 i = 0; i < B.size(); i += 16)
        {
            auto s = sendMsg.data();

            for (u64 j = 0; j < 2; ++j)
            {
                m[0] = m[0] & mask;
                m[1] = m[1] & mask;
                m[2] = m[2] & mask;
                m[3] = m[3] & mask;
                m[4] = m[4] & mask;
                m[5] = m[5] & mask;
                m[6] = m[6] & mask;
                m[7] = m[7] & mask;

                oc::mAesFixedKey.hashBlocks<8>(m, s);

                s += 8;
                m += 8;
            }


            block a00 = _mm_shuffle_epi8(sendMsg[0], shuffle[0]);
            block a01 = _mm_shuffle_epi8(sendMsg[1], shuffle[1]);
            block a02 = _mm_shuffle_epi8(sendMsg[2], shuffle[2]);
            block a03 = _mm_shuffle_epi8(sendMsg[3], shuffle[3]);
            block a04 = _mm_shuffle_epi8(sendMsg[4], shuffle[4]);
            block a05 = _mm_shuffle_epi8(sendMsg[5], shuffle[5]);
            block a06 = _mm_shuffle_epi8(sendMsg[6], shuffle[6]);
            block a07 = _mm_shuffle_epi8(sendMsg[7], shuffle[7]);
            block a08 = _mm_shuffle_epi8(sendMsg[8], shuffle[8]);
            block a09 = _mm_shuffle_epi8(sendMsg[9], shuffle[9]);
            block a10 = _mm_shuffle_epi8(sendMsg[10], shuffle[10]);
            block a11 = _mm_shuffle_epi8(sendMsg[11], shuffle[11]);
            block a12 = _mm_shuffle_epi8(sendMsg[12], shuffle[12]);
            block a13 = _mm_shuffle_epi8(sendMsg[13], shuffle[13]);
            block a14 = _mm_shuffle_epi8(sendMsg[14], shuffle[14]);
            block a15 = _mm_shuffle_epi8(sendMsg[15], shuffle[15]);

            s = sendMsg.data();
            m -= 16;
            for (u64 j = 0; j < 2; ++j)
            {
                s[0] = m[0] ^ delta;
                s[1] = m[1] ^ delta;
                s[2] = m[2] ^ delta;
                s[3] = m[3] ^ delta;
                s[4] = m[4] ^ delta;
                s[5] = m[5] ^ delta;
                s[6] = m[6] ^ delta;
                s[7] = m[7] ^ delta;

                oc::mAesFixedKey.hashBlocks<8>(s, s);

                s += 8;
                m += 8;
            }

            block b00 = _mm_shuffle_epi8(sendMsg[0], shuffle[0]);
            block b01 = _mm_shuffle_epi8(sendMsg[1], shuffle[1]);
            block b02 = _mm_shuffle_epi8(sendMsg[2], shuffle[2]);
            block b03 = _mm_shuffle_epi8(sendMsg[3], shuffle[3]);
            block b04 = _mm_shuffle_epi8(sendMsg[4], shuffle[4]);
            block b05 = _mm_shuffle_epi8(sendMsg[5], shuffle[5]);
            block b06 = _mm_shuffle_epi8(sendMsg[6], shuffle[6]);
            block b07 = _mm_shuffle_epi8(sendMsg[7], shuffle[7]);
            block b08 = _mm_shuffle_epi8(sendMsg[8], shuffle[8]);
            block b09 = _mm_shuffle_epi8(sendMsg[9], shuffle[9]);
            block b10 = _mm_shuffle_epi8(sendMsg[10], shuffle[10]);
            block b11 = _mm_shuffle_epi8(sendMsg[11], shuffle[11]);
            block b12 = _mm_shuffle_epi8(sendMsg[12], shuffle[12]);
            block b13 = _mm_shuffle_epi8(sendMsg[13], shuffle[13]);
            block b14 = _mm_shuffle_epi8(sendMsg[14], shuffle[14]);
            block b15 = _mm_shuffle_epi8(sendMsg[15], shuffle[15]);

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