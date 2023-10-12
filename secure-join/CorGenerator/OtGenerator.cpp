#include "OtGenerator.h"

namespace secJoin
{
    namespace Corrlation
    {

        std::shared_ptr<RequestState> OtGenerator::request(bool sender, u64 n)
        {
            if (mGenState->mBaseOtsStarted)
                throw std::runtime_error("correlations can not be requested once started. " LOCATION);
            auto r = std::make_shared<RequestState>();
            r->mGenState = mGenState;
            r->mSize = n;
            r->mSender = sender;
            r->mReqIndex = mGenState->mRequests.size();
            mGenState->mRequests.push_back(r);

            return r;
        }

        //void fakeFill(u64 m, Batch& ole, oc::block s)
        //{
        //    throw RTE_LOC;
        //    //if (ole.mState->mRole == Role::Receiver)
        //    //{

        //    //    ole.mMsg.resize(m);
        //    //    ole.mChoice.resize(m);
        //    //    memset(ole.mChoice.data(), 0b10101100, ole.mChoice.sizeBytes());

        //    //    for (u32 i = 0; i < ole.mMsg.size(); ++i)
        //    //    {
        //    //        oc::block m0 = std::array<u32, 4>{i, i, i, i};// prng.get();
        //    //        oc::block m1 = std::array<u32, 4>{~i, ~i, ~i, ~i};//prng.get();

        //    //        m0 = m0 ^ s;
        //    //        m1 = m1 ^ s;
        //    //        ole.mMsg[i] = ole.mChoice[i] ? m1 : m0;
        //    //    }
        //    //}
        //    //else
        //    //{
        //    //    ole.mMsg2.resize(m);
        //    //    for (u32 i = 0; i < ole.mMsg2.size(); ++i)
        //    //    {
        //    //        ole.mMsg2[i][0] = std::array<u32, 4>{i, i, i, i};// prng.get();
        //    //        ole.mMsg2[i][1] = std::array<u32, 4>{~i, ~i, ~i, ~i};//prng.get();
        //    //        ole.mMsg2[i][0] = ole.mMsg2[i][0] ^ s;
        //    //        ole.mMsg2[i][1] = ole.mMsg2[i][1] ^ s;
        //    //    }
        //    //}
        //}


        void Batch::init(bool sender, std::shared_ptr<GenState>& state)
        {
            mState = state;
            mSock = state->mSock.fork();
            mPrng = state->mPrng.fork();
            if (sender)
            {
                mSendRecv.emplace<0>();
            }
            else
            {
                mSendRecv.emplace<1>();
            }
            //mStarted = false;
        }


        //void Batch::start()
        //{
        //    auto s = mStarted.exchange(true);
        //    if (s == false)
        //    {
        //        mTask = std::move(mT) | macoro::make_eager();
        //    }
        //}

        macoro::task<> Batch::RecvBatch::recvTask(oc::PRNG& prng, oc::Socket& sock)
        {
            mMsg.resize(mReceiver.mRequestedNumOts);
            mChoice.resize(mReceiver.mRequestedNumOts);
            return mReceiver.silentReceive(mChoice, mMsg, prng, sock);
        }

        void Batch::RecvBatch::mock(u64 batchIdx)
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

        macoro::task<> Batch::SendBatch::sendTask(oc::PRNG& prng, oc::Socket& sock)
        {
            mMsg2.resize(mSender.mRequestNumOts);
            return mSender.silentSend(mMsg2, prng, sock);
        }

        void Batch::SendBatch::mock(u64 batchIdx)
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


        macoro::task<> Batch::getTask() {
            MC_BEGIN(macoro::task<>, this, i = u64{});

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

        void Batch::mock(u64 batchIdx)
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
            TODO("request . clear");
        }



