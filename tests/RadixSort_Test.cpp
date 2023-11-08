#include "RadixSort_Test.h"
#include "secure-join/Sort/RadixSort.h"
#include "cryptoTools/Network/IOService.h"
#include "secure-join/Util/Util.h"
#include "secure-join/Sort/BitInjection.h"
using namespace oc;
using namespace secJoin;

void RadixSort_aggregateSum_test(const oc::CLP& cmd)
{
    u64 n = 123;
    u64 L = 1 << 5;

    oc::Matrix<u32> f(n, L);
    oc::Matrix<u32> s1(n, L);
    oc::Matrix<u32> s2(n, L);
    u64 partyIdx = 0;

    {
        auto L2 = f.cols();
        auto m = f.rows();

        // sum = -1
        u32 sum = -partyIdx;

        // sum over column j.
        for (u64 j = 0; j < L2; ++j)
        {
            auto f0 = f.begin() + j;
            auto s0 = s1.begin() + j;
            for (u64 i = 0; i < m; ++i)
            {
                sum += *f0;
                *s0 = sum;
                f0 += L2;
                s0 += L2;
            }
        }
    }

    RadixSort::aggregateSum(f, s2, 0);

    for (u64 i = 0; i < s1.size(); ++i)
        if (s1(i) != s2(i))
            throw RTE_LOC;
}

void RadixSort_hadamardSum_test(const oc::CLP& cmd)
{
    auto comm = coproto::LocalAsyncSocket::makePair();
    RadixSort s0, s1;
    u64 cols = 1 << s0.mL;
    u64 rows = cmd.getOr("n", 100);

    oc::Matrix<u32> d0(rows, cols), d1(rows, cols);


    PRNG prng(block(0, 0));

    oc::Matrix<u32> r(rows, cols);
    BinMatrix l(1, rows * cols);

    BinMatrix
        l0(1, rows * cols),
        l1(1, rows * cols);

    oc::Matrix<u32> r0(rows, cols), r1(rows, cols);
    AdditivePerm p0, p1;
    p0.init2(0, rows);
    p1.init2(1, rows);

    auto lIter = oc::BitIterator(l.data());
    for (u64 i = 0; i < rows; ++i)
    {
        for (u64 j = 0; j < cols; ++j)
        {
            r(i, j) = prng.get<u64>() % 4;
        }
        *(lIter + (prng.get<u64>() % cols)) = 1;
        lIter = lIter + cols;
    }

    share(l, l0, l1, prng);
    share(r, r0, r1, prng);

    CorGenerator g0, g1;

    g0.init(comm[0].fork(), prng, 0, 1<<18, cmd.getOr("mock", 1));
    g1.init(comm[1].fork(), prng, 1, 1<<18, cmd.getOr("mock", 1));
    s0.mDebug = cmd.isSet("debug");
    s1.mDebug = cmd.isSet("debug");

    //s0.request(
    s0.init(0, rows, s0.mL); s0.request(g0);
    s1.init(1, rows, s0.mL); s1.request(g1);

    macoro::sync_wait(macoro::when_all_ready(
        s0.preprocess(comm[0], prng),
        s1.preprocess(comm[1], prng)
    ));
    //s0.initArith2BinCircuit(rows);
    //s1.initArith2BinCircuit(rows);
    macoro::sync_wait(macoro::when_all_ready(
        s0.hadamardSum(s0.mRounds[0], l0, r0, p0, comm[0]),
        s1.hadamardSum(s1.mRounds[0], l1, r1, p1, comm[1])
    ));

    Perm ff = reveal(p0, p1);
    //auto c = reveal(c0, c1);

    oc::Matrix<u32> exp(rows, 1);
    exp.setZero();

    lIter = oc::BitIterator(l.data());
    for (u64 j = 0; j < (u64)exp.size(); ++j)
    {
        for (u64 i = 0; i < cols; ++i)
        {
            //if (c(j, i) != r(j, i) * l(j, i))
            //    throw RTE_LOC;

            exp(j) += r(j, i) * *lIter++;
        }
    }

    for (u64 i = 0; i < rows; ++i)
        if (exp(i) != ff[i])
        {
            //std::cout << exp << "\n\n" << ff << std::endl;
            throw RTE_LOC;
        }
}

