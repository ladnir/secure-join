#include "UnitTests.h"

#include "LowMCPerm_Test.h"
#include "PaillierPerm_Test.h"
#include "ComposedPerm_Test.h"
#include "AltModPrf_Test.h"
#include "GMW_Test.h"
#include "RadixSort_Test.h"

#include <functional>

#include "AltModPerm_Test.h"
#include "PaillierPerm_Test.h"
#include "ComposedPerm_Test.h"
#include "AdditivePerm_Test.h"
#include "CSVParser_Test.h"

#include "AggTree_Tests.h"
#include "OmJoin_Test.h"
#include "CWrapper_Test.h"
#include "CorGenerator_Test.h"
#include "Average_Test.h"

namespace secJoin_Tests
{
    oc::TestCollection Tests(
        [](oc::TestCollection& t)
        {


            t.add("CorGenerator_Ot_Test                          ", CorGenerator_Ot_Test);
            t.add("CorGenerator_BinOle_Test                      ", CorGenerator_BinOle_Test);
            t.add("CorGenerator_mixed_Test                       ", CorGenerator_mixed_Test);
            
            //t.add("Generator_BinOle_Test                         ", Generator_BinOle_Test);
            //t.add("Generator_Ot_Test                             ", Generator_Ot_Test);
            //t.add("Generator_ArithTriple_Test                    ", Generator_ArithTriple_Test);

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
            t.add("AltModPrf_mod3BitDecompostion_test              ", AltModPrf_mod3BitDecompostion_test);


            t.add("AltModPrf_AMult_test                            ", AltModPrf_AMult_test);
            t.add("AltModPrf_BMult_test                            ", AltModPrf_BMult_test);
            
            t.add("AltModPrf_sampleMod3_test                       ", AltModPrf_sampleMod3_test);
            t.add("AltModPrf_mod3_test                             ", AltModPrf_mod3_test);
            t.add("AltModPrf_mod2_test                             ", AltModPrf_mod2_test);
            t.add("AltModPrf_plain_test                            ", AltModPrf_plain_test);
            t.add("AltModPrf_proto_test                            ", AltModPrf_proto_test);

            t.add("AltModPerm_setup_test                           ", AltModPerm_setup_test);
            t.add("AltModPerm_apply_test                           ", AltModPerm_apply_test);
            t.add("AltModPerm_sharedApply_test                     ", AltModPerm_sharedApply_test);
            t.add("AltModPerm_prepro_test                          ", AltModPerm_prepro_test);

            t.add("LocMC_eval_test                               ", LocMC_eval_test);

            t.add("plaintext_perm_test                           ", plaintext_perm_test);
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
            t.add("OmJoin_join_csv_Test                          ", OmJoin_join_csv_Test);
            t.add("OmJoin_wrapper_join_test                      ", OmJoin_wrapper_join_test);
            t.add("OmJoin_wrapper_avg_test                       ", OmJoin_wrapper_avg_test);

            t.add("Average_concatColumns_Test                    ", Average_concatColumns_Test);
            t.add("Average_getControlBits_Test                   ", Average_getControlBits_Test);
            t.add("Average_avg_Test                              ", Average_avg_Test);
            t.add("Average_avg_csv_Test                          ", Average_avg_csv_Test);
            
            

        });
}
