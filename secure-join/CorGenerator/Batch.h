#pragma once
#include "secure-join/Defines.h"
#include "secure-join/CorGenerator/Base.h"

#include <vector>
#include <memory>
#include <numeric>

#include "macoro/task.h"
#include "macoro/manual_reset_event.h"

#include "Correlations.h"

namespace secJoin
{
    struct GenState;
    struct Batch
    {

        Batch(GenState* state, oc::Socket&& s, PRNG&& p)
            : mGenState(state)
            , mSock(std::move(s))
            , mPrng(std::move(p))
        {}

        GenState* mGenState = nullptr;

        // The size of the batch
        u64 mSize = 0;

        // the index of the batch
        u64 mIndex = 0;

        // true if the correlation is ready to be consumed.
        macoro::async_manual_reset_event mCorReady;

        // true once the batch has been requested to start
        macoro::async_manual_reset_event mStart;

        // The socket that this batch runs on
        coproto::Socket mSock;

        // randomness source.
        PRNG mPrng;

        // true if the task for this batch has been started.
        // When a task is split between one or more requests,
        // multiple requests might try to start it. This flag 
        // decides who is first.
        std::atomic_bool mStarted = false;

        virtual BaseRequest getBaseRequest() = 0;

        virtual void setBase(span<oc::block> rMsg, span<std::array<oc::block, 2>> sMsg) = 0;

        // Get the task associated with this batch. If the task
        // has already been retrieved, this will be empty.
        virtual macoro::task<> getTask() = 0;

        //virtual void mock(u64 batchIdx) = 0;

        // get the correlation. c must match the type.
        virtual void getCor(Cor* c, u64 begin, u64 size) = 0;

        virtual void clear() = 0;

        void start();

        virtual u64 getBatchTypeId() = 0;
    };

    std::shared_ptr<Batch> makeBatch(GenState* state, u64 sender, CorType type, oc::Socket&& sock, PRNG&& p);


    // represents a subset of a batch. 
    struct BatchSegment 
    {

        // The batch being referenced.
        std::shared_ptr<Batch> mBatch;

        // the begin index of this correlation within the referenced batch.
        u64 mBegin = 0;

        // the size of the correlation with respect to the referenced batch.
        u64 mSize = 0;

        BatchSegment(std::shared_ptr<Batch>& b, u64 begin, u64 size)
            : mBatch(b)
            , mBegin(begin)
            , mSize(size)
            , mWeakBatch(mBatch)
        {
        }

        std::weak_ptr<Batch> mWeakBatch;
    };




    template <typename... Fs>
    struct match : Fs... {
        using Fs::operator()...;
        // constexpr match(Fs &&... fs) : Fs{fs}... {}
    };
    template<class... Ts> match(Ts...)->match<Ts...>;

    template <typename Var, typename... Fs>
    constexpr decltype(auto) operator| (Var&& v, match<Fs...> const& match) {
        return std::visit(match, v);
    }

}