#include "BinOleBatch.h"
#include "CorGenerator.h"
#include "secure-join/Util/match.h"
#include "secure-join/Util/Simd.h"
#include <cstring>

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

    BaseRequest OleBatch::getBaseRequest(StationaryWorker* worker)
    {
        BaseRequest r;

        if (mGenState->mMock || worker)
            return r;

        mSendRecv | match{
            [&](SendBatch& send) {
                configureBatchOt(send.mSender, mSize);
                r.mSendSize = send.mSender.baseCount().mBaseOtCount;
            },

            [&](RecvBatch& recv) {
                configureBatchOt(recv.mReceiver, mSize);
                r.mChoice = recv.mReceiver.sampleBaseChoiceBits(mPrng);
            }
        };
        return r;
    }

    void OleBatch::setBase(BaseCor& sMsg, StationaryWorker* worker)
    {

        if (mGenState->mMock || worker)
            return;

        mSendRecv | match{
            [&](SendBatch& send) {
                send.mSender.setBaseCors(sMsg.getSendOt(send.mSender.baseCount().mBaseOtCount), {}, mPrng.get<block>());
            },
            [&](RecvBatch& recv) {
                sMsg.setOtBase(recv.mReceiver);
            }
        };
    }

    macoro::task<> OleBatch::getTask(BatchThreadState& threadState)
    {
        return mSendRecv | match{
            [&](SendBatch& send) {
                   return send.sendTask(mGenState, mIndex, mSize, mPrng, mSock, mAdd, mMult, mCorReady, threadState);
            },
            [&](RecvBatch& recv) {
                  return  recv.recvTask(mGenState,mIndex, mSize, mPrng, mSock, mAdd, mMult, mCorReady, threadState);
            }
        };
    }

    void OleBatch::SendBatch::mock(u64 batchIdx, span<oc::block> add, span<oc::block> mult)
    {
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
        macoro::async_manual_reset_event& corReady,
        BatchThreadState& threadState)
    {
        auto baseSend = std::vector<std::array<block, 2>>{};

        add.resize(oc::divCeil(size, 128));
        mult.resize(oc::divCeil(size, 128));
        if (state->mMock)
        {
            mock(batchIdx, add, mult);
        }
        else if (threadState.mStationary)
        {
            auto& worker = *threadState.mStationary;
            auto& protocol = worker.begin(worker.recvOt, CorType::Ole, size, false);
            co_await protocol.silentReceiveInplace(size, prng, sock, oc::ChoiceBitPacking::True);
            compressRecver(protocol.mA, add, mult);
            worker.finish(protocol);
        }
        else
        {

            if (state->mDebug)
            {
                baseSend.resize(mReceiver.baseCount().mBaseOtCount);
                co_await sock.recv(baseSend);

                {
                    for (u64 i = 0; i < baseSend.size(); ++i)
                    {
                        if (std::get<0>(mReceiver.mGenVar).mBaseOTs(i) != baseSend[i][std::get<0>(mReceiver.mGenVar).mBaseChoices(i)])
                        {
                            std::cout << "OleBatch::getTask() base OTs do not match. " << LOCATION << std::endl;
                            std::terminate();
                        }
                    }
                }
            }

            assert(mReceiver.hasBaseCors());
            mReceiver.mA = std::move(threadState.mA);
            mReceiver.mSpin = std::move(threadState.mSpin);
            std::get<0>(mReceiver.mGenVar).mTempBuffer = std::move(threadState.mPprfTemp);
            std::get<0>(mReceiver.mGenVar).mEagerSend = false;

            co_await mReceiver.silentReceiveInplace(mReceiver.mRequestNumOts, prng, sock, oc::ChoiceBitPacking::True);
            compressRecver(mReceiver.mA, add, mult);

            threadState.mA = std::move(mReceiver.mA);
            threadState.mSpin = std::move(mReceiver.mSpin);
            threadState.mPprfTemp = std::move(std::get<0>(mReceiver.mGenVar).mTempBuffer);

        }

        corReady.set();
    }

    macoro::task<>  OleBatch::SendBatch::sendTask(
        GenState* state,
        u64 batchIdx,
        u64 size,
        PRNG& prng,
        oc::Socket& sock,
        oc::AlignedUnVector<oc::block>& add,
        oc::AlignedUnVector<oc::block>& mult,
        macoro::async_manual_reset_event& corReady,
        BatchThreadState& threadState)
    {

        add.resize(oc::divCeil(size, 128));
        mult.resize(oc::divCeil(size, 128));
        if (state->mMock)
        {
            mock(batchIdx, add, mult);
        }
        else if (threadState.mStationary)
        {
            auto& worker = *threadState.mStationary;
            auto& protocol = worker.begin(worker.sendOt, CorType::Ole, size, true);
            co_await protocol.silentSendInplace(protocol.mDelta, size, prng, sock);
            compressSender(*protocol.mDelta, protocol.mB, add, mult);
            worker.finish(protocol);
        }
        else
        {
            if (state->mDebug)
            {
                co_await sock.send(coproto::copy(std::get<0>(mSender.mGenVar).mBaseOTs));
            }

            assert(mSender.hasBaseCors());
            
            mSender.mB = std::move(threadState.mB);
            mSender.mSpin = std::move(threadState.mSpin);
            std::get<0>(mSender.mGenVar).mTempBuffer = std::move(threadState.mPprfTemp);
            std::get<0>(mSender.mGenVar).mEagerSend = false;

            co_await mSender.silentSendInplace(mSender.mDelta, mSender.mRequestNumOts, prng, sock);
            compressSender(*mSender.mDelta, mSender.mB, add, mult);

            threadState.mB = std::move(mSender.mB);
            threadState.mSpin = std::move(mSender.mSpin);
            threadState.mPprfTemp = std::move(std::get<0>(mSender.mGenVar).mTempBuffer);
        }

        corReady.set();
    }


    // the LSB of A is the choice bit of the OT.
    void OleBatch::RecvBatch::compressRecver(
        span<oc::block> A,
        span<oc::block> add,
        span<oc::block> mult)
    {
        auto aIter = reinterpret_cast<u8*>(add.data());
        auto bIter8 = (u8*)mult.data();


        if (add.size() * 128 != A.size())
            throw RTE_LOC;
        if (mult.size() * 128 != A.size())
            throw RTE_LOC;

        auto OneBlock = block(1);
        auto AllOneBlock = block(~0ull, ~0ull);
        block mask = OneBlock ^ AllOneBlock;

        auto m = A.data();

        for (u64 i = 0; i < A.size(); i += 16)
        {
            const u16 choices = packByteBits<0>(gatherLowBytes(m));
            std::memcpy(bIter8, &choices, sizeof(choices));
            bIter8 += sizeof(choices);
            for (u64 j = 0; j < 2; ++j)
            {
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

            const u16 ap = packByteBits<0>(gatherLowBytes(m));
            std::memcpy(aIter, &ap, sizeof(ap));
            aIter += sizeof(ap);
            m += 16;

        }
    }


    void OleBatch::SendBatch::compressSender(
        block delta,
        span<oc::block> B,
        span<oc::block> add,
        span<oc::block> mult)
    {

        auto bIter = reinterpret_cast<u8*>(add.data());
        auto aIter = reinterpret_cast<u8*>(mult.data());

        if (add.size() * 128 != B.size())
            throw RTE_LOC;
        if (mult.size() * 128 != B.size())
            throw RTE_LOC;
        using block = oc::block;

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


            const u16 ap = packByteBits<0>(gatherLowBytes(sendMsg.data()));

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

            const u16 bp = packByteBits<0>(gatherLowBytes(sendMsg.data()));

            assert(aIter + sizeof(u16) <= reinterpret_cast<u8*>(mult.data() + mult.size()));
            assert(bIter + sizeof(u16) <= reinterpret_cast<u8*>(add.data() + add.size()));

            u16 mixed = ap ^ bp;
            std::memcpy(aIter, &mixed, sizeof(mixed));
            aIter += sizeof(mixed);
            std::memcpy(bIter, &ap, sizeof(ap));
            bIter += sizeof(ap);
        }
    }


}