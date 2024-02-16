
#include "PermCor_Test.h"
#include "secure-join/Perm/PermCorrelation.h"
#include "coproto/Socket/LocalAsyncSock.h"
#include "Common.h"
#include "secure-join/Util/Util.h"

using namespace secJoin;

/*
This is the case where input has already
arrived and you want the protocol to
take care of the preprocessing phase
*/
void PermCor_apply_test(const oc::CLP& cmd)
{
    u64 n = cmd.getOr("n", 1000);
    u64 rowSize = cmd.getOr("m", 63);

    PRNG prng0(oc::ZeroBlock);
    PRNG prng1(oc::OneBlock);

    Perm pi(n, prng0);
    PermCorSender perm0;
    PermCorReceiver perm1;


    oc::Matrix<u8> x(n, rowSize),
        yExp(n, rowSize),
        sout1(n, rowSize),
        sout2(n, rowSize);

    prng0.get(x.data(), x.size());

    auto sock = coproto::LocalAsyncSocket::makePair();

    for (auto invPerm : { PermOp::Regular, PermOp::Inverse })
    {
        genPerm(pi, perm0, perm1, rowSize, prng0);

        auto res1 = coproto::sync_wait(coproto::when_all_ready(
            perm0.apply<u8>(invPerm, sout1, sock[0]),
            perm1.apply<u8>(invPerm, x, sout2, sock[1])
        ));

        std::get<0>(res1).result();
        std::get<1>(res1).result();

        oc::Matrix<oc::u8>  yAct = reveal(sout2, sout1);

        pi.apply<u8>(x, yExp, invPerm);

        if (eq(yExp, yAct) == false)
            throw RTE_LOC;
    }
}


void PermCor_sharedApply_test(const oc::CLP& cmd)
{
    u64 n = cmd.getOr("n", 1000);
    u64 rowSize = cmd.getOr("m", 63);

    PRNG prng0(oc::ZeroBlock);
    PRNG prng1(oc::OneBlock);

    Perm pi(n, prng0);
    PermCorSender perm0;
    PermCorReceiver perm1;


    oc::Matrix<u8> 
        x0(n, rowSize),
        x1(n, rowSize),
        yExp(n, rowSize),
        sout1(n, rowSize),
        sout2(n, rowSize);

    prng0.get(x0.data(), x0.size());
    prng0.get(x1.data(), x1.size());

    auto sock = coproto::LocalAsyncSocket::makePair();

    for (auto invPerm : { PermOp::Regular, PermOp::Inverse })
    {
        genPerm(pi, perm0, perm1, rowSize, prng0);

        auto res1 = coproto::sync_wait(coproto::when_all_ready(
            perm0.apply<u8>(invPerm, x0, sout1, sock[0]),
            perm1.apply<u8>(invPerm, x1, sout2, sock[1])
        ));

        std::get<0>(res1).result();
        std::get<1>(res1).result();

        oc::Matrix<oc::u8>  yAct = reveal(sout2, sout1);
        oc::Matrix<oc::u8>  x = reveal(x0, x1);

        pi.apply<u8>(x, yExp, invPerm);

        if (eq(yExp, yAct) == false)
            throw RTE_LOC;
    }
}
