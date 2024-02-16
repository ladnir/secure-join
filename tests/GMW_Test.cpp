//
#include "GMW_Test.h"
#include "secure-join/Defines.h"
#include "secure-join/GMW/Gmw.h"
//#include "secure-join/GMW/SilentTripleGen.h"
#include "cryptoTools/Network/IOService.h"
#include "cryptoTools/Network/Session.h"
#include "cryptoTools/Circuit/BetaLibrary.h"
#include "Common.h"
#include "coproto/Socket/LocalAsyncSock.h"

#include "secure-join/Util/Util.h"
using coproto::LocalAsyncSocket;
using namespace secJoin;

using PRNG = PRNG;
namespace secJoin_Tests
{

    void makeTriple(span<block> a, span<block> b, span<block> c, span<block> d, PRNG& prng)
    {
        prng.get(a.data(), a.size());
        prng.get(b.data(), b.size());
        prng.get(c.data(), c.size());
        for (u64 i = 0; i < static_cast<u64>(a.size()); i++)
        {
            d[i] = (a[i] & c[i]) ^ b[i];
        }
    }



    void Gmw_half_test(const oc::CLP& cmd)
    {

        Gmw cmp0, cmp1;
        block seed = oc::toBlock(cmd.getOr<u64>("s", 0));
        PRNG prng(seed);

        u64 n = cmd.getOr("n", 100ull);
        u64 bc = cmd.getOr("bc", 16);
        //        u64 eqSize = cmd.getOr("e", n / 2);

        CorGenerator gen0, gen1;
        gen0.init(oc::Socket{}, prng, 0, 1 << 18, cmd.getOr("mock", 1));
        gen1.init(oc::Socket{}, prng, 1, 1 << 18, cmd.getOr("mock", 1));

        std::vector<block> input0(n), input1(n), z0(n), z1(n);

        prng.get(input0.data(), input0.size());
        prng.get(input1.data(), input1.size());

        //auto cir = isZeroCircuit(bc);
        //cmp0.init(n * 128, cir, gen0);
        //cmp1.init(n * 128, cir, gen1);
        cmp0.mN128 = n;
        cmp1.mN128 = n;

        std::vector<block> a0(n), b0(n), c0(n), d0(n);
        makeTriple(a0, b0, c0, d0, prng);

        std::vector<block> buff(n);
        auto iter = buff.data();
        iter = cmp0.multSendP1(input0.data(), oc::GateType::And, a0.data(), iter);
        if (iter != buff.data() + buff.size())
            throw RTE_LOC;
        iter = buff.data();
        iter = cmp1.multRecvP2(input1.data(), z1.data(), c0.data(), d0.data(), iter);

        if (iter != buff.data() + buff.size())
            throw RTE_LOC;
        iter = buff.data();
        iter = cmp1.multSendP2(input1.data(), oc::GateType::And, c0.data(), iter);
        if (iter != buff.data() + buff.size())
            throw RTE_LOC;
        iter = buff.data();
        iter = cmp0.multRecvP1(input0.data(), z0.data(), oc::GateType::And, b0.data(), iter);
        if (iter != buff.data() + buff.size())
            throw RTE_LOC;

        for (u64 i = 0; i < n; i++)
        {
            auto exp = (input0[i] & input1[i]);
            auto act = z0[i] ^ z1[i];
            if (neq(act, exp))
            {
                oc::lout << "exp " << exp << std::endl;
                oc::lout << "act " << act << std::endl;
                oc::lout << "dif " << (act ^ exp) << std::endl;
                throw RTE_LOC;
            }
        }
    }

