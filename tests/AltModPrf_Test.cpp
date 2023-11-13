#include "AltModPrf_Test.h"
#include "secure-join/Prf/DarkMatter22Prf.h"
#include "secure-join/Prf/DarkMatter32Prf.h"
#include "secure-join/Prf/AltModPrf.h"
#include "cryptoTools/Crypto/PRNG.h"
#include "cryptoTools/Common/Matrix.h"

#include "cryptoTools/Common/TestCollection.h"
#include "secure-join/Util/Util.h"
#include "secure-join/Prf/F3LinearCode.h"

using namespace secJoin;

void DarkMatter22Prf_plain_test()
{
    throw oc::UnitTestSkipped("known issue");
    PRNG prng(oc::ZeroBlock);
    block256 k = prng.get();
    block256 x = prng.get();

    DarkMatter22Prf prf;
    prf.setKey(k);

    auto y = prf.eval(x);

    oc::Matrix<u64> K(256, 256), B(128, 256);
    std::vector<u64> X(256);
    for (u64 i = 0; i < 256; ++i)
    {
        X[i] = *oc::BitIterator((u8*)&x, i);

        u8 ki = *oc::BitIterator((u8*)&k, i);
        for (u64 j = 0; j < 256; ++j)
        {
            auto jj = (j + i) % 256;
            K(j, jj) = ki;
        }
    }
    for (u64 i = 0; i < 128; ++i)
    {
        for (u64 j = 0; j < 256; ++j)
        {
            B(i, j) = *oc::BitIterator((u8*)&prf.mB[i], j);
        }
    }

    //std::cout << "K\n";
    //for (u64 i = 0; i < 256; ++i)
    //{
    //    for (u64 j = 0; j < 256; ++j)
    //    {
    //        std::cout << K(i, j) << " ";
    //    }
    //    std::cout << std::endl;
    //}

    std::vector<u64> KX(256);

    //std::cout << "KX ";
    for (u64 i = 0; i < 256; ++i)
    {
        for (u64 j = 0; j < 256; ++j)
        {
            assert(K(i, j) < 2);
            assert(X[j] < 2);
            KX[i] += K(i, j) * X[j];
        }
        //std::cout << KX[i] << " ";
    }
    //std::cout << std::endl;


    std::vector<u64> v(256), u(256), w(256), Y(128);
    for (u64 i = 0; i < 256; ++i)
    {
        v[i] = KX[i] % 2;
        u[i] = (KX[i] % 3) % 2;
        w[i] = v[i] ^ u[i];
    }
    //std::cout << "W  ";
    //for (u64 i = 0; i < 256; ++i)
    //{
    //    std::cout << w[i] << " ";
    //}
    //std::cout << std::endl;

    for (u64 i = 0; i < 128; ++i)
    {
        for (u64 j = 0; j < 256; ++j)
        {
            Y[i] ^= B(i, j) * w[j];
        }

        if (Y[i] != (u8)*oc::BitIterator((u8*)&y, i))
        {
            throw RTE_LOC;
        }
    }



}


void DarkMatter32Prf_plain_test()
{

    throw oc::UnitTestSkipped("known issue");
    PRNG prng(oc::ZeroBlock);
    block256 k = prng.get();
    block256 x = prng.get();

    DarkMatter32Prf prf;
    prf.setKey(k);

    auto y = prf.eval(x);

    oc::Matrix<u64> K(256, 256), B(128, 256);
    std::vector<u64> X(256);
    for (u64 i = 0; i < 256; ++i)
    {
        X[i] = *oc::BitIterator((u8*)&x, i);

        u8 ki = *oc::BitIterator((u8*)&k, i);
        for (u64 j = 0; j < 256; ++j)
        {
            auto jj = (j + i) % 256;
            K(j, jj) = ki;
        }
    }
    for (u64 i = 0; i < 128; ++i)
    {
        for (u64 j = 0; j < 256; ++j)
        {
            B(i, j) = *oc::BitIterator((u8*)&DarkMatter22Prf::mB[i], j);
        }
    }

    //std::cout << "K\n";
    //for (u64 i = 0; i < 256; ++i)
    //{
    //    for (u64 j = 0; j < 256; ++j)
    //    {
    //        std::cout << K(i, j) << " ";
    //    }
    //    std::cout << std::endl;
    //}

    std::vector<u64> KX(256);

    //std::cout << "KX ";
    for (u64 i = 0; i < 256; ++i)
    {
        for (u64 j = 0; j < 256; ++j)
        {
            assert(K(i, j) < 2);
            assert(X[j] < 2);
            KX[i] += K(i, j) * X[j];
        }
        //std::cout << KX[i] << " ";
    }
    //std::cout << std::endl;


    std::vector<u64> w(256), Y(128);
    for (u64 i = 0; i < 256; ++i)
    {
        w[i] = (KX[i] % 3) % 2;
    }
    //std::cout << "W  ";
    //for (u64 i = 0; i < 256; ++i)
    //{
    //    std::cout << w[i] << " ";
    //}
    //std::cout << std::endl;

    for (u64 i = 0; i < 128; ++i)
    {
        for (u64 j = 0; j < 256; ++j)
        {
            Y[i] ^= B(i, j) * w[j];
        }

        if (Y[i] != (u8)*oc::BitIterator((u8*)&y, i))
        {
            throw RTE_LOC;
        }
    }
}

