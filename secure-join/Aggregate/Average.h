#pragma once
#include "secure-join/Sort/RadixSort.h"
#include "secure-join/Join/Table.h"
#include "secure-join/Join/OmJoin.h"
#include "cryptoTools/Circuit/BetaLibrary.h"
#include "secure-join/Util/Util.h"

namespace secJoin
{
    using SharedTable = Table;
	using SharedColumn = Column;
    struct Average
    {
        bool mInsecurePrint = false, mInsecureMockSubroutines = false;
        oc::BetaLibrary::Optimized op = oc::BetaLibrary::Optimized::Depth;


        static void concatColumns(ColRef groupByCol, std::vector<ColRef> average, BinMatrix& ret,
            std::vector<OmJoin::Offset>& offsets, CorGenerator& ole);

        macoro::task<> avg(ColRef groupByCol, std::vector<ColRef> avgCol, SharedTable& out,
            oc::PRNG& prng, CorGenerator& ole, coproto::Socket& sock);

        static macoro::task<> getControlBits(BinMatrix& keys, coproto::Socket& sock, BinMatrix& out,
        CorGenerator& ole);

        static AggTree::Operator getAddCircuit(std::vector<OmJoin::Offset>& offsets, 
            oc::BetaLibrary::Optimized op);

        static void getOutput(SharedTable& out, std::vector<ColRef> avgCol, ColRef groupByCol,
            BinMatrix& keys, BinMatrix& data, BinMatrix& controlBits, 
            std::vector<OmJoin::Offset>& offsets, std::vector<OmJoin::Offset>& keyOffsets);

        static  macoro::task<> updateActiveFlag(BinMatrix& data, BinMatrix& choice, 
            BinMatrix& out, CorGenerator& ole, coproto::Socket& sock);
    
    

    };

}