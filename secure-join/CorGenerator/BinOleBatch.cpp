#include "BinOleBatch.h"
#include "CorGenerator.h"
#include "secure-join/Util/match.h"
#include "PackedOtHash.h"
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

        if (mGenState->mMock)
            return r;

        if (worker)
            return worker->requestOtBase(CorType::Ole, mSize, mSendRecv.index() == 0, mPrng);

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

        if (mGenState->mMock)
            return;

        if (worker)
            return worker->setOtBase(CorType::Ole, mSize, mSendRecv.index() == 0, sMsg);

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
        if (A.size() % 128 || add.size() != A.size() / 128 || mult.size() != add.size())
            throw RTE_LOC;

#if defined(ENABLE_SSE) && defined(__VAES__) && defined(__AVX2__) && !defined(__AVX512F__)
        packedOt::compressReceiver256<1>(A, add, {&mult});
#else
        for (u64 i = 0; i < A.size(); i += packedOt::groupSize)
        {
            const packedOt::Group group(A.data() + i);
            const auto choices = group.choices<0>();
            const auto bits = group.hashes<1>();
            packedOt::store(mult.data(), i, choices);
            packedOt::store(add.data(), i, bits);
        }
#endif
    }

    void OleBatch::SendBatch::compressSender(
        block delta,
        span<oc::block> B,
        span<oc::block> add,
        span<oc::block> mult)
    {
        if (B.size() % 128 || add.size() != B.size() / 128 || mult.size() != add.size())
            throw RTE_LOC;

#if defined(ENABLE_SSE) && defined(__VAES__) && defined(__AVX2__) && !defined(__AVX512F__)
        const auto& keys = oc::mAesFixedKey.mRoundKey;
        const block mask(~0ull, ~1ull);
        const auto messageMask = _mm256_broadcastsi128_si256(mask.mData);
        const auto firstZero = _mm256_broadcastsi128_si256(keys[0].mData);
        const auto firstOne = _mm256_broadcastsi128_si256((keys[0] ^ (delta & mask)).mData);
        __m256i roundKeys[10];
        for (unsigned r = 0; r < 10; ++r)
            roundKeys[r] = _mm256_broadcastsi128_si256(keys[r + 1].mData);

        for (u64 i = 0; i < B.size(); i += 8)
        {
            // Four pairs of input blocks, with both choices in flight.
            __m256i state[8];
            for (unsigned j = 0; j < 4; ++j)
            {
                const auto x = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(B.data() + i + 2 * j));
                const auto masked = _mm256_and_si256(x, messageMask);
                state[j] = _mm256_xor_si256(masked, firstZero);
                state[j + 4] = _mm256_xor_si256(masked, firstOne);
            }
#pragma GCC unroll 9
            for (unsigned r = 0; r < 9; ++r)
                for (auto& v : state)
                    v = _mm256_aesenc_epi128(v, roundKeys[r]);
            for (auto& v : state)
                v = _mm256_aesenclast_epi128(v, roundKeys[9]);

            // Both hash inputs have bit zero cleared, so AES supplies the output bit.
            const auto zero = packedOt::lowWords(state[0], state[1], state[2], state[3]);
            const auto one = packedOt::lowWords(state[4], state[5], state[6], state[7]);
            const auto z = packedOt::packBit<0>(packedOt::inInputOrder(zero));
            const auto o = packedOt::packBit<0>(packedOt::inInputOrder(one));
            reinterpret_cast<u8*>(add.data())[i / 8] = z;
            reinterpret_cast<u8*>(mult.data())[i / 8] = z ^ o;
        }
#else
        for (u64 i = 0; i < B.size(); i += packedOt::groupSize)
        {
            const packedOt::Group group(B.data() + i);
            const auto zero = group.hashes<1>();
            const auto one = group.hashes<1>(delta);
            packedOt::store(add.data(), i, zero);
            packedOt::store(mult.data(), i, zero ^ one);
        }
#endif
    }
}
