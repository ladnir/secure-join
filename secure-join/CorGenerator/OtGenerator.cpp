#include "OtGenerator.h"

namespace secJoin
{
    namespace Corrlation
    {

        std::shared_ptr<RequestState> OtGenerator::request(u64 n)
        {
            if (mState->mStarted)
                throw std::runtime_error("correlations can not be requested once started. " LOCATION);
            auto r = std::make_shared<RequestState>();
            r->mState = mState;
            r->mSize = n;
            mState->mRequests.push_back(r);

            return r;
        }

        void fakeFill(u64 m, Batch& ole, oc::block s)
        {
            if (ole.mState->mRole == Role::Receiver)
            {

                ole.mMsg.resize(m);
                ole.mChoice.resize(m);
                memset(ole.mChoice.data(), 0b10101100, ole.mChoice.sizeBytes());

                for (u32 i = 0; i < ole.mMsg.size(); ++i)
                {
                    oc::block m0 = std::array<u32, 4>{i, i, i, i};// prng.get();
                    oc::block m1 = std::array<u32, 4>{~i, ~i, ~i, ~i};//prng.get();

                    m0 = m0 ^ s;
                    m1 = m1 ^ s;
                    ole.mMsg[i] = ole.mChoice[i] ? m1 : m0;
                }
            }
            else
            {
                ole.mMsg2.resize(m);
                for (u32 i = 0; i < ole.mMsg2.size(); ++i)
                {
                    ole.mMsg2[i][0] = std::array<u32, 4>{i, i, i, i};// prng.get();
                    ole.mMsg2[i][1] = std::array<u32, 4>{~i, ~i, ~i, ~i};//prng.get();


                    ole.mMsg2[i][0] = ole.mMsg2[i][0] ^ s;
                    ole.mMsg2[i][1] = ole.mMsg2[i][1] ^ s;
                }
            }
        }


        void Batch::init(std::shared_ptr<State>& state)
        {
            mState = state;
            mSock = state->mSock.fork();
            mPrng = state->mPrng.fork();
            mStarted = false;
        }

        void Batch::finalize()
        {
            if (mState->mRole == Role::Sender)
            {
                mSender.configure(mSize);
                mMsg2.resize(mSender.silentBaseOtCount());
                mState->requestBase(mMsg2, mHaveBase);
                mT = sendTask();
            }
            else
            {
                mReceiver.configure(mSize);
                mMsg.resize(mReceiver.silentBaseOtCount());
                mChoice = mReceiver.sampleBaseChoiceBits(mState->mPrng);
                mState->requestBase(mMsg, std::move(mChoice), mHaveBase);
                mT = recvTask();
            }
        }


        void Batch::start()
        {
            auto s = mStarted.exchange(true);
            if (s == false)
            {
                mTask = std::move(mT) | macoro::make_eager();
            }
        }

        macoro::task<> Batch::recvTask()
        {
            mMsg.resize(mReceiver.mRequestedNumOts);
            mChoice.resize(mReceiver.mRequestedNumOts);
            return mReceiver.silentReceive(mChoice, mMsg, mPrng, mSock);
        }

        macoro::task<> Batch::sendTask()
        {
            mMsg2.resize(mSender.mRequestNumOts);
            return mSender.silentSend(mMsg2, mPrng, mSock);
        }


        macoro::task<> Batch::task() {
            MC_BEGIN(macoro::task<>, this, i = u64{});

            if (!mT.handle())
                throw RTE_LOC;

            MC_AWAIT(mT);
            mDone.set();
            MC_END();
        }







        macoro::task<> RequestState::start()
        {
            MC_BEGIN(macoro::task<>, this,
                i = u64{},
                s = bool{});
            if (mState->mMock)
                MC_RETURN_VOID();

            s = mState->mStarted.exchange(true);
            if (s == false)
            {
                MC_AWAIT(mState->start());
            }

            MC_AWAIT(mState->startRequest(this->shared_from_this()));
            //for (i = 0; i < mBatches.size(); ++i)
            //    mBatches[i].mBatch->start();

            for (i = 0; i < mBatches.size(); ++i)
            {
                MC_AWAIT(mBatches[i].mBatch->mDone);
            }

            MC_END();
        }

        u64 RequestState::batchCount()
        {
            return oc::divCeil(mSize, mState->mBatchSize) + 1;
        }

        u64 RequestState::size() { return mSize; }

        void RequestState::clear()
        {
            TODO("request . clear");
        }



        void State::requestBase(span<std::array<oc::block, 2>> msg, macoro::async_manual_reset_event& done)
        {
            if (mStarted)
                throw RTE_LOC;
            mSendRequest.emplace_back(msg, done);
        }

        void State::requestBase(span<oc::block> msg, oc::BitVector&& choice, macoro::async_manual_reset_event& done)
        {
            if (mStarted)
                throw RTE_LOC;
            mRecvRequest.emplace_back(msg, std::move(choice), done);
        }

        macoro::task<> State::startRequest(std::shared_ptr<RequestState>& r)
        {

        }

