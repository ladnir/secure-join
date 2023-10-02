#pragma once
#include "secure-join/Defines.h"
#include "secure-join/CorGenerator/Base.h"
#include "cryptoTools/Common/Aligned.h"
#include "cryptoTools/Common/BitVector.h"

#include <vector>
#include <memory>

#include "macoro/task.h"
#include "macoro/channel.h"
#include "macoro/macros.h"
#include "macoro/manual_reset_event.h"

#include "libOTe/TwoChooseOne/Silent/SilentOtExtSender.h"
#include "libOTe/TwoChooseOne/Silent/SilentOtExtReceiver.h"

namespace secJoin
{
    namespace Corrlation
    {

        struct State;
        struct RequestState;

        struct Batch
        {
            Batch() = default;
            Batch(Batch&&) { throw RTE_LOC; };
            oc::SilentOtExtReceiver mReceiver;
            oc::SilentOtExtSender mSender;

            u64 mSize = 0;
            oc::AlignedUnVector<oc::block> mMsg;
            oc::AlignedUnVector<std::array<oc::block, 2>> mMsg2;
            oc::BitVector mChoice;

            macoro::eager_task<> mTask;
            macoro::task<> mT;
            macoro::async_manual_reset_event mHaveBase, mDone;

            coproto::Socket mSock;
            oc::PRNG mPrng;
            std::shared_ptr<State> mState;

            std::atomic_bool mStarted;


            void init(std::shared_ptr<State>& state);

            void finalize();
            macoro::task<> recvTask();
            macoro::task<> sendTask();
            macoro::task<> task();

            void start();
        };


        struct BatchOffset { std::shared_ptr<Batch> mBatch; u64 mBegin = 0, mSize = 0; };

        struct RequestState : std::enable_shared_from_this<RequestState>
        {
            // the total size of the request
            u64 mSize = 0;

            // the index of the next batch in the get() call.
            u64 mIdx = 0;

            // Where in the i'th batch should we take the correlations.
            std::vector<BatchOffset> mBatches;

            // The core state.
            std::shared_ptr<State> mState;

            // set by the batch when it completes.
            macoro::async_manual_reset_event mDone;


            macoro::task<> start();
            u64 batchCount();
            u64 size();
            void clear();
        };

        struct OtRecv
        {

            OtRecv() = default;
            OtRecv(const OtRecv&) = delete;
            OtRecv& operator=(const OtRecv&) = delete;
            OtRecv(OtRecv&&) = default;
            OtRecv& operator=(OtRecv&&) = default;

            std::shared_ptr<RequestState> mRequest;
            oc::BitVector mChoice;
            oc::span<oc::block> mMsg;

            u64 size() const { return mMsg.size(); }

            oc::BitVector& choice() { return mChoice; }

            oc::span<oc::block> msg() { return mMsg; }
        };



        struct OtSend
        {

            OtSend() = default;
            OtSend(const OtSend&) = delete;
            OtSend& operator=(const OtSend&) = delete;
            OtSend(OtSend&&) = default;
            OtSend& operator=(OtSend&&) = default;

            std::shared_ptr<RequestState> mRequest;
            oc::span<std::array<oc::block, 2>> mMsg;

            u64 size() const
            {
                return mMsg.size();
            }

            oc::span<std::array<oc::block, 2>> msg() { return mMsg; }
        };


        struct SendBaseRequest
        {
            SendBaseRequest(span<std::array<oc::block, 2>> m, macoro::async_manual_reset_event& d)
                : mMsg(m)
                , mDone(d)
            {}
            span<std::array<oc::block, 2>> mMsg;
            macoro::async_manual_reset_event& mDone;
        };

        struct RecvBaseRequest
        {
            RecvBaseRequest(
                span<oc::block> m,
                oc::BitVector&& c,
                macoro::async_manual_reset_event& d
            )
                : mMsg(m)
                , mChoice(std::move(c))
                , mDone(d)
            {}
            span<oc::block> mMsg;
            oc::BitVector mChoice;
            macoro::async_manual_reset_event& mDone;
        };


