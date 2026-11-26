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
#include "secure-join/CorGenerator/F4Vole/SilentF4VoleSender.h"
#include "secure-join/CorGenerator/F4Vole/SilentF4VoleReceiver.h"

namespace secJoin
{


    // 1 out of 4 OT of bit strings
    struct F4BitOtBatch : Batch
    {
        F4BitOtBatch(GenState* state, bool sender, oc::Socket&& s, PRNG&& p);

        // The "send" specific state
        struct SendBatch
        {
            SendBatch() {}

            // The OT Sender
            SilentF4VoleSender mSender;
            std::array<oc::AlignedUnVector<oc::block>, 4> mOts;

            block mDelta = oc::ZeroBlock;

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
                span<oc::block> A);

            void mock(u64 batchIdx, u64 n);

            void clear()
            {
                mSender.clear();
                mOts = {};
                mDelta = oc::ZeroBlock;
            }
        };


        // The "specific" specific state
        struct RecvBatch
        {
            RecvBatch() {}

            // The OT receiver
            SilentF4VoleReceiver mReceiver;
            oc::AlignedUnVector<oc::block> mOts, mChoiceLsb, mChoiceMsb;

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

            void clear()
            {
                mReceiver.clear();
                mOts = {};
                mChoiceLsb = {};
                mChoiceMsb = {};
            }
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
            std::visit([](auto& side) { side.clear(); }, mSendRecv);
        }
    };


}