void DarkMatter22Prf_util_test()
{
    throw oc::UnitTestSkipped("known issue");
    u64 n = 234 * 256;
    PRNG prng(oc::ZeroBlock);
    auto m3 = sampleMod3(prng, n);

    if (m3.size() != n)
        throw RTE_LOC;

    std::vector<u64> counts(3);
    for (auto i = 0ull; i < n; ++i)
    {
        if (m3[i] > 2)
            throw RTE_LOC;

        //std::cout << int(m3[i]) << " ";
        ++counts[m3[i]];
    }
    //std::cout << std::endl;

    std::vector<u8> dst(n / 4);
    oc::AlignedUnVector<u16> mm(n);
    compressMod3(dst, m3);
    decompressMod3(mm, dst);

    if (std::equal(mm.begin(), mm.end(), m3.begin()) == false)
        throw RTE_LOC;
    //std::cout << counts[0] << " " << counts[1] << " " << counts[2] << std::endl;

}

void DarkMatter22Prf_proto_test(const oc::CLP& cmd)
{
    throw oc::UnitTestSkipped("known issue");

    u64 n = cmd.getOr("n", 100);
    bool noCheck = cmd.isSet("nc");

    oc::Timer timer;

    DarkMatter22PrfSender sender;
    DarkMatter22PrfReceiver recver;

    sender.setTimer(timer);
    recver.setTimer(timer);

    std::vector<block256> x(n);
    std::vector<oc::block> y0(n), y1(n);

    auto sock = coproto::LocalAsyncSocket::makePair();

    PRNG prng0(oc::ZeroBlock);
    PRNG prng1(oc::OneBlock);

    DarkMatter22Prf dm;
    block256 k;
    k = prng0.get();
    dm.setKey(k);
    sender.setKey(k);


    prng0.get(x.data(), x.size());
    //memset(x.data(), -1, n * sizeof(block256));
    std::vector<oc::block> rk(256);
    std::vector<std::array<oc::block, 2>> sk(256);
    for (u64 i = 0; i < 256; ++i)
    {
        sk[i][0] = oc::block(i, 0);
        sk[i][1] = oc::block(i, 1);
        rk[i] = oc::block(i, *oc::BitIterator((u8*)&k, i));
    }
    sender.setKeyOts(rk);
    recver.setKeyOts(sk);

    auto r = coproto::sync_wait(coproto::when_all_ready(
        sender.evaluate(y0, sock[0], prng0),
        recver.evaluate(x, y1, sock[1], prng1)
    ));

    std::get<0>(r).result();
    std::get<1>(r).result();

    if (cmd.isSet("v"))
    {
        std::cout << timer << std::endl;
        std::cout << sock[0].bytesReceived() / 1000.0 << " " << sock[0].bytesSent() / 1000.0 << " kB " << std::endl;
    }

    if (noCheck)
        return;

    for (u64 ii = 0; ii < n; ++ii)
    {
        block256 v;
        block256m3 u;
        memset(&v, 0, sizeof(v));
        memset(&u, 0, sizeof(u));
        for (u64 i = 0; i < dm.mKeyMask.size(); ++i)
        {
            auto xi = x[ii].rotate(i) & dm.mKeyMask[i];
            v ^= xi;
            u ^= xi;
        }

        block256 u2 = u.mod2();
        block256 w = v ^ u2;

        alignas(32) std::array<std::array<oc::block, 128>, 2> bw;
        for (u64 i = 0; i < 128; ++i)
        {
            bw[0][i] = dm.mB[i].mData[0] & w.mData[0];
            bw[1][i] = dm.mB[i].mData[1] & w.mData[1];
        }
        oc::transpose128(bw[0].data());
        oc::transpose128(bw[1].data());

        oc::block y;
        memset(&y, 0, sizeof(y));
        for (u64 i = 0; i < 128; ++i)
            y = y ^ bw[0][i];
        for (u64 i = 0; i < 128; ++i)
            y = y ^ bw[1][i];

        if ((sender.mV[ii] ^ recver.mV[ii]) != v)
            throw RTE_LOC;

        for (u64 j = 0; j < 256; ++j)
        {
            auto act = (sender.mU[ii][j] + recver.mU[ii][j]) % 3;
            if (act != u.mData[j])
                throw RTE_LOC;
        }


        auto act = (sender.mU2[ii] ^ recver.mU2[ii]);
        if (act != u2)
            throw RTE_LOC;


        auto yy = (y0[ii] ^ y1[ii]);
        if (yy != y)
            throw RTE_LOC;
    }
}



