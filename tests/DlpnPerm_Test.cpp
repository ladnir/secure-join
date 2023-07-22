#include "DlpnPerm_Test.h"

using namespace secJoin;

/*
This is the case where input has not
arrived yet but we can start the pre
processing phase

Advantage: setup can run in background
before the input comes
*/
void DlpnPerm_setup_test(const oc::CLP& cmd)
{
    u64 n = cmd.getOr("n", 1000);
    u64 rowSize = cmd.getOr("m", 63);
    // bool invPerm = false;

    oc::PRNG prng0(oc::ZeroBlock);
    oc::PRNG prng1(oc::OneBlock);

    secJoin::DLpnPermSender dlpnPerm1;
    secJoin::DLpnPermReceiver dlpnPerm2;

    oc::Matrix<oc::block> 
        aExp(n, oc::divCeil(rowSize, 16));

    Perm pi(n, prng0);

    // Fake Setup
    OleGenerator ole0, ole1;
    ole0.fakeInit(OleGenerator::Role::Sender);
    ole1.fakeInit(OleGenerator::Role::Receiver);

    // DLpnPrf dm;
    oc::block kk = prng0.get();

    // Setuping up the OT Keys
    std::vector<oc::block> rk(128);
    std::vector<std::array<oc::block, 2>> sk(128);
    for (u64 i = 0; i < 128; ++i)
    {
        sk[i][0] = oc::block(i, 0);
        sk[i][1] = oc::block(i, 1);
        rk[i] = oc::block(i, *oc::BitIterator((u8*)&kk, i));
    }
    dlpnPerm1.setKeyOts(sk);
    dlpnPerm2.setKeyOts(kk, rk);

    auto sock = coproto::LocalAsyncSocket::makePair();

    for (auto invPerm : { false,true })
    {

        // the preprocessing phase
        auto res = coproto::sync_wait(coproto::when_all_ready(
            dlpnPerm1.setup(pi, rowSize, prng0, sock[0], invPerm, ole1),
            dlpnPerm2.setup(n, rowSize, prng1, sock[1], ole0)
        ));

        oc::Matrix<oc::block>  permPiA = reveal(dlpnPerm1.mDelta, dlpnPerm2.mB);

        pi.apply<oc::block>(dlpnPerm2.mA, aExp, invPerm);

        if (eq(aExp, permPiA) == false)
        {
            std::cout << "A & permuted A are not the same" << std::endl;
            throw RTE_LOC;
        }
    }

}


/*
This is the case where input has already
arrived and you want the protocol to
take care of the preprocessing phase
*/
void DlpnPerm_apply_test(const oc::CLP& cmd)
{
    u64 n = cmd.getOr("n", 1000);
    u64 rowSize = cmd.getOr("m", 63);

    // bool invPerm = false;

    oc::PRNG prng0(oc::ZeroBlock);
    oc::PRNG prng1(oc::OneBlock);



    secJoin::DLpnPermSender dlpnPerm1;
    secJoin::DLpnPermReceiver dlpnPerm2;


    oc::Matrix<u8> x(n, rowSize),
        yExp(n, rowSize),
        sout1(n, rowSize),
        sout2(n, rowSize);

    prng0.get(x.data(), x.size());
    Perm pi(n, prng0);
    // // std::cout << "The Current Permutation is " << pi.mPerm << std::endl;

    // Fake Setup
    OleGenerator ole0, ole1;
    ole0.fakeInit(OleGenerator::Role::Sender);
    ole1.fakeInit(OleGenerator::Role::Receiver);

    // auto res = coproto::sync_wait(coproto::when_all_ready(
    //     dlpnPerm1.receiverSetup(ole0),
    //     dlpnPerm2.senderSetup(ole1)
    // ));
    // std::get<0>(res).result();
    // std::get<1>(res).result();

    auto sock = coproto::LocalAsyncSocket::makePair();

    for (auto invPerm : { false,true })
    {
        auto res1 = coproto::sync_wait(coproto::when_all_ready(
            dlpnPerm1.apply<u8>(pi, sout1, prng0, sock[0], invPerm, ole0),
            dlpnPerm2.apply<u8>(x, sout2, prng1, sock[1], ole1)
        ));


        std::get<0>(res1).result();
        std::get<1>(res1).result();

        coproto::sync_wait(coproto::when_all_ready(
            ole0.stop(),
            ole1.stop()
        ));

        oc::Matrix<oc::u8>  yAct = reveal(sout2, sout1);

        pi.apply<u8>(x, yExp, invPerm);

        if (eq(yExp, yAct) == false)
            throw RTE_LOC;

    }

}


