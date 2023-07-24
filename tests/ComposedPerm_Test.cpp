#include "secure-join/Perm/ComposedPerm.h"
#include "ComposedPerm_Test.h"
using namespace secJoin;
#include "secure-join/Util/Util.h"

// This is the insecure perm test
void ComposedPerm_basic_test()
{


    u64 n = 5;    // total number of rows
    u64 rowSize = 1;

    oc::Matrix<u8> x(n, rowSize);
    
    oc::PRNG prng(oc::block(0,0));


    auto chls = coproto::LocalAsyncSocket::makePair();
    OleGenerator ole0, ole1;
    ole0.fakeInit(OleGenerator::Role::Sender);
    ole1.fakeInit(OleGenerator::Role::Receiver);
    // std::vector<u64> pi0(n), pi1(n);
    prng.get(x.data(), x.size());


    std::array<oc::Matrix<u8>, 2> sout;
    std::array<oc::Matrix<u8>, 2> xShares = share(x,prng);

    Perm p0(n, prng);
    Perm p1(n, prng);
    Perm pi = p0.composeSwap(p1);
    oc::Matrix<u8> t(n, rowSize),yExp(n, rowSize), yAct(n, rowSize);

    ComposedPerm perm1(p0, 0); 
    ComposedPerm perm2(p1, 1);
    perm1.mIsSecure = false;
    perm2.mIsSecure = false;

    for(auto invPerm : { PermOp::Regular,PermOp::Inverse })
    {
    
        if(invPerm == PermOp::Inverse)
        {
            p1.apply<u8>(x, t, invPerm);
            p0.apply<u8>(t, yAct, invPerm);
        }
        else
        {
            p0.apply<u8>(x, t, invPerm);
            p1.apply<u8>(t, yAct, invPerm);
        }

        pi.apply<u8>(x, yExp, invPerm);
        if(eq(yAct,yExp) == false)
            throw RTE_LOC;

        sout[0].resize(n, rowSize);
        sout[1].resize(n, rowSize);

        auto proto0 = perm1.apply<u8>(invPerm, xShares[0], sout[0], chls[0], ole0);
        auto proto1 = perm2.apply<u8>(invPerm, xShares[1], sout[1], chls[1], ole1);

        auto res = macoro::sync_wait(macoro::when_all_ready(std::move(proto0), std::move(proto1)));
        std::get<0>(res).result();
        std::get<1>(res).result();

        yAct = reveal(sout[0], sout[1]);
        if(eq(yAct,yExp) == false)
            throw RTE_LOC;
    }

}

// this is the secure replicated perm test
void ComposedPerm_shared_test()
{
    // u64 n = cmd.getOr("n", 1000);
    // u64 rowSize = cmd.getOr("m",63);

    u64 n = 1000;    // total number of rows
    u64 rowSize = 63;

    oc::Matrix<u8> x(n, rowSize), 
        yExp(n,rowSize),
        t(n, rowSize), 
        yAct(n, rowSize);

    oc::PRNG prng0(oc::ZeroBlock);
    oc::PRNG prng1(oc::OneBlock);

    prng0.get(x.data(), x.size());

    auto chls = coproto::LocalAsyncSocket::makePair();
     // Fake Setup
    OleGenerator ole0, ole1;
    // macoro::thread_pool tp;
    ole0.fakeInit(OleGenerator::Role::Sender);
    ole1.fakeInit(OleGenerator::Role::Receiver);

    std::array<oc::Matrix<u8>, 2> sout;
    std::array<oc::Matrix<u8>, 2> xShares = share(x,prng0);

    Perm p0(n, prng0);
    Perm p1(n, prng1);
    Perm pi = p0.composeSwap(p1);


    ComposedPerm perm1(p0, 0); 
    ComposedPerm perm2(p1, 1);


    // Setuping up the OT Keys
    oc::block kk = prng0.get();
    std::vector<oc::block> rk(128);
    std::vector<std::array<oc::block, 2>> sk(128);
    for (u64 i = 0; i < 128; ++i)
    {
        sk[i][0] = oc::block(i, 0);
        sk[i][1] = oc::block(i, 1);
        rk[i] = oc::block(i, *oc::BitIterator((u8*)&kk, i));
    }
    perm2.setKeyOts(kk,rk, sk);
    perm1.setKeyOts(kk, rk, sk);

    for(auto invPerm : { PermOp::Regular,PermOp::Inverse })
    {
    
        if(invPerm == PermOp::Inverse)
        {
            p1.apply<u8>(x, t, invPerm);
            p0.apply<u8>(t, yAct, invPerm);
        }
        else
        {
            p0.apply<u8>(x, t, invPerm);
            p1.apply<u8>(t, yAct, invPerm);
        }

        pi.apply<u8>(x, yExp, invPerm);
        if(eq(yAct,yExp) == false)
            throw RTE_LOC;

        sout[0].resize(n, rowSize);
        sout[1].resize(n, rowSize);

        auto res = macoro::sync_wait(macoro::when_all_ready(
            perm1.apply<u8>(invPerm, xShares[0], sout[0], chls[0], ole0),
            perm2.apply<u8>(invPerm, xShares[1], sout[1], chls[1], ole1)
            ));
        std::get<1>(res).result();
        std::get<0>(res).result();
        

        yAct = reveal(sout[0], sout[1]);

        if(eq(yAct,yExp) == false)
            throw RTE_LOC;
    }

}

