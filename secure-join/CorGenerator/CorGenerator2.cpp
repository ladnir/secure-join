#include "CorGenerator2.h"

namespace secJoin
{
    namespace Corrlation2
    {

        RequestState::RequestState(CorType t, u64 size, std::shared_ptr<GenState>& state, u64 idx)
            : mType(t)
            , mSize(size)
            , mGenState(state)
            , mReqIndex(idx)
        { }

        std::shared_ptr<RequestState> CorGenerator2::request(CorType t, u64  n )
        {
            if (mGenState->mBaseOtsStarted)
                throw std::runtime_error("correlations can not be requested once started. " LOCATION);
            auto r = std::make_shared<RequestState>(t, n, mGenState, mGenState->mRequests.size());
            mGenState->mRequests.push_back(r);
            return r;
        }

        OtBatch::OtBatch(bool sender, oc::Socket&& s, oc::PRNG&& p)
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

        macoro::task<> OtBatch::RecvOtBatch::recvTask(oc::PRNG& prng, oc::Socket& sock)
        {
            mMsg.resize(mReceiver.mRequestedNumOts);
            mChoice.resize(mReceiver.mRequestedNumOts);
            return mReceiver.silentReceive(mChoice, mMsg, prng, sock);
        }

        void OtBatch::RecvOtBatch::mock(u64 batchIdx)
        {
            auto s = oc::mAesFixedKey.ecbEncBlock(oc::block(batchIdx, 0));
            memset(mChoice.data(), s.get<u8>(0), mChoice.sizeBytes());

            for (u32 i = 0; i < mMsg.size(); ++i)
            {
                oc::block m0 = std::array<u32, 4>{i, i, i, i};// prng.get();
                oc::block m1 = std::array<u32, 4>{~i, ~i, ~i, ~i};//prng.get();

                m0 = m0 ^ s;
                m1 = m1 ^ s;
                mMsg[i] = mChoice[i] ? m1 : m0;
            }
        }

        macoro::task<> OtBatch::SendOtBatch::sendTask(oc::PRNG& prng, oc::Socket& sock)
        {
            mMsg2.resize(mSender.mRequestNumOts);
            return mSender.silentSend(mMsg2, prng, sock);
        }

        void OtBatch::SendOtBatch::mock(u64 batchIdx)
        {
            auto s = oc::mAesFixedKey.ecbEncBlock(oc::block(batchIdx, 0));
            for (u32 i = 0; i < mMsg2.size(); ++i)
            {
                mMsg2[i][0] = std::array<u32, 4>{i, i, i, i};// prng.get();
                mMsg2[i][1] = std::array<u32, 4>{~i, ~i, ~i, ~i};//prng.get();


                mMsg2[i][0] = mMsg2[i][0] ^ s;
                mMsg2[i][1] = mMsg2[i][1] ^ s;
            }
        }


        void OtBatch::getCor(Cor* c, u64 begin, u64 size)
        {
            if (mSendRecv.index() == 0)
            {
                if (c->mType != CorType::SendOt)
                    std::terminate();

                auto& d = *static_cast<OtSend*>(c);
                d.mMsg = std::get<0>(mSendRecv).mMsg2.subspan(begin, size);
            }
            else
            {
                if (c->mType != CorType::RecvOt)
                    std::terminate();

                auto& d = *static_cast<OtRecv*>(c);
                auto& recv = std::get<1>(mSendRecv);
                auto& msg = recv.mMsg;
                auto& choice = recv.mChoice;

                d.mMsg = msg.subspan(begin, size);

                if (size == choice.size())
                    d.mChoice = std::move(choice);
                else
                {
                    d.mChoice.resize(0);
                    d.mChoice.append(choice, size, begin);
                }
            }
        }

        BaseRequest OtBatch::getBaseRequest()
        {
            BaseRequest r;
            if (mSendRecv.index() == 0)
            {
                auto& send = std::get<0>(mSendRecv);
                send.mSender.configure(mSize);
                r.mSendSize = send.mSender.silentBaseOtCount();
            }
            else
            {
                auto& recv = std::get<1>(mSendRecv);
                recv.mReceiver.configure(mSize);
                r.mChoice = recv.mReceiver.sampleBaseChoiceBits(mPrng);
            }

            return r;
        }