void DlpnPerm_sharedApply_test(const oc::CLP& cmd)
{
    u64 n = cmd.getOr("n", 1000);
    u64 rowSize = cmd.getOr("m", 63);

    // bool invPerm = false;

    oc::PRNG prng0(oc::ZeroBlock);
    oc::PRNG prng1(oc::OneBlock);


    secJoin::DLpnPermSender dlpnPerm1;
    secJoin::DLpnPermReceiver dlpnPerm2;


    oc::Matrix<u8> x(n, rowSize),
        yExp(n, rowSize),
        sout1(n, rowSize),
        sout2(n, rowSize);

    prng0.get(x.data(), x.size());
    Perm pi(n, prng0);
    // // std::cout << "The Current Permutation is " << pi.mPerm << std::endl;

    // Fake Setup
    OleGenerator ole0, ole1;
    ole0.fakeInit(OleGenerator::Role::Sender);
    ole1.fakeInit(OleGenerator::Role::Receiver);

    auto sock = coproto::LocalAsyncSocket::makePair();
    auto res = coproto::sync_wait(coproto::when_all_ready(
        dlpnPerm1.genKeyOts(ole0, sock[0]),
        dlpnPerm2.genKeyOts(ole1, sock[1])
    ));

    std::get<0>(res).result();
    std::get<1>(res).result();

    std::array<oc::Matrix<u8>, 2> xShares = share(x, prng0);


    for (auto invPerm : { false,true })
    {
        auto res1 = coproto::sync_wait(coproto::when_all_ready(
            dlpnPerm1.apply<u8>(pi, xShares[0], sout1, prng0, sock[0], invPerm, ole1),
            dlpnPerm2.apply<u8>(xShares[1], sout2, prng1, sock[1], ole0)
        ));


        std::get<0>(res1).result();
        std::get<1>(res1).result();

        coproto::sync_wait(coproto::when_all_ready(
            ole0.stop(),
            ole1.stop()
        ));

        oc::Matrix<oc::u8>  yAct = reveal(sout2, sout1);

        pi.apply<u8>(x, yExp, invPerm);

        if (eq(yExp, yAct) == false)
            throw RTE_LOC;

    }

}

void DlpnPerm_prepro_test(const oc::CLP& cmd)
{

    u64 n = cmd.getOr("n", 1000);
    u64 rowSize = cmd.getOr("m", 32);



    // bool invPerm = false;

    oc::PRNG prng0(oc::ZeroBlock);
    oc::PRNG prng1(oc::OneBlock);

    secJoin::DLpnPermSender dlpnPerm1;
    secJoin::DLpnPermReceiver dlpnPerm2;

    oc::Matrix<u8> x(n, rowSize),
        yExp(n, rowSize),
        sout1(n, rowSize),
        sout2(n, rowSize);

    prng0.get(x.data(), x.size());
    Perm pi(n, prng0);
    // // std::cout << "The Current Permutation is " << pi.mPerm << std::endl;

    // Fake Setup
    OleGenerator ole0, ole1;
    ole0.fakeInit(OleGenerator::Role::Sender);
    ole1.fakeInit(OleGenerator::Role::Receiver);

    auto sock = coproto::LocalAsyncSocket::makePair();
    auto res = coproto::sync_wait(coproto::when_all_ready(
        dlpnPerm1.genKeyOts(ole0, sock[0]),
        dlpnPerm2.genKeyOts(ole1, sock[1])
    ));

    std::get<0>(res).result();
    std::get<1>(res).result();

    std::array<oc::Matrix<u8>, 2> xShares = share(x, prng0);


    for (auto invPerm : { false, true  })
    {
        auto res0 = coproto::sync_wait(coproto::when_all_ready(
            dlpnPerm1.preprocess(n, rowSize, prng0, sock[0], ole1),
            dlpnPerm2.preprocess(n, rowSize, prng1, sock[1], ole0)
        ));
        std::get<0>(res0).result();
        std::get<1>(res0).result();

        {
            oc::PRNG prng(oc::ZeroBlock);
            auto A = dlpnPerm2.mA;
            //std::vector<u64> A(n);
            //std::iota(A.begin(), A.end(), 0);

            auto pre = dlpnPerm1.mPrePerm;
            for (u64 i = 0; i < n; ++i)
                for (u64 j = 0;j < A.cols(); ++j)
                    if ((dlpnPerm1.mDelta(i, j) ^ dlpnPerm2.mB(i,j)) != A(pre[i], j))
                        throw RTE_LOC;
            //Perm pre(n, prng);
            //Perm pi(n, prng);

            // D + B = pre(A)
            auto preA = pre.apply<oc::block>(A);
            for (u64 i = 0; i < n; ++i)
                for (u64 j = 0;j < A.cols(); ++j)
                    if (preA(i, j) != A(pre[i], j))
                        throw RTE_LOC;

            //auto piA = pi.apply(A);

            // we current have the correlation 
            // 
            //          mDelta ^ mB  = pre(mA)
            //   pre^-1(mDelta ^ mB) = mA
            // 
            // if we multiply both sides by (pi^-1 o pre) we get
            // 
            //   (pi^-1 o pre)( pre^-1(mDelta ^ mB)) = (pi^-1 o pre) (mA)
            //   (pi^-1 o pre o pre^-1)(mDelta ^ mB)) = (pi^-1 o pre) (mA)
            //   (pi^-1)(mDelta ^ mB)) = (pi^-1 o pre)(mA)
            //   mDelta ^ mB = pi((pi^-1 o pre)(mA))
            //   mDelta ^ mB = pi(mA')
            // 
            // where mA' = (pi^-1 o pre)(mA)
            //           = delta(mA)
            auto pii = invPerm ? pi.inverse() : pi;
            auto delta = pii.inverse().compose(pre);

            auto AA = delta.apply<oc::block>(A);
            for (u64 i = 0; i < n; ++i)
                for (u64 j = 0;j < A.cols(); ++j)
                    if (preA(i, j) != AA(pii[i], j))
                        throw RTE_LOC;

        }


        auto res1 = coproto::sync_wait(coproto::when_all_ready(
            dlpnPerm1.apply<u8>(pi, xShares[0], sout1, prng0, sock[0], invPerm, ole1),
            dlpnPerm2.apply<u8>(xShares[1], sout2, prng1, sock[1], ole0)
        ));
        std::get<0>(res1).result();
        std::get<1>(res1).result();


        oc::Matrix<oc::u8>  yAct = reveal(sout2, sout1);

        pi.apply<u8>(x, yExp, invPerm);

        if (eq(yExp, yAct) == false)
            throw RTE_LOC;

    }

}
