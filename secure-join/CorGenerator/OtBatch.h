#pragma once
#include "secure-join/Defines.h"
#include "secure-join/CorGenerator/Base.h"
#include "cryptoTools/Common/Aligned.h"
#include "cryptoTools/Common/BitVector.h"

#include <vector>
#include <memory>

#include "macoro/task.h"
#include "macoro/manual_reset_event.h"

#include "libOTe/TwoChooseOne/Silent/SilentOtExtSender.h"
#include "libOTe/TwoChooseOne/Silent/SilentOtExtReceiver.h"

#include "Batch.h"
#include "Correlations.h"

namespace secJoin
{


    struct OtBatch : Batch
    {
        OtBatch(bool sender, oc::Socket&& s, PRNG&& p);
        OtBatch(OtBatch&&) { throw RTE_LOC; };

        ~OtBatch()
        {
            std::cout << "~OtBatch()" << std::endl;
        }

        struct SendOtBatch
        {
            oc::SilentOtExtSender mSender;
            oc::AlignedUnVector<std::array<oc::block, 2>> mMsg2;

            macoro::task<> sendTask(PRNG& prng, oc::Socket& sock);
            void mock(u64 batchIdx);
        };

        struct RecvOtBatch
        {
            oc::SilentOtExtReceiver mReceiver;
            oc::AlignedUnVector<oc::block> mMsg;
            oc::BitVector mChoice;

            macoro::task<> recvTask(PRNG& prng, oc::Socket& sock);
            void mock(u64 batchIdx);
        };

        macoro::variant<SendOtBatch, RecvOtBatch> mSendRecv;

        void getCor(Cor* c, u64 begin, u64 size) override;

        BaseRequest getBaseRequest() override;

        void setBase(span<oc::block> rMsg, span<std::array<oc::block, 2>> sMsg) override;

        // Get the task associated with this batch.
        macoro::task<> getTask() override;

        bool sender() { return mSendRecv.index() == 0; }

        void mock(u64 batchIdx) override;

        void clear() override;
    };













    //namespace Corrlation
    //{

    //    struct GenState;
    //    struct RequestState;

    //    struct Batch
    //    {
    //        Batch() = default;
    //        Batch(Batch&&) { throw RTE_LOC; };

    //        struct SendBatch
    //        {
    //            oc::SilentOtExtSender mSender;
    //            oc::AlignedUnVector<std::array<oc::block, 2>> mMsg2;

    //            macoro::task<> sendTask(PRNG& prng, oc::Socket& sock);
    //            void mock(u64 batchIdx);
    //        };

    //        struct RecvBatch
    //        {
    //            oc::SilentOtExtReceiver mReceiver;
    //            oc::AlignedUnVector<oc::block> mMsg;
    //            oc::BitVector mChoice;

    //            macoro::task<> recvTask(PRNG& prng, oc::Socket& sock);
    //            void mock(u64 batchIdx);
    //        };

    //        macoro::variant<SendBatch, RecvBatch> mSendRecv;

    //        // The size of the batch
    //        u64 mSize = 0;

    //        // the index of the batch
    //        u64 mIndex = 0;

    //        // true if the correlation is ready to be consumed.
    //        macoro::async_manual_reset_event mCorReady;

    //        // true once the base OTs have been set and 
    //        // ready for the main phase to begin.
    //        macoro::async_manual_reset_event mHaveBase;

    //        // The socket that this batch runs on
    //        coproto::Socket mSock;

    //        // randomness source.
    //        PRNG mPrng;

    //        // The common state
    //        std::shared_ptr<GenState> mState;

    //        // true if the task for this batch has been started.
    //        // When a task is split between one or more requests,
    //        // multiple requests might try to start it. This flag 
    //        // decides who is first.
    //        std::atomic_bool mStarted;

    //        // Fork the socket held in the state.
    //        void init(bool sender, std::shared_ptr<GenState>& state);

    //        // mark this batch as final. At this point no more correlations 
    //        // can be added to the batch. The task mT is created at this point.
    //        //void finalize();

