#pragma once

#include "Correlations.h"
#include "F4Vole/SilentF4VoleReceiver.h"
#include "F4Vole/SilentF4VoleSender.h"
#include <libOTe/TwoChooseOne/Silent/SilentOtExtReceiver.h>
#include <libOTe/TwoChooseOne/Silent/SilentOtExtSender.h>
#include <map>
#include <mutex>
#include <utility>

namespace secJoin
{
    struct BaseRequest;
    struct BaseCor;

    struct SpinBanks
    {
        SpinBanks() = default;
        SpinBanks(SpinBanks&&) = default;
        SpinBanks(const SpinBanks&) = delete;
        std::map<u64, std::optional<oc::SpinCode>> codes;
    };

    // Only public banks survive a new peer session. Each active run leases
    // its own vector, including when start() overlaps a subsequent init().
    struct SpinBankPool
    {
        std::mutex mutex;
        std::vector<std::vector<SpinBanks>> idle;
        std::vector<SpinBanks> acquire(u64 workers);
        void release(std::vector<SpinBanks> banks);
    };

    struct StationaryWorker
    {
        StationaryWorker(u64 index, u64 party, block session, SpinBanks& banks)
            : index(index), party(party), session(session), banks(banks) {}

        // One stream per exact request geometry, correlation family and direction.
        // The batch borrows the returned stream until its conversion finishes.
        template<class Protocol>
        std::pair<Protocol&, bool> stream(std::map<std::pair<CorType, u64>, Protocol>& streams,
            CorType type, u64 size, bool sending)
        {
            auto [it, fresh] = streams.try_emplace({type, size});
            auto& protocol = it->second;
            if (fresh)
            {
                if constexpr (std::is_same_v<Protocol, SilentF4VoleSender> ||
                              std::is_same_v<Protocol, SilentF4VoleReceiver>)
                    protocol.configure(size, oc::SilentBaseType::BaseExtend, 128, {}, oc::SdNoiseDistribution::Stationary);
                else
                    protocol.configure(size, 2, 1, oc::SilentSecType::SemiHonest,
                        oc::SdNoiseDistribution::Stationary, oc::MultType::Spin);
                protocol.mCodeSeed = initialSeed(type, size, sending ? party : 1 - party);
            }
            return {protocol, fresh};
        }

        template<class Protocol>
        Protocol& begin(std::map<std::pair<CorType, u64>, Protocol>& streams,
            CorType type, u64 size, bool sending)
        {
            auto& protocol = stream(streams, type, size, sending).first;
            auto& bank = banks.codes[oc::SpinCode::messageSize(size)];
            if (!bank)
                bank.emplace(size, protocol.mCodeSeed, oc::SdNoiseDistribution::Stationary, bankSeed(size));
            protocol.mSpin = std::exchange(bank, std::nullopt);
            buffer(protocol) = std::move(blocks);
            return protocol;
        }

        template<class Protocol>
        void finish(Protocol& protocol)
        {
            auto& bank = banks.codes.at(protocol.mSpin->messageSize());
            bank = std::exchange(protocol.mSpin, std::nullopt);
            blocks = std::move(buffer(protocol));
        }

        BaseRequest requestOtBase(CorType type, u64 size, bool sending, PRNG& prng);
        void setOtBase(CorType type, u64 size, bool sending, BaseCor& base);

        u64 bankSeed(u64 size) const;
        block initialSeed(CorType type, u64 size, u64 senderParty) const;
        u64 index, party;
        block session;
        SpinBanks& banks;
        oc::AlignedUnVector<block> blocks;
        std::map<std::pair<CorType, u64>, oc::SilentOtExtSender> sendOt;
        std::map<std::pair<CorType, u64>, oc::SilentOtExtReceiver> recvOt;
        std::map<std::pair<CorType, u64>, SilentF4VoleSender> sendF4;
        std::map<std::pair<CorType, u64>, SilentF4VoleReceiver> recvF4;

    private:
        template<class Protocol>
        static auto& buffer(Protocol& protocol)
        {
            if constexpr (std::is_same_v<Protocol, oc::SilentOtExtSender> ||
                          std::is_same_v<Protocol, SilentF4VoleSender>)
                return protocol.mB;
            else
                return protocol.mA;
        }
    };
}