void RadixSort_oneHot_test(const oc::CLP& cmd)
{

    u64 L = 2;
    u64 n = cmd.getOr("n",324);
    u64 mod = 1ull << L;
    auto comm = coproto::LocalAsyncSocket::makePair();
    std::array<std::future<void>, 2> f;
    std::array<CorGenerator, 2> g;
    PRNG prng(oc::ZeroBlock);
    g[0].init(comm[0].fork(), prng, 0, 1<<18, cmd.getOr("mock", 1));
    g[1].init(comm[1].fork(), prng, 1, 1<<18, cmd.getOr("mock", 1));

    Matrix<u8> kk(n, 1);
    for (u64 i = 0; i < n; ++i)
        kk(i) = prng.get<u8>() % mod;

    std::array<oc::Matrix<u8>, 2> k, bits;

    share(kk, k[0], k[1], prng);
    RadixSort rs;
    rs.initIndexToOneHotCircuit(L);
    auto cir = rs.mIndexToOneHotCircuit;

    std::array<Gmw, 2> gmw;
    gmw[0].init(n, cir);
    gmw[1].init(n, cir);

    gmw[0].setInput(0, k[0]);
    gmw[1].setInput(0, k[1]);

    macoro::sync_wait(macoro::when_all_ready(
        gmw[0].run(g[0], comm[0], prng),
        gmw[1].run(g[1], comm[1], prng)
    ));

    bits[0].resize(n, 1);
    bits[1].resize(n, 1);
    gmw[0].getOutput(0, bits[0]);
    gmw[1].getOutput(0, bits[1]);

    auto bb = reveal(bits[0], bits[1]);

    for (u64 i = 0; i < n; ++i)
    {
        oc::BitIterator iter((u8*)&bb(i, 0));
        for (u64 j = 0; j < mod; ++j, ++iter)
        {
            auto exp = kk(i) == j ? 1 : 0;
            auto act = *iter;
            if (exp != act)
                throw RTE_LOC;
        }
    }
}


void RadixSort_bitInjection_test(const oc::CLP& cmd)
{

    auto comm = coproto::LocalAsyncSocket::makePair();
    u64 L = 21;
    u64 n = cmd.getOr("n", 128 * 7);


    PRNG prng(block(0, 0));

    oc::Matrix<u8> k(n, oc::divCeil(L, 8));
    oc::Matrix<u8> k0(n, k.cols()), k1(n, k.cols());
    oc::Matrix<u32> f0(n, L), f1(n, L);

    for (u64 i = 0; i < k.size(); ++i)
        k(i) = prng.get();
    if (L % 8)
    {
        auto r = L % 8;
        auto mask = (1 << r) - 1;
        for (u64 i = 0; i < k.rows(); ++i)
            k[i].back() &= mask;

    }
    CorGenerator g0, g1;
    g0.init(comm[0].fork(), prng, 0, 1<<18, cmd.getOr("mock", 1));
    g1.init(comm[1].fork(), prng, 1, 1<<18, cmd.getOr("mock", 1));

    share(k, L, k0, k1, prng);

    BitInject bi0, bi1;


    bi0.init(n, L);
    bi1.init(n, L);

    bi0.request(g0);
    bi1.request(g1);

    macoro::sync_wait(macoro::when_all_ready(
        bi0.preprocess(),
        bi1.preprocess()
        ));

    macoro::sync_wait(macoro::when_all_ready(
        bi0.bitInjection(k0, 32, f0, comm[0]),
        bi1.bitInjection(k1, 32, f1, comm[1])
    ));

    auto ff = reveal(f0, f1);
    if ((u64)ff.rows() != n)
        throw RTE_LOC;
    if ((u64)ff.cols() != L)
        throw RTE_LOC;

    for (u64 i = 0; i < k.rows(); ++i)
    {
        auto iter = oc::BitIterator(k[i].data());
        for (u64 j = 0; j < L; ++j)
        {
            auto v = ff(i, j);
            auto b = *iter++;
            if (v != b)
            {
                throw RTE_LOC;
            }
        }
    }
}

