#include "OleGenerator_Test.h"
#include "secure-join/OleGenerator.h"

void OleGenerator_Basic_Test(const oc::CLP& cmd)
{
    using namespace secJoin;

    OleGenerator g0, g1;
    macoro::thread_pool tp;
    auto chl = coproto::LocalAsyncSocket::makePair();

    u64 totalSize = 1ull << cmd.getOr("total", 18);
    u64 reservoirSize = 1ull << cmd.getOr("res", 16);
    u64 numConcurrent = cmd.getOr("concur", 4);
    u64 chunkSize = 1ull << cmd.getOr("size", 14);

    oc::PRNG prng(oc::CCBlock);
    auto work = tp.make_work();
    tp.create_threads(cmd.getOr("nt", 6));

    g0.init(OleGenerator::Role::Sender, tp, chl[0], prng, numConcurrent, chunkSize);
    g1.init(OleGenerator::Role::Receiver, tp, chl[1], prng, numConcurrent, chunkSize);

    auto r0 = macoro::sync_wait(g0.binOleRequest(totalSize, reservoirSize));
    auto r1 = macoro::sync_wait(g1.binOleRequest(totalSize, reservoirSize));

    u64 s = 0;
    while (s < totalSize)
    {

        BinOle t0, t1;
        auto n = prng.get<u64>() % chunkSize;

        auto r = macoro::sync_wait(macoro::when_all_ready(
            r0.get(),
            r1.get()
        ));

        t0 = std::get<0>(r).result();
        t1 = std::get<1>(r).result();

        if (cmd.isSet("nc") == false)
        {
            for (u64 i = 0; i < t0.mAdd.size(); ++i)
            {
                if ((t0.mAdd[i] ^ t1.mAdd[i]) != (t0.mMult[i] & t1.mMult[i]))
                    throw RTE_LOC;
            }
        }

        s += t0.size();
    }

    macoro::sync_wait(macoro::when_all_ready(
        g0.stop(),
        g1.stop()
    ));
}