    //        // Get the task associated with this batch.
    //        macoro::task<> getTask();

    //        // start the task associated with this batch.
    //        //void start();

    //        bool sender() { return mSendRecv.index() == 0; }

    //        void mock(u64 batchIdx);

    //    };


    //    struct BatchOffset {

    //        // The batch being referenced.
    //        std::shared_ptr<Batch> mBatch;

    //        // the begin index of this correlation within the referenced batch.
    //        u64 mBegin = 0;

    //        // the size of the correlation with respect to the referenced batch.
    //        u64 mSize = 0;
    //    };

    //    struct RequestState : std::enable_shared_from_this<RequestState>
    //    {
    //        // is the OT sender
    //        bool mSender = false;

    //        // the total size of the request
    //        u64 mSize = 0;

    //        // the index of this request.
    //        u64 mReqIndex = 0;

    //        // the index of the next batch in the get() call.
    //        u64 mNextBatchIdx = 0;

    //        // a flag encoding if the request has been started.
    //        std::atomic_bool mStarted = false;

    //        // Where in the i'th batch should we take the correlations.
    //        std::vector<BatchOffset> mBatches;

    //        // The core state.
    //        std::shared_ptr<GenState> mGenState;

    //        // set by the batch when it completes.
    //        macoro::async_manual_reset_event mDone;

    //        // Return a task that starts the preprocessing.
    //        macoro::task<> startReq();

    //        // returns the number of batches this request has.
    //        u64 batchCount();

    //        // returns the total number of correlations requested.
    //        u64 size();

    //        // clears the state associated with the request.
    //        void clear();
    //    };


    //    // A receiver OT correlation.
    //    struct OtRecv
    //    {

    //        OtRecv() = default;
    //        OtRecv(const OtRecv&) = delete;
    //        OtRecv& operator=(const OtRecv&) = delete;
    //        OtRecv(OtRecv&&) = default;
    //        OtRecv& operator=(OtRecv&&) = default;

    //        // The request associated with this correlation.
    //        std::shared_ptr<RequestState> mRequest;

    //        // The choice bits 
    //        oc::BitVector mChoice;

    //        // the OT messages
    //        oc::span<oc::block> mMsg;

    //        // the number of correlations this chunk has.
    //        u64 size() const { return mMsg.size(); }

    //        // The choice bits 
    //        oc::BitVector& choice() { return mChoice; }

    //        // the OT messages
    //        oc::span<oc::block> msg() { return mMsg; }
    //    };



    //    // A sender OT correlation.
    //    struct OtSend
    //    {

    //        OtSend() = default;
    //        OtSend(const OtSend&) = delete;
    //        OtSend& operator=(const OtSend&) = delete;
    //        OtSend(OtSend&&) = default;
    //        OtSend& operator=(OtSend&&) = default;

    //        // The request associated with this correlation.
    //        std::shared_ptr<RequestState> mRequest;

    //        // the OT messages
    //        oc::span<std::array<oc::block, 2>> mMsg;

    //        u64 size() const
    //        {
    //            return mMsg.size();
    //        }

    //        oc::span<std::array<oc::block, 2>> msg() { return mMsg; }
    //    };


    //    struct GenState : std::enable_shared_from_this<GenState>
    //    {
    //        GenState() = default;
    //        GenState(const GenState&) = delete;
    //        GenState(GenState&&) = delete;

    //        oc::SoftSpokenShOtSender<> mSendBase;
    //        oc::SoftSpokenShOtReceiver<> mRecvBase;

    //        std::vector<std::shared_ptr<Batch>> mBatches;
    //        std::vector<std::shared_ptr<RequestState>> mRequests;

    //        PRNG mPrng;
    //        coproto::Socket mSock;
    //        u64 mBatchSize = 0;
    //        std::atomic<bool> mGenerationInProgress = false;
    //        bool mMock = false;

    //        u64 mPartyIdx = 0;

    //        void requestBase(span<std::array<oc::block, 2>> msg, macoro::async_manual_reset_event& done);
    //        void requestBase(span<oc::block> msg, oc::BitVector&& choice, macoro::async_manual_reset_event& done);

