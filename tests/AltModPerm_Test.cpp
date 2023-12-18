#include "AltModPerm_Test.h"

using namespace secJoin;
void AltModProtoCheck(AltModPrfSender& sender, AltModPrfReceiver& recver);

/*
This is the case where input has not
arrived yet but we can start the pre
processing phase

Advantage: setup can run in background
before the input comes
*/
void AltModPerm_setup_test(const oc::CLP& cmd)
{
    u64 n = cmd.getOr("n", 1000);
    u64 rowSize = cmd.getOr("m", 63);
    bool debug = cmd.isSet("debug");
    // bool invPerm = false;

    PRNG prng0(oc::ZeroBlock);
    PRNG prng1(oc::OneBlock);

    secJoin::AltModPermSender AltModPerm0;
    secJoin::AltModPermReceiver AltModPerm1;

    oc::Matrix<oc::block>
        aExp(n, oc::divCeil(rowSize, 16));

    Perm pi(n, prng0);

    // Fake Setup
    CorGenerator ole0, ole1;

    auto sock = coproto::LocalAsyncSocket::makePair();
    ole0.init(sock[0].fork(), prng0, 0, 1<<18, cmd.getOr("mock", 1));
    ole1.init(sock[1].fork(), prng1, 1, 1<<18, cmd.getOr("mock", 1));

    AltModPrf dm; dm.setKey(prng0.get());
    //oc::block kk = prng0.get();

    // Setuping up the OT Keys
    std::vector<oc::block> rk(AltModPrf::KeySize);
    std::vector<std::array<oc::block, 2>> sk(AltModPrf::KeySize);
    for (u64 i = 0; i < AltModPrf::KeySize; ++i)
    {
        sk[i][0] = oc::block(i, 0);
        sk[i][1] = oc::block(i, 1);
        rk[i] = oc::block(i, *oc::BitIterator((u8*)&dm.mExpandedKey, i));
    }
    AltModPerm0.setKeyOts(sk);
    auto kkk = dm.getKey();
    AltModPerm1.setKeyOts(kkk, rk);
    for (u64 i = 0; i < AltModPrf::KeySize; ++i)
    {
        auto ki = *oc::BitIterator((u8*)&AltModPerm1.mSender.mPrf.mExpandedKey, i);
        if (AltModPerm0.mRecver.mKeyOTs[i][ki].getSeed() != AltModPerm1.mSender.mKeyOTs[i].getSeed())
        {
            std::cout << "bad key ot " << i << "\nki=" << ki << " " << AltModPerm1.mSender.mKeyOTs[i].getSeed() << " vs \n"
                << AltModPerm0.mRecver.mKeyOTs[i][0].getSeed() << " " << AltModPerm0.mRecver.mKeyOTs[i][1].getSeed() << std::endl;
            throw RTE_LOC;
        }
    }
    if (AltModPerm1.mSender.getKey() != dm.getKey())
        throw RTE_LOC;
    if (AltModPerm1.mSender.mPrf.mExpandedKey != dm.mExpandedKey)
        throw RTE_LOC;

    AltModPerm0.mDebug = debug;
    AltModPerm1.mDebug = debug;
    AltModPerm0.mRecver.mDebug = debug;
    AltModPerm1.mSender.mDebug = debug;

    //for (auto invPerm : { PermOp::Regular,PermOp::Inverse })
    {
        AltModPerm0.init(n, rowSize);
        AltModPerm1.init(n, rowSize);
        AltModPerm0.setPermutation(pi);

        AltModPerm0.request(ole0);
        AltModPerm1.request(ole1);

        //for (u64 i = 0; i < ole0.mGenState->mRequests.size(); ++i)
        //{
        //    std::cout << i << " " << ole0.mGenState->mRequests[i]->mSize << std::endl;
        //    std::cout << i << " " << ole1.mGenState->mRequests[i]->mSize << std::endl;
        //}

        // the preprocessing phase
        auto res = coproto::sync_wait(coproto::when_all_ready(
            AltModPerm0.setup(prng0, sock[0]),
            AltModPerm1.setup(prng1, sock[1])
        ));

        oc::Matrix<oc::block>  permPiA = reveal(AltModPerm0.mDelta, AltModPerm1.mB);

        pi.apply<oc::block>(AltModPerm1.mA, aExp);

        if (eq(aExp, permPiA) == false)
        {
            std::cout << "A & permuted A are not the same" << std::endl;

            if (debug)
            {
                AltModProtoCheck(AltModPerm1.mSender, AltModPerm0.mRecver);
            }

            throw RTE_LOC;
        }
    }

}