        macoro::task<> GenState::startBaseOts()
        {
            MC_BEGIN(macoro::task<>, this,
                i = u64{},
                j = u64{},
                s = u64{},
                r = u64{},
                sendCount = u64{ 0 },
                sMsg = oc::AlignedUnVector<std::array<oc::block, 2>>{},
                rMsg = oc::AlignedUnVector<oc::block>{},
                choice = oc::BitVector{},
                sProto = macoro::task<>{},
                rProto = macoro::task<>{},
                sPrng = oc::PRNG{},
                rPrng = oc::PRNG{},
                socks = std::array<oc::Socket, 2>{}
            );

            if (!mBaseOtsStarted)
                std::terminate();

            // map request to batches
            for (i = 0;i < mRequests.size(); ++i)
            {
                for (j = 0;j < mRequests[i]->mSize;)
                {
                    if (mBatches.size() == 0 || mBatches.back()->mSize == mBatchSize)
                    {
                        mBatches.emplace_back(std::make_shared<Batch>());
                        mBatches.back()->init(mRequests[i]->mSender, shared_from_this());
                        mBatches.back()->mIndex = mBatches.size();
                    }

                    auto begin = mBatches.back()->mSize;
                    auto remReq = mRequests[i]->mSize - j;
                    auto remAvb = mBatchSize - begin;
                    auto size = oc::roundUpTo(128, std::min<u64>(remReq, remAvb));
                    assert(size <= remAvb);

                    std::cout << "batch " << i << "." << mRequests[i]->mBatches.size()
                        << " ( " << mBatches.size() - 1 << ")" << size << std::endl;

                    mBatches.back()->mSize += size;

                    mRequests[i]->mBatches.emplace_back(BatchOffset{ mBatches.back(), begin, size });

                    j += size;
                }
            }

            if (mMock)
                MC_RETURN_VOID();

            // make base OT requests
            for (i = 0; i < mBatches.size();++i)
            {
                auto& batch = *mBatches[i];
                if (!batch.mSize)
                    std::terminate();

                if (batch.mSendRecv.index() == 0)
                {
                    auto& send = std::get<0>(batch.mSendRecv);
                    send.mSender.configure(batch.mSize);
                    sendCount += send.mSender.silentBaseOtCount();
                }
                else
                {
                    auto& recv = std::get<1>(batch.mSendRecv);
                    recv.mReceiver.configure(batch.mSize);
                    recv.mChoice = recv.mReceiver.sampleBaseChoiceBits(mPrng);
                    choice.append(recv.mChoice);
                }
            }

            socks[0] = mSock;
            socks[1] = mSock.fork();

            std::cout << "s " << sendCount << " " << socks[mPartyIdx].mId << " r " << choice.size() << " " << socks[1 ^ mPartyIdx].mId << std::endl;

            if (sendCount)
            {
                sMsg.resize(sendCount);
                sPrng = mPrng.fork();
                sMsg.resize(sendCount);
                sProto = mSendBase.send(sMsg, sPrng, socks[mPartyIdx]);
            }

            // perform recv base OTs
            if (choice.size())
            {
                rPrng = mPrng.fork();
                rMsg.resize(choice.size());
                rProto = mRecvBase.receive(choice, rMsg, mPrng, socks[1 ^ mPartyIdx]);
            }

            // perform the protocol (in parallel if both are active).
            if (sendCount && choice.size())
                MC_AWAIT(macoro::when_all_ready(
                    std::move(rProto), std::move(sProto)
                ));
            else if (sendCount)
            {
                MC_AWAIT(sProto);
            }
            else
            {
                MC_AWAIT(rProto);
            }

            std::cout << "done s " << sendCount << " r " << choice.size() << std::endl;


            for (i = 0, s = 0, r = 0; i < mBatches.size(); ++i)
            {
                auto& batch = *mBatches[i];
                if (batch.mSendRecv.index() == 0)
                {
                    auto& send = std::get<0>(batch.mSendRecv);
                    send.mSender.setSilentBaseOts(sMsg.subspan(s, send.mSender.silentBaseOtCount()));
                    s += send.mSender.silentBaseOtCount();
                }
                else
                {
                    auto& recv = std::get<1>(batch.mSendRecv);
                    recv.mReceiver.setSilentBaseOts(rMsg.subspan(r, recv.mReceiver.silentBaseOtCount()));
                    r += recv.mReceiver.silentBaseOtCount();
                }

                std::cout << " batch " << i << " done" << std::endl;
                batch.mHaveBase.set();
            }


            MC_END();
        }

        void GenState::set(SendBase& b) { mRecvBase.setBaseOts(b.get()); }
        void GenState::set(RecvBase& b) { mSendBase.setBaseOts(b.get(), b.mChoice); }





        macoro::task<> OtRecvGenerator::Request::get(OtRecv& d)
        {
            MC_BEGIN(macoro::task<>, this, &d);


            if (mReqState->mNextBatchIdx >= mReqState->mBatches.size())
                throw RTE_LOC;

            if (mReqState->mStarted == false)
                std::terminate();

            MC_AWAIT(mReqState->mBatches[mReqState->mNextBatchIdx].mBatch->mCorReady);

            {
                auto& batch = mReqState->mBatches[mReqState->mNextBatchIdx++];
                auto& recv = std::get<1>(batch.mBatch->mSendRecv);
                auto& msg = recv.mMsg;
                auto& choice = recv.mChoice;
                auto begin = batch.mBegin;
                auto size = batch.mSize;

                d.mRequest = mReqState;
                d.mMsg = msg.subspan(begin, size);

                if (size == choice.size())
                    d.mChoice = std::move(choice);
                else
                {
                    d.mChoice.resize(0);
                    d.mChoice.append(choice, size, begin);
                }
            }

            MC_END();
        }

        macoro::task<> OtSendGenerator::Request::get(OtSend& d)
        {
            MC_BEGIN(macoro::task<>, this, &d);

            if (mReqState->mNextBatchIdx >= mReqState->mBatches.size())
                throw RTE_LOC;

            MC_AWAIT(mReqState->mBatches[mReqState->mNextBatchIdx].mBatch->mCorReady);

            {

                auto& idx = mReqState->mNextBatchIdx;
                auto& batch = mReqState->mBatches[idx];
                auto& send = std::get<0>(batch.mBatch->mSendRecv);
                auto& msg = send.mMsg2;
                auto begin = batch.mBegin;
                auto size = batch.mSize;
                d.mRequest = mReqState;
                d.mMsg = msg.subspan(begin, size);
                ++idx;
            }

            MC_END();
        }

    }

}