void DarkMatter32Prf_proto_test(const oc::CLP& cmd)
{

    throw oc::UnitTestSkipped("known issue " LOCATION);
    u64 n = cmd.getOr("n", 100);
    bool noCheck = cmd.isSet("nc");

    oc::Timer timer;

    DarkMatter32PrfSender sender;
    DarkMatter32PrfReceiver recver;

    sender.setTimer(timer);
    recver.setTimer(timer);

    std::vector<block256> x(n);
    std::vector<oc::block> y0(n), y1(n);

    auto sock = coproto::LocalAsyncSocket::makePair();

    PRNG prng0(oc::ZeroBlock);
    PRNG prng1(oc::OneBlock);

    DarkMatter32Prf dm;
    block256 k;
    k = prng0.get();
    dm.setKey(k);
    sender.setKey(k);


    prng0.get(x.data(), x.size());
    //memset(x.data(), -1, n * sizeof(block256));
    std::vector<oc::block> rk(256);
    std::vector<std::array<oc::block, 2>> sk(256);
    for (u64 i = 0; i < 256; ++i)
    {
        sk[i][0] = oc::block(i, 0);
        sk[i][1] = oc::block(i, 1);
        rk[i] = oc::block(i, *oc::BitIterator((u8*)&k, i));
    }
    sender.setKeyOts(rk);
    recver.setKeyOts(sk);

    auto r = coproto::sync_wait(coproto::when_all_ready(
        sender.evaluate(y0, sock[0], prng0),
        recver.evaluate(x, y1, sock[1], prng1)
    ));

    std::get<0>(r).result();
    std::get<1>(r).result();

    if (cmd.isSet("v"))
    {
        std::cout << timer << std::endl;
        std::cout << sock[0].bytesReceived() / 1000.0 << " " << sock[0].bytesSent() / 1000.0 << " kB " << std::endl;
    }

    if (noCheck)
        return;

    for (u64 ii = 0; ii < n; ++ii)
    {
        block256m3 u;
        memset(&u, 0, sizeof(u));
        for (u64 i = 0; i < dm.mKeyMask.size(); ++i)
        {
            auto xi = x[ii].rotate(i) & dm.mKeyMask[i];
            u ^= xi;
        }

        block256 w = u.mod2();

        auto y = DarkMatter32Prf::compress(w);



        for (u64 j = 0; j < 256; ++j)
        {
            auto act = (sender.mU[ii][j] + recver.mU[ii][j]) % 3;
            if (act != u.mData[j])
                throw RTE_LOC;
        }


        auto act = (sender.mU2[ii] ^ recver.mU2[ii]);
        if (act != w)
            throw RTE_LOC;


        auto yy = (y0[ii] ^ y1[ii]);
        if (yy != y)
            throw RTE_LOC;
    }
}

void mult(oc::DenseMtx& C, std::vector<u64>& X, std::vector<u64>& Y)
{
    for (u64 i = 0; i < C.rows(); ++i)
    {
        Y[i] = 0;
        for (u64 j = 0; j < C.cols(); ++j)
        {
            Y[i] += C(i, j) * X[j];
        }
    }
}


