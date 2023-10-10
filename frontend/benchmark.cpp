#include "benchmark.h"
#include "secure-join/Prf/DLpnPrf.h"
#include "secure-join/Perm/PaillierPerm.h"
#include "secure-join/Perm/LowMCPerm.h"
#include "secure-join/Perm/DLpnPerm.h"
#include "secure-join/Perm/ComposedPerm.h"
#include "secure-join/Sort/RadixSort.h"
#include "secure-join/AggTree/AggTree.h"

namespace secJoin
{

    void benchmark(const oc::CLP& cmd)
    {
        if (cmd.isSet("paillier"))
        {
            PaillierPerm_banch(cmd);
        }
        if (cmd.isSet("lowmcperm"))
        {
            LowMCPerm_bench(cmd);
        }
        if (cmd.isSet("dlpnperm"))
        {
            DlpnPerm_bench(cmd);
        }
        if (cmd.isSet("radix"))
        {
            RadixSort_bench(cmd);
        }
        if (cmd.isSet("aggtree"))
        {
            AggTree_bench(cmd);
        }

        if (cmd.isSet("Dlpn"))
        {
            Dlpn_benchmark(cmd);
        }
        if (cmd.isSet("lowmc"))
        {
            lowmc_benchmark(cmd);
        }
        if (cmd.isSet("CompressB"))
        {
            Dlpn_compressB_benchmark(cmd);
        }
    }

    void PaillierPerm_banch(const oc::CLP& cmd)
    {
#ifdef SECUREJOIN_ENABLE_PAILLIER
        using namespace secJoin;

        u64 n = cmd.getOr("n", 1ull << cmd.getOr("nn", 10));    // total number of rows
        //u64 rowSize = cmd.getOr("m", 16);
        PaillierPerm perm0, perm1;

        auto chls = coproto::LocalAsyncSocket::makePair();
        oc::PRNG prng0(oc::block(0, 0)), prng1(oc::block(1, 1));

        std::vector<u64> pi(n), z(n), y(n), x(n);

        for (u64 i = 0; i < n; ++i)
            pi[i] = prng0.get<u64>() % n;

        auto proto0 = perm0.applyPerm(pi, prng0, z, chls[0]);
        auto proto1 = perm1.applyVec(x, prng1, y, chls[1]);

        oc::Timer timer;
        auto begin = timer.setTimePoint("begin");
        auto res = macoro::sync_wait(macoro::when_all_ready(std::move(proto0), std::move(proto1)));
        auto end = timer.setTimePoint("end");

        // check the results for errors.
        std::get<0>(res).result();
        std::get<1>(res).result();

        std::cout << "pailler-perm n:" << n << ", " <<
            std::chrono::duration_cast<std::chrono::milliseconds>(end - begin).count() << "ms " <<
            chls[0].bytesSent() / double(n) << "+" << chls[0].bytesReceived() / double(n) << "=" <<
            (chls[0].bytesSent() + chls[0].bytesReceived()) / double(n) << " bytes/eval " << std::endl;
        //for (u64 i = 0; i < n; ++i)
        //{
        //    if (z[i] + y[i] != x[pi[i]])
        //    {
        //        std::cout << i << " z " << z[i] << " y " << y[i] << " " << -y[i] << " x " << x[pi[i]] << " p " << pi[i] << std::endl;
        //        throw RTE_LOC;
        //    }
        //}
#else

        throw std::runtime_error("SECUREJOIN_ENABLE_PAILLIER not defined.");
#endif
    }



