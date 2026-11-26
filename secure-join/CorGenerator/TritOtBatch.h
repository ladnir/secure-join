#pragma once
#include "secure-join/Defines.h"
#include "secure-join/CorGenerator/Base.h"

#include "cryptoTools/Common/Aligned.h"
#include "cryptoTools/Common/BitVector.h"

#include <vector>
#include <memory>

#include "macoro/task.h"
#include "macoro/channel.h"
#include "macoro/macros.h"
#include "macoro/manual_reset_event.h"
#include "macoro/variant.h"


#include "Batch.h"
#include "Correlations.h"
#include "libOTe/TwoChooseOne/Silent/SilentOtExtSender.h"
#include "libOTe/TwoChooseOne/Silent/SilentOtExtReceiver.h"

namespace secJoin
{


    // 1 out of 2 OT of F3 elements
    struct TritOtBatch : Batch
    {
        TritOtBatch(GenState* state, bool sender, oc::Socket&& s, PRNG&& p);

        // The "send" specific state
        struct SendBatch
        {
            // The OT Sender
            oc::SilentOtExtSender mSender;

            // the lsb of the two messages.
            std::array<oc::AlignedUnVector<oc::block>, 2> mLsb;

            // the msb of the two messages.
            std::array<oc::AlignedUnVector<oc::block>, 2> mMsb;

            // return the task that generate the Sender correlation.
            macoro::task<> sendTask(
                GenState* state,
                u64 batchIdx,
                u64 size,
                PRNG& prng,
                oc::Socket& sock,
                macoro::async_manual_reset_event& corReady,
                BatchThreadState& threadState);

            // The routine that compresses the sender's OT messages
            // into OLEs. Basically, it just tasks the LSB of the OTs.
            void compressSender(
                block delta,
                span<oc::block> A);

            void mock(u64 batchIdx, u64 n);
        };


        // The "specific" specific state
        struct RecvBatch
        {
            // The OT receiver
            oc::SilentOtExtReceiver mReceiver;
            // the lsb and msb of the selected message.
            oc::AlignedUnVector<oc::block> mLsb, mMsb;

            // the choice bit of the selection
            oc::AlignedUnVector<oc::block> mChoice;

            // return the task that generate the Sender correlation.
            macoro::task<> recvTask(
                GenState* state,
                u64 batchIdx,
                u64 size,
                PRNG& prng,
                oc::Socket& sock,
                macoro::async_manual_reset_event& corReady,
                BatchThreadState& threadState);

            // The routine that compresses the sender's OT messages
            // into OLEs. Basically, it just tasks the LSB of the OTs.
            void compressRecver(span<oc::block> B);

            void mock(u64 batchIdx, u64 n);
        };

        macoro::variant<SendBatch, RecvBatch> mSendRecv;

        void getCor(Cor* c, u64 begin, u64 size) override;

        BaseRequest getBaseRequest(StationaryWorker* worker) override;

        void setBase(BaseCor& base, StationaryWorker* worker) override;

        // Get the task associated with this batch.
        macoro::task<> getTask(BatchThreadState&) override;

        //void mock(u64 batchIdx) override;

        u64 getBatchTypeId() override
        {
            return mSendRecv.index();
        }


        void clear() override
        {
            mSendRecv.emplace<0>();
        }
    };


}