void AltModPrf_mod3BitDecompostion_test()
{
    u64 n = 256;
    u64 m = 1024;


    oc::Matrix<u16> u(n, m);
    oc::Matrix<oc::block> u0(n, m / 128);
    oc::Matrix<oc::block> u1(n, m / 128);

    mod3BitDecompostion(u, u0, u1);


    for (u64 i = 0; i < n; ++i)
    {
        auto iter0 = oc::BitIterator((u8*)u0.data(i));
        auto iter1 = oc::BitIterator((u8*)u1.data(i));
        for (u64 j = 0; j < m; ++j)
        {
            auto uu0 = u(i, j) & 1;
            auto uu1 = (u(i, j) >> 1) & 1;

            if (uu0 != *iter0++)
                throw RTE_LOC;
            if (uu1 != *iter1++)
                throw RTE_LOC;
        }
    }
}

void AltModPrf_sampleMod3_test(const oc::CLP& cmd)
{
    u64 n = 1ull << cmd.getOr("nn", 16);
    PRNG prng(oc::ZeroBlock);

    oc::AlignedUnVector<block> lsb(n), msb(n);
    sampleMod3Lookup(prng, msb, lsb);
    for (u64 i = 0;i < n;++i)
    {
        for (u64 j = 0;j < 128; ++j)
        {
            auto lsbj = bit(lsb[i], j);
            auto msbj = bit(msb[i], j);

            if ((lsbj + 2 * msbj) > 2)
                throw RTE_LOC;
        }
    }
    //sampleMod3(prng, buff);
}

void AltModPrf_AMult_test(const oc::CLP& cmd)
{
    F3AccPermCode c;
    u64 k = cmd.getOr("k", 128);
    u64 n = cmd.getOr("n", k / 2);
    u64 l = cmd.getOr("l", 1 << 4);
    u64 p = cmd.getOr("p", 0);

    c.init(k, n, p);

    //std::cout << c << std::endl;
    {
        auto m = c.getMatrix();
        std::vector<u8> v(k), y(n);
        PRNG prng(oc::ZeroBlock);
        for (u64 j = 0; j < k;++j)
            v[j] = prng.get<u8>() % 3;

        auto vv = v;
        auto yy = y;
        c.encode<u8>(vv, y);

        for (u64 i = 0; i < n; ++i)
        {
            for (u64 j = 0; j < k;++j)
            {
                yy[i] = (yy[i] + (v[j] * m(i, j))) % 3;
            }
        }

        if (yy != y)
            throw RTE_LOC;
    }

    {
        oc::Matrix<block>msb(k, l), lsb(k, l);
        oc::Matrix<block>msbOut(n, l), lsbOut(n, l);
        PRNG prng(oc::ZeroBlock);
        sampleMod3Lookup(prng, msb, lsb);


        auto msb2 = msb;
        auto lsb2 = lsb;
        c.encode(msb2, lsb2, msbOut, lsbOut);

        for (u64 i = 0; i < l * 128; ++i)
        {
            std::vector<u8> v(k), y(n), yy(n);
            for (u64 j = 0; j < k; ++j)
                v[j] = bit(lsb[j].data(), i) + bit(msb[j].data(), i) * 2;
            for (u64 j = 0; j < n; ++j)
                y[j] = bit(lsbOut[j].data(), i) + bit(msbOut[j].data(), i) * 2;

            c.encode<u8>(v, yy);

            if (y != yy)
            {
                std::cout << "v  ";
                for (u64 j = 0; j < k; ++j)
                    std::cout << (int)v[j] << " ";
                std::cout << std::endl;
                std::cout << "y  ";
                for (u64 j = 0; j < n; ++j)
                    std::cout << (int)y[j] << " ";
                std::cout << std::endl << "yy ";
                for (u64 j = 0; j < n; ++j)
                    std::cout << (int)yy[j] << " ";
                std::cout << std::endl;
                throw RTE_LOC;
            }
        }
    }
}



