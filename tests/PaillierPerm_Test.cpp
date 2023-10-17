
#include "secure-join/Perm/PaillierPerm.h"
#include "cryptoTools/Common/TestCollection.h"


void PaillierPerm_basic_test()
{
#ifdef SECUREJOIN_ENALBLE_PAILLIER
    using namespace secJoin;

    u64 n = 100;
    PaillierPerm perm0, perm1;
    
    auto chls = coproto::LocalAsyncSocket::makePair();
    PRNG prng0(oc::block(0,0)), prng1(oc::block(1,1));

    std::vector<u64> pi(n), z(n), y(n), x(n);

    for(u64 i =0; i < n; ++i)
        pi[i] = prng0.get<u64>() % n;

    auto proto0 = perm0.applyPerm(pi, prng0, z, chls[0]);
    auto proto1 = perm1.applyVec(x, prng1, y, chls[1]);

    auto res = macoro::sync_wait(macoro::when_all_ready(std::move(proto0), std::move(proto1)));

    // check the results for errors.
    std::get<0>(res).result();
    std::get<1>(res).result();

    for(u64 i =0; i < n; ++i)
    {
        if(z[i] + y[i] != x[pi[i]])
        {
            std::cout << i << " z " << z[i] << " y " << y[i] << " " << -y[i] <<  " x " << x[pi[i]] << " p " << pi[i] << std::endl;
            throw RTE_LOC;
        }
    }
#else

    throw oc::UnitTestSkipped("SECUREJOIN_ENALBLE_PAILLIER not defined.");
#endif
}


