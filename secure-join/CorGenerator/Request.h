#pragma once
#include"secure-join/Defines.h"
#include <memory>

#include "macoro/task.h"
#include "macoro/macros.h"

#include "Batch.h"

namespace secJoin
{
    struct GenState;

    struct RequestState// : std::enable_shared_from_this<RequestState>
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
        bool mStarted = false;

        // Where in the i'th batch should we take the correlations.
        std::vector<BatchSegment> mBatches_;

        void addBatch(BatchSegment b);

        // The core state.
        std::shared_ptr<GenState> mGenState;

        // starts the preprocessing.
        void startReq();

        // returns the number of mBatches this request has.
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

        // returns a task that can be awaited to get
        // the requested correlation. If the correlation 
        // has not been started, it will be started.
        // The request may be split across multiple requests
        // in which case get(...) should be called multiple time.
        macoro::task<> get(Cor& d)
        {
            if (mReqState->mNextBatchIdx >= mReqState->mBatches_.size())
                throw std::runtime_error("get was call more times than there are batches. " LOCATION);

            // make sure the request has been started.
            start();

            auto batch = &mReqState->mBatches_[mReqState->mNextBatchIdx++];
            co_await batch->mBatch->mCorReady;

            if (batch->mBatch->mError)
                std::rethrow_exception(batch->mBatch->mError);
            batch->mBatch->getCor(&d, batch->mBegin, batch->mSize);
            d.mBatch = std::move(batch->mBatch);
        }

        // eagerly start the generation of the correlation.
        void start() {
            return mReqState->startReq();
        }

        // the number of batches that this request is split over.
        u64 batchCount() const { return  initialized() ? mReqState->batchCount() : 0; }

        // the total size of the requested correlation.
        u64 size() const { return  initialized() ? mReqState->mSize : 0; }

        // returns true if this represents an actual request.
        bool initialized() const { return mReqState.get() != nullptr; }

        void clear() { 
            mReqState = {};
        }
    };
}