void AltModPrf_BMult_test(const oc::CLP& cmd)
{
    u64 n = 1ull << cmd.getOr("nn", 12);
    u64 n128 = n / 128;
    oc::Matrix<oc::block> v(256, n128);
    std::vector<block256> V(n);
    PRNG prng(oc::ZeroBlock);
    prng.get(V.data(), V.size());

    for (u64 i = 0; i < n; ++i)
    {
        for (u64 j = 0; j < 256; ++j)
        {
            *oc::BitIterator((u8*)&v(j, 0), i) = bit(V[i], j);
        }
    }
    std::vector<oc::block> y(n);
    compressB(v, y);

    //oc::Matrix<u64> B(128, 256);
    //for (u64 i = 0; i < 128; ++i)
    //{
    //    u64 j = 0;
    //    for (; j < 128; ++j)
    //        B(i, j) = j == i ? 1 : 0;
    //    for (; j < B.cols(); ++j)
    //    {
    //        B(i, j) = *oc::BitIterator((u8*)&AltModPrf::mB[i], j - 128);
    //    }
    //}

    for (u64 i = 0; i < n; ++i)
    {
        auto Y = AltModPrf::compress(V[i]);
        if (y[i] != Y)
            throw RTE_LOC;

        Y = oc::ZeroBlock;
        AltModPrf::mBCode.encode((u8*)&V[i].mData[1], (u8*)&Y);

        {
            auto w = V[i];
            oc::AlignedArray<block, 128> bw;

            for (u64 i = 0; i < 128; ++i)
            {
                //bw[0][i] = B[i].mData[0] & w.mData[0];
                bw[i] = AltModPrf::mB[i] & w.mData[1];
            }
            oc::transpose128(bw.data());
            //oc::transpose128(bw[1].data());

            block r = oc::ZeroBlock;//w[0];
            //memset(&r, 0, sizeof(r));
            //for (u64 i = 0; i < 128; ++i)
            //    r = r ^ bw[0][i];
            for (u64 i = 0; i < 128; ++i)
                r = r ^ bw[i];

            //oc::block b = oc::ZeroBlock;
            //for (u64 ii = 0; ii < 128; ++ii)
            //{
            //    if (bit(V[i].mData[1], ii))
            //    {
            //        b = b ^ AltModPrf::mB[ii];
            //    }
            //}

            //if (Y != b)
            //    throw RTE_LOC;
            if (r != Y)
                throw RTE_LOC;
        }
        //Y = Y ^ V[i].mData[0];
        Y = Y ^ V[i].mData[0];
        if (y[i] != Y)
            throw RTE_LOC;
    }
}

void AltModPrf_mod2_test(const oc::CLP& cmd)
{


    u64 n = cmd.getOr("n", 128);
    u64 m = cmd.getOr("m", 128);
    auto m128 = oc::divCeil(m, 128);


    u64 printI = cmd.getOr("i", -1);
    u64 printJ = cmd.getOr("j", -1);

    PRNG prng0(oc::ZeroBlock);
    PRNG prng1(oc::OneBlock);
    oc::Timer timer;

    AltModPrfSender sender;
    AltModPrfReceiver recver;

    //sender.mPrintI = printI;
    //sender.mPrintJ = printJ;
    //recver.mPrintI = printI;
    //recver.mPrintJ = printJ;

    sender.setTimer(timer);
    recver.setTimer(timer);

    oc::Matrix<u16> u(n, m);
    std::array<oc::Matrix<u16>, 2> us;
    us[0].resize(n, m);
    us[1].resize(n, m);
    for (u64 i = 0; i < u.rows(); ++i)
    {
        for (u64 j = 0; j < u.cols(); ++j)
        {

            u(i, j) = prng0.get<u8>() % 3;
            us[0](i, j) = prng0.get<u8>() % 3;
            us[1](i, j) = u8(u(i, j) + 3 - us[0](i, j)) % 3;
            assert((u8(us[0](i, j) + us[1](i, j)) % 3) == u(i, j));
        }
    }


    //auto us = xorShare(u, prng0);

    std::array<oc::Matrix<oc::block>, 2> u0s, u1s;
    u0s[0].resize(n, m128);
    u0s[1].resize(n, m128);
    u1s[0].resize(n, m128);
    u1s[1].resize(n, m128);
    mod3BitDecompostion(us[0], u0s[0], u1s[0]);
    mod3BitDecompostion(us[1], u0s[1], u1s[1]);

    //if (i == printI && j == printJ)
    if (printI < n)
    {
        auto i = printI;
        auto j = printJ;
        std::cout << "\nu(" << i << ", " << j << ") \n"
            << "    = " << u(i, j) << "\n"
            << "    = " << us[0](i, j) << " + " << us[1](i, j) << "\n"
            << "    = " << bit(u1s[0](i, 0), j) << bit(u0s[0](i, 0), j) << " + "
            << bit(u1s[1](i, 0), j) << bit(u0s[1](i, 0), j) << std::endl;
    }

    //auto u0s = share(u0, prng0);
    //auto u1s = share(u1, prng0);
    std::array<oc::Matrix<oc::block>, 2> outs;
    outs[0].resize(n, m128);
    outs[1].resize(n, m128);

    auto sock = coproto::LocalAsyncSocket::makePair();


    CorGenerator ole0, ole1;
    auto chls = coproto::LocalAsyncSocket::makePair();
    ole0.init(chls[0].fork(), prng0, 0, 1 << 18, cmd.getOr("mock", 1));
    ole1.init(chls[1].fork(), prng1, 1, 1 << 18, cmd.getOr("mock", 1));


    sender.init(n);
    recver.init(n);
    //    sender.request(ole0);
    //    recver.request(ole1);
    sender.mOleReq = ole0.binOleRequest(u0s[0].rows() * u0s[0].cols() * 256);
    recver.mOleReq = ole1.binOleRequest(u0s[0].rows() * u0s[0].cols() * 256);

    auto s0 = sender.mOleReq.start() | macoro::make_eager();
    auto s1 = recver.mOleReq.start() | macoro::make_eager();

    macoro::sync_wait(macoro::when_all_ready(
        sender.mod2(u0s[0], u1s[0], outs[0], sock[0]),
        recver.mod2(u0s[1], u1s[1], outs[1], sock[1])
    ));
    macoro::sync_wait(macoro::when_all_ready(std::move(s0), std::move(s1)));

    auto out = reveal(outs);


    for (u64 i = 0; i < n; ++i)
    {
        auto iter = oc::BitIterator((u8*)out[i].data());
        for (u64 j = 0; j < m; ++j)
        {
            u8 uij = u(i, j);
            u8 exp = uij % 2;
            u8 act = *iter++;
            if (exp != act)
            {
                std::cout << "i " << i << " j " << j << "\n"
                    << "act " << int(act) << " = "
                    << *oc::BitIterator((u8*)&outs[0](i, 0), j) << " ^ "
                    << *oc::BitIterator((u8*)&outs[1](i, 0), j) << std::endl
                    << "exp " << int(exp) << " = " << u(i, j) << " = " << us[0](i, j) << " + " << us[1](i, j) << std::endl;
                throw RTE_LOC;
            }
        }
    }
}

