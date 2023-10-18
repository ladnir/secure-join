#pragma once

#include "cryptoTools/Common/CLP.h"

void DarkMatter22Prf_plain_test();
void DarkMatter22Prf_util_test();
void DarkMatter22Prf_proto_test(const oc::CLP& cmd);

void DarkMatter32Prf_plain_test();
void DarkMatter32Prf_proto_test(const oc::CLP& cmd);



void AltModPrf_mod3BitDecompostion_test();
void AltModPrf_sampleMod3_test(const oc::CLP& cmd);

void AltModPrf_AMult_test(const oc::CLP& cmd);
void AltModPrf_BMult_test(const oc::CLP& cmd);
void AltModPrf_mod2_test(const oc::CLP& cmd);
void AltModPrf_mod3_test(const oc::CLP& cmd);
void AltModPrf_plain_test();
void AltModPrf_proto_test(const oc::CLP& cmd);