        macoro::task<> State::start()
        {
            MC_BEGIN(macoro::task<>, this,
                i = u64{},
                j = u64{},
                n = u64{},
                sMsg = oc::AlignedUnVector<std::array<oc::block, 2>>{},
                rMsg = oc::AlignedUnVector<oc::block>{},
                choice = oc::BitVector{});


            // map request to batches
            for (i = 0;i < mRequests.size(); ++i)
            {
                for (j = 0;j < mRequests[i]->mSize;)
                {
                    if (mBatches.size() == 0 || mBatches.back()->mSize == mBatchSize)
                    {
                        mBatches.emplace_back(std::make_shared<Batch>());
                        mBatches.back()->init(shared_from_this());
                    }

                    auto begin = mBatches.back()->mSize;
                    auto size = std::min<u64>(mRequests[i]->mSize - j, mBatchSize - begin);

                    mRequests[i]->mBatches.emplace_back(mBatches.back(), begin, size);

                    j += size;
                }
            }

            // make base OT requests
            for (i = 0; i < mBatches.size();++i)
                mBatches[i]->finalize();

            // perform send base OTs
            if (mSendRequest.size())
            {
                for (i = 0; i < mSendRequest.size(); ++i)
                    n += mSendRequest[i].mMsg.size();
                sMsg.resize(n);
                MC_AWAIT(mSendBase.send(sMsg, mPrng, mSock));

                for (i = 0, j = 0; i < mSendRequest.size(); ++i)
                {
                    std::copy(
                        sMsg.begin() + j,
                        sMsg.begin() + j + mSendRequest[i].mMsg.size(),
                        mSendRequest[i].mMsg.begin());
                    j += mSendRequest[i].mMsg.size();
                    mSendRequest[i].mDone.set();
                }
            }

            // perform recv base OTs
            if (mRecvRequest.size())
            {
                for (i = 0; i < mRecvRequest.size(); ++i)
                    n += mRecvRequest[i].mMsg.size();
                sMsg.resize(n);
                choice.reserve(n);
                for (i = 0; i < mRecvRequest.size(); ++i)
                    choice.append(mRecvRequest[i].mChoice);

                MC_AWAIT(mRecvBase.receive(choice, rMsg, mPrng, mSock));

                for (i = 0, j = 0; i < mRecvRequest.size(); ++i)
                {
                    std::copy(
                        rMsg.begin() + j,
                        rMsg.begin() + j + mRecvRequest[i].mMsg.size(),
                        mRecvRequest[i].mMsg.begin());
                    j += mRecvRequest[i].mMsg.size();

                    mRecvRequest[i].mDone.set();
                }
            }



            MC_END();
        }

        void State::set(SendBase& b) { mRecvBase.setBaseOts(b.get()); }
        void State::set(RecvBase& b) { mSendBase.setBaseOts(b.get(), b.mChoice); }





        macoro::task<> OtRecvGenerator::Request::get(OtRecv& d)
        {
            MC_BEGIN(macoro::task<>, this, &d);


            if (mState->mIdx >= mState->mBatches.size())
                throw RTE_LOC;

            if (mState->mState->mMock)
            {
                std::terminate();
                //auto s = mState->mBatches[mState->mIdx].mSize;
                //d.mBatch = std::make_shared<Batch>();

                //oc::block sid;
                //memcpy(&sid, &mState->mBatches[mState->mIdx].mBatch->mSock.mId, sizeof(oc::block));
                //fakeFill(s, *d.mBatch, sid);
                //d.mBatch->mDone.set();
            }

            MC_AWAIT(mState->mBatches[mState->mIdx].mBatch->mDone);

            {
                auto& idx = mState->mIdx;
                auto& batch = mState->mBatches[idx];
                auto& msg = batch.mBatch->mMsg;
                auto& choice = batch.mBatch->mChoice;
                auto begin = batch.mBegin;
                auto size = batch.mSize;

                d.mMsg = msg.subspan(begin, size);

                if (size == choice.size())
                    d.mChoice = std::move(choice);
                else
                {
                    d.mChoice.resize(0);
                    d.mChoice.append(choice, size, begin);
                }

                ++idx;
            }

            MC_END();
        }

        macoro::task<> OtSendGenerator::Request::get(OtSend& d)
        {

            MC_BEGIN(macoro::task<>, this, &d);

            if (mState->mIdx >= mState->mBatches.size())
                throw RTE_LOC;

            if (mState->mState->mMock)
            {
                std::terminate();
                //auto s = mState->mBatches[mState->mIdx].mSize;
                //d.mBatch = std::make_shared<Batch>();

                //oc::block sid;
                //memcpy(&sid, &mState->mBatches[mState->mIdx].mBatch->mSock.mId, sizeof(oc::block));
                //fakeFill(s, *d.mBatch, sid);
                //d.mBatch->mDone.set();
            }

            MC_AWAIT(mState->mBatches[mState->mIdx].mBatch->mDone);

            {

                auto& idx = mState->mIdx;
                auto& batch = mState->mBatches[idx];
                auto& msg = batch.mBatch->mMsg2;
                auto begin = batch.mBegin;
                auto size = batch.mSize;
                d.mMsg = msg.subspan(begin, size);
                ++idx;
            }

            MC_END();
        }

    }

}