    void LowMCPerm_bench(const oc::CLP& cmd)
    {
        // User input
        u64 n = cmd.getOr("n", 1ull << cmd.getOr("nn", 10));    // total number of rows
        u64 rowSize = cmd.getOr("m", 16);

        oc::Matrix<u8> x(n, rowSize), yExp(n, rowSize);

        LowMCPerm m1, m2;
        oc::PRNG prng(oc::block(0, 0));

        auto chls = coproto::LocalAsyncSocket::makePair();
        OleGenerator ole0, ole1;
        ole0.fakeInit(OleGenerator::Role::Sender);
        ole1.fakeInit(OleGenerator::Role::Receiver);


        // Initializing the vector x & permutation pi
        prng.get(x.data(), x.size());
        Perm pi(n, prng);

        std::array<oc::Matrix<u8>, 2> sout;
        sout[0].resize(n, rowSize);
        sout[1].resize(n, rowSize);
        oc::Timer timer;

        auto proto0 = m1.apply<u8>(x, sout[0], prng, chls[0], ole0);
        auto proto1 = m2.apply<u8>(pi, sout[1], prng, chls[1], false, ole1);



        auto begin = timer.setTimePoint("begin");
        auto res = macoro::sync_wait(macoro::when_all_ready(std::move(proto0), std::move(proto1)));
        auto end = timer.setTimePoint("end");

        std::get<0>(res).result();
        std::get<1>(res).result();


        std::cout << "LowMC-perm n:" << n << ", " <<
            std::chrono::duration_cast<std::chrono::milliseconds>(end - begin).count() << "ms " <<
            chls[0].bytesSent() / double(n) << "+" << chls[0].bytesReceived() / double(n) << "=" <<
            (chls[0].bytesSent() + chls[0].bytesReceived()) / double(n) << " bytes/eval " << std::endl;
        std::cout << ole0.mNumBinOle / double(n) << " " << ole1.mNumBinOle / double(n) << " binOle/per" << std::endl;;

    }



    void DlpnPerm_bench(const oc::CLP& cmd)
    {
        // User input
        u64 n = cmd.getOr("n", 1ull << cmd.getOr("nn", 10));    // total number of rows
        u64 rowSize = cmd.getOr("m", 16);

        oc::Matrix<u8> x(n, rowSize), yExp(n, rowSize);

        DLpnPermReceiver m1;
        DLpnPermSender m2;
        oc::PRNG prng(oc::block(0, 0));

        auto chls = coproto::LocalAsyncSocket::makePair();
        OleGenerator ole0, ole1;
        ole0.fakeInit(OleGenerator::Role::Sender);
        ole1.fakeInit(OleGenerator::Role::Receiver);


        // Initializing the vector x & permutation pi
        prng.get(x.data(), x.size());
        Perm pi(n, prng);

        std::array<oc::Matrix<u8>, 2> sout;
        sout[0].resize(n, rowSize);
        sout[1].resize(n, rowSize);
        oc::Timer timer;

        auto proto0 = m1.apply<u8>(x, sout[0], prng, chls[0], ole0);
        auto proto1 = m2.apply<u8>(pi, sout[1], prng, chls[1], false, ole1);



        auto begin = timer.setTimePoint("begin");
        auto res = macoro::sync_wait(macoro::when_all_ready(std::move(proto0), std::move(proto1)));
        auto end = timer.setTimePoint("end");

        std::get<0>(res).result();
        std::get<1>(res).result();


        std::cout << "Dlpn-perm n:" << n << ", " <<
            std::chrono::duration_cast<std::chrono::milliseconds>(end - begin).count() << "ms " <<
            chls[0].bytesSent() / double(n) << "+" << chls[0].bytesReceived() / double(n) << "=" <<
            (chls[0].bytesSent() + chls[0].bytesReceived()) / double(n) << " bytes/eval " << std::endl;
        std::cout << ole0.mNumBinOle / double(n) << " " << ole1.mNumBinOle / double(n) << " binOle/per" << std::endl;;

    }




    void RadixSort_bench(const oc::CLP& cmd)
    {


        u64 n = cmd.getOr("n", 1ull << cmd.getOr("nn", 10));    // total number of rows
        u64 bitCount = cmd.getOr("m", oc::log2ceil(n));

        OleGenerator g0, g1;
        g0.fakeInit(OleGenerator::Role::Sender);
        g1.fakeInit(OleGenerator::Role::Receiver);

        PRNG prng(block(0, 0));
        RadixSort s0, s1;

        oc::Timer timer;
        s0.setTimer(timer);
        s1.setTimer(timer);

        AdditivePerm p0, p1;
        Perm exp(n);

        BinMatrix k0(n, bitCount), k1(n, bitCount);

        auto chls = coproto::LocalAsyncSocket::makePair();

        auto begin = timer.setTimePoint("begin");
        macoro::sync_wait(macoro::when_all_ready(
            s0.genPerm(k0, p0, g0, chls[0]),
            s1.genPerm(k1, p1, g1, chls[1])
        ));
        auto end = timer.setTimePoint("end");

        std::cout << "Radix n:" << n << ", m:" << bitCount << ", " <<
            std::chrono::duration_cast<std::chrono::milliseconds>(end - begin).count() << "ms " <<
            chls[0].bytesSent() / double(n) << "+" << chls[0].bytesReceived() / double(n) << "=" <<
            (chls[0].bytesSent() + chls[0].bytesReceived()) / double(n) << " bytes/eval " << std::endl;
        std::cout << g0.mNumBinOle / double(n) << " " << g1.mNumBinOle / double(n) << " binOle/per" << std::endl;;

    }



