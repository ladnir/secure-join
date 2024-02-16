#include "Batch.h"
#include "OtBatch.h"
#include "BinOleBatch.h"
#include "CorGenerator.h"

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
        default:
            std::terminate();
            break;
        }
    }


    void Batch::start()
    {
        if (mStarted.exchange(true) == false)
        {
            //if (mStart.is_set() == false)
            mGenState->startBatch(this);

            assert(mStart.is_set());
            //mStart.set();
        }
    }

}