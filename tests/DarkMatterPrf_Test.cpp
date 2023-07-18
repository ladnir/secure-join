#include "DarkMatterPrf_Test.h"
#include "secure-join/Prf/DarkMatter22Prf.h"
#include "secure-join/Prf/DarkMatter32Prf.h"
#include "secure-join/Prf/DLpnPrf.h"
#include "cryptoTools/Crypto/PRNG.h"
#include "cryptoTools/Common/Matrix.h"

#include "cryptoTools/Common/TestCollection.h"
#include "secure-join/Util/Util.h"

using namespace secJoin;

void DarkMatter22Prf_plain_test()
{
    throw oc::UnitTestSkipped("known issue");
    oc::PRNG prng(oc::ZeroBlock);
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
    oc::PRNG prng(oc::ZeroBlock);
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
    oc::PRNG prng(oc::ZeroBlock);
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

    oc::PRNG prng0(oc::ZeroBlock);
    oc::PRNG prng1(oc::OneBlock);

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

    oc::PRNG prng0(oc::ZeroBlock);
    oc::PRNG prng1(oc::OneBlock);

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


void DLpnPrf_mod3BitDecompostion_test()
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

void DLpnPrf_BMult_test()
{
    u64 n = 256;
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

    for (u64 i = 0; i < n; ++i)
    {
        auto Y = DLpnPrf::compress(V[i]);
        if (y[i] != Y)
            throw RTE_LOC;

    }
}

void DLpnPrf_mod2_test(const oc::CLP& cmd)
{


    u64 n = cmd.getOr("n", 128);
    u64 m = cmd.getOr("m", 128);
    auto m128 = oc::divCeil(m, 128);


    u64 printI = cmd.getOr("i", -1);
    u64 printJ = cmd.getOr("j", -1);

    oc::PRNG prng0(oc::ZeroBlock);
    oc::PRNG prng1(oc::OneBlock);
    oc::Timer timer;

    DLpnPrfSender sender;
    DLpnPrfReceiver recver;

    sender.mPrintI = printI;
    sender.mPrintJ = printJ;
    recver.mPrintI = printI;
    recver.mPrintJ = printJ;

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


    OleGenerator ole0, ole1;
    ole0.fakeInit(OleGenerator::Role::Sender);
    ole1.fakeInit(OleGenerator::Role::Receiver);

    auto req0 = macoro::sync_wait(ole0.binOleRequest(2 * n * m));
    auto req1 = macoro::sync_wait(ole1.binOleRequest(2 * n * m));

    macoro::sync_wait(macoro::when_all_ready(
        sender.mod2(u0s[0], u1s[0], outs[0], sock[0], req0),
        recver.mod2(u0s[1], u1s[1], outs[1], sock[1], req1)
    ));

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

void DLpnPrf_mod3_test(const oc::CLP& cmd)
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


void DLpnPrf_plain_test()
{


    u64 n = DLpnPrf::KeySize;
    u64 m = 256;
    u64 t = 128;
    oc::PRNG prng(oc::ZeroBlock);
    oc::block kk = prng.get();
    oc::block xx = prng.get();

    std::array<oc::block, DLpnPrf::KeySize / 128> x;
    if (x.size() > 1) {

        for (u64 i = 0; i < x.size(); ++i)
            x[i] = xx ^ oc::block(i, i);
        oc::mAesFixedKey.hashBlocks<DLpnPrf::KeySize / 128>(x.data(), x.data());
    }
    else
        x[0] = xx;

    DLpnPrf prf;

    prf.setKey(kk);


    auto y = prf.eval(xx);

    oc::Matrix<u64> B(t, m);
    std::vector<u64> X(n), K(n), H(n), U(m), W(m), Y(t);
    for (u64 i = 0; i < n; ++i)
    {
        X[i] = *oc::BitIterator((u8*)&x, i);
        K[i] = *oc::BitIterator((u8*)&prf.mKey, i);
        H[i] = X[i] & K[i];

        //if (i < 20)
        //    std::cout << "H[" << i << "] = " << (H[i]) << " = " << K[i] << " * " << X[i] << std::endl;

    }
    for (u64 i = 0; i < t; ++i)
    {
        for (u64 j = 0; j < m; ++j)
        {
            B(i, j) = *oc::BitIterator((u8*)&DarkMatter22Prf::mB[i], j);
        }
    }

    //oc::DenseMtx Accumulator(X.size(), X.size());
    //oc::DenseMtx Expander(X.size() / 2, X.size());

    //for (u64 i = 0; i < X.size(); ++i)
    //{
    //    for (u64 j = 0; j <= i; ++j)
    //    {
    //        Accumulator(i, j) = 1;
    //    }

    //    Expander(i / 2, prf.mPi[i]) = 1;
    //}

    //auto C = Expander * Accumulator;

    //std::cout << "C\n" << C << std::endl;

    //mult(C, H, U);
    {
        //assert(mPi.size() != 0);

        //for (u64 k = 0; k < prf.KeySize; ++k)
        //{
        //    U[k] = H[k];
        //}
        if (prf.KeySize == 256)
        {

            U[0] = H[0];
            for (u64 k = 1; k < prf.KeySize; ++k)
            {
                U[k] = H[k] + U[k - 1];
                U[k] %= 3;
            }
        }
        else if (prf.KeySize == 128)
        {

            U[0] = H[0];
            for (u64 k = 1; k < prf.KeySize; ++k)
            {
                U[k] = H[k] + U[k - 1];
                U[k] %= 3;
            }
            for (u64 k = 0; k < prf.KeySize; ++k)
            {
                U[k + 128] = U[k];
                //U[k + 128] %= 3;
            }
        }
        else
        {
            assert(0);
        }

        //auto pik = prf.mPi.data();
        //for (u64 k = 0; k < m; ++k)
        //{
        //    U[k] = (
        //        H[pik[0]] +
        //        H[pik[1]]
        //        ) % 3;
        //    pik += 2;
        //}
    }
    for (u64 i = 0; i < m; ++i)
    {
        //if (i < 20)
        //    std::cout << "U[" << i << "] = " << (U[i] % 3) << std::endl;

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

}

void DLpnPrf_proto_test(const oc::CLP& cmd)
{


    u64 n = cmd.getOr("n", 1024);
    bool noCheck = cmd.isSet("nc");

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
    //memset(x.data(), -1, n * sizeof(block256));
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

    auto r = coproto::sync_wait(coproto::when_all_ready(
        sender.evaluate(y0, sock[0], prng0, ole0),
        recver.evaluate(x, y1, sock[1], prng1, ole1)
    ));

    std::get<0>(r).result();
    std::get<1>(r).result();

    coproto::sync_wait(coproto::when_all_ready(
        ole0.stop(),
        ole1.stop()
    ));

    if (cmd.isSet("v"))
    {
        std::cout << timer << std::endl;
        std::cout << sock[0].bytesReceived() / 1000.0 << " " << sock[0].bytesSent() / 1000.0 << " kB " << std::endl;
    }

    if (noCheck)
        return;

    for (u64 ii = 0; ii < n; ++ii)
    {
        oc::block y;
        //if (sender.mU.size())
        //{

        //    std::array<u16, sender.mPrf.KeySize> h;
        //    std::array<oc::block, sender.mPrf.KeySize / 128> X;
        //    if (sender.mPrf.KeySize / 128 > 1)
        //    {
        //        for (u64 i = 0; i < X.size(); ++i)
        //            X[i] = x[ii] ^ oc::block(i, i);
        //        oc::mAesFixedKey.hashBlocks<X.size()>(X.data(), X.data());
        //    }
        //    else
        //    {
        //        X[0] = x[ii];
        //    }

        //    auto kIter = oc::BitIterator((u8*)sender.mPrf.mKey.data());
        //    auto xIter = oc::BitIterator((u8*)X.data());
        //    for (u64 i = 0; i < sender.mPrf.KeySize; ++i)
        //    {
        //        u8 xi = *xIter;
        //        u8 ki = *kIter;
        //        h[i] = ki & xi;

        //        assert(recver.mH.cols() == x.size());
        //        auto r = recver.mH(i, ii);
        //        auto s = sender.mH(i, ii);
        //        //auto neg = (3 - r) % 3;
        //        auto act = (s + r) % 3;
        //        if (act != h[i])
        //            throw RTE_LOC;

        //        ++kIter;
        //        ++xIter;
        //    }

        //    block256m3 u;
        //    sender.mPrf.compressH(h, u);

        //    for (u64 i = 0; i < 256; ++i)
        //    {
        //        if ((sender.mU[i][ii] + recver.mU[i][ii]) % 3 != u.mData[i])
        //        {
        //            throw RTE_LOC;
        //        }

        //    }

        //    block256 w;
        //    for (u64 i = 0; i < u.mData.size(); ++i)
        //    {
        //        *oc::BitIterator((u8*)&w, i) = u.mData[i] % 2;

        //        auto v0 = bit(sender.mV(i, 0), ii);
        //        auto v1 = bit(recver.mV(i, 0), ii);

        //        if ((v0 ^ v1) != u.mData[i] % 2)
        //        {
        //            throw RTE_LOC;
        //        }
        //    }


        //    y = sender.mPrf.compress(w);
        //}
        //else
            y = sender.mPrf.eval(x[ii]);

        auto yy = (y0[ii] ^ y1[ii]);
        if (yy != y)
        {
            std::cout << "act " << yy << std::endl;
            std::cout << "exp " << y << std::endl;
            throw RTE_LOC;
        }
    }
}
