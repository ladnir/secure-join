#include "UnitTests.h"

#include "LowMCPerm_Test.h"
#include "PaillierPerm_Test.h"
#include "ComposedPerm_Test.h"
#include "DarkMatterPrf_Test.h"
#include "OleGenerator_Test.h"
#include "GMW_Test.h"
#include "RadixSort_Test.h"

#include <functional>

#include "DlpnPerm_Test.h"
#include "PaillierPerm_Test.h"
#include "ComposedPerm_Test.h"
#include "AdditivePerm_Test.h"
#include "CSVParser_Test.h"

#include "AggTree_Tests.h"
#include "OmJoin_Test.h"
#include "CWrapper_Test.h"
#include "CorGenerator_Test.h"

namespace secJoin_Tests
{
    oc::TestCollection Tests(
        [](oc::TestCollection& t)
        {


            t.add("CorGenerator_Ot_Test                          ", CorGenerator_Ot_Test);
            t.add("CorGenerator_BinOle_Test                      ", CorGenerator_BinOle_Test);
            t.add("Generator_BinOle_Test                         ", Generator_BinOle_Test);
            t.add("Generator_Ot_Test                             ", Generator_Ot_Test);
            t.add("Generator_ArithTriple_Test                    ", Generator_ArithTriple_Test);

            t.add("Gmw_half_test                                 ", Gmw_half_test);
            t.add("Gmw_basic_test                                ", Gmw_basic_test);
            t.add("Gmw_inOut_test                                ", Gmw_inOut_test);
            t.add("Gmw_xor_test                                  ", Gmw_xor_test);
            t.add("Gmw_and_test                                  ", Gmw_and_test);
            t.add("Gmw_na_and_test                               ", Gmw_na_and_test);
            t.add("Gmw_or_test                                   ", Gmw_or_test);
            t.add("Gmw_xor_and_test                              ", Gmw_xor_and_test);
            t.add("Gmw_aa_na_and_test                            ", Gmw_aa_na_and_test);
            t.add("Gmw_add_test                                  ", Gmw_add_test);
            t.add("Gmw_noLevelize_test                           ", Gmw_noLevelize_test);

            // t.add("PaillierPerm_basic_test                       ", PaillierPerm_basic_test);
            t.add("DLpnPrf_mod3BitDecompostion_test              ", DLpnPrf_mod3BitDecompostion_test);


            t.add("DLpnPrf_BMult_test                            ", DLpnPrf_BMult_test);
            t.add("DLpnPrf_mod3_test                             ", DLpnPrf_mod3_test);
            t.add("DLpnPrf_mod2_test                             ", DLpnPrf_mod2_test);
            t.add("DLpnPrf_plain_test                            ", DLpnPrf_plain_test);
            t.add("DLpnPrf_proto_test                            ", DLpnPrf_proto_test);

            t.add("DlpnPerm_setup_test                           ", DlpnPerm_setup_test);
            t.add("DlpnPerm_apply_test                           ", DlpnPerm_apply_test);
            t.add("DlpnPerm_sharedApply_test                     ", DlpnPerm_sharedApply_test);
            t.add("DlpnPerm_prepro_test                          ", DlpnPerm_prepro_test);

            t.add("LocMC_eval_test                               ", LocMC_eval_test);

            t.add("LowMCPerm_perm_test                           ", LowMCPerm_perm_test);
            t.add("LowMCPerm_secret_shared_input_perm_test       ", LowMCPerm_secret_shared_input_perm_test);
            t.add("ComposedPerm_basic_test                       ", ComposedPerm_basic_test);
            t.add("ComposedPerm_shared_test                      ", ComposedPerm_shared_test);
            t.add("ComposedPerm_prepro_test                      ", ComposedPerm_prepro_test);
            t.add("AdditivePerm_xor_test                         ", AdditivePerm_xor_test);
            t.add("AdditivePerm_prepro_test                      ", AdditivePerm_prepro_test);

            t.add("RadixSort_aggregateSum_test                   ", RadixSort_aggregateSum_test);
            t.add("RadixSort_oneHot_test                         ", RadixSort_oneHot_test);
            t.add("RadixSort_bitInjection_test                   ", RadixSort_bitInjection_test);
            t.add("RadixSort_genValMasks2_test                   ", RadixSort_genValMasks2_test);
            t.add("RadixSort_hadamardSum_test                    ", RadixSort_hadamardSum_test);
            t.add("RadixSort_genBitPerm_test                     ", RadixSort_genBitPerm_test);
            t.add("RadixSort_genPerm_test                        ", RadixSort_genPerm_test);
            t.add("RadixSort_mock_test                           ", RadixSort_mock_test);


            t.add("secret_share_table_test                       ", secret_share_table_test);
            t.add("table_write_csv_test                          ", table_write_csv_test);


            t.add("AggTree_plain_Test                            ", AggTree_plain_Test);
            t.add("AggTree_levelReveal_Test                      ", AggTree_levelReveal_Test);
            t.add("AggTree_dup_pre_levelReveal_Test              ", AggTree_dup_pre_levelReveal_Test);
            t.add("AggTree_dup_singleSetLeaves_Test              ", AggTree_dup_singleSetLeaves_Test);
            t.add("AggTree_dup_setLeaves_Test                    ", AggTree_dup_setLeaves_Test);
            t.add("AggTree_dup_upstream_cir_Test                 ", AggTree_dup_upstream_cir_Test);
            t.add("AggTree_xor_upstream_Test                     ", AggTree_xor_upstream_Test);
            t.add("AggTree_dup_pre_downstream_cir_Test           ", AggTree_dup_pre_downstream_cir_Test);
            t.add("AggTree_dup_downstream_Test                   ", AggTree_dup_downstream_Test);
            t.add("AggTree_xor_full_downstream_Test              ", AggTree_xor_full_downstream_Test);
            t.add("AggTree_xor_Partial_downstream_Test           ", AggTree_xor_Partial_downstream_Test);
            t.add("AggTree_dup_pre_full_Test                     ", AggTree_dup_pre_full_Test);
            t.add("AggTree_xor_pre_full_Test                     ", AggTree_xor_pre_full_Test);

            t.add("OmJoin_loadKeys_Test                          ", OmJoin_loadKeys_Test);
            t.add("OmJoin_getControlBits_Test                    ", OmJoin_getControlBits_Test);
            t.add("OmJoin_concatColumns_Test                     ", OmJoin_concatColumns_Test);
            t.add("OmJoin_getOutput_Test                         ", OmJoin_getOutput_Test);
            t.add("OmJoin_join_Test                              ", OmJoin_join_Test);
            t.add("OmJoin_join_BigKey_Test                       ", OmJoin_join_BigKey_Test);
            t.add("OmJoin_join_Reveal_Test                       ", OmJoin_join_Reveal_Test);
            t.add("OmJoin_join_round_Test                        ", OmJoin_join_round_Test);
            t.add("OmJoin_wrapper_test                           ", OmJoin_wrapper_test);

        });
}
