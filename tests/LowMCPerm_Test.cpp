#include "LowMCPerm_Test.h"
#include "secure-join/Perm/LowMCPerm.h"
#include "secure-join/Util/Util.h"
#include "cryptoTools/Common/TestCollection.h"

using namespace secJoin;

void LowMC_eval_test(const oc::CLP& cmd)
{
    u64 n = cmd.getOr("n", 100);

    oc::Matrix<u8> x(n, 16);// , x2Perm(n, rowSize);

    //auto cir = LowMCPerm::mLowMcCir();
    static oc::BetaCircuit cir;
    if (cir.mGates.size() == 0)
    {

        LowMC2<10,128,128,20>(false).to_enc_circuit(cir, true);
        cir.levelByAndDepth();
    }

    std::vector<oc::Matrix<u8>> k(cir.mInputs.size()-2);// (n, LowMC2<>::getBlockSize());// , x2Perm(n, rowSize);

    for (u64 i = 0; i < k.size(); ++i)
    {
        k[i].resize(n, 16);
    }

    PRNG prng0(oc::block(0, 0));
    PRNG prng1(oc::block(0, 1));

    auto chls = coproto::LocalAsyncSocket::makePair();
    CorGenerator ole0, ole1;
    ole0.init(chls[0].fork(), prng0, 0, 1<<18, cmd.getOr("mock", 1));
    ole1.init(chls[1].fork(), prng1, 1, 1<<18, cmd.getOr("mock", 1));
    Gmw gmw0, gmw1;
    gmw0.init(n, cir, ole0);
    gmw1.init(n, cir, ole1);

    gmw0.setInput(0, x);
    gmw0.setInput(1, x);
    gmw1.setZeroInput(0);
    gmw1.setZeroInput(1);

    for (u64 i = 0; i < k.size(); ++i)
    {
        gmw0.setInput(i + 2, k[i]);
        gmw1.setZeroInput(i + 2);
    }

    auto proto0 = gmw0.run(chls[0]);
    auto proto1 = gmw1.run(chls[1]);

    auto res = macoro::sync_wait(macoro::when_all_ready(
        std::move(proto0), 
        std::move(proto1),
        ole0.start(), ole1.start()
    ));

    std::get<0>(res).result();
    std::get<1>(res).result();
    if (cmd.isSet("v"))
    {
        std::cout << chls[0].bytesReceived() / 1000.0 << " " << chls[0].bytesSent() / 1000.0 << " kB " << std::endl;
    }
}

void LowMCPerm_perm_test(const oc::CLP& cmd)
{
    // User input
    u64 n = 10;    // total number of rows
    u64 rowSize = cmd.getOr("m",63);      

    oc::Matrix<u8> x(n, rowSize), yExp(n,rowSize);

    LowMCPerm m1, m2;
    PRNG prng(oc::block(0, 0));

    auto chls = coproto::LocalAsyncSocket::makePair();
    CorGenerator ole0, ole1;


    // Initializing the vector x & permutation pi
    prng.get(x.data(), x.size());
    Perm pi(n,prng);

    for(auto invPerm : {PermOp::Regular,PermOp::Inverse})
    {

        ole0.init(chls[0].fork(), prng, 0, 1 << 18, cmd.getOr("mock", 1));
        ole1.init(chls[1].fork(), prng, 1, 1 << 18, cmd.getOr("mock", 1));
        pi.apply<u8>(x, yExp, invPerm);

        std::array<oc::Matrix<u8>, 2> sout;
        sout[0].resize(n, rowSize);
        sout[1].resize(n, rowSize);

        throw oc::UnitTestSkipped("needs updating for new CorGen");
        //m1.init(n, rowSize, ole0);
        //m2.init(n, rowSize, ole1);

        auto proto0 = m1.apply<u8>(x, sout[0], prng, chls[0], ole0);
        auto proto1 = m2.apply<u8>(pi, invPerm, sout[1], prng, chls[1], ole1);

        auto res = macoro::sync_wait(macoro::when_all_ready(
            std::move(proto0), std::move(proto1),
            ole0.start(), ole1.start()));

        std::get<0>(res).result();
        std::get<1>(res).result();

        auto yAct = reveal(sout[0], sout[1]);
        if(eq(yExp, yAct) == false)
            throw RTE_LOC;
    }
}

void LowMCPerm_secret_shared_input_perm_test(const oc::CLP& cmd)
{
    // User input
    u64 n = 144;    // total number of rows
    u64 rowSize = 66;
    

    oc::Matrix<u8> x(n, rowSize), yExp(n,rowSize);

    LowMCPerm m1, m2;
    PRNG prng(oc::block(0,0));

    auto chls = coproto::LocalAsyncSocket::makePair();

    Perm pi(n,prng);

    CorGenerator ole0, ole1;
    
    // Initializing the vector x & permutation pi
    prng.get(x.data(), x.size());
    std::array<oc::Matrix<u8>, 2> xShares = share(x,prng);


    for(auto invPerm : { PermOp::Regular,PermOp::Inverse })
    {
        pi.apply<u8>(x, yExp, invPerm);
        std::array<oc::Matrix<u8>, 2> sout;
        sout[0].resize(n, rowSize);
        sout[1].resize(n, rowSize);

        ole0.init(chls[0].fork(), prng, 0, 1 << 18, cmd.getOr("mock", 1));
        ole1.init(chls[1].fork(), prng, 1, 1 << 18, cmd.getOr("mock", 1));
        throw oc::UnitTestSkipped("needs updating for new CorGen");
        //m1.init(n, rowSize, ole0);
        //m2.init(n, rowSize, ole1);
        auto proto0 = m1.apply<u8>(xShares[0], sout[0], prng, chls[0], ole0);
        auto proto1 = m2.apply<u8>(pi,invPerm, xShares[1], sout[1], prng, chls[1], ole1);

        auto res = macoro::sync_wait(macoro::when_all_ready(
            std::move(proto0), std::move(proto1),
            ole0.start(), ole1.start()));

        std::get<0>(res).result();
        std::get<1>(res).result();

        auto yAct = reveal(sout[0], sout[1]);
        if(!eq(yAct, yExp))
            throw RTE_LOC;
    }
}
