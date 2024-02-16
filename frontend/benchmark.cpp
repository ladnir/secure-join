#include "benchmark.h"
#include "secure-join/Prf/AltModPrf.h"
#include "secure-join/Sort/RadixSort.h"
#include "secure-join/Join/OmJoin.h"

namespace secJoin
{
    void CorGen_benchmark(const oc::CLP& cmd)
    {
        // request size
        u64 n = cmd.getOr("n", 1ull << cmd.getOr("nn", 20));

        // number of ole requests
        u64 ole = cmd.getOr("ole", 32);

        // number of ot requests
        u64 ot = cmd.getOr("ot", 32);

        // batch size
        u64 batch = 1ull<< cmd.getOr("b", 18);

        u64 trials = cmd.getOr("trials", 1);

        bool gen = cmd.isSet("gen");

        u64 chunk = cmd.getOr("chunk", 5);


        u64 nt = cmd.getOr("nt", 1);

        macoro::thread_pool pool;
        auto e = pool.make_work();
        pool.create_threads(nt);

        oc::Timer timer;


        std::vector<oc::block> x(n);
        std::vector<oc::block> y0(n), y1(n);

        auto sock = coproto::LocalAsyncSocket::makePair();

        sock[0].setExecutor(pool);
        sock[1].setExecutor(pool);

        PRNG prng0(oc::ZeroBlock);
        PRNG prng1(oc::OneBlock);

        for (u64 trial = 0; trial < trials; ++trial)
        {
            //timer.setTimePoint("begin");

            CorGenerator g[2];

            g[0].init(std::move(sock[0]), prng0, 0, batch);
            g[1].init(std::move(sock[1]), prng1, 1, batch);

            std::vector<std::array<BinOleRequest, 2>> oleReqs(ole);
            std::vector<OtRecvRequest> otRecvReqs(ot);
            std::vector<OtSendRequest> otSendReqs(ot);

            for (u64 i = 0; i < std::max<u64>(ole, ot); ++i)
            {
                if (i < ole)
                {
                    oleReqs[i][0] = g[0].binOleRequest(n);
                    oleReqs[i][1] = g[1].binOleRequest(n);
                }
                if (i < ot)
                {
                    otRecvReqs[i] = g[0].recvOtRequest(n);
                    otSendReqs[i] = g[1].sendOtRequest(n);
                }
            }
            //std::cout << "request done \n\n" << std::endl;
            for (u64 i = 0; i < std::max<u64>(ole, ot); )
            {
                //std::cout << "\ni = "<<i<<" \n\n" << std::endl;

                std::vector<macoro::eager_task<>> tasks;

                for (u64 b = 0; b < chunk; ++b, ++i)
                {
                    if (i < ole)
                    {
                        throw RTE_LOC;
                        //tasks.push_back(oleReqs[i][0].start() | macoro::start_on(pool));
                        //tasks.push_back(oleReqs[i][1].start() | macoro::start_on(pool));
                    }
                    if (i < ot)
                    {
                        //tasks.push_back(otRecvReqs[i].start() | macoro::start_on(pool));
                        //tasks.push_back(otSendReqs[i].start() | macoro::start_on(pool));
                    }
                }


                i -= chunk;
                //std::cout << "\nget i = " << i << " \n" << std::endl;
                for (u64 b = 0; b < chunk; ++b, ++i)
                {
                    if (i < ole)
                    {
                        u64 m = 0;
                        BinOle o[2];
                        while (m < n)
                        {
                            //std::cout << "OLE get " << i << std::endl;
                            macoro::sync_wait(macoro::when_all_ready(
                                oleReqs[i][0].get(o[0]),
                                oleReqs[i][1].get(o[1])));

                            m += o[0].size();
                        }
                    }
                    //std::cout << "OLE done " << i << std::endl;

                    if (i < ot)
                    {
                        u64 m = 0;
                        OtRecv r;
                        OtSend s;
                        while (m < n)
                        {
                            macoro::sync_wait(macoro::when_all_ready(
                                otRecvReqs[i].get(r),
                                otSendReqs[i].get(s)));
                            m += r.size();
                        }
                    }
                }

                for (auto& tt : tasks)
                    macoro::sync_wait(tt);

            }
            //timer.setTimePoint("end");

        }

        if (cmd.isSet("v"))
        {
            std::cout << timer << std::endl;
        }
    }


