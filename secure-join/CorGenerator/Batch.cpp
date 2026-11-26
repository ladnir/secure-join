#include "Batch.h"
#include "OtBatch.h"
#include "BinOleBatch.h"
#include "CorGenerator.h"
#include "F4BitOtBatch.h"
#include "TritOtBatch.h"

namespace secJoin
{


    std::shared_ptr<Batch> makeBatch(GenState* state, u64 sender, CorType type, oc::Socket&& sock, PRNG&& p)
    {
        switch (type)
        {
        case CorType::Ot:
            return std::make_shared<OtBatch>(state, sender, std::move(sock), std::move(p));
            break;
        case CorType::Ole:
            return std::make_shared<OleBatch>(state, sender, std::move(sock), std::move(p));
            break;
        case CorType::F4BitOt:
            return std::make_shared<F4BitOtBatch>(state, sender, std::move(sock), std::move(p));
            break;
        case CorType::TritOt:
            return std::make_shared<TritOtBatch>(state, sender, std::move(sock), std::move(p));
            break;
        default:
            std::terminate();
            break;
        }
    }


    macoro::task<> Batch::run(BatchThreadState& worker)
    {
        try
        {
            co_await getTask(worker);
            co_return;
        }
        catch (...)
        {
            mError = std::current_exception();
        }
        mGenState->abort();
        mCorReady.set();
        if (!mSock.closed())
            co_await mSock.close();
        std::rethrow_exception(mError);
    }

    void Batch::start()
    {
        if (mStarted.exchange(true) == false)
        {
            mGenState->startBatch(this);
            assert(mStart.is_set());
        }
    }

}