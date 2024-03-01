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
    


        static void concatColumns(
            BinMatrix& dst,
            span<BinMatrix*> cols)
        {
            auto m = cols.size();
            //auto n = cols[0]->rows();
            //auto d0 = dst.data();
            auto e0 = dst.data() + dst.size();

            std::vector<u64>
                offsets(m),
                sizes(m),
                srcSteps(m);
            std::vector<u8*> srcs(m);
            u64 rem = dst.cols();
            for (u64 i = 0; i < m; ++i)
            {
                sizes[i] = oc::divCeil(cols[i]->bitsPerEntry(), 8);
                if (i)
                    offsets[i] = offsets[i - 1] + sizes[i - 1];

                srcs[i] = cols[i]->data();
                srcSteps[i] = cols[i]->mData.cols();
            }

            for (u64 j = 0; j < m; ++j)
            {
                auto n = cols[j]->rows();
                assert(n <= dst.rows());
                auto d0 = dst.data() + offsets[j];

                auto src = srcs[j];
                auto size = sizes[j];
                auto step = srcSteps[j];
                for (u64 i = 0; i < n; ++i)
                {
                    assert(d0 + size <= e0);
                    memcpy(d0, src, size);

                    src += step;
                    d0 += rem;
                }
            }
        }
    

    };

}