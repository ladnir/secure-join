#pragma once
#include"secure-join/Defines.h"
#include <memory>

#include "macoro/task.h"
#include "macoro/macros.h"

#include "Batch.h"

namespace secJoin
{
    struct GenState;

    //struct Session
    //{
    //    // true if the base OTs has been started. This 
    //    // will guard starting them to ensure only one 
    //    // thread does it.
    //    std::atomic<bool> mBaseStarted = false;

    //    u64 mReqIndex = 0;
    //};


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
        bool mStarted = false;

        // Where in the i'th batch should we take the correlations.
        std::vector<BatchSegment> mBatches_;

        void addBatch(BatchSegment b);

        // The core state.
        std::shared_ptr<GenState> mGenState;

        // a session is a object that tracts per base OT batch information.
        //std::shared_ptr<Session> mSession;

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

        macoro::task<> get(Cor& d)
        {
            MC_BEGIN(macoro::task<>, this, &d,
                batch = (BatchSegment*)nullptr
            );

            if (mReqState->mNextBatchIdx >= mReqState->mBatches_.size())
                throw std::runtime_error("get was call more times than there are batches. " LOCATION);

            // make sure the request has been started.
            start();

            batch = &mReqState->mBatches_[mReqState->mNextBatchIdx++];
            MC_AWAIT(batch->mBatch->mCorReady);

            batch->mBatch->getCor(&d, batch->mBegin, batch->mSize);
            d.mBatch = std::move(batch->mBatch);

            MC_END();
        }

        void start() {
            return mReqState->startReq();
        }

        u64 batchCount() const { return  initialized() ? mReqState->batchCount() : 0; }

        u64 size() const { return  initialized() ? mReqState->mSize : 0; }

        bool initialized() const { return mReqState.get() != nullptr; }

        void clear() { 
            mReqState = {};
        }
    };
}