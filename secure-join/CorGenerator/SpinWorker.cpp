#include "SpinWorker.h"
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
