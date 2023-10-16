#pragma once
#include "cryptoTools/Common/CLP.h"

void perfectShuffle_32_Test();
void perfectShuffle_span_Test();
void perfectShuffle_128_Test();
void perfectShuffle_1024_Test();
void perfectShuffle_sseSpan_Test();

void AggTree_plain_Test();
void AggTree_levelReveal_Test();

void AggTree_dup_pre_levelReveal_Test();
void AggTree_dup_singleSetLeaves_Test();

void AggTree_dup_setLeaves_Test();
void AggTree_dup_upstream_cir_Test(const oc::CLP& cmd);
void AggTree_xor_upstream_Test(const oc::CLP& cmd);
void AggTree_dup_pre_downstream_cir_Test(const oc::CLP& cmd);
void AggTree_dup_downstream_Test(const oc::CLP& cmd);
void AggTree_xor_full_downstream_Test(const oc::CLP& cmd);
void AggTree_xor_Partial_downstream_Test(const oc::CLP& cmd);
void AggTree_dup_pre_full_Test(const oc::CLP& cmd);
void AggTree_xor_pre_full_Test(const oc::CLP& cmd);


//void AggTree_dup_suf_upstream_Test(const oc::CLP& cmd);
//void AggTree_dup_suf_downstream_Test(const oc::CLP& cmd);
//void AggTree_dup_suf_full_Test(const oc::CLP& cmd);
//void AggTree_xor_suf_full_Test(const oc::CLP& cmd);
//
//void AggTree_xor_full_check_Test(const oc::CLP& cmd);
//void AggTree_xor_full_full_Test(const oc::CLP& cmd);