    void Gmw_basic_test(const oc::CLP& cmd)
    {
        Gmw cmp0, cmp1;
        block seed = oc::toBlock(cmd.getOr<u64>("s", 0));
        PRNG prng(seed);

        u64 n = cmd.getOr("n", 100ull);


        std::array<std::vector<block>, 2> x, y;
        std::array<std::vector<block>, 2> z;
        x[0].resize(n);
        x[1].resize(n);
        y[0].resize(n);
        y[1].resize(n);
        z[0].resize(n);
        z[1].resize(n);

        for (u64 i = 0; i < 2; ++i)
        {
            prng.get(x[i].data(), n);
            prng.get(y[i].data(), n);
        }

        CorGenerator gen0, gen1;
        cmp0.mN128 = n;
        cmp1.mN128 = n;


        std::vector<block> a0(n), b0(n), c0(n), d0(n);
        std::vector<block> a1(n), b1(n), c1(n), d1(n);
        makeTriple(a0, b0, c0, d0, prng);
        makeTriple(a1, b1, c1, d1, prng);

        // a  * c  = b  + d
        //cmp0.setTriples(a0, b0, c1, d1);
        //cmp1.setTriples(a1, b1, c0, d0);

        std::vector<block> buff(n * 2);
        auto iter = buff.data();
        iter = cmp0.multSendP1(x[0].data(), y[0].data(), oc::GateType::And, a0.data(), c1.data(), iter);

        if (iter != buff.data() + buff.size())
            throw RTE_LOC;
        iter = buff.data();
        iter = cmp1.multRecvP2(x[1].data(), y[1].data(), z[1].data(), b1.data(), c0.data(), d0.data(), iter);
        if (iter != buff.data() + buff.size())
        {
            std::cout << (iter - buff.data()) << std::endl;
            throw RTE_LOC;
        }
        iter = buff.data();
        iter = cmp1.multSendP2(x[1].data(), y[1].data(), a1.data(), c0.data(), iter);
        if (iter != buff.data() + buff.size())
            throw RTE_LOC;
        iter = buff.data();
        iter = cmp0.multRecvP1(x[0].data(), y[0].data(), z[0].data(), oc::GateType::And, b0.data(), c1.data(), d1.data(), iter);
        if (iter != buff.data() + buff.size())
            throw RTE_LOC;

        for (u64 i = 0; i < n; i++)
        {
            auto exp =
                (x[0][i] ^ x[1][i]) &
                (y[0][i] ^ y[1][i]);

            auto act = z[0][i] ^ z[1][i];
            if (neq(act, exp))
            {
                oc::lout << "exp " << exp << std::endl;
                oc::lout << "act " << act << std::endl;
                oc::lout << "dif " << (act ^ exp) << std::endl;
                throw RTE_LOC;
            }
        }
    }

    void trim(std::vector<block>& v, u64 l)
    {
        if (l > 128)
            throw RTE_LOC;
        block mask = oc::ZeroBlock;
        oc::BitIterator iter((u8*)&mask, 0);
        for (u64 i = 0; i < l; i++)
            *iter++ = 1;

        //oc::lout << "mask " << l << " " << mask << std::endl;
        for (u64 i = 0; i < v.size(); i++)
        {
            v[i] = v[i] & mask;
        }
    }

    std::vector<u64> randomSubset(u64 n, u64 m, PRNG& prng)
    {
        std::vector<u64> set(n), ret(m);
        std::iota(set.begin(), set.end(), 0);

        for (u64 i = 0; i < m; i++)
        {
            auto j = prng.get<u64>() % set.size();
            ret[i] = set[j];
            std::swap(set[j], set.back());
            set.pop_back();
        }
        return ret;
    }

    using oc::Matrix;

    std::array<Matrix<u8>, 2> share(span<i64> v, PRNG& prng)
    {
        auto n = v.size();
        Matrix<u8>
            s0(n, sizeof(u64)),
            s1(n, sizeof(u64));

        prng.get(s0.data(), s0.size());

        for (u64 i = 0; i < n; ++i)
        {
            auto& i0s0 = *(i64*)s0[i].data();
            auto& i0s1 = *(i64*)s1[i].data();

            i0s1 = v[i] ^ i0s0;
        }

        return { s0, s1 };
    }


    std::array<Matrix<u8>, 2> share(Matrix<u8> v, PRNG& prng)
    {
        auto n = v.rows();
        Matrix<u8>
            s0(n, v.cols()),
            s1(n, v.cols());

        prng.get(s0.data(), s0.size());

        for (u64 i = 0; i < v.size(); ++i)
            s1(i) = v(i) ^ s0(i);

        return { s0, s1 };
    }


    template <typename T>
    std::vector<T> reconstruct(std::array<Matrix<u8>, 2> shares)
    {
        if (shares[0].cols() != sizeof(T))
            throw RTE_LOC;
        if (shares[1].cols() != sizeof(T))
            throw RTE_LOC;
        if (shares[0].rows() != shares[1].rows())
            throw RTE_LOC;

        std::vector<T> ret(shares[0].rows());
        oc::MatrixView<u8> v((u8*)ret.data(), ret.size(), sizeof(T));

        for (u64 i = 0; i < v.size(); ++i)
            v(i) = shares[0](i) ^ shares[1](i);

        return ret;
    }