    void Radix_benchmark(const oc::CLP& cmd)
    {

        u64 n = cmd.getOr("n", 1ull << cmd.getOr("nn", 10));
        u64 m = cmd.getOr("m", 32);
        u64 batch = cmd.getOr("b", 16);
        u64 trials = cmd.getOr("trials", 1);
        u64 nt = cmd.getOr("nt", std::thread::hardware_concurrency());
        bool gen = cmd.isSet("gen");

        macoro::thread_pool pool;
        auto w = pool.make_work();
        pool.create_threads(nt);

        oc::Timer timer;

        RadixSort s0, s1;

        s0.setTimer(timer);
        s1.setTimer(timer);

        std::vector<oc::block> x(n);
        std::vector<oc::block> y0(n), y1(n);

        auto sock = coproto::LocalAsyncSocket::makePair();
        sock[0].setExecutor(pool);
        sock[1].setExecutor(pool);

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
            s0.init(0, n, m, g[0]);
            s1.init(1, n, m, g[1]);

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

    void OmJoin_benchmark(const oc::CLP& cmd)
    {

        u64 nL = cmd.getOr("Ln", 1ull << cmd.getOr("Lnn", cmd.getOr("nn", 10)));
        u64 nR = cmd.getOr("Rn", 1ull << cmd.getOr("Rnn", cmd.getOr("nn", 10)));
        u64 dL = cmd.getOr("Ld", cmd.getOr("d", 10));
        u64 dR = cmd.getOr("Rd", cmd.getOr("d", 10));

        auto b = cmd.getOr("b", 18);
        u64 keySize = cmd.getOr("m", 32);
        bool mock = cmd.getOr("mock", 1);

        Table L, R;

        L.init(nL, { {
            {"L1", TypeID::IntID, keySize},
            {"L2", TypeID::IntID, 16}
        } });
        R.init(nR, { {
            {"R1", TypeID::IntID, keySize},
            {"R2", TypeID::IntID, 7}
        } });

        PRNG prng(oc::ZeroBlock);
        std::array<Table, 2> Ls, Rs;
        share(L, Ls, prng);
        share(R, Rs, prng);

        OmJoin join0, join1;

        auto sock = coproto::LocalAsyncSocket::makePair();
        CorGenerator ole0, ole1;
        ole0.init(sock[0].fork(), prng, 0, 1 << b, mock);
        ole1.init(sock[1].fork(), prng, 1, 1 << b, mock);

        PRNG prng0(oc::ZeroBlock);
        PRNG prng1(oc::OneBlock);

        Table out[2];

        auto exp = join(L[0], R[0], { L[0], R[1], L[1] });
        oc::Timer timer;
        join0.setTimer(timer);

        auto begin = timer.setTimePoint("begin");
        auto r = macoro::sync_wait(macoro::when_all_ready(
            join0.join(Ls[0][0], Rs[0][0], { Ls[0][0], Rs[0][1], Ls[0][1] }, out[0], prng0, ole0, sock[0]),
            join1.join(Ls[1][0], Rs[1][0], { Ls[1][0], Rs[1][1], Ls[1][1] }, out[1], prng1, ole1, sock[1])
        ));
        auto end = timer.setTimePoint("end");

        std::cout << "radix Ln:" << nL << ", Rn:" << nR << " m:" << keySize << "  Ld: " << dL << ", Rd:" << dR << "  ~ " <<
            std::chrono::duration_cast<std::chrono::milliseconds>(end - begin).count() << "ms " <<
            sock[0].bytesSent() / double(nL + nR) << "+" << sock[0].bytesReceived() / double(nL + nR) << "=" <<
            (sock[0].bytesSent() + sock[0].bytesReceived()) / double(nL + nR) << " bytes/elem " << std::endl;

        if (cmd.isSet("timing"))
            std::cout << timer << std::endl;
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
        AltModPrf::KeyType kk;
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
            rk[i] = oc::block(i, *oc::BitIterator((u8*)&sender.mPrf.mExpandedKey, i));
        }
        sender.setKeyOts(kk, rk);
        recver.setKeyOts(sk);

        auto begin = timer.setTimePoint("begin");
        for (u64 t = 0; t < trials; ++t)
        {
            sender.init(n, ole0);
            recver.init(n, ole1);

            auto r = coproto::sync_wait(coproto::when_all_ready(
                sender.evaluate(y0, sock[0], prng0),
                recver.evaluate(x, y1, sock[1], prng1)
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

    void AltMod_expandA_benchmark(const oc::CLP& cmd)
    {

        F3AccPermCode c;
        auto l = cmd.getOr("n", 1 << cmd.getOr("nn", 18));
        auto k = cmd.getOr("k", 128 * 4);
        auto p = cmd.getOr("p", 0);
        auto batch = 1ull << cmd.getOr("b", 10);
        auto n = k / 2;
        c.init(k, n, p);

        {
            oc::Matrix<block>msb(k, l / 128), lsb(k, l / 128);
            oc::Matrix<block>msbOut(n, l / 128), lsbOut(n, l / 128);
            PRNG prng(oc::ZeroBlock);
            sampleMod3Lookup(prng, msb, lsb);


            auto msb2 = msb;
            auto lsb2 = lsb;


            oc::Timer timer;
            auto b = timer.setTimePoint("begin");
            c.encode(msb2, lsb2, msbOut, lsbOut, batch);

            auto e = timer.setTimePoint("end");

            std::cout << "multA n:" << l << ", " <<
                std::chrono::duration_cast<std::chrono::milliseconds>(e - b).count() << "ms " << std::endl;;
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
        if (cmd.isSet("old"))
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