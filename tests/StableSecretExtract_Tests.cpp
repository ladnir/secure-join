#include "secure-join/Sort/StableSecretExtract.h"
#include "secure-join/Util/Util.h"
#include "coproto/Socket/LocalAsyncSock.h"
#include "macoro/sync_wait.h"
#include "macoro/when_all.h"
#include <array>

namespace secJoin_Tests
{
    void StableSecretExtract_Test()
    {
        using namespace secJoin;
        PRNG random(oc::toBlock(892541));
        for (u64 active : {1, 5, 16, 17})
            for (u64 pattern = 0; pattern < 3; ++pattern)
            {
                constexpr u64 rows = 17;
                std::array<u64, rows> positions;
                std::iota(positions.begin(), positions.end(), 0);
                if (pattern == 1) std::reverse(positions.begin(), positions.end());
                if (pattern == 2)
                    for (u64 i = rows; i > 1; --i) std::swap(positions[i - 1], positions[random.get<u64>() % i]);
                BinMatrix flags(rows, 1), shares[2];
                for (u64 i = 0; i < active; ++i) flags(positions[i], 0) = 1;
                share(flags, shares[0], shares[1], random);
                oc::Matrix<u32> payload[2] = {oc::Matrix<u32>(rows, 1), oc::Matrix<u32>(rows, 1)};
                std::vector<u32> expected;
                for (u64 i = 0; i < rows; ++i)
                {
                    auto value = random.get<u32>();
                    if (!i) value = ~u32(0);
                    payload[0](i, 0) = random.get<u32>();
                    payload[1](i, 0) = payload[0](i, 0) ^ value;
                    if (flags(i, 0)) expected.push_back(value);
                }
                auto sockets = coproto::LocalAsyncSocket::makePair();
                PRNG prng[2] = {PRNG(oc::sysRandomSeed()), PRNG(oc::sysRandomSeed())};
                CorGenerator cor[2]; StableSecretExtract extract[2]; AdditivePerm output[2];
                for (u64 role = 0; role < 2; ++role)
                {
                    cor[role].init(sockets[role].fork(), prng[role], role, 1, 1 << 16, false);
                    extract[role].init(rows, active, 32, cor[role]); extract[role].preprocess();
                }
                auto ready = macoro::sync_wait(macoro::when_all_ready(cor[0].start(), cor[1].start(),
                    extract[0].prepare(sockets[0], prng[0]), extract[1].prepare(sockets[1], prng[1])));
                std::apply([](auto&... r) { (r.result(), ...); }, ready);
                auto merged = macoro::sync_wait(macoro::when_all_ready(
                    extract[0].apply(shares[0], payload[0], output[0], sockets[0]),
                    extract[1].apply(shares[1], payload[1], output[1], sockets[1])));
                std::get<0>(merged).result(); std::get<1>(merged).result();
                if (output[0].size() != active || output[1].size() != active) throw std::runtime_error("extraction size");
                for (u64 i = 0; i < active; ++i)
                    if ((output[0].mShare[i] ^ output[1].mShare[i]) != expected[i])
                        throw std::runtime_error("stable extraction differs from plaintext subsequence");
                bool rejected = false;
                try { macoro::sync_wait(extract[0].apply(shares[0], payload[0], output[0], sockets[0])); }
                catch (const std::logic_error&) { rejected = true; }
                if (!rejected) throw std::runtime_error("extraction accepted correlation reuse");
            }
    }
}
