#include "Request.h"
#include "CorGenerator.h"
#include "BinOleBatch.h"
#include "OtBatch.h"

namespace secJoin
{

    RequestState::RequestState(CorType t, bool sender, u64 size, std::shared_ptr<GenState>& state, u64 idx)
        : mType(t)
        , mSender(sender)
        , mSize(size)
        , mReqIndex(idx)
        , mGenState(state)
        //, mSession(state->mSession)
    {
        //if (!mSession)
        //{
        //    std::cout << "CorGenerator Session == nullptr. " LOCATION << std::endl;
        //    std::terminate();
        //}
    }


    void RequestState::addBatch(BatchSegment b)
    {
        switch (mType)
        {
        case CorType::Ot:
            if (dynamic_cast<OtBatch*>(b.mBatch.get()) == nullptr)
                std::terminate();
            break;
        case CorType::Ole:
            if (dynamic_cast<OleBatch*>(b.mBatch.get()) == nullptr)
                break;
        default:
            break;
        }

        mBatches_.emplace_back(std::move(b));
    }

    void RequestState::startReq()
    {
        for (u64 i = 0; i < mBatches_.size(); ++i)
        {
            // the batch might have already finished. lets see if it exists
            // and if it needs to be started.
            auto batch = mBatches_[i].mWeakBatch.lock();
            if(batch)
                batch->start();
        }
    }

    u64 RequestState::batchCount()
    {
        return oc::divCeil(mSize, mGenState->mBatchSize) + 1;
    }

    u64 RequestState::size() { return mSize; }

    void RequestState::clear()
    {
        throw RTE_LOC;
    }
}