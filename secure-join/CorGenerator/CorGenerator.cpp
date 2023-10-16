#include "CorGenerator.h"
#include "macoro/macros.h"
#include "BinOleBatch.h"
#include "OtBatch.h"

namespace secJoin
{

    std::shared_ptr<RequestState> CorGenerator::request(CorType t, u64  n)
    {
        if (mGenState->mGenerationInProgress)
            throw std::runtime_error("correlations can not be requested while another batch is in progress. " LOCATION);
        auto r = std::make_shared<RequestState>(t, n, mGenState, mGenState->mRequests.size());
        mGenState->mRequests.push_back(r);
        return r;
    }

    macoro::task<> GenState::startBaseOts()
    {
        MC_BEGIN(macoro::task<>, this,
            i = u64{},
            j = u64{},
            r = u64{},
            s = u64{},
            sMsg = oc::AlignedUnVector<std::array<oc::block, 2>>{},
            rMsg = oc::AlignedUnVector<oc::block>{},
            sProto = macoro::task<>{},
            rProto = macoro::task<>{},
            sPrng = oc::PRNG{},
            rPrng = oc::PRNG{},
            socks = std::array<oc::Socket, 2>{},

            sendOtBatch = std::shared_ptr<Batch>{},
            recvOtBatch = std::shared_ptr<Batch>{},
            oleBatch = std::shared_ptr<Batch>{},
            batches = std::vector<std::shared_ptr<Batch>>{},

            req = BaseRequest{},
            reqs = std::vector<BaseRequest>{},
            temp = std::vector<u8>{},
            res = macoro::result<void>{}

        );

        if (!mGenerationInProgress)
            std::terminate();

        // map request to batches
        for (i = 0;i < mRequests.size(); ++i)
        {
            for (j = 0;j < mRequests[i]->mSize;)
            {
                std::shared_ptr<Batch>& batch = [&]() {
                    switch (mRequests[i]->mType)
                    {
                    case CorType::RecvOt:
                        return recvOtBatch;
                    case CorType::SendOt:
                        return sendOtBatch;
                    case CorType::Ole:
                        return oleBatch;
                    default:
                        std::terminate();
                    }
                }();

                if (batch == nullptr)
                {
                    batches.push_back(makeBatch(mRequests[i]->mType, mPartyIdx, mSock.fork(), mPrng.fork()));
                    batches.back()->mIndex = batches.size();
                    batch = batches.back();
                }

                auto begin = batch->mSize;
                auto remReq = mRequests[i]->mSize - j;
                auto remAvb = mBatchSize - begin;
                auto size = oc::roundUpTo(128, std::min<u64>(remReq, remAvb));
                assert(size <= remAvb);

                batch->mSize += size;

                mRequests[i]->addBatch(BatchOffset{ batch, begin, size });
                j += size;

                if (remAvb == size)
                    batch = nullptr;
            }

            mRequests[i] = nullptr;
        }

        mRequests = {};

        if (mMock)
        {
            mGenerationInProgress = false;
            MC_RETURN_VOID();
        }

        // make base OT requests
        reqs.reserve(batches.size());
        for (i = 0; i < batches.size();++i)
        {
            auto& batch = *batches[i];
            if (!batch.mSize)
                std::terminate();
            reqs.push_back(batch.getBaseRequest());
        }

        req = BaseRequest(reqs);

        socks[0] = mSock;
        socks[1] = mSock.fork();


        if (req.mSendSize)
        {
            sMsg.resize(req.mSendSize);
            sPrng = mPrng.fork();
            sMsg.resize(req.mSendSize);
            sProto = mSendBase.send(sMsg, sPrng, socks[mPartyIdx]);
        }

        // perform recv base OTs
        if (req.mChoice.size())
        {
            rPrng = mPrng.fork();
            rMsg.resize(req.mChoice.size());
            rProto = mRecvBase.receive(req.mChoice, rMsg, mPrng, socks[1 ^ mPartyIdx]);
        }

        // perform the protocol (in parallel if both are active).
        if (req.mSendSize && req.mChoice.size())
        {
            MC_AWAIT(macoro::when_all_ready(
                std::move(rProto), std::move(sProto)
            ));
        }
        else if (req.mSendSize)
        {
            MC_AWAIT(sProto);
        }
        else
        {
            MC_AWAIT(rProto);
        }

        for (i = 0, r = 0, s = 0; i < batches.size(); ++i)
        {
            auto& batch = *batches[i];
            auto rBase = rMsg.subspan(r, reqs[i].mChoice.size());
            r += reqs[i].mChoice.size();

            auto sBase = sMsg.subspan(s, reqs[i].mSendSize);
            s += reqs[i].mSendSize;

            batch.setBase(rBase, sBase);
            batch.mHaveBase.set();
        }

        mGenerationInProgress = false;

        MC_END();
    }

    void GenState::set(SendBase& b) { mRecvBase.setBaseOts(b.get()); }
    void GenState::set(RecvBase& b) { mSendBase.setBaseOts(b.get(), b.mChoice); }

}