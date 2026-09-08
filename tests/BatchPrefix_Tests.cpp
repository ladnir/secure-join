#include "BatchPrefix_Tests.h"
#include "secure-join/AggTree/BatchPrefix.h"
#include "secure-join/Util/Util.h"
#include "coproto/Socket/LocalAsyncSock.h"
#include "macoro/sync_wait.h"
#include "macoro/when_all.h"
#include <algorithm>
#include <array>
#include <stdexcept>

namespace secJoin_Tests
{
    namespace
    {
        using namespace secJoin;
        struct PrefixCase
        {
            u64 batches, leaves, width;
            BinMatrix expected;
            std::array<BinMatrix, 2> values, controls, outputs;
            std::array<BatchPrefix, 2> protocols;
        };

        macoro::task<> runPrefixes(std::vector<std::unique_ptr<PrefixCase>>& cases,
            u64 party, coproto::Socket& socket)
        {
            for (u64 i = 0; i < cases.size(); ++i)
            {
                auto& c = *cases[i];
                if (i % 2)
                {
                    // The implementation explicitly supports in-place output.
                    co_await c.protocols[party].apply(c.values[party], c.controls[party],
                        c.values[party], socket);
                    c.outputs[party] = std::move(c.values[party]);
                }
                else
                    co_await c.protocols[party].apply(c.values[party], c.controls[party],
                        c.outputs[party], socket);
            }
        }
    }

    void BatchPrefix_Test()
    {
        using namespace secJoin;
        PRNG random(oc::toBlock(20260907));
        PRNG protocolRandom[2] = { PRNG(oc::sysRandomSeed()), PRNG(oc::sysRandomSeed()) };
        auto sockets = coproto::LocalAsyncSocket::makePair();
        CorGenerator generators[2];
        for (u64 party = 0; party < 2; ++party)
            generators[party].init(sockets[party].fork(), protocolRandom[party], party, 1, 1 << 18, false);

        std::vector<std::unique_ptr<PrefixCase>> cases;
        for (u64 group : {0, 1, 2, 8, 16})
        for (auto dimensions : {std::array<u64, 3>{1, 1, 1}, {7, 2, 7},
            {129, 8, 13}, {4, 16, 257}, {3, 64, 67}, {1, 128, 896},
            {127, 2, 65}, {128, 2, 65}, {129, 2, 65},
            {127, 4, 1}, {128, 4, 1}, {129, 4, 1}, {4, 2048, 145}})
        {
            auto c = std::make_unique<PrefixCase>();
            c->batches = dimensions[0]; c->leaves = dimensions[1]; c->width = dimensions[2];
            const auto rows = c->batches * c->leaves;
            BinMatrix values(rows, c->width), controls(rows, 1);
            random.get(values.data(), values.size());
            values.trim();
            c->expected = values;
            for (u64 batch = 0; batch < c->batches; ++batch)
                for (u64 i = 0; i < c->leaves; ++i)
                {
                    const auto row = batch * c->leaves + i;
                    // The 129x8 case exhausts every seven-bit control pattern.
                    // Other cases cover all-copy, all-reset, alternating and random.
                    auto control = c->leaves == 8 ? (batch >> (i ? i - 1 : 0)) & 1 :
                        batch % 4 == 0 ? 1 : batch % 4 == 1 ? 0 :
                        batch % 4 == 2 ? i % 2 : random.get<u8>() & 1;
                    controls(row, 0) = static_cast<u8>(control);
                    if (i && control)
                        std::copy(c->expected[row - 1].begin(), c->expected[row - 1].end(),
                            c->expected[row].begin());
                }
            share(values, c->values[0], c->values[1], random);
            share(controls, c->controls[0], c->controls[1], random);
            u64 depth = 0;
            for (auto n = c->leaves; n > 1; n /= 2) ++depth;
            for (u64 party = 0; party < 2; ++party)
            {
                c->protocols[party].init(c->batches, c->leaves, c->width, generators[party], group);
                u64 groupDepth = 0;
                for (auto g = group; g > 1; g /= 2) ++groupDepth;
                const auto expectedDepth = group && group < c->leaves ? depth + groupDepth : depth ? 2 * depth - 1 : 0;
                if (c->protocols[party].numRounds() != expectedDepth)
                    throw std::runtime_error("BatchPrefix communication depth differs from tree depth");
                c->protocols[party].preprocess();
            }
            cases.push_back(std::move(c));
        }

        auto tasks = macoro::sync_wait(macoro::when_all_ready(
            generators[0].start(), generators[1].start(),
            runPrefixes(cases, 0, sockets[0]), runPrefixes(cases, 1, sockets[1])));
        std::get<0>(tasks).result(); std::get<1>(tasks).result();
        std::get<2>(tasks).result(); std::get<3>(tasks).result();
        for (const auto& c : cases)
            for (u64 i = 0; i < c->expected.size(); ++i)
                if ((c->outputs[0](i) ^ c->outputs[1](i)) != c->expected(i))
                    throw std::runtime_error("BatchPrefix output differs from sequential reference");

        // Reject reuse before any transport access or correlation consumption.
        auto& c = *cases[0];
        bool rejected = false;
        try
        {
            macoro::sync_wait(c.protocols[0].apply(c.values[0], c.controls[0],
                c.outputs[0], sockets[0]));
        }
        catch (const std::logic_error&) { rejected = true; }
        if (!rejected)
            throw std::runtime_error("BatchPrefix accepted reused correlations");
    }
}
