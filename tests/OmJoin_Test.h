#pragma once

#include "cryptoTools/Common/CLP.h"

void OmJoin_loadKeys_Test();
void OmJoin_getControlBits_Test(const oc::CLP&);
void OmJoin_concatColumns_Test();
void OmJoin_getOutput_Test();
void OmJoin_join_Test(const oc::CLP&);
void OmJoin_join_BigKey_Test(const oc::CLP&);
void OmJoin_join_Reveal_Test(const oc::CLP&);
void OmJoin_join_round_Test(const oc::CLP&);
void OmJoin_join_csv_Test(const oc::CLP&);