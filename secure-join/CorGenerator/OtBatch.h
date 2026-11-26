#pragma once
#include "secure-join/Defines.h"
#include "secure-join/CorGenerator/Base.h"
#include "cryptoTools/Common/Aligned.h"
#include "cryptoTools/Common/BitVector.h"

#include <vector>
#include <memory>

#include "macoro/task.h"
#include "macoro/manual_reset_event.h"

#include "libOTe/TwoChooseOne/Silent/SilentOtExtSender.h"
#include "libOTe/TwoChooseOne/Silent/SilentOtExtReceiver.h"

#include "Batch.h"
#include "Correlations.h"

namespace secJoin
{
    struct OtBatch : Batch
    {
        OtBatch(GenState* state, bool sender, oc::Socket&& s, PRNG&& p);

        struct SendOtBatch
        {
            oc::SilentOtExtSender mSender;
            oc::AlignedUnVector<std::array<oc::block, 2>> mMsg2;

            macoro::task<> sendTask(PRNG& prng, oc::Socket& sock);
            void mock(u64 batchIdx);
        };

        struct RecvOtBatch
        {
            oc::SilentOtExtReceiver mReceiver;
            oc::AlignedUnVector<oc::block> mMsg;
            oc::BitVector mChoice;

            macoro::task<> recvTask(PRNG& prng, oc::Socket& sock);
            void mock(u64 batchIdx);
        };

        macoro::variant<SendOtBatch, RecvOtBatch> mSendRecv;

        void getCor(Cor* c, u64 begin, u64 size) override;

        BaseRequest getBaseRequest(StationaryWorker* worker) override;

        void setBase(BaseCor& base, StationaryWorker* worker) override;

        // Get the task associated with this batch.
        macoro::task<> getTask(BatchThreadState&) override;

        bool sender() { return mSendRecv.index() == 0; }

        void clear() override;

        u64 getBatchTypeId() override
        {
            return 2 + mSendRecv.index();
        }
    };

}