    //        macoro::task<> startBaseOts();
    //        void set(SendBase& b);
    //        void set(RecvBase& b);

    //    };


    //    struct OtGenerator
    //    {
    //        std::shared_ptr<GenState> mGenState;


    //        template<typename Base>
    //        void init(
    //            u64 batchSize,
    //            coproto::Socket& sock,
    //            PRNG& prng,
    //            Base& base,
    //            u64 partyIdx,
    //            bool mock)
    //        {
    //            mGenState = std::make_shared<GenState>();
    //            mGenState->mBatchSize = batchSize;
    //            mGenState->mSock = sock;
    //            mGenState->mPrng = prng.fork();
    //            mGenState->set(base);
    //            mGenState->mPartyIdx = partyIdx;
    //            mGenState->mMock = mock;
    //        }

    //        std::shared_ptr<RequestState> request(bool sender, u64 n);

    //        bool started()
    //        {
    //            return mGenState && mGenState->mGenerationInProgress;
    //        }

    //        bool initialized()
    //        {
    //            return mGenState.get();
    //        }


    //    };

    //    struct OtRecvGenerator : public OtGenerator
    //    {

    //        struct Request
    //        {
    //            Request() = default;
    //            Request(const Request&) = delete;
    //            Request(Request&&) = default;

    //            Request& operator=(Request&&) = default;

    //            std::shared_ptr<RequestState> mReqState;

    //            macoro::task<> get(OtRecv& d);

    //            macoro::task<> start() { 
    //                if(mReqState->mStarted.exchange(true) == false)
    //                    return mReqState->startReq();
    //                else
    //                {
    //                    MC_BEGIN(macoro::task<>);
    //                    MC_END();
    //                }
    //            }

    //            u64 batchCount() { return mReqState->batchCount(); }

    //            u64 size() { return mReqState->mSize; }

    //            void clear() { mReqState->clear(); }
    //        };

    //        void init(
    //            u64 batchSize,
    //            coproto::Socket& sock,
    //            PRNG& prng,
    //            SendBase& base,
    //            u64 partyIdx,
    //            bool mock)
    //        {
    //            OtGenerator::init(batchSize, sock, prng, base,partyIdx, mock);
    //        }

    //        Request request(u64 n) { return Request{ OtGenerator::request(false, n) }; }

    //    };

    //    struct OtSendGenerator : public OtGenerator
    //    {

    //        struct Request
    //        {
    //            Request() = default;
    //            Request(const Request&) = delete;
    //            Request(Request&&) = default;
    //            Request& operator=(Request&&) = default;

    //            std::shared_ptr<RequestState> mReqState;

    //            macoro::task<> get(OtSend& d);

    //            macoro::task<> start() { 
    //                if (mReqState->mStarted.exchange(true) == false)
    //                    return mReqState->startReq();
    //                else
    //                {
    //                    MC_BEGIN(macoro::task<>);
    //                    MC_END();
    //                }
    //            }

    //            u64 batchCount() { return mReqState->batchCount(); }

    //            u64 size() { return mReqState->mSize; }

    //            void clear() { mReqState->clear(); }
    //        };

    //        void init(
    //            u64 batchSize,
    //            coproto::Socket& sock,
    //            PRNG& prng,
    //            RecvBase& base,
    //            u64 partyIdx,
    //            bool mock)
    //        {
    //            OtGenerator::init(batchSize, sock, prng, base, partyIdx, mock);
    //        }

    //        Request request(u64 n) { return Request{ OtGenerator::request(true, n) }; }
    //    };

    //}


    //using OtRecvGenerator = Corrlation::OtRecvGenerator;
    //using OtSendGenerator = Corrlation::OtSendGenerator;
    //using OtRecvRequest = Corrlation::OtRecvGenerator::Request;
    //using OtSendRequest = Corrlation::OtSendGenerator::Request;
    //using OtRecv = Corrlation::OtRecv;
    //using OtSend = Corrlation::OtSend;

}