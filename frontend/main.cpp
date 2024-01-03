#include "tests/UnitTests.h"
#include "cryptoTools/Common/CLP.h"
#include "benchmark.h"
#include "secure-join/Defines.h"
#include "coproto/Proto/SessionID.h"
using namespace secJoin;


int main(int argc, char** argv)
{
    oc::CLP clp(argc, argv);

    if (!clp.isSet("u"))
        clp.set("u");

    if (clp.isSet("bench"))
    {

        if (clp.isSet("CorGen"))
        {
            CorGen_benchmark(clp);
        }
        if (clp.isSet("join"))
        {
            OmJoin_benchmark(clp);
        }
        if (clp.isSet("AltMod"))
        {
            AltMod_benchmark(clp);
        }
        if (clp.isSet("radix"))
        {
            Radix_benchmark(clp);
        }
        if (clp.isSet("bmult"))
        {
            AltMod_compressB_benchmark(clp);
        }
        if (clp.isSet("amult"))
        {
            AltMod_expandA_benchmark(clp);
        }
        if (clp.isSet("transpose"))
        {
            transpose_benchmark(clp);
        }
        if (clp.isSet("mod3"))
        {
            AltMod_sampleMod3_benchmark(clp);
        }
        return 0;
    }


    secJoin_Tests::Tests.runIf(clp);
    // secJoin_Tests::Tests.runAll();
    return 0;
}