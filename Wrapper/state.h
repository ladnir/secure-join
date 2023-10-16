#pragma once
#include "coproto/Socket/BufferingSocket.h"
#include "secure-join/Join/Table.h"
#include "secure-join/Join/OmJoin.h"

namespace secJoin
{
    struct WrapperState
    {
        std::vector<ColumnInfo> mLColInfo, mRColInfo;
        std::vector<ColRef> selectCols; // Remove this later
        Table mLTable, mRTable, mShareTable, mOutTable;
        oc::PRNG mPrng;
        OmJoin mJoin;
        CorGenerator mOle;
        coproto::BufferingSocket mSock;
        macoro::eager_task<void> mProtocol;
    };
}