void AltModPrf_mod3_test(const oc::CLP& cmd)
{


    PRNG prng(oc::ZeroBlock);
    u64 n = 100;
    for (u64 i = 0; i < n;++i)
    {
        u8 x = prng.get<u8>() % 3;
        u8 y = prng.get<u8>() % 3;
        auto a = (x >> 1) & 1;
        auto b = x & 1;
        auto c = (y >> 1) & 1;
        auto d = y & 1;

        auto ab = a ^ b;
        auto z1 = (1 ^ d ^ b) * (ab ^ c);
        auto z0 = (1 ^ a ^ c) * (ab ^ d);
        auto e = (x + y) % 3;
        if (z0 != (e & 1))
            throw RTE_LOC;
        if (z1 != (e >> 1))
            throw RTE_LOC;
    }


    //     c
    // ab  0  1   // msb = bc+a(1+c)  = bc + a + ac
    // 00  0  0          = a + (b+a)c =
    // 01  0  1          = 
    // 10  1  0    
    // 
    //     0  1   // lsb = b(1+c) + (1+b+a)c
    // 00  0  1          = b + (1 + a) c
    // 01  1  0 
    // 10  0  0
    for (u64 i = 0; i < n;++i)
    {
        u8 x = prng.get<u8>() % 3;
        u8 c = prng.get<u8>() % 2;
        auto a = (x >> 1) & 1;
        auto b = x & 1;

        //auto ab = a ^ b;
        //auto z1 = (1 ^ b) * (ab ^ c);
        //auto z0 = (1 ^ a ^ c) * (ab);
        auto z1 = a ^ (a ^ b) * c;
        auto z0 = b ^ (1 ^ a) * c;
        auto e = (x + c) % 3;
        if (z0 != (e & 1))
            throw RTE_LOC;
        if (z1 != (e >> 1))
            throw RTE_LOC;

        oc::block A = oc::block::allSame(-a);
        oc::block B = oc::block::allSame(-b);
        oc::block C = oc::block::allSame(-c);
        oc::block Z0 = oc::block::allSame(-z0);
        oc::block Z1 = oc::block::allSame(-z1);

        mod3Add(
            span<oc::block>(&A, 1), span<oc::block>(&B, 1),
            span<oc::block>(&A, 1), span<oc::block>(&B, 1),
            span<oc::block>(&C, 1));

        if (Z0 != B)
            throw RTE_LOC;
        if (Z1 != A)
            throw RTE_LOC;
    }

}


