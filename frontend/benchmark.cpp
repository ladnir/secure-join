#include "benchmark.h"
#include "secure-join/Prf/DLpnPrf.h"

namespace secJoin
{
    void Dlpn_benchmark(const oc::CLP& cmd)
    {



        u64 n = cmd.getOr("n", 1ull << cmd.getOr("nn", 10));
        u64 trials = cmd.getOr("trials", 1);

        oc::Timer timer;

        DLpnPrfSender sender;
        DLpnPrfReceiver recver;

        sender.setTimer(timer);
        recver.setTimer(timer);

        std::vector<oc::block> x(n);
        std::vector<oc::block> y0(n), y1(n);

        auto sock = coproto::LocalAsyncSocket::makePair();

        oc::PRNG prng0(oc::ZeroBlock);
        oc::PRNG prng1(oc::OneBlock);

        DLpnPrf dm;
        oc::block kk;
        kk = prng0.get();
        dm.setKey(kk);
        //sender.setKey(kk);

        CorGenerator ole0, ole1;
        ole0.mock(CorGenerator::Role::Sender);
        ole1.mock(CorGenerator::Role::Receiver);


        prng0.get(x.data(), x.size());
        std::vector<oc::block> rk(sender.mPrf.KeySize);
        std::vector<std::array<oc::block, 2>> sk(sender.mPrf.KeySize);
        for (u64 i = 0; i < sender.mPrf.KeySize; ++i)
        {
            sk[i][0] = oc::block(i, 0);
            sk[i][1] = oc::block(i, 1);
            rk[i] = oc::block(i, *oc::BitIterator((u8*)&sender.mPrf.mKey, i));
        }
        sender.setKeyOts(kk, rk);
        recver.setKeyOts(sk);

        auto begin = timer.setTimePoint("begin");
        for (u64 t = 0; t < trials; ++t)
        {

            auto r = coproto::sync_wait(coproto::when_all_ready(
                sender.evaluate(y0, sock[0], prng0, ole0),
                recver.evaluate(x, y1, sock[1], prng1, ole1)
            ));
            std::get<0>(r).result();
            std::get<1>(r).result();
        }
        auto end = timer.setTimePoint("end");

        std::cout << "DlpnPrf n:" << n << ", " <<
            std::chrono::duration_cast<std::chrono::milliseconds>(end - begin).count() << "ms " <<
            sock[0].bytesSent() / double(n) << "+" << sock[0].bytesReceived() / double(n) << "=" <<
            (sock[0].bytesSent() + sock[0].bytesReceived()) / double(n) << " bytes/eval " << std::endl;
        //std::cout << ole0.mNumBinOle / double(n) << " " << ole1.mNumBinOle / double(n) << " binOle/per" << std::endl;;
        if (cmd.isSet("v"))
        {
            std::cout << timer << std::endl;
            std::cout << sock[0].bytesReceived() / 1000.0 << " " << sock[0].bytesSent() / 1000.0 << " kB " << std::endl;
        }
    }
    void Dlpn_compressB_benchmark(const oc::CLP& cmd)
    {
        u64 n = cmd.getOr("n", 1ull << cmd.getOr("nn", 20));
        oc::AlignedUnVector<oc::block> y(n);
        oc::Matrix<oc::block> v(256, n / 128);
        oc::Timer timer;
        auto b = timer.setTimePoint("begin");
        compressB(v, y);
        auto e = timer.setTimePoint("end");
        oc::block bb;
        for (u64 i = 0; i < n; ++i)
            bb = bb ^ y[i];

        std::cout << "compressB n:" << n << ", " <<
            std::chrono::duration_cast<std::chrono::milliseconds>(e - b).count() << "ms " << std::endl;;

        std::cout << bb << std::endl;
    }
}