void RadixSort_genValMasks2_test(const oc::CLP& cmd)
{

    auto comm = coproto::LocalAsyncSocket::makePair();
    //u64 L = 1;
    //u64 n = 128 * 8;

    for (u64 n : { 10ull, 324ull, 1ull << cmd.getOr("nn", 10) })
    {
        for (u64 L : { 1, 2, 5 })
        {

            PRNG prng(block(0, 0));
            RadixSort s0, s1;

            BinMatrix k(n, L);
            BinMatrix k0(n, L), k1(n, L);
            BinMatrix fBin0, fBin1;
            oc::Matrix<u32> f0(n, 1ull << L), f1(n, 1ull << L);

            std::vector<std::vector<i64>> vals(1 << L);
            for (u64 i = 0; i < k.rows(); ++i)
            {
                u64 v = prng.get<u64>() & ((1 << L) - 1);
                k(i) = v;
                vals[v].push_back(i);
            }

            CorGenerator g0, g1;
            g0.init(comm[0].fork(), prng, 0, 1<<18, cmd.getOr("mock", 1));
            g1.init(comm[1].fork(), prng, 1, 1<<18, cmd.getOr("mock", 1));

            share(k, k0, k1, prng);
            s0.mL = L;
            s1.mL = L;

            s0.init(0, n, L);
            s1.init(1, n, L);
            s0.request(g0);
            s1.request(g1);

            macoro::sync_wait(macoro::when_all_ready(
                s0.preprocess(comm[0], prng),
                s1.preprocess(comm[1], prng)
            ));
            macoro::sync_wait(macoro::when_all_ready(
                s0.genValMasks2(s0.mRounds[0], L, k0, f0, fBin0, comm[0]),
                s1.genValMasks2(s1.mRounds[0], L, k1, f1, fBin1, comm[1])
            ));

            auto ff = reveal(f0, f1);
            auto ffBin = reveal(fBin0, fBin1);
            if ((u64)ff.rows() != n)
                throw RTE_LOC;
            if ((u64)ff.cols() != (1ull << L))
                throw RTE_LOC;

            //for (u64 ii = 0; ii < k.size(); ++ii)
            //    std::cout << (int)k(ii) << " ";

            //std::cout << std::endl;

            //for (u64 i = 0; i < ff.rows(); ++i)
            //{
            //    std::cout << std::endl;
            //    for (u64 j = 0; j < ff.cols(); ++j)
            //        std::cout << ff(i, j) << " ";
            //}
            //std::cout << std::endl;

            auto iter = oc::BitIterator(ffBin.data());

            for (u64 j = 0; j < n; ++j)
            {
                for (u64 i = 0; i < (1ull << L); ++i)
                {

                    if (*iter++ != ff(j, i))
                        throw RTE_LOC;

                    if ((u64)k(j) == i)
                    {
                        auto ee = ff(j, i);
                        if (ee != 1)
                            throw RTE_LOC;
                    }
                    else
                    {
                        if (ff(j, i) != 0)
                        {

                            throw RTE_LOC;
                        }
                    }
                }
            }

        }
    }

}

bool areEqual(
    oc::span<u8> a,
    oc::span<u8> b,
    u64 bitCount)
{
    auto mod8 = bitCount & 7;
    auto div8 = bitCount >> 3;
    if (a.size() * 8 < bitCount)
        throw RTE_LOC;
    if (b.size() * 8 < bitCount)
        throw RTE_LOC;

    if (mod8)
    {
        u8 mask = mod8 ? ((1 << mod8) - 1) : ~0;

        if (div8)
        {
            auto c1 = memcmp(a.data(), b.data(), div8);
            if (c1)
                return false;
        }

        if (mod8)
        {
            auto cc = a[div8] ^ b[div8];
            if (mask & cc)
                return false;
        }
        return true;
    }
    else
    {
        return memcmp(a.data(), b.data(), div8) == 0;
    }
}

