#pragma once
#include "cryptoTools/Common/CLP.h"

void plaintext_perm_test(const oc::CLP& cmd);
void ComposedPerm_apply_test(const oc::CLP& cmd);
void ComposedPerm_compose_test(const oc::CLP& cmd);
void ComposedPerm_derandomize_test(const oc::CLP& cmd);

//void ComposedPerm_AltModGen_test(const oc::CLP& cmd);
//void ComposedPerm_prepro_test(const oc::CLP& cmd);
// void check_results(Matrix<u8> &x, std::array<Matrix<u8>, 2> &sout, std::vector<u64> &pi, bool invPerm);
//void check_results(Matrix<u8> &x, std::array<Matrix<u8>, 2> &sout, std::vector<u64> &pi0, std::vector<u64> &pi1);
//std::array<Matrix<u8>, 2> share(Matrix<u8> v, PRNG& prng);