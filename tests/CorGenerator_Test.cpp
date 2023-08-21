#include "CorGenerator_Test.h"
#include "secure-join/CorGenerator/OtGenerator.h"
#include "secure-join/CorGenerator/BinOleGenerator.h"

using namespace secJoin;
void CorGenerator_Ot_Test(const oc::CLP&)
{

    u64 n = (1ull << 16) + 3234;

    for (auto mock : { false, true })
    {

        oc::PRNG prng(oc::ZeroBlock);
        SendBase sBase;sBase.resize(128);
        RecvBase rBase;rBase.resize(128);
        rBase.mChoice.randomize(prng);
        for (u64 i = 0; i < 128; ++i)
        {
            sBase.mBase[i][0] = prng.get<oc::block>();
            sBase.mBase[i][1] = prng.get<oc::block>();
            rBase.mBase[i] = sBase.mBase[i][rBase.mChoice[i]].getSeed();
        }

        auto sock = coproto::LocalAsyncSocket::makePair();
        OtSendGenerator send;
        OtRecvGenerator recv;
        send.init(n, 1 << 14, sock[0], prng, rBase, mock);
        recv.init(n, 1 << 14, sock[1], prng, sBase, mock);

        auto p0 = send.task() | macoro::make_eager();
        auto p1 = recv.task() | macoro::make_eager();


        u64 s = 0;
        while (s < n)
        {
            OtSend sot;
            OtRecv rot;

            auto r = macoro::sync_wait(macoro::when_all_ready(
                send.get(sot),
                recv.get(rot)
            ));

            std::get<0>(r).result();
            std::get<1>(r).result();

            for (u64 i = 0; i < sot.size(); ++i)
            {
                if (sot.mMsg[i][rot.mChoice[i]] != rot.mMsg[i])
                    throw RTE_LOC;
            }

            s += sot.size();
        }


        auto r = macoro::sync_wait(macoro::when_all_ready(
            std::move(p0), std::move(p1)
        ));

        std::get<0>(r).result();
        std::get<1>(r).result();

    }

    //auto r = macoro::sync_wait(macoro::when_all_ready(
    //    std::move(t0),
    //    std::move(t1)
    //));

    //std::get<0>(r).result();
    //std::get<1>(r).result();
}

void CorGenerator_BinOle_Test(const oc::CLP&)
{

    u64 n = (1ull << 16) + 3234;

    for (auto mock : { false, true })
    {


        oc::PRNG prng(oc::ZeroBlock);
        SendBase sBase;sBase.resize(128);
        RecvBase rBase;rBase.resize(128);
        rBase.mChoice.randomize(prng);
        for (u64 i = 0; i < 128; ++i)
        {
            sBase.mBase[i][0] = prng.get<oc::block>();
            sBase.mBase[i][1] = prng.get<oc::block>();
            rBase.mBase[i] = sBase.mBase[i][rBase.mChoice[i]].getSeed();
        }

        auto sock = coproto::LocalAsyncSocket::makePair();
        BinOleRequest send;
        BinOleRequest recv;
        send.init(n, 1 << 14, sock[0], prng, rBase, mock);
        recv.init(n, 1 << 14, sock[1], prng, sBase, mock);

        auto p0 = send.task() | macoro::make_eager();
        auto p1 = recv.task() | macoro::make_eager();


        u64 s = 0;
        while (s < n)
        {
            BinOle sot;
            BinOle rot;

            auto r = macoro::sync_wait(macoro::when_all_ready(
                send.get(sot),
                recv.get(rot)
            ));

            std::get<0>(r).result();
            std::get<1>(r).result();

            for (u64 i = 0; i < sot.mMult.size(); ++i)
            {
                if ((sot.mMult[i] & rot.mMult[i]) != (sot.mAdd[i] ^ rot.mAdd[i]))
                    throw RTE_LOC;
            }

            s += sot.size();
        }

        auto r = macoro::sync_wait(macoro::when_all_ready(
            std::move(p0), std::move(p1)
        ));

        std::get<0>(r).result();
        std::get<1>(r).result();
    }
    //auto r = macoro::sync_wait(macoro::when_all_ready(
    //    std::move(t0),
    //    std::move(t1)
    //));

    //std::get<0>(r).result();
    //std::get<1>(r).result();
}