inline auto printDiff(oc::MatrixView<u8> x, oc::MatrixView<u8> y, u64 bitCount) -> void
{
    std::vector<u8> diff(x.cols());

    std::cout << "left ~ right ^ diff " << bitCount << std::endl;
    for (u64 i = 0; i < x.rows(); ++i)
    {
        std::cout << std::setw(3) << std::setfill(' ') << i;
        if (areEqual(x[i], y[i], bitCount) == false)
            std::cout << ">";
        else
            std::cout << " ";


        for (u64 j = 0; j < diff.size(); ++j)
            diff[j] = x(i, j) ^ y(i, j);

        std::cout << hex(x[i]) << " ~ " << hex(y[i]) << " ^ " << hex(diff) << std::endl;
    }
    std::cout << std::dec;
}


void RadixSort_genBitPerm_test(const oc::CLP& cmd)
{

    auto comm = coproto::LocalAsyncSocket::makePair();

    //u64 L = 4;
    //u64 n = 40;
    u64 trials = 1;
    for (auto m : { 3, 10, 15 })
        for (auto n : { 10ull, 40ull, 1ull << cmd.getOr("nn", 10)  })
        {
            for (auto L : { 1, 3, 5 })
            {

                if (L > m)
                    continue;

                for (u64 tt = 1; tt < trials; ++tt)
                {
                    PRNG prng(block(tt, 0));
                    RadixSort s[2];
                    std::vector<AdditivePerm> p[2];

                    assert(m < 64);
                    BinMatrix k(n, m);
                    BinMatrix sk[2];
                    CorGenerator g[2];
                    g[0].init(comm[0].fork(), prng, 0, 1<<18, cmd.getOr("mock", 1));
                    g[1].init(comm[1].fork(), prng, 1, 1<<18, cmd.getOr("mock", 1));

                    //m = L;
                    auto ll = oc::divCeil(m, L);
                    std::vector<Perm> exp(ll);
                    std::vector<BinMatrix> ke(ll);
                    for (u64 i = 0; i < 2; ++i)
                    {
                        p[i].resize(ll);
                        sk[i].resize(n, m);
                    }

                    for (u64 j = 0; j < ll; ++j)
                    {
                        auto jj = ll - 1 - j;
                        auto shift = std::min<u64>(L, m - L * jj);
                        auto mask = ((1ull << shift) - 1);

                        ke[jj].resize(n, 1);
                        std::vector<std::vector<u32>> vals(1 << L);
                        for (u64 i = 0; i < k.rows(); ++i)
                        {
                            assert(m <= 64);
                            u64 v = prng.get<u64>() & mask;

                            ke[jj](i) = v;

                            u64 kk = 0;
                            memcpy(&kk, k[i].data(), k[i].size());

                            kk = kk << shift | v;

                            memcpy(k[i].data(), &kk, k[i].size());

                            vals[v].push_back(i);
                        }
                        for (u64 i = 0; i < vals.size(); ++i)
                            exp[jj].mPi.insert(exp[jj].mPi.end(), vals[i].begin(), vals[i].end());

                        exp[jj] = exp[jj].inverse();
                    }

                    share(k, sk[0], sk[1], prng);


                    for (u64 j = 0; j < ll; ++j)
                    {
                        BinMatrix kk[2];
                        for (u64 i = 0; i < 2; ++i)
                            kk[i] = s[i].extract(j * L, L, sk[i]);
                        auto kka = reveal(kk[0], kk[1]);
                        auto d0 = kka.data();
                        auto d1 = ke[j].data();
                        if (kka.size() != ke[j].size() ||
                            kka.rows() != ke[j].rows() ||
                            memcmp(d0, d1, kka.size()))
                        {
                            printDiff(kka, ke[j], L);
                            printDiff(k, k, m);
                            throw RTE_LOC;
                        }

                        macoro::sync_wait(macoro::when_all_ready(
                            s[0].genBitPerm(s[0].mRounds[j], L, kk[0], p[0][j], comm[0]),
                            s[1].genBitPerm(s[1].mRounds[j], L, kk[1], p[1][j], comm[1])
                        ));

                        auto act = reveal(p[0][j], p[1][j]);

                        if (exp[j] != act)
                        {
                            std::cout << j << " " << ll << std::endl;
                            std::cout << "\n" << exp[j] << "\n" << act << std::endl;
                            throw RTE_LOC;
                        }
                    }

                    //std::cout << L << " passed" << std::endl;
                }
            }
        }

}
void printStatus(coproto::LocalAsyncSocket& s0)
{

    auto name = [](coproto::internal::SockScheduler::Slot& s) -> std::string
    {
        return {};

        //if (s.mName.size())
        //    return  s.mName;

        //std::stringstream ss;
        //ss << s.mSessionID;
        //return ss.str();
    };

    std::cout << "send buffers: " << std::endl;
    for (auto& b : s0.mImpl->mSendBuffers_)
    {
        std::cout << "name: " << name(*b) << " local: " << b->mLocalId << " remote: " << (i32)b->mRemoteId << " size: " << b->mSendOps2_.begin()->mSendBuff.asSpan().size() << std::endl;
    }


    if (s0.mImpl->mRecvStatus == coproto::internal::SockScheduler::Status::InUse ||
        s0.mImpl->mRecvStatus == coproto::internal::SockScheduler::Status::RequestedRecvOp)
    {
        if (s0.mImpl->mHaveRecvHeader)
        {
            std::cout << "recving on remote: " << s0.mImpl->mRecvHeader[1];
            auto iter = s0.mImpl->mRemoteSlotMapping_.find(s0.mImpl->mRecvHeader[1]);
            if (iter != s0.mImpl->mRemoteSlotMapping_.end())
            {
                std::cout << " name:"<< name(*iter->second);
            }
            std::cout << std::endl;
        }
        else
        {
            std::cout << "recving header " << std::endl;
        }
    }
    else if (s0.mImpl->mRecvStatus == coproto::internal::SockScheduler::Status::Idle)
    {
        std::cout << "recv idea " << std::endl;
    }
    else
        std::cout << "recv closed " << std::endl;



    std::cout << "slots:" << std::endl;
    for (auto& s : s0.mImpl->mSlots_)
    {
        std::cout << "name: " << name(s) << " local: " << s.mLocalId << " remote: " << (i32)s.mRemoteId << std::endl;
        std::cout << "    " << s.mRecvOps2_.size() << " recvs" << std::endl;
        std::cout << "    " << s.mSendOps2_.size() << " sends" << std::endl;
    }
}

