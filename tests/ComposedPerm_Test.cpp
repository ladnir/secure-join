#include "secure-join/Perm/ComposedPerm.h"
#include "ComposedPerm_Test.h"
#include "secure-join/Util/Util.h"
using namespace secJoin;

void plaintext_perm_test(const oc::CLP& cmd)
{
    PRNG prng(oc::ZeroBlock);
    u64 n = 100;
    Perm p0(n, prng), p1(n, prng);

    auto p10 = p1.compose(p0);

    std::vector<u64> v(n);
    prng.get(v.data(), v.size());

    auto p0v = p0.apply(v);
    auto p1p0v = p1.apply(p0v);

    auto p10v = p10.apply(v);

    if (p10v != p1p0v)
        throw RTE_LOC;
}

// This is the insecure perm test
void ComposedPerm_basic_test(const oc::CLP& cmd)
{


    u64 n = 5;    // total number of rows
    u64 rowSize = 1;

    oc::Matrix<u8> x(n, rowSize);
    
    PRNG prng(oc::block(0,0));


    auto chls = coproto::LocalAsyncSocket::makePair();
    CorGenerator ole0, ole1;
    ole0.init(chls[0].fork(), prng, 0, 1<<18, cmd.getOr("mock", 1));
    ole1.init(chls[1].fork(), prng, 1, 1<<18, cmd.getOr("mock", 1));
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
    //perm1.mIsSecure = false;
    //perm2.mIsSecure = false;

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

        perm1.setBytePerRow(xShares[0].cols());
        perm2.setBytePerRow(xShares[0].cols());

        perm1.request(ole0);
        perm2.request(ole1);

        auto proto0 = perm1.apply<u8>(invPerm, xShares[0], sout[0], chls[0], prng);
        auto proto1 = perm2.apply<u8>(invPerm, xShares[1], sout[1], chls[1], prng);

        auto res = macoro::sync_wait(macoro::when_all_ready(std::move(proto0), std::move(proto1)));
        std::get<0>(res).result();
        std::get<1>(res).result();

        yAct = reveal(sout[0], sout[1]);
        if(eq(yAct,yExp) == false)
            throw RTE_LOC;
    }

}

// this is the secure replicated perm test
void ComposedPerm_shared_test(const oc::CLP& cmd)
{
    // u64 n = cmd.getOr("n", 1000);
    // u64 rowSize = cmd.getOr("m",63);

    u64 n = 1000;    // total number of rows
    u64 rowSize = 63;

    oc::Matrix<u8> x(n, rowSize), 
        yExp(n,rowSize),
        t(n, rowSize), 
        yAct(n, rowSize);

    PRNG prng0(oc::ZeroBlock);
    PRNG prng1(oc::OneBlock);

    prng0.get(x.data(), x.size());

    auto chls = coproto::LocalAsyncSocket::makePair();
     // Fake Setup
    CorGenerator ole0, ole1;
    ole0.init(chls[0].fork(), prng0, 0, 1<<18, cmd.getOr("mock", 1));
    ole1.init(chls[1].fork(), prng1, 1, 1<<18, cmd.getOr("mock", 1));

    std::array<oc::Matrix<u8>, 2> sout;
    std::array<oc::Matrix<u8>, 2> xShares = share(x,prng0);

    Perm p0(n, prng0);
    Perm p1(n, prng1);
    Perm pi = p0.composeSwap(p1);


    ComposedPerm perm1(p0, 0); 
    ComposedPerm perm2(p1, 1);


    // Setuping up the OT Keys
    AltModPrf::KeyType kk = prng0.get();
    std::vector<oc::block> rk(AltModPrf::KeySize);
    std::vector<std::array<oc::block, 2>> sk(AltModPrf::KeySize);
    for (u64 i = 0; i < AltModPrf::KeySize; ++i)
    {
        sk[i][0] = oc::block(i, 0);
        sk[i][1] = oc::block(i, 1);
        rk[i] = oc::block(i, *oc::BitIterator((u8*)&kk, i));
    }
    perm2.setKeyOts(kk,rk, sk);
    perm1.setKeyOts(kk, rk, sk);

    for(auto invPerm : { PermOp::Regular,PermOp::Inverse })
    {

        perm1.setBytePerRow(rowSize);
        perm2.setBytePerRow(rowSize);
        
        perm1.request(ole0);
        perm2.request(ole1);

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
            perm1.apply<u8>(invPerm, xShares[0], sout[0], chls[0], prng0),
            perm2.apply<u8>(invPerm, xShares[1], sout[1], chls[1], prng1)
            ));
        std::get<1>(res).result();
        std::get<0>(res).result();
        

        yAct = reveal(sout[0], sout[1]);

        if(eq(yAct,yExp) == false)
            throw RTE_LOC;
    }

}

void ComposedPerm_prepro_test(const oc::CLP& cmd)
{

    // u64 n = cmd.getOr("n", 1000);
    // u64 rowSize = cmd.getOr("m",63);

    u64 n = 1000;    // total number of rows
    u64 rowSize = 63;

    oc::Matrix<u8> x(n, rowSize),
        yExp(n, rowSize),
        t(n, rowSize),
        yAct(n, rowSize);

    PRNG prng0(oc::ZeroBlock);
    PRNG prng1(oc::OneBlock);

    prng0.get(x.data(), x.size());

    auto chls = coproto::LocalAsyncSocket::makePair();
    // Fake Setup
    CorGenerator ole0, ole1;
    ole0.init(chls[0].fork(), prng0, 0, 1<<18, cmd.getOr("mock", 1));
    ole1.init(chls[1].fork(), prng1, 1, 1<<18, cmd.getOr("mock", 1));

    std::array<oc::Matrix<u8>, 2> sout;
    std::array<oc::Matrix<u8>, 2> xShares = share(x, prng0);

    Perm p0(n, prng0);
    Perm p1(n, prng1);
    Perm pi = p0.composeSwap(p1);


    ComposedPerm perm1(p0, 0, rowSize);
    ComposedPerm perm2(p1, 1, rowSize);


    // Setuping up the OT Keys
    AltModPrf::KeyType kk = prng0.get();
    std::vector<oc::block> rk(AltModPrf::KeySize);
    std::vector<std::array<oc::block, 2>> sk(AltModPrf::KeySize);
    for (u64 i = 0; i < AltModPrf::KeySize; ++i)
    {
        sk[i][0] = oc::block(i, 0);
        sk[i][1] = oc::block(i, 1);
        rk[i] = oc::block(i, *oc::BitIterator((u8*)&kk, i));
    }
    perm2.setKeyOts(kk, rk, sk);
    perm1.setKeyOts(kk, rk, sk);

    for (auto invPerm : { PermOp::Regular,PermOp::Inverse })
    {
        perm1.request(ole0);
        perm2.request(ole1);

        auto res0 = macoro::sync_wait(macoro::when_all_ready(
            perm1.setup(chls[0], prng0),
            perm2.setup(chls[1], prng1)
        ));
        std::get<1>(res0).result();
        std::get<0>(res0).result();


        pi.apply<u8>(x, yExp, invPerm);

        sout[0].resize(n, rowSize);
        sout[1].resize(n, rowSize);

        auto res = macoro::sync_wait(macoro::when_all_ready(
            perm1.apply<u8>(invPerm, xShares[0], sout[0], chls[0], prng0),
            perm2.apply<u8>(invPerm, xShares[1], sout[1], chls[1], prng1)
        ));
        std::get<1>(res).result();
        std::get<0>(res).result();


        yAct = reveal(sout[0], sout[1]);

        if (eq(yAct, yExp) == false)
            throw RTE_LOC;
    }
}
