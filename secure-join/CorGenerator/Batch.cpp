#include "Batch.h"
#include "OtBatch.h"
#include "BinOleBatch.h"

namespace secJoin
{


    std::shared_ptr<Batch> makeBatch(u64 sender, CorType type, oc::Socket&& sock, PRNG&& p)
    {
        switch (type)
        {
        case CorType::Ot:
            return std::make_shared<OtBatch>(sender, std::move(sock), std::move(p));
            break;
        case CorType::Ole:
            return std::make_shared<OleBatch>(sender, std::move(sock), std::move(p));
            break;
        default:
            std::terminate();
            break;
        }
    }

}