    void Gmw_inOut_test(const oc::CLP& cmd)
    {
        u64 w = 64;
        u64 n = 100;
        BetaCircuit cir;
        BetaBundle io(w);

        cir.addInputBundle(io);
        cir.mOutputs.push_back(io);


        PRNG prng(block(0, 0));
        CorGenerator ole;
        ole.init(oc::Socket{}, prng, 0, 1 << 18, cmd.getOr("mock", 1));

        Gmw gmw;
        gmw.init(n, cir, ole);

        Matrix<u8> in(n, oc::divCeil(w, 8));
        Matrix<u8> out(n, oc::divCeil(w, 8));

        prng.get(in.data(), in.size());
        gmw.setInput(0, in);
        gmw.getOutput(0, out);

        if (!(in == out))
            throw RTE_LOC;
    }

    void Gmw_test(
        u64 n,
        bool mock,
        oc::BetaCircuit& cir
    )
    {

        auto chls = LocalAsyncSocket::makePair();

        PRNG prng(block(0, 0));

        CorGenerator ole0, ole1;
        ole0.init(chls[0].fork(), prng, 0, 1 << 18, mock);
        ole1.init(chls[1].fork(), prng, 1, 1 << 18, mock);

        Gmw gmw0, gmw1;
        gmw0.init(n, cir, ole0);
        gmw1.init(n, cir, ole1);

        gmw0.mO.mDebug = true;
        gmw1.mO.mDebug = true;

        std::vector<Matrix<u8>> in(cir.mInputs.size());
        std::vector<std::array<Matrix<u8>, 2>> sin(cir.mInputs.size());

        for (u64 i = 0; i < in.size(); ++i)
        {
            in[i].resize(n, oc::divCeil(cir.mInputs[i].size(), 8));
            prng.get(in[i].data(), in[i].size());
            sin[i] = share(in[i], prng);
            gmw0.setInput(i, sin[i][0]);
            gmw1.setInput(i, sin[i][1]);
        }

        std::vector<std::array<Matrix<u8>, 2>> sout(cir.mOutputs.size());
        for (u64 i = 0; i < sout.size(); ++i)
        {
            sout[i][0].resize(n, oc::divCeil(cir.mOutputs[i].size(), 8));
            sout[i][1].resize(n, oc::divCeil(cir.mOutputs[i].size(), 8));
        }

        auto p0 = gmw0.run(chls[0]);
        auto p1 = gmw1.run(chls[1]);
        eval(p0, p1, ole0, ole1);

        for (u64 i = 0; i < sout.size(); ++i)
        {
            gmw0.getOutput(i, sout[i][0]);
            gmw1.getOutput(i, sout[i][1]);
        }

        std::vector<oc::BitVector> vIn(in.size()), vOut(sout.size());
        for (u64 i = 0; i < n; ++i)
        {
            for (u64 j = 0; j < vIn.size(); ++j)
            {
                vIn[j].resize(0);
                vIn[j].append(in[j].data(i), cir.mInputs[j].size());
            }

            for (u64 j = 0; j < vOut.size(); ++j)
                vOut[j].resize(cir.mOutputs[j].size());

            cir.evaluate(vIn, vOut);

            for (u64 j = 0; j < vOut.size(); ++j)
            {

                oc::BitVector act0(sout[j][0].data(i), vOut[j].size());
                oc::BitVector act1(sout[j][1].data(i), vOut[j].size());
                auto act = act0 ^ act1;
                if (vOut[j] != act)
                {
                    std::cout << gateToString(cir.mGates[0].mType) << std::endl;
                    for (u64 k = 0; k < vIn.size(); ++k)
                        std::cout << "in"<<k<<" " << vIn[k] << std::endl;

                    std::cout << "exp " << j << " " << vOut[j] << std::endl;;
                    std::cout << "act " << j << " " << act << std::endl;
                    throw RTE_LOC;
                }
            }
        }
    }

    void Gmw_gate_test(const oc::CLP& cmd)
    {

        u64 w = cmd.getOr("w", 8);
        u64 n = cmd.getOr("n", 441);
        auto mock = cmd.getOr("mock", 1);

        for (auto gt : {
            oc::GateType::And,
            oc::GateType::na_And,
            oc::GateType::nb_And,
            oc::GateType::Or,
            //oc::GateType::Nand,
            oc::GateType::Nor,
            oc::GateType::Xor,
            oc::GateType::Nxor
            })
        {
            BetaCircuit cir;
            BetaBundle a(w);
            BetaBundle b(w);
            BetaBundle c(w);

            cir.addInputBundle(a);
            cir.addInputBundle(b);
            cir.addOutputBundle(c);

            for (u64 i = 0; i < w; ++i)
            {
                cir.addGate(a[i], b[i], gt, c[i]);
                //cir << i << " " << a[i] << " " << b[i] << " " << c[i] << " " << oc::gateToString(gt) << "\n";
            }

            Gmw_test(n, mock, cir);
        }
    }