void AltModPrf_plain_test()
{

    u64 len = 1 << 10;
    u64 n = AltModPrf::KeySize;
    u64 m = 256;
    u64 t = 128;
    PRNG prng(oc::ZeroBlock);
    AltModPrf::KeyType kk = prng.get();
    oc::block xx = prng.get();

    std::array<oc::block, AltModPrf::KeySize / 128> x;
    if (x.size() > 1) {

        for (u64 i = 0; i < x.size(); ++i)
            x[i] = xx ^ oc::block(i, i);
        oc::mAesFixedKey.hashBlocks<x.size() - 1>(x.data() + 1, x.data() + 1);
    }
    else
        x[0] = xx;

    AltModPrf prf;

    prf.setKey(kk);


    auto y = prf.eval(xx);

    oc::Matrix<u64> B(t, m);
    std::vector<u64> X(n), K(n), H(n), U(m), W(m), Y(t);
    for (u64 i = 0; i < n; ++i)
    {
        X[i] = *oc::BitIterator((u8*)&x, i);
        K[i] = *oc::BitIterator((u8*)&prf.mExpandedKey, i);
        H[i] = X[i] & K[i];

        //if (i < 20)
        //    std::cout << "H[" << i << "] = " << (H[i]) << " = " << K[i] << " * " << X[i] << std::endl;

    }
    for (u64 i = 0; i < t; ++i)
    {
        u64 j = 0;
        for (; j < 128; ++j)
            B(i, j) = j == i ? 1 : 0;
        for (; j < m; ++j)
        {
            B(i, j) = *oc::BitIterator((u8*)&AltModPrf::mB[i], j - 128);
        }
    }

    AltModPrf::mACode.encode<u64>(H, U);

    for (u64 i = 0; i < m; ++i)
    {
        W[i] = (U[i] % 3) % 2;
    }
    for (u64 i = 0; i < t; ++i)
    {
        for (u64 j = 0; j < m; ++j)
        {
            Y[i] ^= B(i, j) * W[j];
        }

        if (Y[i] != (u8)*oc::BitIterator((u8*)&y, i))
        {
            throw RTE_LOC;
        }
    }

    {
        std::vector<block> x(len), y(len);
        prf.eval(x, y);
        for (u64 i = 0; i < len; ++i)
        {
            auto exp = prf.eval(x[i]);
            if (y[i] != exp)
                throw RTE_LOC;
        }
    }
}