    void AggTree_bench(const oc::CLP& cmd)
    {


        u64 n = cmd.getOr("n", 1ull << cmd.getOr("nn", 10));    // total number of rows
        u64 m = cmd.getOr("m", oc::log2ceil(n));

        OleGenerator g0, g1;
        g0.fakeInit(OleGenerator::Role::Sender);
        g1.fakeInit(OleGenerator::Role::Receiver);
        auto chls = coproto::LocalAsyncSocket::makePair();

        PRNG prng(block(0, 0));

        std::array<OleGenerator, 2> g;
        g[0].fakeInit(OleGenerator::Role::Receiver);
        g[1].fakeInit(OleGenerator::Role::Sender);

        AggTree t[2];

        BinMatrix s[2];
        BinMatrix c[2];
        BinMatrix d0(n, m), d1(n, m);
        s[0].resize(n, m);
        s[1].resize(n, m);
        c[0].resize(n, 1);
        c[1].resize(n, 1);

        auto opCir = [](
            oc::BetaCircuit& cir,
            const oc::BetaBundle& left,
            const oc::BetaBundle& right,
            oc::BetaBundle& out)
        {
            cir.addCopy(left, out);
        };

        AggTreeType type = AggTreeType::Prefix;
        oc::Timer timer;
        auto begin = timer.setTimePoint("begin");
        macoro::sync_wait(macoro::when_all_ready(
            t[0].apply(s[0], c[0], opCir, type, chls[0], g[0], d0),
            t[1].apply(s[1], c[1], opCir, type, chls[1], g[1], d1)
        ));


        auto end = timer.setTimePoint("end");

        std::cout << "aggTree n:" << n << ", m:" << m << ", " <<
            std::chrono::duration_cast<std::chrono::milliseconds>(end - begin).count() << "ms " <<
            chls[0].bytesSent() / double(n) << "+" << chls[0].bytesReceived() / double(n) << "=" <<
            (chls[0].bytesSent() + chls[0].bytesReceived()) / double(n) << " bytes/eval " << std::endl;
        std::cout << g0.mNumBinOle / double(n) << " " << g1.mNumBinOle / double(n) << " binOle/per" << std::endl;;

    }



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
        sender.setKey(kk);

        OleGenerator ole0, ole1;
        ole0.fakeInit(OleGenerator::Role::Sender);
        ole1.fakeInit(OleGenerator::Role::Receiver);


        prng0.get(x.data(), x.size());
        std::vector<oc::block> rk(sender.mPrf.KeySize);
        std::vector<std::array<oc::block, 2>> sk(sender.mPrf.KeySize);
        for (u64 i = 0; i < sender.mPrf.KeySize; ++i)
        {
            sk[i][0] = oc::block(i, 0);
            sk[i][1] = oc::block(i, 1);
            rk[i] = oc::block(i, *oc::BitIterator((u8*)&sender.mPrf.mKey, i));
        }
        sender.setKeyOts(rk);
        recver.setKeyOts(sk);
        macoro::thread_pool::work w;
        macoro::thread_pool pool(2, w);
        auto begin = timer.setTimePoint("begin");
        for (u64 t = 0; t < trials; ++t)
        {

            sock[0].setExecutor(pool);
            sock[1].setExecutor(pool);
            auto p0 = sender.evaluate(y0, sock[0], prng0, ole0) | macoro::start_on(pool);
            auto p1 = recver.evaluate(x, y1, sock[1], prng1, ole1)| macoro::start_on(pool);

            auto r = coproto::sync_wait(coproto::when_all_ready(std::move(p0),std::move(p1)));
            std::get<0>(r).result();
            std::get<1>(r).result();
        }
        auto end = timer.setTimePoint("end");
        w = {};

