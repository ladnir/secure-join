#include "DlpnPerm_Test.h"


void Dlpn_perm_test(const oc::CLP& cmd)
{
    // u64 n = cmd.getOr("n", 1000);
    // u64 rowSize = cmd.getOr("m",63);

    u64 n =4;
    u64 rowSize = 1;

    
    oc::PRNG prng0(oc::ZeroBlock);
    oc::PRNG prng1(oc::OneBlock);

    DLpnPrfSender sender;
    DLpnPrfReceiver recver;
    secJoin::DLpnPerm dlpnPerm1, dlpnPerm2;

    oc::Timer timer;
    sender.setTimer(timer);
    recver.setTimer(timer);

    oc::Matrix<u8> x(n, rowSize), 
        yExp(n,rowSize),
        aExp(n,rowSize),
        delta(n,rowSize), 
        b(n,rowSize), 
        a(n,rowSize), 
        sout2(n,rowSize);


    prng0.get(x.data(), x.size());
    Perm pi(n,prng0);
    // std::cout << "The Current Permutation is " << pi.mPerm << std::endl;

    // Fake Setup
    OleGenerator ole0, ole1;
    macoro::thread_pool tp;
    // auto w = tp.make_work();
    // tp.create_threads(6);
    ole0.fakeInit(OleGenerator::Role::Sender);
    ole1.fakeInit(OleGenerator::Role::Receiver);


    DLpnPrf dm;
    oc::block kk;
    kk = prng0.get();
    dm.setKey(kk);
    sender.setKey(kk);
    // Setuping up the OT Keys
    std::vector<oc::block> rk(sender.mPrf.KeySize);
    std::vector<std::array<oc::block, 2>> sk(sender.mPrf.KeySize);
    for (u64 i = 0; i < sender.mPrf.KeySize; ++i)
    {
        sk[i][0] = oc::block(i, 0);
        sk[i][1] = oc::block(i, 1);
        rk[i] = oc::block(i, *oc::BitIterator((u8*)&sender.mPrf.mKey, i));
    }
    sender.setKeyOts(rk);
    recver.setKeyOts(sk);


    auto sock = coproto::LocalAsyncSocket::makePair();

    // std::vector<oc::Matrix<u8>> temp;
    // temp.push_back(a);
    // temp.push_back(b);


    // the preprocessing phase
    auto res = coproto::sync_wait(coproto::when_all_ready(
        dlpnPerm1.setup(pi, rowSize, delta, prng0, sock[0], ole1, recver),
        dlpnPerm2.setup(a , b , prng1, sock[1], ole0, n, rowSize, sender, dm)
    ));

    oc::Matrix<oc::u8>  permPiA = reveal(delta,b);

    pi.apply<u8>(a, aExp, true);
    if(eq(aExp, permPiA) == false)
    {
        std::cout << "A & permuted A are not the same" << std::endl;
        throw RTE_LOC;
    }
        

    std::get<0>(res).result();
    std::get<1>(res).result();

    coproto::sync_wait(coproto::when_all_ready(
        ole0.stop(),
        ole1.stop()
    ));


    // the online phase (where input are already arrived)
    auto res1 = coproto::sync_wait(coproto::when_all_ready(
        dlpnPerm1.apply(pi, delta , sout2 , rowSize, sock[0]),
        dlpnPerm2.apply( x,a ,sock[1]))
    );

    std::get<0>(res1).result();
    std::get<1>(res1).result();

    // Need to check if permutation is working or not

    // Need to xor sout2 & b
    oc::Matrix<oc::u8>  yAct = reveal(sout2,b);
            
    pi.apply<u8>(x, yExp, true);

    if(eq(yExp, yAct) == false)
        throw RTE_LOC;


   
}