void AltModProtoCheck(AltModPrfSender& sender, AltModPrfReceiver& recver)
{
    auto x = recver.mDebugInput;
    auto n = x.size();

    oc::Matrix<block> xt(AltModPrf::KeySize, oc::divCeil(n, 128));
    AltModPrf::expandInput(x, xt);

    for (u64 ii = 0; ii < n; ++ii)
    {

        std::array<u16, AltModPrf::KeySize> h;
        std::array<oc::block, AltModPrf::KeySize / 128> X;
        AltModPrf::expandInput(x[ii], X);



        auto kIter = oc::BitIterator((u8*)sender.mPrf.mExpandedKey.data());
        auto xIter = oc::BitIterator((u8*)X.data());
        for (u64 i = 0; i < AltModPrf::KeySize; ++i)
        {
            if (bit(X, i) != bit(xt[i].data(), ii))
                throw RTE_LOC;

            u8 xi = *xIter;
            u8 ki = *kIter;
            h[i] = ki & xi;

            assert(recver.mDebugXk0.cols() == oc::divCeil(x.size(), 128));
            auto r0 = bit(recver.mDebugXk0.data(i), ii);
            auto r1 = bit(recver.mDebugXk1.data(i), ii);
            auto s0 = bit(sender.mDebugXk0.data(i), ii);
            auto s1 = bit(sender.mDebugXk1.data(i), ii);


            auto s = 2 * s1 + s0;
            auto r = 2 * r1 + r0;

            //auto neg = (3 - r) % 3;
            auto act = (s + r) % 3;
            if (act != h[i])
                throw RTE_LOC;

            ++kIter;
            ++xIter;
        }

        block256m3 u;
        sender.mPrf.mtxMultA(h, u);

        for (u64 i = 0; i < 256; ++i)
        {
            auto r0 = bit(recver.mDebugU0.data(i), ii);
            auto r1 = bit(recver.mDebugU1.data(i), ii);
            auto s0 = bit(sender.mDebugU0.data(i), ii);
            auto s1 = bit(sender.mDebugU1.data(i), ii);

            auto s = 2 * s1 + s0;
            auto r = 2 * r1 + r0;

            if ((s + r) % 3 != u.mData[i])
            {
                throw RTE_LOC;
            }

        }

        block256 w;
        for (u64 i = 0; i < u.mData.size(); ++i)
        {
            *oc::BitIterator((u8*)&w, i) = u.mData[i] % 2;

            auto v0 = bit(sender.mDebugV(i, 0), ii);
            auto v1 = bit(recver.mDebugV(i, 0), ii);

            if ((v0 ^ v1) != u.mData[i] % 2)
            {
                throw RTE_LOC;
            }
        }


        //    auto yy = sender.mPrf.compress(w);

        //    y = sender.mPrf.eval(x[ii]);
        //else
        //    y = sender.mPrf.eval(x[ii]);

        //auto yy = (y0[ii] ^ y1[ii]);
        //if (yy != y)
        //{
        //    std::cout << "i   " << ii << std::endl;
        //    std::cout << "act " << yy << std::endl;
        //    std::cout << "exp " << y << std::endl;
        //    throw RTE_LOC;
        //}
    }
}

void AltModPrf_proto_test(const oc::CLP& cmd)
{


    u64 n = cmd.getOr("n", 1024);
    bool noCheck = cmd.isSet("nc");
    bool debug = cmd.isSet("debug");

    oc::Timer timer;

    AltModPrfSender sender;
    AltModPrfReceiver recver;
    sender.mDebug = debug;
    recver.mDebug = debug;

    sender.setTimer(timer);
    recver.setTimer(timer);

    std::vector<oc::block> x(n);
    std::vector<oc::block> y0(n), y1(n);

    auto sock = coproto::LocalAsyncSocket::makePair();

    PRNG prng0(oc::ZeroBlock);
    PRNG prng1(oc::OneBlock);

    AltModPrf dm;
    dm.setKey(prng0.get());
    //sender.setKey(kk);

    CorGenerator ole0, ole1;
    ole0.init(sock[0].fork(), prng0, 0, 1 << 18, cmd.getOr("mock", 1));
    ole1.init(sock[1].fork(), prng1, 1, 1 << 18, cmd.getOr("mock", 1));



    prng0.get(x.data(), x.size());
    //memset(x.data(), -1, n * sizeof(block256));
    std::vector<oc::block> rk(AltModPrf::KeySize);
    std::vector<std::array<oc::block, 2>> sk(AltModPrf::KeySize);
    for (u64 i = 0; i < AltModPrf::KeySize; ++i)
    {
        sk[i][0] = oc::block(i, 0);
        sk[i][1] = oc::block(i, 1);
        rk[i] = oc::block(i, *oc::BitIterator((u8*)&dm.mExpandedKey, i));
    }
    sender.setKeyOts(dm.getKey(), rk);
    recver.setKeyOts(sk);

    auto r = coproto::sync_wait(coproto::when_all_ready(
        sender.evaluate(y0, sock[0], prng0, ole0),
        recver.evaluate(x, y1, sock[1], prng1, ole1)
    ));

    std::get<0>(r).result();
    std::get<1>(r).result();


    if (cmd.isSet("v"))
    {
        std::cout << timer << std::endl;
        std::cout << sock[0].bytesReceived() / 1000.0 << " " << sock[0].bytesSent() / 1000.0 << " kB " << std::endl;
    }

    if (noCheck)
        return;

    for (u64 ii = 0; ii < n; ++ii)
    {
        if (debug)
        {
            AltModProtoCheck(sender, recver);
        }

        auto y = sender.mPrf.eval(x[ii]);

        auto yy = (y0[ii] ^ y1[ii]);
        if (yy != y)
        {
            std::cout << "i   " << ii << std::endl;
            std::cout << "act " << yy << std::endl;
            std::cout << "exp " << y << std::endl;
            throw RTE_LOC;
        }
    }
}
