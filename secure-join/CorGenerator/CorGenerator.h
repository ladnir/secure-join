#pragma once
#include "secure-join/Defines.h"
#include "secure-join/CorGenerator/Base.h"

#include <vector>
#include <memory>
#include <numeric>

#include "macoro/task.h"

#include "Correlations.h"
#include "Batch.h"
#include "Request.h"

namespace secJoin
{

    struct GenState;
    struct RequestState;


    struct GenState : std::enable_shared_from_this<GenState>
    {
        GenState() = delete;
        GenState(u64 partyIdx, PRNG&& prng, oc::Socket s, u64 batchSize, bool mock)
            : mPartyIdx(partyIdx)
            , mPrng(std::move(prng))
            , mSock(std::move(s))
            , mBatchSize(batchSize)
            , mMock(mock)
        { }

        GenState(const GenState&) = delete;
        GenState(GenState&&) = delete;

        oc::SoftSpokenShOtSender<> mSendBase;
        oc::SoftSpokenShOtReceiver<> mRecvBase;



        // all of the batches. 
        //std::vector<std::shared_ptr<Batch>> mBatches;

        // all of the requests. These are broken down into batches.
        std::vector<std::shared_ptr<RequestState>> mRequests;

        // randomness source
        PRNG mPrng;

        // the base socket that each subprotocol is forked from.
        coproto::Socket mSock;

        // the size that a batch of OT/OLEs should be generated in.
        u64 mBatchSize = 0;

        // true if the base OTs has been started. This 
        // will guard starting them to ensure only one 
        // thread does it.
        std::atomic<bool> mGenerationInProgress = false;

        // true if we should just fake the correlation generation
        bool mMock = false;

        // used to determine which party should go first when its ambiguous.
        u64 mPartyIdx = -1;

        // returns a task that constructs the base OTs and assigns them to batches.
        macoro::task<> startBaseOts();

        void set(SendBase& b);
        void set(RecvBase& b);
    };

    struct CorGenerator
    {
        std::shared_ptr<GenState> mGenState;


        void init(
            coproto::Socket&& sock,
            PRNG& prng,
            u64 partyIdx,
            u64 batchSize = 1 << 16,
            bool mock = false)
        {
            mGenState = std::make_shared<GenState>(partyIdx, prng.fork(), std::move(sock), batchSize, mock);
        }

        Request<OtRecv> recvOtRequest(u64 n) { return Request<OtRecv>{request(CorType::RecvOt, oc::roundUpTo(n, 128))}; }
        Request<OtSend> sendOtRequest(u64 n) { return Request<OtSend>{request(CorType::SendOt, oc::roundUpTo(n, 128))}; }
        Request<BinOle> binOleRequest(u64 n) { return Request<BinOle>{request(CorType::Ole, oc::roundUpTo(n, 128))}; }

        void setBaseOts(SendBase& sb, RecvBase& rb)
        {
            mGenState->set(sb);
            mGenState->set(rb);
        }

        bool started()const
        {
            return mGenState && mGenState->mGenerationInProgress;
        }

        bool initialized()const
        {
            return mGenState.get();
        }

        u64 partyIdx() const
        {
            if (!mGenState)
                throw RTE_LOC;
            return mGenState->mPartyIdx;
        }

    private:

        std::shared_ptr<RequestState> request(CorType, u64);

    };


    using OtRecvRequest = Request<OtRecv>;
    using OtSendRequest = Request<OtSend>;
    using BinOleRequest = Request<BinOle>;

}