        struct State : std::enable_shared_from_this<State>
        {
            State() = default;
            State(const State&) = delete;
            State(State&&) = delete;

            oc::SoftSpokenShOtSender<> mSendBase;
            oc::SoftSpokenShOtReceiver<> mRecvBase;
            std::vector<SendBaseRequest> mSendRequest;
            std::vector<RecvBaseRequest> mRecvRequest;

            std::vector<std::shared_ptr<Batch>> mBatches;
            std::vector<std::shared_ptr<RequestState>> mRequests;

            oc::PRNG mPrng;
            coproto::Socket mSock;
            u64 mBatchSize = 0;
            std::atomic<bool> mStarted = false;
            bool mMock = false;

            macoro::mpsc::channel<std::shared_ptr<RequestState>> mChannel;

            void requestBase(span<std::array<oc::block, 2>> msg, macoro::async_manual_reset_event& done);
            void requestBase(span<oc::block> msg, oc::BitVector&& choice, macoro::async_manual_reset_event& done);

            macoro::task<> startRequest(std::shared_ptr<RequestState>& r);

            macoro::task<> start();
            void set(SendBase& b);
            void set(RecvBase& b);

        };


        struct OtGenerator
        {
            std::shared_ptr<State> mState;


            template<typename Base>
            void init(
                u64 batchSize,
                coproto::Socket& sock,
                oc::PRNG& prng,
                Base& base,
                bool mock)
            {
                mState = std::make_shared<State>();
                mState->mRole = role;
                mState->mBatchSize = batchSize;
                mState->mSock = sock;
                mState->mPrng = prng.fork();
                mState->set(base);
                mState->mMock = mock;
            }

            std::shared_ptr<RequestState> request(u64 n);

            bool started()
            {
                return mState && mState->mStarted;
            }

            bool initialized()
            {
                return mState.get();
            }


        };

        struct OtRecvGenerator : public OtGenerator
        {

            struct Request 
            {
                Request() = default;
                Request(const Request&) = delete;
                Request(Request&&) = default;

                Request& operator=(Request&&) = default;

                std::shared_ptr<RequestState> mState;

                macoro::task<> get(OtRecv& d);

                macoro::task<> start() { mState->start(); }

                u64 batchCount() { mState->batchCount(); }

                u64 size() { mState->mSize; }

                void clear() { mState->clear(); }
            };

            void init(
                u64 batchSize,
                coproto::Socket& sock,
                oc::PRNG& prng,
                SendBase& base,
                bool mock)
            {
                OtGenerator::init(batchSize, sock, prng, base, mock);
            }

            Request request(u64 n) { return Request{ OtGenerator::request(n) }; }

        };

        struct OtSendGenerator : public OtGenerator
        {

            struct Request
            {
                Request() = default;
                Request(const Request&) = delete;
                Request(Request&&) = default;
                Request& operator=(Request&&) = default;

                std::shared_ptr<RequestState> mState;

                macoro::task<> get(OtSend& d);

                macoro::task<> start() { mState->start(); }

                u64 batchCount() { mState->batchCount(); }

                u64 size() { mState->mSize; }

                void clear() { mState->clear(); }
            };

            void init(
                u64 batchSize,
                coproto::Socket& sock,
                oc::PRNG& prng,
                RecvBase& base,
                bool mock)
            {
                OtGenerator::init(batchSize, sock, prng, base, mock);
            }

            Request request(u64 n) { return Request{ OtGenerator::request(n) }; }
        };

    }



    using OtRecvRequest = Corrlation::OtRecvGenerator::Request;
    using OtSendRequest = Corrlation::OtSendGenerator::Request;
    using OtRecv = Corrlation::OtRecv;
    using OtSend = Corrlation::OtSend;

}