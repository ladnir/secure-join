#pragma once
#include "secure-join/Aggregate/Average.h"
#include "secure-join/Util/Util.h"
#include "cryptoTools/Common/CLP.h"
#include "secure-join/Util/CSVParser.h"
#include "Wrapper/Defines.h"


void Average_concatColumns_Test();
void Average_getControlBits_Test(const oc::CLP& cmd);
void Average_avg_Test(const oc::CLP& cmd);
void Average_avg_csv_Test(const oc::CLP& cmd);