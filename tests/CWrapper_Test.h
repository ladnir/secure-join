#pragma once
#include "Wrapper/CppWrapper.h"
#include "cryptoTools/Common/CLP.h"
#include "secure-join/Defines.h"

void OmJoin_wrapper_join_test(const oc::CLP& cmd);
void OmJoin_wrapper_avg_test(const oc::CLP& cmd);
void OmJoin_wrapper_where_test(const oc::CLP& cmd);
void runProtocol(secJoin::WrapperState* visaState, secJoin::WrapperState* bankState, bool verbose);