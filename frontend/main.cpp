#include "tests/UnitTests.h"
#include "cryptoTools/Common/CLP.h"
#include "benchmark.h"
using namespace secJoin;

int main(int argc, char** argv)
{
    oc::CLP clp(argc, argv);

    if (!clp.isSet("u"))
        clp.set("u");

    if (clp.isSet("bench"))
    {
        benchmark(clp);
        return 0;
    }
    //clp.set("u");
    secJoin_Tests::Tests.runIf(clp);
    // secJoin_Tests::Tests.runAll();
    return 0;
}