    void Gmw_xor_and_test(const oc::CLP& cmd)
    {
        u64 w = cmd.getOr("w", 33);
        u64 n = cmd.getOr("n", 233);
        auto mock = cmd.getOr("mock", 1);

        auto sockets = LocalAsyncSocket::makePair();

        BetaCircuit cir;
        BetaBundle a(w);
        BetaBundle b(w);
        BetaBundle c(w);
        BetaBundle t0(w);
        BetaBundle z(w);

        cir.addInputBundle(a);
        cir.addInputBundle(b);
        cir.addInputBundle(c);
        cir.addTempWireBundle(t0);
        cir.addOutputBundle(z);

        for (u64 i = 0; i < w; ++i)
        {
            cir.addGate(a[i], c[i], oc::GateType::Xor, t0[i]);
            cir.addGate(t0[i], b[i], oc::GateType::And, z[i]);
        }
        Gmw_test(n, mock, cir);

    }

    void Gmw_aa_na_and_test(const oc::CLP& cmd)
    {

        u64 w = cmd.getOr("w", 40);
        u64 n = cmd.getOr("n", 128);
        auto mock = cmd.getOr("mock", 1);

        auto sockets = LocalAsyncSocket::makePair();

        BetaCircuit cir;

        BetaBundle a(w);
        BetaBundle b(w);
        BetaBundle t0(w);
        BetaBundle t1(w);
        BetaBundle z(w);

        cir.addInputBundle(a);
        cir.addInputBundle(b);
        cir.addTempWireBundle(t0);
        cir.addTempWireBundle(t1);
        cir.addOutputBundle(z);

        for (u64 i = 0; i < w; ++i)
        {
            cir.addCopy(a[i], t0[i]);
            cir.addCopy(t0[i], t1[i]);
            cir.addGate(t0[i], b[i], oc::GateType::na_And, z[i]);
        }

        Gmw_test(n, mock, cir);
    }


    void Gmw_add_test(const oc::CLP& cmd)
    {
        oc::BetaLibrary lib;

        u64 n = 133;
        u64 w = 31;
        auto mock = cmd.getOr("mock", 1);

        auto cir = *lib.uint_uint_add(w, w, w, oc::BetaLibrary::Optimized::Depth);
        Gmw_test(n, mock, cir);
    }

    void Gmw_noLevelize_test(const oc::CLP& cmd)
    {
        oc::BetaLibrary lib;

        auto cir = *lib.int_int_add(64, 64, 64);

        auto sockets = LocalAsyncSocket::makePair();

        Gmw gmw0, gmw1;
        block seed = oc::toBlock(cmd.getOr<u64>("s", 0));
        PRNG prng(seed);

        u64 n = 100;

        std::vector<i64> in0(n), in1(n), out(n);
        std::array<Matrix<u8>, 2> sout;
        sout[0].resize(n, sizeof(i64));
        sout[1].resize(n, sizeof(i64));

        prng.get(in0.data(), n);
        prng.get(in1.data(), n);
        auto sin0 = share(in0, prng);
        auto sin1 = share(in1, prng);

        gmw0.mLevelize = BetaCircuit::LevelizeType::NoReorder;
        gmw1.mLevelize = BetaCircuit::LevelizeType::NoReorder;


        CorGenerator ole0, ole1;
        ole0.init(sockets[0].fork(), prng, 0, 1 << 18, cmd.getOr("mock", 1));
        ole1.init(sockets[1].fork(), prng, 1, 1 << 18, cmd.getOr("mock", 1));

        gmw0.init(n, cir, ole0);
        gmw1.init(n, cir, ole1);
        gmw0.mO.mDebug = true;
        gmw1.mO.mDebug = true;


        gmw0.setInput(0, sin0[0]);
        gmw0.setInput(1, sin1[0]);
        gmw1.setInput(0, sin0[1]);
        gmw1.setInput(1, sin1[1]);

        auto p0 = gmw0.run(sockets[0]);
        auto p1 = gmw1.run(sockets[1]);
        eval(p0, p1, ole0, ole1);

        gmw0.getOutput(0, sout[0]);
        gmw1.getOutput(0, sout[1]);

        out = reconstruct<i64>(sout);

        for (u64 i = 0; i < n; ++i)
        {
            auto exp = (in0[i] + in1[i]);
            auto act = out[i];
            if (act != exp)
            {
                std::cout << "i   " << i << std::endl;
                std::cout << "exp " << exp << std::endl;
                std::cout << "act " << act << std::endl;
                throw RTE_LOC;
            }
        }

    }

}