/*
This is the case where input has already
arrived and you want the protocol to
take care of the preprocessing phase
*/
void AltModPerm_apply_test(const oc::CLP& cmd)
{
    u64 n = cmd.getOr("n", 1000);
    u64 rowSize = cmd.getOr("m", 63);

    // bool invPerm = false;

    PRNG prng0(oc::ZeroBlock);
    PRNG prng1(oc::OneBlock);



    secJoin::AltModPermSender AltModPerm0;
    secJoin::AltModPermReceiver AltModPerm1;


    oc::Matrix<u8> x(n, rowSize),
        yExp(n, rowSize),
        sout1(n, rowSize),
        sout2(n, rowSize);

    prng0.get(x.data(), x.size());
    Perm pi(n, prng0);
    // // std::cout << "The Current Permutation is " << pi.mPi << std::endl;

    CorGenerator ole0, ole1;
    auto sock = coproto::LocalAsyncSocket::makePair();
    ole0.init(sock[0].fork(), prng0, 0, 1<<18, cmd.getOr("mock", 1));
    ole1.init(sock[1].fork(), prng1, 1, 1<<18, cmd.getOr("mock", 1));


    AltModPerm0.init(n, rowSize);
    AltModPerm1.init(n, rowSize);
    AltModPerm0.setPermutation(pi);

    for (auto invPerm : { PermOp::Regular,PermOp::Inverse })
    {
        AltModPerm0.request(ole0);
        AltModPerm1.request(ole1);


        //
        AltModPerm1.mDebug = true;
        AltModPerm0.mDebug = true;
        AltModPerm1.mSender.mDebug = true;
        AltModPerm0.mRecver.mDebug = true;


        auto res0 = coproto::sync_wait(coproto::when_all_ready(
            AltModPerm0.preprocess(),
            AltModPerm1.preprocess()
        ));


        std::get<0>(res0).result();
        std::get<1>(res0).result();

        auto res1 = coproto::sync_wait(coproto::when_all_ready(
            AltModPerm0.apply<u8>(invPerm, sout1, prng0, sock[0]),
            AltModPerm1.apply<u8>(invPerm, x, sout2, prng1, sock[1])
        ));


        std::get<0>(res1).result();
        std::get<1>(res1).result();

        oc::Matrix<oc::u8>  yAct = reveal(sout2, sout1);

        pi.apply<u8>(x, yExp, invPerm);

        if (eq(yExp, yAct) == false)
            throw RTE_LOC;

    }

}


void AltModPerm_sharedApply_test(const oc::CLP& cmd)
{
    u64 n = cmd.getOr("n", 1000);
    u64 rowSize = cmd.getOr("m", 63);

    // bool invPerm = false;

    PRNG prng0(oc::ZeroBlock);
    PRNG prng1(oc::OneBlock);


    secJoin::AltModPermSender AltModPerm0;
    secJoin::AltModPermReceiver AltModPerm1;


    oc::Matrix<u8> x(n, rowSize),
        yExp(n, rowSize),
        sout1(n, rowSize),
        sout2(n, rowSize);

    prng0.get(x.data(), x.size());
    Perm pi(n, prng0);
    // // std::cout << "The Current Permutation is " << pi.mPi << std::endl;

    // Fake Setup
    CorGenerator ole0, ole1;
    auto sock = coproto::LocalAsyncSocket::makePair();
    ole0.init(sock[0].fork(), prng0, 0, 1<<18, cmd.getOr("mock", 1));
    ole1.init(sock[1].fork(), prng1, 1, 1<<18, cmd.getOr("mock", 1));

    std::array<oc::Matrix<u8>, 2> xShares = share(x, prng0);

    AltModPerm0.init(n, rowSize);
    AltModPerm1.init(n, rowSize);
    AltModPerm0.setPermutation(pi);

    for (auto invPerm : { PermOp::Regular,PermOp::Inverse })
    {
        AltModPerm0.request(ole0);
        AltModPerm1.request(ole1);

        //if (ole0 != ole1)
        //    throw RTE_LOC;


        auto res1 = coproto::sync_wait(coproto::when_all_ready(
            AltModPerm0.apply<u8>(invPerm, xShares[0], sout1, prng0, sock[0]),
            AltModPerm1.apply<u8>(invPerm, xShares[1], sout2, prng1, sock[1])
        ));


        std::get<0>(res1).result();
        std::get<1>(res1).result();

        oc::Matrix<oc::u8>  yAct = reveal(sout2, sout1);

        pi.apply<u8>(x, yExp, invPerm);

        if (eq(yExp, yAct) == false)
            throw RTE_LOC;

    }

}

