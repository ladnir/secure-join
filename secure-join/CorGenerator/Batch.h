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
    struct Batch
    {
        // The size of the batch
        u64 mSize = 0;

        // the index of the batch
        u64 mIndex = 0;

        // true if the correlation is ready to be consumed.
        macoro::async_manual_reset_event mCorReady;


        // true once the base OTs have been set and 
        // ready for the main phase to begin.
        macoro::async_manual_reset_event mHaveBase;

        // The socket that this batch runs on
        coproto::Socket mSock;

        // randomness source.
        oc::PRNG mPrng;

        // true if the task for this batch has been started.
        // When a task is split between one or more requests,
        // multiple requests might try to start it. This flag 
        // decides who is first.
        std::atomic_bool mStarted;

        virtual BaseRequest getBaseRequest() = 0;

        virtual void setBase(span<oc::block> rMsg, span<std::array<oc::block, 2>> sMsg) = 0;

        // Get the task associated with this batch. If the task
        // has already been retrieved, this will be empty.
        virtual macoro::task<> getTask() = 0;

        virtual void mock(u64 batchIdx) = 0;

        // get the correlation. c must match the type.
        virtual void getCor(Cor* c, u64 begin, u64 size) = 0;
    };

    std::shared_ptr<Batch> makeBatch(CorType type, u64 partyIdx, oc::Socket&& sock, oc::PRNG&& p);

    struct BatchOffset {

        // The batch being referenced.
        std::shared_ptr<Batch> mBatch;

        // the begin index of this correlation within the referenced batch.
        u64 mBegin = 0;

        // the size of the correlation with respect to the referenced batch.
        u64 mSize = 0;
    };

}