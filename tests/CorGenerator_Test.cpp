#include "CorGenerator_Test.h"
#include "secure-join/CorGenerator/CorGenerator2.h"
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
        Corrlation2::CorGenerator2 send;
        Corrlation2::CorGenerator2 recv;
        send.init(1 << 14, sock[0], prng, rBase, 0, mock);
        recv.init(1 << 14, sock[1], prng, sBase, 1, mock);

        auto sReq = send.requestSendOt(n);
        auto rReq = recv.requestRecvOt(n);
        auto p0 = sReq.start() | macoro::make_eager();
        auto p1 = rReq.start() | macoro::make_eager();


        u64 s = 0;
        while (s < n)
        {
            Corrlation2::OtSend sot;
            Corrlation2::OtRecv rot;

            auto r = macoro::sync_wait(macoro::when_all_ready(
                sReq.get(sot),
                rReq.get(rot)
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
        Corrlation2::CorGenerator2  send;
        Corrlation2::CorGenerator2  recv;
        send.init(1 << 14, sock[0], prng, rBase, 0, mock);
        recv.init(1 << 14, sock[1], prng, sBase, 1, mock);


        auto sReq = send.requestBinOle(n);
        auto rReq = recv.requestBinOle(n);
        auto p0 = sReq.start() | macoro::make_eager();
        auto p1 = rReq.start() | macoro::make_eager();


        u64 s = 0;
        while (s < n)
        {
            Corrlation2::BinOle sot;
            Corrlation2::BinOle rot;

            auto r = macoro::sync_wait(macoro::when_all_ready(
                sReq.get(sot),
                rReq.get(rot)
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