void AltModPerm_prepro_test(const oc::CLP& cmd)
{

    u64 n = cmd.getOr("n", 1000);
    u64 rowSize = cmd.getOr("m", 32);



    // bool invPerm = false;

    PRNG prng0(oc::ZeroBlock);
    PRNG prng1(oc::OneBlock);

    secJoin::AltModPermSender AltModPerm0;
    secJoin::AltModPermReceiver AltModPerm1;

    oc::Matrix<u8> x(n, rowSize / 2),
        yExp(n, rowSize / 2),
        sout1(n, rowSize / 2),
        sout2(n, rowSize / 2);

    prng0.get(x.data(), x.size());
    Perm pi(n, prng0);
    // // std::cout << "The Current Permutation is " << pi.mPi << std::endl;

    // Fake Setup
    CorGenerator ole0, ole1;
    auto sock = coproto::LocalAsyncSocket::makePair();

    ole0.init(sock[0].fork(), prng0, 0, 1<<18, cmd.getOr("mock", 1));
    ole1.init(sock[1].fork(), prng1, 1, 1<<18, cmd.getOr("mock", 1));

    std::array<oc::Matrix<u8>, 2> xShares = share(x, prng0);


    for (auto invPerm : { PermOp::Regular, PermOp::Inverse })
    {

        AltModPerm0.init(n, rowSize);
        AltModPerm1.init(n, rowSize);
        AltModPerm0.request(ole0);
        AltModPerm1.request(ole1);
        AltModPerm0.clearPermutation();
        AltModPerm1.clearPermutation();
        auto res0 = coproto::sync_wait(coproto::when_all_ready(
            AltModPerm0.setup(prng0, sock[0]),
            AltModPerm1.setup(prng1, sock[1])
        ));
        std::get<0>(res0).result();
        std::get<1>(res0).result();

        {
            PRNG prng(oc::ZeroBlock);
            auto A = AltModPerm1.mA;
            //std::vector<u64> A(n);
            //std::iota(A.begin(), A.end(), 0);

            auto pre = AltModPerm0.mPrePerm;
            for (u64 i = 0; i < n; ++i)
                for (u64 j = 0;j < A.cols(); ++j)
                    if ((AltModPerm0.mDelta(i, j) ^ AltModPerm1.mB(i, j)) != A(pre[i], j))
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
            //auto pii = invPerm ? pi.inverse() : pi;
            auto delta = pi.inverse().compose(pre);

            auto AA = delta.apply<oc::block>(A);
            for (u64 i = 0; i < n; ++i)
                for (u64 j = 0;j < A.cols(); ++j)
                    if (preA(i, j) != AA(pi[i], j))
                        throw RTE_LOC;

        }

        AltModPerm0.setPermutation(pi);

        for (i64 i = 0; i < 2; ++i)
        {
            //AltModPerm0.request(ole0);
            //AltModPerm1.request(ole1);

            auto res1 = coproto::sync_wait(coproto::when_all_ready(
                AltModPerm0.apply<u8>(invPerm, xShares[0], sout1, prng0, sock[0]),
                AltModPerm1.apply<u8>(invPerm, xShares[1], sout2, prng1, sock[1])
            ));
            std::get<0>(res1).result();
            std::get<1>(res1).result();


            oc::Matrix<oc::u8>  yAct = reveal(sout2, sout1);

            pi.apply<u8>(x, yExp, invPerm);

            if (eq(yExp, yAct) == false)
                throw RTE_LOC;

        }
    }

}
