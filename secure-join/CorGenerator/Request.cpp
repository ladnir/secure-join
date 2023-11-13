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
        , mSession(state->mSession)
    {
        if (!mSession)
        {
            std::cout << "CorGenerator Session == nullptr. " LOCATION << std::endl;
            std::terminate();
        }
    }


    void RequestState::addBatch(BatchOffset b)
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

    macoro::task<> RequestState::startReq()
    {
        MC_BEGIN(macoro::task<>, this,
            i = u64{},
            s = bool{},
            tasks = std::vector<macoro::eager_task<>>{}
        );
        // check to see if the base OTs have been started.
        s = mSession->mBaseStarted.exchange(true);
        if (s == false)
        {
            mGenState->mSession = {};
            MC_AWAIT(mGenState->startBaseOts());
        }

        if (mGenState->mMock)
        {

            for (i = 0; i < mBatches_.size(); ++i)
            {
                if (mBatches_[i].mBatch->mStarted.exchange(true) == false)
                {
                    mBatches_[i].mBatch->mock(mBatches_[i].mBatch->mIndex);
                    mBatches_[i].mBatch->mCorReady.set();
                }
            }
        }
        else
        {
            tasks.reserve(mBatches_.size());
            for (i = 0; i < mBatches_.size(); ++i)
            {
                if (mBatches_[i].mBatch->mStarted.exchange(true) == false)
                {
                    tasks.emplace_back(mBatches_[i].mBatch->getTask() | macoro::make_eager());
                }
            }

            for (i = 0; i < tasks.size(); ++i)
            {
                MC_AWAIT(tasks[i]);
            }
        }

        MC_END();
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