void ComposedPerm_prepro_test()
{

    // u64 n = cmd.getOr("n", 1000);
    // u64 rowSize = cmd.getOr("m",63);

    u64 n = 1000;    // total number of rows
    u64 rowSize = 63;

    oc::Matrix<u8> x(n, rowSize),
        yExp(n, rowSize),
        t(n, rowSize),
        yAct(n, rowSize);

    oc::PRNG prng0(oc::ZeroBlock);
    oc::PRNG prng1(oc::OneBlock);

    prng0.get(x.data(), x.size());

    auto chls = coproto::LocalAsyncSocket::makePair();
    // Fake Setup
    OleGenerator ole0, ole1;
    // macoro::thread_pool tp;
    ole0.fakeInit(OleGenerator::Role::Sender);
    ole1.fakeInit(OleGenerator::Role::Receiver);

    std::array<oc::Matrix<u8>, 2> sout;
    std::array<oc::Matrix<u8>, 2> xShares = share(x, prng0);

    Perm p0(n, prng0);
    Perm p1(n, prng1);
    Perm pi = p0.composeSwap(p1);


    ComposedPerm perm1(p0, 0);
    ComposedPerm perm2(p1, 1);


    // Setuping up the OT Keys
    oc::block kk = prng0.get();
    std::vector<oc::block> rk(128);
    std::vector<std::array<oc::block, 2>> sk(128);
    for (u64 i = 0; i < 128; ++i)
    {
        sk[i][0] = oc::block(i, 0);
        sk[i][1] = oc::block(i, 1);
        rk[i] = oc::block(i, *oc::BitIterator((u8*)&kk, i));
    }
    perm2.setKeyOts(kk, rk, sk);
    perm1.setKeyOts(kk, rk, sk);

    for (auto invPerm : { PermOp::Regular,PermOp::Inverse })
    {
        auto res0 = macoro::sync_wait(macoro::when_all_ready(
            perm1.preprocess(n, rowSize, chls[0], ole0, prng0),
            perm2.preprocess(n, rowSize, chls[1], ole1, prng1)
        ));
        std::get<1>(res0).result();
        std::get<0>(res0).result();


        pi.apply<u8>(x, yExp, invPerm);

        sout[0].resize(n, rowSize);
        sout[1].resize(n, rowSize);

        auto res = macoro::sync_wait(macoro::when_all_ready(
            perm1.apply<u8>(invPerm, xShares[0], sout[0], chls[0], ole0),
            perm2.apply<u8>(invPerm, xShares[1], sout[1], chls[1], ole1)
        ));
        std::get<1>(res).result();
        std::get<0>(res).result();


        yAct = reveal(sout[0], sout[1]);

        if (eq(yAct, yExp) == false)
            throw RTE_LOC;
    }
}
