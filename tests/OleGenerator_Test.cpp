#include "OleGenerator_Test.h"
#include "secure-join/OleGenerator.h"
#include "cryptoTools/Common/TestCollection.h"
#include "secure-join/CorGenerator/OtGenerator.h"

void Generator_BinOle_Test(const oc::CLP& cmd)
{
    //throw oc::UnitTestSkipped("known issue");

    using namespace secJoin;

    auto chl = coproto::LocalAsyncSocket::makePair();

    u64 totalSize = 1ull << cmd.getOr("total", 20);
    u64 reservoirSize = 1ull << cmd.getOr("res", 16);
    u64 numConcurrent = cmd.getOr("concur", 4);
    u64 chunkSize = 1ull << cmd.getOr("size", 14);

    oc::PRNG prng(oc::CCBlock);
    macoro::thread_pool tp;
    auto work = tp.make_work();
    tp.create_threads(cmd.getOr("nt", 1));

    for (u64 j = 0; j < 2; ++j)
    {

        OleGenerator g0, g1;

        if (j)
        {
            g0.init(OleGenerator::Role::Sender, tp, chl[0], prng, numConcurrent, chunkSize);
            g1.init(OleGenerator::Role::Receiver, tp, chl[1], prng, numConcurrent, chunkSize);
        }
        else
        {
            g0.fakeInit(OleGenerator::Role::Sender);
            g1.fakeInit(OleGenerator::Role::Receiver);
        }

        auto r0 = macoro::sync_wait(g0.binOleRequest(totalSize, reservoirSize));
        auto r1 = macoro::sync_wait(g1.binOleRequest(totalSize, reservoirSize));

        //std::thread thrd = std::thread([&] {
        //    std::this_thread::sleep_for(std::chrono::seconds(2));
        //    std::cout << "g0---------------\n" << g0.mLog << std::endl;
        //    std::cout << "g1---------------\n" << g1.mLog << std::endl;
        //    });

        u64 s = 0;
        while (s < totalSize)
        {

            BinOle t0, t1;
            auto r = macoro::sync_wait(macoro::when_all_ready(
                r0.get(),
                r1.get()
            ));

            t0 = std::move(std::get<0>(r).result());
            t1 = std::move(std::get<1>(r).result());

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

        //thrd.join();
    }
}

void Generator_Ot_Test(const oc::CLP&cmd)
{

    using namespace secJoin;

    auto chl = coproto::LocalAsyncSocket::makePair();

    u64 totalSize = 1ull << cmd.getOr("total", 18);
    u64 reservoirSize = 1ull << cmd.getOr("res", 16);
    u64 numConcurrent = cmd.getOr("concur", 4);
    u64 chunkSize = 1ull << cmd.getOr("size", 14);

    oc::PRNG prng(oc::CCBlock);
    macoro::thread_pool tp;
    auto work = tp.make_work();
    tp.create_threads(cmd.getOr("nt", 6));

    for (u64 j = 0; j < 1; ++j)
    {

        OleGenerator g0, g1;

        if (j)
        {
            g0.init(OleGenerator::Role::Sender, tp, chl[0], prng, numConcurrent, chunkSize);
            g1.init(OleGenerator::Role::Receiver, tp, chl[1], prng, numConcurrent, chunkSize);
        }
        else
        {
            g0.fakeInit(OleGenerator::Role::Sender);
            g1.fakeInit(OleGenerator::Role::Receiver);
        }

        auto r0 = macoro::sync_wait(g0.otRecvRequest(totalSize, reservoirSize));
        auto r1 = macoro::sync_wait(g1.otSendRequest(totalSize, reservoirSize));

        u64 s = 0;
        while (s < totalSize)
        {
            OtRecv t0;
            OtSend t1;
            auto r = macoro::sync_wait(macoro::when_all_ready(
                r0.get(),
                r1.get()
            ));

            t0 = std::move(std::get<0>(r).result());
            t1 = std::move(std::get<1>(r).result());

            if (cmd.isSet("nc") == false)
            {
                for (u64 i = 0; i < t0.size(); ++i)
                {
                    if (t0.mMsg[i] != t1.mMsg[i][t0.mChoice[i]])
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

    //throw oc::UnitTestSkipped("not impl");
}

void Generator_ArithTriple_Test(const oc::CLP&cmd)
{
    //using namespace secJoin;

    //auto chl = coproto::LocalAsyncSocket::makePair();

    //u64 totalSize = 1ull << cmd.getOr("total", 18);
    //u64 reservoirSize = 1ull << cmd.getOr("res", 16);
    //u64 numConcurrent = cmd.getOr("concur", 4);
    //u64 chunkSize = 1ull << cmd.getOr("size", 14);

    //oc::PRNG prng(oc::CCBlock);
    //macoro::thread_pool tp;
    //auto work = tp.make_work();
    //tp.create_threads(cmd.getOr("nt", 6));

    //for (u64 j = 0; j < 1; ++j)
    //{

    //    OleGenerator g0, g1;

    //    if (j)
    //    {
    //        g0.init(OleGenerator::Role::Sender, tp, chl[0], prng, numConcurrent, chunkSize);
    //        g1.init(OleGenerator::Role::Receiver, tp, chl[1], prng, numConcurrent, chunkSize);
    //    }
    //    else
    //    {
    //        g0.fakeInit(OleGenerator::Role::Sender);
    //        g1.fakeInit(OleGenerator::Role::Receiver);
    //    }

    //    auto r0 = macoro::sync_wait(g0.arithTripleRequest(totalSize, 32, reservoirSize));
    //    auto r1 = macoro::sync_wait(g1.arithTripleRequest(totalSize, 32, reservoirSize));

    //    u64 s = 0;
    //    while (s < totalSize)
    //    {
    //        ArithTriple t0, t1;
    //        auto r = macoro::sync_wait(macoro::when_all_ready(
    //            r0.get(),
    //            r1.get()
    //        ));

    //        t0 = std::get<0>(r).result();
    //        t1 = std::get<1>(r).result();

    //        if (cmd.isSet("nc") == false)
    //        {
    //            for (u64 i = 0; i < t0.size(); ++i)
    //            {
    //                auto a = t0.mA[i] + t1.mA[i];
    //                auto b = t0.mB[i] + t1.mB[i];
    //                auto c = t0.mC[i] + t1.mC[i];

    //                if (a * b != c)
    //                    throw RTE_LOC;
    //            }
    //        }

    //        s += t0.size();
    //    }

    //    macoro::sync_wait(macoro::when_all_ready(
    //        g0.stop(),
    //        g1.stop()
    //    ));
    //}

    //throw oc::UnitTestSkipped("not impl");
}