void printStatus(coproto::LocalAsyncSocket& s0, coproto::LocalAsyncSocket& s1)
{

    std::cout << "S0\n-----------------------" << std::endl;
    printStatus(s0);
    std::cout << "S1\n-----------------------" << std::endl;
    printStatus(s1);
    //for (auto& b : s0.mImpl->m)
    //{
    //    std::cout << "local: " << b->mLocalId << " remote: " << b->mRemoteId << " size: " << b->mSendOps2_.begin()->mSendBuff.asSpan().size() << std::endl;
    //}

}



void RadixSort_genPerm_test(const oc::CLP& cmd)
{
    auto comm = coproto::LocalAsyncSocket::makePair();

    u64 trials = 1;

    for (auto n : { 6ull, 1ull<<cmd.getOr("nn", 10) })
    {
        for (auto bitCount : { 9,17 })
        {
            for (auto L : { 2 })
            {
                for (u64 tt = 0; tt < trials; ++tt)
                {
                    CorGenerator g0, g1;

                    PRNG prng(block(0, 0));
                    g0.init(comm[0].fork(), prng, 0, 1 << 18, cmd.getOr("mock", 1));
                    g1.init(comm[1].fork(), prng, 1, 1 << 18, cmd.getOr("mock", 1));

                    g0.mGenState->mDebug = cmd.isSet("debug");
                    g1.mGenState->mDebug = cmd.isSet("debug");

                    RadixSort s0, s1;
                    s0.mL = L;
                    s1.mL = L;

                    //s0.mDebug = true;
                    //s1.mDebug = true;

                    oc::Timer timer;
                    s0.setTimer(timer);
                    s1.setTimer(timer);

                    AdditivePerm p0, p1;
                    Perm exp(n);

                    std::vector<u64> k64(n);
                    BinMatrix k(n, bitCount);
                    BinMatrix k0, k1;

                    auto mask = ((1ull << bitCount) - 1);
                    for (u64 i = 0; i < k.rows(); ++i)
                    {
                        assert(bitCount < 64);
                        u64 v = prng.get<u64>() & mask;
                        k64[i] = v;
                        memcpy(k[i].data(), &v, k[i].size());
                    }

                    std::stable_sort(exp.begin(), exp.end(),
                        [&](const auto& a, const auto& b) {
                            return (k64[a] < k64[b]);
                        });
                    exp = exp.inverse();

                    share(k, k0, k1, prng);

                    s0.init(0, n, bitCount);
                    s1.init(1, n, bitCount);
                    s0.request(g0);
                    s1.request(g1);
                    //auto t = std::thread([&]() {
                    //    std::this_thread::sleep_for(std::chrono::milliseconds(10000));
                    //    printStatus(comm[0], comm[1]);
                    //    });
                    macoro::sync_wait(macoro::when_all_ready(
                        s0.preprocess(comm[0], prng),
                        s1.preprocess(comm[1], prng)
                    ));
                    macoro::sync_wait(macoro::when_all_ready(
                        s0.genPerm(k0, p0, comm[0], prng),
                        s1.genPerm(k1, p1, comm[1], prng)
                    ));

                    auto act = reveal(p0, p1);

                    //t.join();
                    if (exp != act)
                    {
                        std::cout << "n " << n << " b " << bitCount << " L " << L << std::endl;
                        std::cout << "\n" << exp << "\n" << act << std::endl;
                        throw RTE_LOC;
                    }

                }
            }
        }
    }

}


