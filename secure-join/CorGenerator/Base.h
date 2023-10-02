#pragma once
#include "secure-join/Defines.h"
#include "cryptoTools/Common/Aligned.h"
#include "cryptoTools/Common/BitVector.h"

#include <vector>
#include <memory>

#include "macoro/task.h"
#include "macoro/channel.h"
#include "macoro/macros.h"

#include "libOTe/TwoChooseOne/Silent/SilentOtExtSender.h"
#include "libOTe/TwoChooseOne/Silent/SilentOtExtReceiver.h"

namespace secJoin
{
    struct SendBase
    {
        std::vector<std::array<oc::PRNG, 2>> mBase;

        std::vector<std::array<oc::block, 2>> get()
        {
            std::vector<std::array<oc::block, 2>> r(mBase.size());
            for (u64 i = 0;i < r.size();++i)
            {
                r[i][0] = mBase[i][0].get();
                r[i][1] = mBase[i][1].get();
            }
            return r;
        }


        void resize(u64 n)
        {
            mBase.resize(n);
        }

        SendBase fork()
        {
            SendBase s;
            s.resize(mBase.size());
            for (u64 i = 0;i < mBase.size();++i)
            {
                s.mBase[i][0].SetSeed(mBase[i][0].get<oc::block>());
                s.mBase[i][1].SetSeed(mBase[i][1].get<oc::block>());
            }

            return s;
        }
    };

    struct RecvBase
    {
        std::vector<oc::PRNG> mBase;
        oc::BitVector mChoice;

        std::vector<oc::block> get()
        {
            std::vector<oc::block> r(mBase.size());
            for (u64 i = 0;i < r.size();++i)
            {
                r[i] = mBase[i].get();
            }
            return r;
        }



        void resize(u64 n)
        {
            mBase.resize(n);
            mChoice.resize(n);
        }
        
        RecvBase fork()
        {
            RecvBase s;
            s.resize(mBase.size());
            for (u64 i = 0;i < mBase.size();++i)
            {
                s.mBase[i].SetSeed(mBase[i].get<oc::block>());
            }

            s.mChoice = mChoice;
            return s;
        }
    };


    namespace detail
    {


        struct Generator
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

                void clear()
                {
                    mBatch = {};
                    mMult = {};
                    mAdd = {};
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

                u64 batchCount()
                {
                    return mState->mCorrelations.size();
                }

                u64 size()
                {
                    u64 s = 0;
                    for (auto b : mOffsets)
                        s += b.mSize;
                    return s;
                }

                macoro::task<> close() {
                    TODO("request . close");
                }
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

            bool started()
            {
                return mState && mState->mStarted;
            }

            bool initialized()
            {
                return mState.get();
            }

        };
    }
}