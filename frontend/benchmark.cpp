#include "benchmark.h"
#include "secure-join/Prf/AltModPrf.h"
#include "secure-join/Sort/RadixSort.h"

namespace secJoin
{
    void Radix_benchmark(const oc::CLP& cmd)
    {

        u64 n = cmd.getOr("n", 1ull << cmd.getOr("nn", 10));
        u64 m = cmd.getOr("m", 32);
        u64 batch = cmd.getOr("b", 16);
        u64 trials = cmd.getOr("trials", 1);

        bool gen = cmd.isSet("gen");

        oc::Timer timer;

        RadixSort s0, s1;

        s0.setTimer(timer);
        s1.setTimer(timer);

        std::vector<oc::block> x(n);
        std::vector<oc::block> y0(n), y1(n);

        auto sock = coproto::LocalAsyncSocket::makePair();

        PRNG prng0(oc::ZeroBlock);
        PRNG prng1(oc::OneBlock);

        BinMatrix k[2];
        k[0].resize(n, m);
        k[1].resize(n, m);

        AdditivePerm d[2];

        CorGenerator g[2];

        g[0].init(sock[0].fork(), prng0, 0, 1 << batch, !gen);
        g[1].init(sock[1].fork(), prng1, 1, 1 << batch, !gen);

        auto begin = timer.setTimePoint("begin");
        for (u64 t = 0; t < trials; ++t)
        {
            s0.init(0, n, m);
            s1.init(1, n, m);
            s0.request(g[0]);
            s1.request(g[1]);

            auto r = coproto::sync_wait(coproto::when_all_ready(
                s0.genPerm(k[0], d[0], sock[0], prng0),
                s1.genPerm(k[1], d[1], sock[1], prng1)
            ));

            std::get<0>(r).result();
            std::get<1>(r).result();
        }
        auto end = timer.setTimePoint("end");

        std::cout << "radix n:" << n << ", m:" << m << "  : " <<
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
    void AltMod_benchmark(const oc::CLP& cmd)
    {



        u64 n = cmd.getOr("n", 1ull << cmd.getOr("nn", 10));
        u64 trials = cmd.getOr("trials", 1);

        oc::Timer timer;

        AltModPrfSender sender;
        AltModPrfReceiver recver;

        sender.setTimer(timer);
        recver.setTimer(timer);

        std::vector<oc::block> x(n);
        std::vector<oc::block> y0(n), y1(n);

        auto sock = coproto::LocalAsyncSocket::makePair();

        PRNG prng0(oc::ZeroBlock);
        PRNG prng1(oc::OneBlock);

        AltModPrf dm;
        oc::block kk;
        kk = prng0.get();
        dm.setKey(kk);
        //sender.setKey(kk);

        CorGenerator ole0, ole1;
        ole0.init(sock[0].fork(), prng0, 0, 1 << 18, cmd.getOr("mock", 1));
        ole1.init(sock[1].fork(), prng1, 1, 1 << 18, cmd.getOr("mock", 1));


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

        std::cout << "AltModPrf n:" << n << ", " <<
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
    void AltMod_compressB_benchmark(const oc::CLP& cmd)
    {
        u64 n = cmd.getOr("n", 1ull << cmd.getOr("nn", 20));

        if (cmd.isSet("single"))
        {
            oc::AlignedUnVector<oc::block> y(n);
            oc::AlignedUnVector<oc::block> v(n);
            PRNG prng(oc::ZeroBlock);
            prng.get(v.data(), v.size());
            oc::Timer timer;
            auto b = timer.setTimePoint("begin");
            for (u64 i = 0; i < n; ++i)
            {
                AltModPrf::mBCode.encode(v[i].data(), y[i].data());
            }
            //compressB(v, y);
            auto e = timer.setTimePoint("end");
            oc::block bb;
            for (u64 i = 0; i < n; ++i)
                bb = bb ^ y.data()[i];

            std::cout << "single compressB n:" << n << ", " <<
                std::chrono::duration_cast<std::chrono::milliseconds>(e - b).count() << "ms " << std::endl;;

            std::cout << bb << std::endl;
        }
        {
            oc::AlignedUnVector<oc::block> y(n);
            oc::Matrix<oc::block> v(256, n / 128);
            oc::Timer timer;
            auto b = timer.setTimePoint("begin");
            compressB(v, y);
            auto e = timer.setTimePoint("end");
            oc::block bb;
            for (u64 i = 0; i < n; ++i)
                bb = bb ^ y.data()[i];

            std::cout << "batched compressB n:" << n << ", " <<
                std::chrono::duration_cast<std::chrono::milliseconds>(e - b).count() << "ms " << std::endl;;

            std::cout << bb << std::endl;
        }
    }

    void AltMod_sampleMod3_benchmark(const oc::CLP& cmd)
    {
        u64 n = cmd.getOr("n", 1ull << cmd.getOr("nn", 16));

        oc::AlignedUnVector<block> msb(n), lsb(n);
        PRNG prng(oc::ZeroBlock);

        {

            oc::Timer timer;
            auto b = timer.setTimePoint("begin");
            sampleMod3Lookup(prng, msb, lsb);
            auto e = timer.setTimePoint("end");

            std::cout << "mod3lookup n:" << n << ", " <<
                std::chrono::duration_cast<std::chrono::milliseconds>(e - b).count() << "ms " << std::endl;;
        }
        if(cmd.isSet("old"))
        {
            oc::AlignedUnVector<u8> bb;
            oc::Timer timer;
            auto b = timer.setTimePoint("begin");
            sampleMod3(prng, msb, lsb, bb);
            auto e = timer.setTimePoint("end");

            std::cout << "mod3 old n:" << n << ", " <<
                std::chrono::duration_cast<std::chrono::milliseconds>(e - b).count() << "ms " << std::endl;;
        }
    }

    void transpose_benchmark(const oc::CLP& cmd)
    {
        u64 n = oc::roundUpTo(cmd.getOr("n", 1ull << cmd.getOr("nn", 20)), 128);
        oc::AlignedUnVector<oc::block> y(n);

        oc::Timer timer;
        auto b = timer.setTimePoint("begin");
        for (u64 i = 0; i < n; i += 128)
        {
            oc::transpose128(&y[i]);
        }
        auto e = timer.setTimePoint("end");

        std::cout << "transpose n:" << n << ", " <<
            std::chrono::duration_cast<std::chrono::milliseconds>(e - b).count() << "ms " << std::endl;;

    }
}