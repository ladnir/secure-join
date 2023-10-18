#include "tests/UnitTests.h"
#include "cryptoTools/Common/CLP.h"
#include "benchmark.h"
#include "secure-join/Defines.h"

using namespace secJoin;


int main(int argc, char** argv)
{
    oc::CLP clp(argc, argv);

    if (!clp.isSet("u"))
        clp.set("u");

    if (clp.isSet("bench"))
    {
        if (clp.isSet("AltMod"))
        {
            AltMod_benchmark(clp);
            return 0;
        }
        if (clp.isSet("radix"))
        {
            Radix_benchmark(clp);
            return 0;
        }
        if (clp.isSet("bmult"))
        {
            AltMod_compressB_benchmark(clp);
            return 0;
        }
        if (clp.isSet("amult"))
        {
            AltMod_expandA_benchmark(clp);
            return 0;
        }
        if (clp.isSet("transpose"))
        {
            transpose_benchmark(clp);
            return 0;
        }
        if (clp.isSet("mod3"))
        {
            AltMod_sampleMod3_benchmark(clp);
            return 0;
        }
    }
    //clp.set("u");
    secJoin_Tests::Tests.runIf(clp);
    // secJoin_Tests::Tests.runAll();
    return 0;
}