        std::cout << "DlpnPrf n:" << n << ", " <<
            std::chrono::duration_cast<std::chrono::milliseconds>(end - begin).count() << "ms " <<
            sock[0].bytesSent() / double(n) << "+" << sock[0].bytesReceived() / double(n) << "=" <<
            (sock[0].bytesSent() + sock[0].bytesReceived()) / double(n) << " bytes/eval " << std::endl;
        std::cout << ole0.mNumBinOle / double(n) << " " << ole1.mNumBinOle / double(n) << " binOle/per" << std::endl;;
        if (cmd.isSet("v"))
        {
            std::cout << timer << std::endl;
            std::cout << sock[0].bytesReceived() / 1000.0 << " " << sock[0].bytesSent() / 1000.0 << " kB " << std::endl;
        }

        //begin = timer.setTimePoint("");
        //oc::AlignedUnVector X(n * 256);
        //end = timer.setTimePoint("");
    }


    void lowmc_benchmark(const oc::CLP& cmd)
    {



        u64 n = cmd.getOr("n", 1ull << cmd.getOr("nn", 10));
        u64 trials = cmd.getOr("trials", 1);

        oc::Timer timer;

        Gmw sender;
        Gmw recver;

        sender.setTimer(timer);
        recver.setTimer(timer);

        auto sock = coproto::LocalAsyncSocket::makePair();

        oc::PRNG prng0(oc::ZeroBlock);
        oc::PRNG prng1(oc::OneBlock);
        constexpr int k = 128;


        //constexpr int blockSize = 256;
        //constexpr int sbox = 63;
        //constexpr int r = 14;
        constexpr int blockSize = 128;
        constexpr int sbox = 3;
        constexpr int r = 88;


        LowMC2<sbox, blockSize, k, r> lowmc(false);
        oc::BetaCircuit cir;
        lowmc.to_enc_circuit(cir, false);
        cir.levelByAndDepth();

        OleGenerator ole0, ole1;
        ole0.fakeInit(OleGenerator::Role::Sender);
        ole1.fakeInit(OleGenerator::Role::Receiver);


        std::array<BinMatrix, 2> x;
        x[0].resize(n, blockSize);
        x[1].resize(n, blockSize);

        std::vector<std::array<BinMatrix, 2>> key(lowmc.roundkeys.size());
        for (u64 i = 0; i < key.size();++i)
        {
            key[i][0].resize(n, blockSize);
            key[i][1].resize(n, blockSize);
        }

        macoro::thread_pool::work w;
        macoro::thread_pool pool(2, w);
        auto begin = timer.setTimePoint("begin");
        for (u64 t = 0; t < trials; ++t)
        {
            sender.init(n, cir, ole0);
            recver.init(n, cir, ole1);

            sender.setInput(0, x[0]);
            recver.setInput(0, x[1]);
            for (u64 i = 0; i < key.size();++i)
            {
                sender.setInput(i + 1, key[i][0]);
                recver.setInput(i + 1, key[i][1]);
            }

            sock[0].setExecutor(pool);
            sock[1].setExecutor(pool);

            begin = timer.setTimePoint("begin");
            auto p0 = sender.run(sock[0]) | macoro::start_on(pool);
            auto p1 = recver.run(sock[1]) | macoro::start_on(pool);

            auto r = coproto::sync_wait(coproto::when_all_ready(std::move(p0), std::move(p1)));
            std::get<0>(r).result();
            std::get<1>(r).result();
        }
        auto end = timer.setTimePoint("end");
        w = {};

        std::cout << "DlpnPrf n:" << n << ", " <<
            std::chrono::duration_cast<std::chrono::milliseconds>(end - begin).count() << "ms " <<
            sock[0].bytesSent() / double(n) << "+" << sock[0].bytesReceived() / double(n) << "=" <<
            (sock[0].bytesSent() + sock[0].bytesReceived()) / double(n) << " bytes/eval " << std::endl;
        std::cout << ole0.mNumBinOle / double(n) << " " << ole1.mNumBinOle / double(n) << " binOle/per" << std::endl;;
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