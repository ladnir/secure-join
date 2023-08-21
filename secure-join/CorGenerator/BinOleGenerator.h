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




    struct BinOleGenerator
    {
        struct State;

        struct Batch
        {
            Batch() = default;
            Batch(Batch&&) { throw RTE_LOC; };
            oc::SilentOtExtReceiver mReceiver;
            oc::SilentOtExtSender mSender;
            oc::AlignedUnVector<oc::block> mMult, mAdd;

            u64 mSize = 0;
            oc::BitVector mChoice;
            oc::AlignedUnVector<oc::block> mMsg;
            oc::AlignedUnVector<std::array<oc::block, 2>> mMsg2;

            macoro::eager_task<> mTask;
            macoro::task<> mT;
            macoro::async_manual_reset_event mDone;


            coproto::Socket mSock;
            oc::PRNG mPrng;
            std::shared_ptr<State> mState;

            std::atomic_bool mStarted;

            void init(std::shared_ptr<State>& state);
            //void init(u64 size, coproto::Socket& sock, oc::PRNG& prng, SendBase& base);
            //void init(u64 size, coproto::Socket& sock, oc::PRNG& prng, RecvBase& base);
            macoro::task<> recvTask();
            macoro::task<> sendTask();
            macoro::task<> task();

            void start()
            {
                auto s = mStarted.exchange(true);
                if (s == false)
                {
                    mTask = std::move(mT) | macoro::make_eager();
                }
            }

            void compressRecver(oc::BitVector& bv, span<oc::block> recvMsg, span<oc::block> add, span<oc::block> mult);
            void compressSender(span<std::array<oc::block, 2>> sendMsg, span<oc::block> add, span<oc::block> mult);
        };


        struct BinOle
        {

            BinOle() = default;
            BinOle(const BinOle&) = delete;
            BinOle& operator=(const BinOle&) = delete;
            BinOle(BinOle&&) = default;
            BinOle& operator=(BinOle&&) = default;

            std::shared_ptr<Batch> mBatch;
            oc::span<oc::block> mMult, mAdd;
            u64 size() const
            {
                return mMult.size() * 128;
            }
        };

        struct Request
        {
            Request() = default;
            Request(const Request&) = delete;
            Request(Request&&) = default;
            Request& operator=(Request&&) = default;

            struct Offset
            {
                std::shared_ptr<Batch> mBatch;
                u64 mOffset = 0, mSize = 0;
            };

            std::shared_ptr<State> mState;
            u64 mIdx = 0;
            std::vector<Offset> mOffsets;

            macoro::task<> start();
            macoro::task<> get(BinOle& d);

            macoro::task<> close() { throw RTE_LOC; }
        };

        struct State
        {
            State() = default;
            State(const State&) = delete;
            State(State&&) = delete;

            std::vector<std::shared_ptr<Batch>> mCorrelations;
            std::vector<Request> mRequests;

            oc::PRNG mPrng;
            coproto::Socket mSock;

            u64 mIdx = 0, mSize = 0, mRole = 0, mBatchSize = 0;
            bool mStarted = false;
            bool mMock = false;


            SendBase mSendBase;
            RecvBase mRecvBase;

            void set(SendBase& b) { mSendBase = b.fork(); }
            void set(RecvBase& b) { mRecvBase = b.fork(); }

        };
        std::shared_ptr<State> mState;

        template<typename Base>
        void init(
            u64 batchSize,
            coproto::Socket& sock,
            oc::PRNG& prng,
            Base& base,
            bool mock);

        Request request(u64 n);

    };

    using BinOleRequest = BinOleGenerator::Request;
    using BinOle = BinOleGenerator::BinOle;
}