        void OtBatch::setBase(span<oc::block> rMsg, span<std::array<oc::block, 2>> sMsg)
        {
            if (mSendRecv.index() == 0)
            {
                if (rMsg.size())
                    std::terminate();
                auto& send = std::get<0>(mSendRecv);
                send.mSender.setSilentBaseOts(sMsg);
            }
            else
            {
                if (sMsg.size())
                    std::terminate();
                auto& recv = std::get<1>(mSendRecv);
                recv.mReceiver.setSilentBaseOts(rMsg);
            }
        }

        macoro::task<> OtBatch::getTask() {
            MC_BEGIN(macoro::task<>, this);

            if (mSendRecv.index() == 0)
            {
                MC_AWAIT(std::get<0>(mSendRecv).sendTask(mPrng, mSock));
            }
            else
            {
                MC_AWAIT(std::get<1>(mSendRecv).recvTask(mPrng, mSock));
            }
            mCorReady.set();
            MC_END();
        }

        void OtBatch::mock(u64 batchIdx)
        {
            if (mSendRecv.index() == 0)
            {
                std::get<0>(mSendRecv).mMsg2.resize(mSize);
                std::get<0>(mSendRecv).mock(batchIdx);
            }
            else
            {
                std::get<1>(mSendRecv).mChoice.resize(mSize);
                std::get<1>(mSendRecv).mMsg.resize(mSize);
                std::get<1>(mSendRecv).mock(batchIdx);
            }
        }



        OleBatch::OleBatch(bool sender, oc::Socket&& s, oc::PRNG&& p)
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
            if (mSendRecv.index() == 0)
            {
                auto& send = std::get<0>(mSendRecv);
                send.mSender.configure(mSize);
                r.mSendSize = send.mSender.silentBaseOtCount();
            }
            else
            {
                auto& recv = std::get<1>(mSendRecv);
                recv.mReceiver.configure(mSize);
                r.mChoice = recv.mReceiver.sampleBaseChoiceBits(mPrng);
            }
            return r;
        }

        void OleBatch::setBase(span<oc::block> rMsg, span<std::array<oc::block, 2>> sMsg)
        {
            if (mSendRecv.index() == 0)
            {
                if (rMsg.size())
                    std::terminate();
                auto& send = std::get<0>(mSendRecv);
                send.mSender.setSilentBaseOts(sMsg);
            }
            else
            {
                if (sMsg.size())
                    std::terminate();
                auto& recv = std::get<1>(mSendRecv);
                recv.mReceiver.setSilentBaseOts(rMsg);
            }
        }

        macoro::task<> OleBatch::getTask()
        {
            if (mSendRecv.index() == 0)
                return std::get<0>(mSendRecv).sendTask(mPrng,mSock, mAdd, mMult, mCorReady);
            else
                return std::get<1>(mSendRecv).recvTask(mPrng, mSock, mAdd, mMult, mCorReady);
        }

