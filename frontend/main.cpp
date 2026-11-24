#include "tests/UnitTests.h"
#include "cryptoTools/Common/CLP.h"
#include "benchmark.h"
#include "secure-join/Defines.h"
#include "coproto/Proto/SessionID.h"
#include "macoro/task.h"
#include "macoro/sync_wait.h"
#include "macoro/when_all.h"
#include "coproto/Socket/AsioSocket.h"
#include "cryptoTools/Common/Timer.h"
#include "tests_cryptoTools/UnitTests.h"



namespace secJoin
{
    void buildMod3Table2();
    void buildMod3Table4();
}
using namespace secJoin;

int main(int argc, char** argv)
{
    oc::CLP clp(argc, argv);

    if (!clp.isSet("u"))
        clp.set("u");
    

    //buildMod3Table2();
    //buildMod3Table4();


    if (clp.isSet("bench"))
    {

        if (clp.isSet("CorGen"))
        {
            CorGen_benchmark(clp);
        }
        if (clp.isSet("OT"))
        {
            OT_benchmark(clp);
        }
        if (clp.isSet("f4"))
        {
            F4_benchmark(clp);
        }
        if (clp.isSet("join"))
        {
            OmJoin_benchmark(clp);
        }
        if (clp.isSet("AltMod"))
        {
            AltMod_benchmark(clp);
        }
        if (clp.isSet("AltModPerm"))
        {
            AltModPerm_benchmark(clp);
        }
        if (clp.isSet("PprfPerm"))
        {
            PprfPerm_benchmark(clp);
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
        if (clp.isSet("encodex"))
        {
            AltMod_encodeX_benchmark(clp);
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

    oc::TestCollection tests;
    if(clp.isSet("cryptoTools"))
    {
        tests = tests_cryptoTools::Tests;
    }

    tests += secJoin_Tests::Tests;
    return tests.runIf(clp) == oc::TestCollection::Result::failed ? 1 : 0;
}
