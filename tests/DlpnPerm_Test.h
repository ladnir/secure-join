#pragma once
#include "secure-join/Perm/DLpnPerm.h"
#include "cryptoTools/Common/CLP.h"
#include "secure-join/Util/Util.h"

void DlpnPerm_setup_test(const oc::CLP& cmd);
void DlpnPerm_apply_test(const oc::CLP& cmd);
void DlpnPerm_sharedApply_test(const oc::CLP& cmd);
void DlpnPerm_prepro_test(const oc::CLP& cmd);
