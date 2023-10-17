#include "Batch.h"
#include "OtBatch.h"
#include "BinOleBatch.h"

namespace secJoin
{


    std::shared_ptr<Batch> makeBatch(CorType type, u64 partyIdx, oc::Socket&& sock, PRNG&& p)
    {
        switch (type)
        {
        case CorType::SendOt:
            return std::make_shared<OtBatch>(true, std::move(sock), std::move(p));
            break;
        case CorType::RecvOt:
            return std::make_shared<OtBatch>(false, std::move(sock), std::move(p));
            break;
        case CorType::Ole:
            return std::make_shared<OleBatch>(partyIdx, std::move(sock), std::move(p));
            break;
        default:
        std:
            break;
        }
    }

}