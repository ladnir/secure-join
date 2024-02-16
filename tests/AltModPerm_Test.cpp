#include "AltModPerm_Test.h"
#include "secure-join/Perm/AltModPerm.h"
#include "secure-join/Perm/AltModComposedPerm.h"

using namespace secJoin;

void AltModProtoCheck(AltModPrfSender& sender, AltModPrfReceiver& recver);

void AltModPerm_setup_test(const oc::CLP& cmd)
{
    u64 n = cmd.getOr("n", 1000);
    u64 rowSize = cmd.getOr("m", 63);
    bool debug = cmd.isSet("debug");
    // bool invPerm = false;

    PRNG prng0(oc::ZeroBlock);
    PRNG prng1(oc::OneBlock);

    AltModPermGenSender AltModPerm0;
    AltModPermGenReceiver AltModPerm1;

    oc::Matrix<oc::block>
        aExp(n, oc::divCeil(rowSize, 16));

    auto sock = coproto::LocalAsyncSocket::makePair();
    Perm pi(n, prng0);
    CorGenerator ole0, ole1;

    AltModPerm0.mDebug = debug;
    AltModPerm1.mDebug = debug;
    AltModPerm0.mPrfRecver.mDebug = debug;
    AltModPerm1.mPrfSender.mDebug = debug;

    for (auto invPerm : { PermOp::Regular,PermOp::Inverse })
    {

        ole0.init(sock[0].fork(), prng0, 0, 1 << 18, cmd.getOr("mock", 1));
        ole1.init(sock[1].fork(), prng1, 1, 1 << 18, cmd.getOr("mock", 1));

        AltModPerm0.init(n, rowSize, ole0);
        AltModPerm1.init(n, rowSize, ole1);

        PermCorSender perm0;
        PermCorReceiver perm1;

        auto r = macoro::sync_wait(macoro::when_all_ready(
            AltModPerm0.generate(pi, prng0, sock[0], perm0),
            AltModPerm1.generate(prng1, sock[1], perm1), 
            ole0.start(), ole1.start()
        ));

        std::get<0>(r).result();
        std::get<1>(r).result();

        validate(perm0, perm1);
    }
}

void AltModComposedPerm_setup_test(const oc::CLP& cmd)
{
    u64 n = cmd.getOr("n", 100);
    u64 bytesPerRow = cmd.getOr("m", 19);
    bool mock = cmd.getOr("mock", 1);
    bool debug = cmd.isSet("debug");

    AltModComposedPerm comPerm0, comPerm1;
    auto sock = coproto::LocalAsyncSocket::makePair();
    PRNG prng(oc::CCBlock);

    CorGenerator cor0, cor1;
    cor0.init(sock[0].fork(), prng, 0, 1 << 18, mock);
    cor1.init(sock[1].fork(), prng, 1, 1 << 18, mock);
    cor0.mGenState->mDebug = debug;
    cor1.mGenState->mDebug = debug;

    comPerm0.init(0, n, bytesPerRow, cor0);
    comPerm1.init(1, n, bytesPerRow, cor1);

    ComposedPerm cp0, cp1;
    Perm p0(n, prng), p1(n, prng);
    auto p = p1.compose(p0);

    auto r = macoro::sync_wait(macoro::when_all_ready(
        cor0.start(),
        cor1.start(),
        comPerm0.generate(sock[0], prng, p0, cp0),
        comPerm1.generate(sock[1], prng, p1, cp1)
    ));

    std::get<0>(r).result();
    std::get<1>(r).result();
    std::get<2>(r).result();
    std::get<3>(r).result();

    validate(p, cp0, cp1);
}
