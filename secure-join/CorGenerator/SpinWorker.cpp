#include "SpinWorker.h"
#include "Base.h"
#include <cryptoTools/Crypto/RandomOracle.h>

namespace secJoin
{
    void prepareRegularSpin(std::optional<oc::SpinCode>& code, u64 size)
    {
        const auto k = oc::SpinCode::messageSize(size);
        const block seed(12528943721987127, 98743297823479812);
        if (code && code->messageSize() == k &&
            code->encoder().setup_options().mode == spin::SetupMode::Full)
        {
            const auto spec = code->encoder().specification();
            if (spec.route_seed == seed.get<u64>(0) && spec.inner_seed == seed.get<u64>(1))
                return;
        }
        static std::mutex mutex;
        static std::map<u64, std::shared_ptr<const spin::PreparedEncoder>> plans;
        std::shared_ptr<const spin::PreparedEncoder> plan;
        {
            std::lock_guard lock(mutex);
            auto& entry = plans[k];
            if (!entry)
            {
                entry = std::make_shared<spin::PreparedEncoder>(spin::CodeSpec{
                    k, spin::Parameters::PacketRs8T64, seed.get<u64>(0), seed.get<u64>(1) });
            }
            plan = entry;
        }
        code.emplace(std::move(plan));
    }

    std::vector<SpinBanks> SpinBankPool::acquire(u64 workers)
    {
        std::lock_guard lock(mutex);
        std::vector<SpinBanks> result;
        if (!idle.empty())
        {
            result = std::move(idle.back());
            idle.pop_back();
        }
        if (result.size() < workers)
            result.resize(workers);
        return result;
    }

    void SpinBankPool::release(std::vector<SpinBanks> banks)
    {
        std::lock_guard lock(mutex);
        idle.push_back(std::move(banks));
    }

    BaseRequest StationaryWorker::requestOtBase(CorType type, u64 size, bool sending, PRNG& prng)
    {
        BaseRequest request;
        if (sending)
        {
            auto [protocol, fresh] = stream(sendOt, type, size, true);
            const auto counts = protocol.baseCount();
            if (fresh)
                request.mSendSize = counts.mBaseOtCount;
            request.mSendBinaryVoles = counts.mBaseVoleCount;
        }
        else
        {
            auto [protocol, fresh] = stream(recvOt, type, size, false);
            if (fresh)
                request.mChoice = protocol.sampleBaseChoiceBits(prng);
            request.mRecvBinaryVoles = protocol.baseCount().mBaseVoleCount;
        }
        return request;
    }

    void StationaryWorker::setOtBase(CorType type, u64 size, bool sending, BaseCor& base)
    {
        if (sending)
        {
            auto& protocol = sendOt.at({type, size});
            const auto counts = protocol.baseCount();
            auto noise = base.get<block>(base.mBinaryB, base.mBinarySendIndex, counts.mBaseVoleCount);
            protocol.setBaseCors(base.getSendOt(counts.mBaseOtCount), noise, base.mBinaryDelta);
        }
        else
        {
            auto& protocol = recvOt.at({type, size});
            const auto counts = protocol.baseCount();
            auto points = base.mOtRecvChoice.subvec(base.mOtRecvIndex, counts.mBaseOtCount);
            auto choices = base.mBinaryChoice.subvec(base.mBinaryRecvIndex, counts.mBaseVoleCount);
            auto noise = base.get<block>(base.mBinaryA, base.mBinaryRecvIndex, counts.mBaseVoleCount);
            protocol.setBaseCors(base.getRecvOt(counts.mBaseOtCount), points, noise, choices);
        }
    }

    u64 StationaryWorker::bankSeed(u64 size) const
    {
        // Distinct worker indices at fixed K, with the same public bank on both peers.
        return oc::mAesFixedKey.ecbEncBlock(block(0x534a2d42414e4b31ull,
            oc::SpinCode::messageSize(size))).get<u64>(0) + index;
    }

    block StationaryWorker::initialSeed(CorType type, u64 size, u64 senderParty) const
    {
        oc::RandomOracle hash(sizeof(block));
        hash.Update("secure-join/stationary/stream/v1");
        hash.Update(session);
        hash.Update(index);
        hash.Update(static_cast<u64>(type));
        hash.Update(size);
        hash.Update(senderParty);
        block seed;
        hash.Final(seed);
        return seed;
    }
}
