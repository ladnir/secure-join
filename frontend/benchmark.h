#include "cryptoTools/Common/CLP.h"


namespace secJoin
{
    void benchmark(const oc::CLP& cmd);
    void Dlpn_benchmark(const oc::CLP& cmd);
    void Dlpn_compressB_benchmark(const oc::CLP& cmd);

    void PaillierPerm_banch(const oc::CLP& cmd);
    void lowmc_benchmark(const oc::CLP& cmd);

    void LowMCPerm_bench(const oc::CLP& cmd);
    void DlpnPerm_bench(const oc::CLP& cmd);
    void RadixSort_bench(const oc::CLP& cmd);
    void AggTree_bench(const oc::CLP& cmd);

}