#pragma once
#include"secure-join/Defines.h"
#include <memory>

#include "macoro/task.h"
#include "macoro/macros.h"

#include "Batch.h"

namespace secJoin
{
    struct GenState;

    struct Session
    {
        // true if the base OTs has been started. This 
        // will guard starting them to ensure only one 
        // thread does it.
        std::atomic<bool> mBaseStarted = false;
    };


    struct RequestState : std::enable_shared_from_this<RequestState>
    {
        RequestState(CorType t, bool sender, u64 size, std::shared_ptr<GenState>&, u64 idx);

        // the type of the correlation
        CorType mType;

        // sender or receiver of the correlation
        bool mSender = 0;

        // the total size of the request
        u64 mSize = 0;

        // the index of this request.
        u64 mReqIndex = 0;

        // the index of the next batch in the get() call.
        u64 mNextBatchIdx = 0;

        // a flag encoding if the request has been started.
        std::atomic_bool mStarted = false;

        // Where in the i'th batch should we take the correlations.
        std::vector<BatchOffset> mBatches_;

        void addBatch(BatchOffset b);

        // The core state.
        std::shared_ptr<GenState> mGenState;

        // a session is a object that tracts per base OT batch information.
        std::shared_ptr<Session> mSession;

        // set by the batch when it completes.
        //macoro::async_manual_reset_event mDone;

        // Return a task that starts the preprocessing.
        macoro::task<> startReq();

        // returns the number of batches this request has.
        u64 batchCount();

        // returns the total number of correlations requested.
        u64 size();

        // clears the state associated with the request.
        void clear();
    };

    template<typename Cor>
    struct Request
    {
        std::shared_ptr<RequestState> mReqState;

        macoro::task<> get(Cor& d)
        {
            MC_BEGIN(macoro::task<>, this, &d,
                batch = (BatchOffset*)nullptr
            );

            if (mReqState->mStarted == false)
                throw std::runtime_error("start() must be awaited before git is called. " LOCATION);

            if (mReqState->mNextBatchIdx >= mReqState->mBatches_.size())
                throw std::runtime_error("get was call more times than there are batches. " LOCATION);

            if (mReqState->mStarted == false)
                std::terminate();

            batch = &mReqState->mBatches_[mReqState->mNextBatchIdx++];
            MC_AWAIT(batch->mBatch->mCorReady);

            batch->mBatch->getCor(&d, batch->mBegin, batch->mSize);
            d.mBatch = std::move(batch->mBatch);

            MC_END();
        }

        macoro::task<> start() {
            if (mReqState->mStarted.exchange(true) == false)
                return mReqState->startReq();
            else
            {
                MC_BEGIN(macoro::task<>);
                MC_END();
            }
        }

        u64 batchCount() const { return  initialized() ? mReqState->batchCount() : 0; }

        u64 size() const { return  initialized() ? mReqState->mSize : 0; }

        bool initialized() const { return mReqState.get() != nullptr; }

        void clear() { 
            mReqState = {};
        }
    };
}