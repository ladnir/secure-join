//#include "AdditivePerm_Test.h"
//
//#include "secure-join/Util/Util.h"
//#include "secure-join/Perm/AdditivePerm.h"
//#include "secure-join/Perm/Permutation.h"
//
//using namespace secJoin;
//
//
//void AdditivePerm_xor_test(const oc::CLP& cmd)
//{
//    // User input
//    u64 n = 5000;
//    u64 rowSize = 7;
//
//    oc::Matrix<u8> x(n, rowSize), yExp(n, rowSize);
//    PRNG prng(oc::block(0, 0));
//    auto chls = coproto::LocalAsyncSocket::makePair();
//
//    Perm mPi(n, prng);
//    prng.get(x.data(), x.size());
//
//    // Secret Sharing s
//    std::array<std::vector<u32>, 2> sShares = xorShare(mPi.mPi, prng);
//    std::array<oc::Matrix<u8>, 2> yShare;
//
//    AdditivePerm vecPerm1;vecPerm1.init2(0, n, rowSize * 2); vecPerm1.setShares(sShares[0]);
//    AdditivePerm vecPerm2;vecPerm2.init2(1, n, rowSize * 2); vecPerm2.setShares(sShares[1]);
//
//    CorGenerator ole0, ole1;
//
//    ole0.init(chls[0].fork(), prng, 0, 1<<18, cmd.getOr("mock", 1));
//    ole1.init(chls[1].fork(), prng, 1, 1<<18, cmd.getOr("mock", 1));
//
//
//    // Setuping up the OT Keys
//    AltModPrf::KeyType kk = prng.get();
//    std::vector<oc::block> rk(AltModPrf::KeySize);
//    std::vector<std::array<oc::block, 2>> sk(AltModPrf::KeySize);
//    for (u64 i = 0; i < AltModPrf::KeySize; ++i)
//    {
//        sk[i][0] = oc::block(i, 0);
//        sk[i][1] = oc::block(i, 1);
//        rk[i] = oc::block(i, *oc::BitIterator((u8*)&kk, i));
//    }
//    vecPerm1.setKeyOts(kk, rk, sk);
//    vecPerm2.setKeyOts(kk, rk, sk);
//
//    vecPerm1.request(ole0);
//    vecPerm2.request(ole1);
//    auto proto0 = vecPerm1.setup(chls[0], prng);
//    auto proto1 = vecPerm2.setup(chls[1], prng);
//
//    auto res = macoro::sync_wait(macoro::when_all_ready(std::move(proto0), std::move(proto1)));
//
//    std::get<0>(res).result();
//    std::get<1>(res).result();
//
//    if (vecPerm1.mRho != vecPerm2.mRho)
//        throw RTE_LOC;
//
//    auto pi = vecPerm1.mRandPi.mSender.mPi->composeSwap(*vecPerm2.mRandPi.mSender.mPi);
//    Perm rhoExp = pi.apply(mPi.mPi);
//    if (rhoExp != vecPerm1.mRho)
//        throw RTE_LOC;
//
//    // Secret Sharing x
//    std::array<oc::Matrix<u8>, 2> xShares = share(x, prng);
//    yShare[0].resize(x.rows(), x.cols());
//    yShare[1].resize(x.rows(), x.cols());
//
//    proto0 = vecPerm1.apply<u8>(PermOp::Regular, xShares[0], yShare[0], prng, chls[0]);
//    proto1 = vecPerm2.apply<u8>(PermOp::Regular, xShares[1], yShare[1], prng, chls[1]);
//
//    auto res1 = macoro::sync_wait(macoro::when_all_ready(std::move(proto0), std::move(proto1)));
//
//    std::get<0>(res1).result();
//    std::get<1>(res1).result();
//
//    auto yAct = reveal(yShare[0], yShare[1]);
//    mPi.apply<u8>(x, yExp);
//
//    if (!eq(yAct, yExp))
//        throw RTE_LOC;
//
//
//    auto res2 = macoro::sync_wait(macoro::when_all_ready(
//        vecPerm1.apply<u8>(PermOp::Inverse, xShares[0], yShare[0], prng, chls[0]),
//        vecPerm2.apply<u8>(PermOp::Inverse, xShares[1], yShare[1], prng, chls[1])
//    ));
//    std::get<0>(res2).result();
//    std::get<1>(res2).result();
//
//
//
//    yAct = reveal(yShare[0], yShare[1]);
//    mPi.apply<u8>(x, yExp, PermOp::Inverse);
//
//}
//
//void AdditivePerm_prepro_test(const oc::CLP& cmd)
//{
//    // User input
//    u64 n = 500;    // total number of rows
//    u64 rowSize = 11;
//
//    oc::Matrix<u8> x(n, rowSize), yExp(n, rowSize);
//    PRNG prng(oc::block(0, 0));
//    auto chls = coproto::LocalAsyncSocket::makePair();
//
//    for (auto setup : { false, true })
//    {
//        Perm mPi(n, prng);
//        prng.get(x.data(), x.size());
//
//        // Secret Sharing s
//        std::array<std::vector<u32>, 2> sShares = xorShare(mPi.mPi, prng);
//        std::array<oc::Matrix<u8>, 2> yShare;
//
//        AdditivePerm vecPerm1;vecPerm1.init2(0, n, rowSize); 
//        AdditivePerm vecPerm2;vecPerm2.init2(1, n, rowSize); 
//
//        if (setup == false)
//        {
//            vecPerm1.setShares(sShares[0]);
//            vecPerm2.setShares(sShares[1]);
//        }
//
//        CorGenerator ole0, ole1;
//        ole0.init(chls[0].fork(), prng, 0, 1 << 18, cmd.getOr("mock", 1));
//        ole1.init(chls[1].fork(), prng, 1, 1 << 18, cmd.getOr("mock", 1));
//
//
//        vecPerm1.request(ole0);
//        vecPerm2.request(ole1);
//
//        auto res0 = macoro::sync_wait(macoro::when_all_ready(
//            vecPerm1.preprocess(),
//            vecPerm2.preprocess()
//        ));
//
//        std::get<0>(res0).result();
//        std::get<1>(res0).result();
//
//        if (setup == true)
//        {
//            auto res1 = macoro::sync_wait(macoro::when_all_ready(
//                vecPerm1.setup(chls[0], prng),
//                vecPerm2.setup(chls[1], prng)
//            ));
//
//            std::get<0>(res1).result();
//            std::get<1>(res1).result();
//
//            if (vecPerm1.mRho != vecPerm2.mRho)
//                throw RTE_LOC;
//            //auto pi = vecPerm1.mRandPi.mSender.mPi->composeSwap(*vecPerm2.mRandPi.mSender.mPi);
//            //Perm rhoExp = pi.apply(mPi.mPi);
//            //if (rhoExp != vecPerm1.mRho)
//            //    throw RTE_LOC;
//
//            vecPerm1.setShares(sShares[0]);
//            vecPerm2.setShares(sShares[1]);
//        }
//
//        // Secret Sharing x
//        std::array<oc::Matrix<u8>, 2> xShares = share(x, prng);
//        yShare[0].resize(x.rows(), x.cols());
//        yShare[1].resize(x.rows(), x.cols());
//
//        auto res1 = macoro::sync_wait(macoro::when_all_ready(
//            vecPerm1.apply<u8>(PermOp::Regular, xShares[0], yShare[0], prng, chls[0]),
//            vecPerm2.apply<u8>(PermOp::Regular, xShares[1], yShare[1], prng, chls[1])
//        ));
//
//        std::get<0>(res1).result();
//        std::get<1>(res1).result();
//
//        auto yAct = reveal(yShare[0], yShare[1]);
//        mPi.apply<u8>(x, yExp);
//
//        if (!eq(yAct, yExp))
//            throw RTE_LOC;
//    }
//}
