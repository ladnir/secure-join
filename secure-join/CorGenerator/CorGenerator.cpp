#include "CorGenerator.h"
#include "macoro/macros.h"
#include "BinOleBatch.h"
#include "OtBatch.h"
#include <map>

namespace secJoin
{

    std::shared_ptr<RequestState> CorGenerator::request(CorType t, u64 role, u64  n)
    {
        if (mGenState->mSession == nullptr)
            mGenState->mSession = std::make_shared<Session>();
        //        if (mGenState->mSession->mBaseStarted)
        //            throw std::runtime_error("correlations can not be requested while another batch is in progress. " LOCATION);
        auto r = std::make_shared<RequestState>(t, role, n, mGenState, mGenState->mRequests.size());
        mGenState->mRequests.push_back(r);
        return r;
    }

    struct Config
    {
        u32 size;
        u16 type;
        u16 role;
    };

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

            otBatch = std::array<std::shared_ptr<Batch>, 2>{},
            oleBatch = std::array<std::shared_ptr<Batch>, 2>{},
            batches = std::vector<std::shared_ptr<Batch>>{},

            req = BaseRequest{},
            reqs = std::vector<BaseRequest>{},
            temp = std::vector<u8>{},
            res = macoro::result<void>{},
            reqChecks = std::map<CorType, oc::RandomOracle>{},
            config = std::vector<Config>{},
            theirConfig = std::vector<Config>{},
            requests = std::vector<std::shared_ptr<RequestState>>{}
        );

        requests = std::move(mRequests);
        // map request to batches
        for (i = 0;i < requests.size(); ++i)
        {
            if (mDebug)
            {
                Config c;
                c.size = requests[i]->mSize;
                c.role = requests[i]->mSender;
                c.type = (u16)requests[i]->mType;
                config.push_back(c);
            }

            for (j = 0;j < requests[i]->mSize;)
            {
                std::shared_ptr<Batch>& batch = [&]() -> std::shared_ptr<Batch>&{
                    switch (requests[i]->mType)
                    {
                    case CorType::Ot:
                        return otBatch[requests[i]->mSender];
                    case CorType::Ole:
                        return oleBatch[requests[i]->mSender];
                    default:
                        std::terminate();
                    }
                }();

                if (batch == nullptr)
                {
                    auto ss = mSock.fork();
                    //for (auto& slot : ss.mImpl->mSlots_)
                    //    if (slot.mSessionID == ss.mId)
                    //        slot.mName = std::string("gen_") + std::to_string(batches.size());
                    batches.push_back(makeBatch(requests[i]->mSender, requests[i]->mType, std::move(ss), mPrng.fork()));
                    batches.back()->mIndex = batches.size();
                    batch = batches.back();
                }

                auto begin = batch->mSize;
                auto remReq = requests[i]->mSize - j;
                auto remAvb = mBatchSize - begin;
                auto size = oc::roundUpTo(std::min<u64>(remReq, remAvb), 128);
                assert(size <= remAvb);

                batch->mSize += size;

                requests[i]->addBatch(BatchOffset{ batch, begin, size });
                j += size;

                if (remAvb == size)
                    batch = nullptr;
            }

            //requests[i] = nullptr;
        }

        //requests = {};

        if (mDebug)
        {
            MC_AWAIT(mSock.send(coproto::copy(config)));
            MC_AWAIT(mSock.recvResize(theirConfig));
            bool misMatch = config.size() != theirConfig.size();
            if (misMatch)
            {
                for (u64 i = 0; i < config.size(); ++i)
                {
                    misMatch =
                        config[i].size != theirConfig[i].size ||
                        config[i].type != theirConfig[i].type ||
                        config[i].role == theirConfig[i].role;
                    if (misMatch)
                        break;
                }
            }
            if (misMatch)
            {
                std::cout << "CorGenerator requires do not match" << std::endl;
                auto m = std::min(config.size(), theirConfig.size());
                for (u64 i = 0; i < m; ++i)
                {

                    auto bad =
                        config[i].size != theirConfig[i].size ||
                        config[i].type != theirConfig[i].type ||
                        config[i].role == theirConfig[i].role;
                    if (bad)
                        std::cout << oc::Color::Red;

                    std::cout << "request[" << i << "], P" << mPartyIdx << ": "
                        << toString((CorType)config[i].type) << " "
                        << " r " << (config[i].role) << " "
                        << " size " << (config[i].size) << "\n"
                        << "\t\tP" << (mPartyIdx ^ 1) << ": "
                        << toString((CorType)theirConfig[i].type) << " "
                        << " r " << (theirConfig[i].role) << " "
                        << " size " << (theirConfig[i].size)
                        << std::endl;

                    if (bad)
                        std::cout << oc::Color::Default;
                }
                if (config.size() > theirConfig.size())
                {
                    std::cout << "P" << mPartyIdx << " has extra requests " << std::endl;
                }
                else if (theirConfig.size() > config.size())
                {
                    std::cout << "P" << (mPartyIdx^1) << " has extra requests " << std::endl;
                }
            }
        }

        if (mMock)
            MC_RETURN_VOID();


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

    void GenState::set(SendBase& b) { auto v = b.get(); mRecvBase.setBaseOts(v); }
    void GenState::set(RecvBase& b) { auto v = b.get(); mSendBase.setBaseOts(v, b.mChoice); }

}