        void OleBatch::mock(u64 batchIdx)
        {
            mAdd.resize(mSize / 128);
            mMult.resize(mSize / 128);

            if (mSendRecv.index() == 0)
                return std::get<0>(mSendRecv).mock(batchIdx, mAdd, mMult);
            else
                return std::get<1>(mSendRecv).mock(batchIdx, mAdd, mMult);

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


        macoro::task<>  OleBatch::RecvBatch::recvTask(
            oc::PRNG& prng, 
            oc::Socket& sock, 
            oc::AlignedUnVector<oc::block>& add, 
            oc::AlignedUnVector<oc::block>& mult,
            macoro::async_manual_reset_event& corReady)
        {
            MC_BEGIN(macoro::task<>, this, &prng, &sock, &add, &mult, &corReady);
            mChoice.resize(mReceiver.mRequestedNumOts);
            mMsg.resize(mReceiver.mRequestedNumOts);

            MC_AWAIT(mReceiver.silentReceive(mChoice, mMsg, prng, sock));
            add.resize(oc::divCeil(mMsg.size(), 128));
            mult.resize(oc::divCeil(mMsg.size(), 128));
            compressRecver(mChoice, mMsg, add, mult);
            corReady.set();
            MC_END();
        }

        macoro::task<>  OleBatch::SendBatch::sendTask(
            oc::PRNG& prng, 
            oc::Socket& sock, 
            oc::AlignedUnVector<oc::block>& add, 
            oc::AlignedUnVector<oc::block>& mult,
            macoro::async_manual_reset_event& corReady)
        {
            MC_BEGIN(macoro::task<>, this, &prng, &sock, &add, &mult, &corReady);

            mMsg2.resize(mSender.mRequestNumOts);
            MC_AWAIT(mSender.silentSend(mMsg2, prng, sock));
            add.resize(oc::divCeil(mMsg2.size(), 128));
            mult.resize(oc::divCeil(mMsg2.size(), 128));
            compressSender(mMsg2, add, mult);
            corReady.set();
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



        macoro::task<> RequestState::startReq()
        {
            MC_BEGIN(macoro::task<>, this,
                i = u64{},
                s = bool{},
                tasks = std::vector<macoro::eager_task<>>{}
            );
            // check to see if the base OTs have been started.
            s = mGenState->mBaseOtsStarted.exchange(true);
            if (s == false)
            {
                MC_AWAIT(mGenState->startBaseOts());
            }
            //mStarted = true;

            if (mGenState->mMock)
            {
                for (i = 0; i < mBatches.size(); ++i)
                {
                    if (mBatches[i].mBatch->mStarted.exchange(true) == false)
                    {
                        mBatches[i].mBatch->mock(mBatches[i].mBatch->mIndex);
                        mBatches[i].mBatch->mCorReady.set();
                    }
                }
            }
            else
            {
                tasks.reserve(mBatches.size());
                for (i = 0; i < mBatches.size(); ++i)
                {
                    if (mBatches[i].mBatch->mStarted.exchange(true) == false)
                    {
                        tasks.emplace_back(mBatches[i].mBatch->getTask() | macoro::make_eager());
                    }
                }

                for (i = 0; i < tasks.size(); ++i)
                {
                    MC_AWAIT(tasks[i]);
                }
            }

            MC_END();
        }

        u64 RequestState::batchCount()
        {
            return oc::divCeil(mSize, mGenState->mBatchSize) + 1;
        }

        u64 RequestState::size() { return mSize; }

        void RequestState::clear()
        {
            throw RTE_LOC;
        }

        macoro::task<> GenState::startBaseOts()
        {
            MC_BEGIN(macoro::task<>, this,
                i = u64{},
                j = u64{},
                r = u64{},
                s = u64{},
                sMsg = oc::AlignedUnVector<std::array<oc::block, 2>>{},
                rMsg = oc::AlignedUnVector<oc::block>{},
                sProto = macoro::task<>{},
                rProto = macoro::task<>{},
                sPrng = oc::PRNG{},
                rPrng = oc::PRNG{},
                socks = std::array<oc::Socket, 2>{},

                sendOtBatch = (Batch*)nullptr,
                recvOtBatch = (Batch*)nullptr,
                oleBatch = (Batch*)nullptr,
                batches = std::vector<std::shared_ptr<Batch>>{},

                req = BaseRequest{},
                reqs = std::vector<BaseRequest>{},
                temp = std::vector<u8>{},
                res = macoro::result<void>{}

            );

            if (!mBaseOtsStarted)
                std::terminate();

            // map request to batches
            for (i = 0;i < mRequests.size(); ++i)
            {
                for (j = 0;j < mRequests[i]->mSize;)
                {
                    Batch** batch = nullptr;
                    switch (mRequests[i]->mType)
                    {
                    case CorType::RecvOt:
                        batch = &recvOtBatch;
                        break;
                    case CorType::SendOt:
                        batch = &sendOtBatch;
                        break;
                    case CorType::Ole:
                        batch = &oleBatch;
                        break;
                    default:
                        std::terminate();
                        break;
                    }

                    if (*batch == nullptr)
                    {
                        batches.push_back(makeBatch(mRequests[i]->mType, mPartyIdx, mSock.fork(), mPrng.fork()));
                        batches.back()->mIndex = batches.size();
                        *batch = batches.back().get();
                    }

                    auto begin = (*batch)->mSize;
                    auto remReq = mRequests[i]->mSize - j;
                    auto remAvb = mBatchSize - begin;
                    auto size = oc::roundUpTo(128, std::min<u64>(remReq, remAvb));
                    assert(size <= remAvb);

                    std::cout << "batch " << i << "." << mRequests[i]->mBatches.size()
                        << " ( " << batches.size() - 1 << ")" << size << std::endl;

                    batches.back()->mSize += size;

                    mRequests[i]->mBatches.emplace_back(BatchOffset{ batches.back(), begin, size });
                    j += size;

                    if (remAvb == size)
                        *batch = nullptr;
                }

                mRequests[i] = nullptr;
            }

            if (mMock)
                MC_RETURN_VOID();

            // make base OT requests
            reqs.reserve(batches.size());
            for (i = 0; i < batches.size();++i)
            {
                auto& batch = *batches[i];
                if (!batch.mSize)
                    std::terminate();
                reqs.push_back(batch.getBaseRequest());
            }

            req = BaseRequest(reqs);

            socks[0] = mSock;
            socks[1] = mSock.fork();


            if (req.mSendSize)
            {
                sMsg.resize(req.mSendSize);
                sPrng = mPrng.fork();
                sMsg.resize(req.mSendSize);
                sProto = mSendBase.send(sMsg, sPrng, socks[mPartyIdx]);
            }

            // perform recv base OTs
            if (req.mChoice.size())
            {
                rPrng = mPrng.fork();
                rMsg.resize(req.mChoice.size());
                rProto = mRecvBase.receive(req.mChoice, rMsg, mPrng, socks[1 ^ mPartyIdx]);
            }

            // perform the protocol (in parallel if both are active).
            if (req.mSendSize && req.mChoice.size())
            {
                std::cout << "s " << req.mSendSize << " " << socks[mPartyIdx].mId << " r " << req.mChoice.size() << " " << socks[1 ^ mPartyIdx].mId << std::endl;
                MC_AWAIT(macoro::when_all_ready(
                    std::move(rProto), std::move(sProto)
                ));
            }
            else if (req.mSendSize)
            {
                std::cout << "s " << req.mSendSize << " " << socks[mPartyIdx].mId << std::endl;
                temp.resize(10);
                MC_AWAIT(socks[mPartyIdx].send(std::move(temp)));

                temp.resize(10);
                MC_AWAIT_TRY(res, socks[mPartyIdx].recv(temp));

                try {
                    res.value();
                }
                catch (std::exception& e)
                {
                    std::cout << e.what() << std::endl;
                }
                MC_AWAIT(sProto);
            }
            else
            {
                std::cout << " r " << req.mChoice.size() << " " << socks[1 ^ mPartyIdx].mId << std::endl;
                temp.resize(10);
                MC_AWAIT(socks[1^mPartyIdx].send(std::move(temp)));

                temp.resize(10);
                MC_AWAIT_TRY(res, socks[1 ^ mPartyIdx].recv(temp));
                try {
                    res.value();
                }
                catch (std::exception& e)
                {
                    std::cout << e.what() << std::endl;
                }
                MC_AWAIT(rProto);
            }

            std::cout << "done s " << req.mSendSize << " r " << req.mChoice.size() << std::endl;


            for (i = 0, r = 0, s = 0; i < batches.size(); ++i)
            {
                auto& batch = *batches[i];
                auto rBase = rMsg.subspan(r, reqs[i].mChoice.size());
                r += reqs[i].mChoice.size();

                auto sBase = sMsg.subspan(s, reqs[i].mSendSize);
                s += reqs[i].mSendSize;

                batch.setBase(rBase, sBase);

                std::cout << " batch " << i << " done" << std::endl;
                batch.mHaveBase.set();
            }


            MC_END();
        }

        void GenState::set(SendBase& b) { mRecvBase.setBaseOts(b.get()); }
        void GenState::set(RecvBase& b) { mSendBase.setBaseOts(b.get(), b.mChoice); }

    }

}