void RadixSort_mock_test(const oc::CLP& cmd)
{
    auto comm = coproto::LocalAsyncSocket::makePair();

    u64 trials = 1;

    for (auto n : { 6,100 })
    {
        for (auto bitCount : { 8,17 })
        {
            for (u64 tt = 0; tt < trials; ++tt)
            {
                PRNG prng(block(0, 0));
                CorGenerator g0, g1;
                g0.init(comm[0].fork(), prng, 0, 1<<18, cmd.getOr("mock", 1));
                g1.init(comm[1].fork(), prng, 1, 1<<18, cmd.getOr("mock", 1));

                RadixSort s0, s1;
                s0.mInsecureMock = true;
                s1.mInsecureMock = true;
                //s0.mDebug = true;
                //s1.mDebug = true;

                oc::Timer timer;
                s0.setTimer(timer);
                s1.setTimer(timer);

                AdditivePerm p0, p1;
                Perm exp(n);

                std::vector<u64> k64(n);
                BinMatrix k(n, bitCount);
                BinMatrix k0, k1;

                auto mask = ((1ull << bitCount) - 1);
                for (u64 i = 0; i < k.rows(); ++i)
                {
                    assert(bitCount < 64);
                    u64 v = prng.get<u64>() & mask;
                    k64[i] = v;
                    memcpy(k[i].data(), &v, k[i].size());
                }

                std::stable_sort(exp.begin(), exp.end(),
                    [&](const auto& a, const auto& b) {
                        return (k64[a] < k64[b]);
                    });
                exp = exp.inverse();

                share(k, k0, k1, prng);

                s0.init(0, n, bitCount);
                s1.init(1, n, bitCount);
                s0.request(g0);
                s1.request(g1);

                macoro::sync_wait(macoro::when_all_ready(
                    s0.preprocess(comm[0], prng),
                    s1.preprocess(comm[1], prng)
                ));
                macoro::sync_wait(macoro::when_all_ready(
                    s0.genPerm(k0, p0, comm[0], prng),
                    s1.genPerm(k1, p1, comm[1], prng)
                ));

                auto act = reveal(p0, p1);


                //std::cout << timer << std::endl;
                if (exp != act)
                {
                    std::cout << "n " << n << " b " << bitCount << std::endl;
                    std::cout << "\nexp : " << exp << "\nact : " << act << std::endl;

                    auto e = exp.inverse();
                    auto a = act.inverse();

                    std::cout << "exp" << std::endl;
                    for (u64 i = 0; i < k.rows(); ++i)
                        std::cout << i << ": " << hex(k[e[i]]) << std::endl;
                    std::cout << "out" << std::endl;
                    for (u64 i = 0; i < k.rows(); ++i)
                        std::cout << i << ": " << hex(k[a[i]]) << std::endl;
                    throw RTE_LOC;
                }

            }
        }
    }

}
