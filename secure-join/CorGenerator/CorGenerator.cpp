#include "CorGenerator.h"
#include "macoro/macros.h"
#include "BinOleBatch.h"
#include "OtBatch.h"
#include <map>

namespace secJoin
{

    std::shared_ptr<RequestState> CorGenerator::request(CorType t, u64  n)
    {
        if (mGenState->mSession == nullptr)
            mGenState->mSession = std::make_shared<Session>();
//        if (mGenState->mSession->mBaseStarted)
//            throw std::runtime_error("correlations can not be requested while another batch is in progress. " LOCATION);
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
            sPrng = PRNG{},
            rPrng = PRNG{},
            socks = std::array<oc::Socket, 2>{},

            sendOtBatch = std::shared_ptr<Batch>{},
            recvOtBatch = std::shared_ptr<Batch>{},
            oleBatch = std::shared_ptr<Batch>{},
            batches = std::vector<std::shared_ptr<Batch>>{},

            req = BaseRequest{},
            reqs = std::vector<BaseRequest>{},
            temp = std::vector<u8>{},
            res = macoro::result<void>{},
            reqChecks = std::map<CorType, oc::RandomOracle>{},
            hash = block{},
            theirHash = block{}
        );

        //if (!mGenerationInProgress)
        //    std::terminate();

        if (mMock)
        {
            //mGenerationInProgress = false;
            for (i = 0;i < mRequests.size(); ++i)
            {

                auto swap = [](CorType t, u64 p)
                {
                    if (p)
                    {
                        if (t == CorType::RecvOt)
                            t = CorType::SendOt;
                        else if (t == CorType::SendOt)
                            t = CorType::RecvOt;
                    }
                    return t;
                };

                //std::cout << "req r " << mPartyIdx<< " t " << int(mRequests[i]->mType) << " s " << mRequests[i]->mSize << std::endl;
                reqChecks[swap(mRequests[i]->mType, mPartyIdx)].Update(mRequests[i]->mSize);

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
                        batches.push_back(makeBatch(mRequests[i]->mType, mPartyIdx, oc::Socket{}, PRNG{}));
                        batches.back()->mIndex = batches.size();
                        batch = batches.back();
                        batch->mSize = mBatchSize;
                        batch->mock(0);
                        batch->mCorReady.set();
                    }

                    batch->mSize = mBatchSize;

                    auto remReq = mRequests[i]->mSize - j;
                    auto size = oc::roundUpTo(std::min<u64>(remReq, mBatchSize), 128);
                    mRequests[i]->addBatch(BatchOffset{ batch, 0, size });

                    j += size;
                }

                mRequests[i] = nullptr;
            }

            hash = oc::ZeroBlock;
            for (auto& c : reqChecks)
            {
                std::array<u8, oc::RandomOracle::HashSize> b;
                oc::RandomOracle ro(16);
                ro.Update(c.first);
                c.second.Final(b);
                ro.Update(b);
                ro.Update(hash);
                ro.Final<block>(hash);
                //std::cout << " H = " << int(c.first) << " " << hash << " " << *(int*)b.data() << std::endl;
            }

            MC_AWAIT(mSock.send(std::move(hash)));
            MC_AWAIT(mSock.recv(theirHash));
            if (hash != theirHash)
            {
                std::cout << "request state mismatch. " LOCATION << std::endl;
                throw RTE_LOC;
            }

            mRequests = {};
            MC_RETURN_VOID();
        }

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
                auto size = oc::roundUpTo(std::min<u64>(remReq, remAvb), 128);
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

        //mGenerationInProgress = false;

        MC_END();
    }

    void GenState::set(SendBase& b) { mRecvBase.setBaseOts(b.get()); }
    void GenState::set(RecvBase& b) { mSendBase.setBaseOts(b.get(), b.mChoice); }

}