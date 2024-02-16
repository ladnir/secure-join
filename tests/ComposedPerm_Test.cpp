#include "secure-join/Perm/ComposedPerm.h"
#include "ComposedPerm_Test.h"
#include "secure-join/Util/Util.h"
#include "secure-join/Perm/AltModComposedPerm.h"
#include "cryptoTools/Crypto/PRNG.h"
#include "secure-join/Join/Table.h"

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
void ComposedPerm_apply_test(const oc::CLP& cmd)
{
    u64 n = 511;    // total number of rows
    u64 rowSize = 111;

    oc::Matrix<u8> x(n, rowSize);
    PRNG prng(oc::block(0, 0));
    auto chls = coproto::LocalAsyncSocket::makePair();
    prng.get(x.data(), x.size());


    std::array<oc::Matrix<u8>, 2> sout;
    std::array<oc::Matrix<u8>, 2> xShares = share(x, prng);

    Perm pi(n, prng);

    oc::Matrix<u8> t(n, rowSize), yExp(n, rowSize), yAct(n, rowSize);

    ComposedPerm perm0;
    ComposedPerm perm1;


    for (auto invPerm : { PermOp::Regular,PermOp::Inverse })
    {
        genPerm(pi, perm0, perm1, rowSize, prng);

        if (invPerm == PermOp::Inverse)
        {
            perm1.permShare().apply<u8>(x, t, invPerm);
            perm0.permShare().apply<u8>(t, yAct, invPerm);
        }
        else
        {
            perm0.permShare().apply<u8>(x, t, invPerm);
            perm1.permShare().apply<u8>(t, yAct, invPerm);
        }

        pi.apply<u8>(x, yExp, invPerm);
        if (eq(yAct, yExp) == false)
            throw RTE_LOC;

        sout[0].resize(n, rowSize);
        sout[1].resize(n, rowSize);

        auto proto0 = perm0.apply<u8>(invPerm, xShares[0], sout[0], chls[0]);
        auto proto1 = perm1.apply<u8>(invPerm, xShares[1], sout[1], chls[1]);

        auto res = macoro::sync_wait(macoro::when_all_ready(std::move(proto0), std::move(proto1)));
        std::get<0>(res).result();
        std::get<1>(res).result();

        yAct = reveal(sout[0], sout[1]);
        if (eq(yAct, yExp) == false)
            throw RTE_LOC;
    }

}

void ComposedPerm_compose_test(const oc::CLP& cmd)
{
    // User input
    u64 n = 5000;

    PRNG prng(oc::block(0, 0));
    auto chls = coproto::LocalAsyncSocket::makePair();

    Perm addPerm(n, prng);
    Perm comPerm(n, prng);

    // Secret Sharing s
    auto sShares = xorShare(addPerm.mPi, prng);
    AdditivePerm addPerm0, dstPerm0;
    AdditivePerm addPerm1, dstPerm1;
    addPerm0.mShare = sShares[0];
    addPerm1.mShare = sShares[1];

    ComposedPerm comPerm0, comPerm1;
    genPerm(comPerm, comPerm0, comPerm1, sizeof(u32), prng);

    auto r = macoro::sync_wait(macoro::when_all_ready(
        comPerm0.compose(addPerm0, dstPerm0, chls[0]),
        comPerm1.compose(addPerm1, dstPerm1, chls[1])
    ));
    std::get<0>(r).result();
    std::get<1>(r).result();

    auto dstPerm = reveal(dstPerm0, dstPerm1);

    if (dstPerm != comPerm.compose(addPerm))
        throw RTE_LOC;
}


// this is the secure replicated perm test
void ComposedPerm_derandomize_test(const oc::CLP& cmd)
{

}
