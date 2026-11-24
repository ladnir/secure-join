#pragma once
#include "secure-join/Sort/RadixSort.h"
#include "secure-join/Join/Table.h"
#include "secure-join/Join/OmJoin.h"
#include "cryptoTools/Circuit/BetaLibrary.h"
#include "secure-join/Util/Util.h"
#include "libOTe/Tools/LinearCode.h"
#include "Extract.h"

namespace secJoin
{
	// This protocol perform a group by operation on the input table.
    // Currently, the reduce operation is hard coded to be addition
	// but this could be generalized in the future.
    // Additionally, a extra "count" column is added that records
	// the number of rows that were grouped together.
    struct GroupBy
    {
        bool mInsecurePrint = false, mInsecureMockSubroutines = false;

        // statical security parameter.
		u64 mStatSecParam = 40;

		// the subprotocol that sorts the keys.
		RadixSort mSort;

		// the sorting permutation.
		AltModComposedPerm mPerm;

		// the subprotocol that will perform the copies.
		AggTree mAggTree;

		// the subprotocol that will compute the control bits.
		Gmw mControlBitGmw;

		// the subprotocol that will compute which output rows are active.
		Gmw mUpdateActiveFlagGmw;

		// the offset of the columns in the data matrix.
		std::vector<OmJoin::Offset> mOffsets;

		u64 mPartyIdx = -1;


        std::optional<Extract> mRemoveInactive;

        // initialize the protocol. The table will be
		// grouped by the `groupByCol` column. The `avgCol`
		// columns will be added together. The `ole` object
		// is used to generate the correlated randomness.
        void init(
            ColRef groupByCol,
            std::vector<ColRef> avgCol,
            CorGenerator& ole,
            bool removeInactiveFlag = false);


		// This is the main protocol function. It will group the input table
		// by the `groupByCol` column. The `avgCol` columns will be added together.
		// The output is written to the `out` table.
        macoro::task<> groupBy(
            ColRef groupByCol,
            std::vector<ColRef> avgCol,
            SharedTable& out,
            oc::PRNG& prng,
            coproto::Socket& sock);


        // internal functions
        // -----------------------------------------

        void extractKeyInfo(
            BinMatrix& data,
            BinMatrix& grpByData,
            BinMatrix& actFlag,
            BinMatrix& compressKeys,
            const std::vector<OmJoin::Offset>& offsets);

        void loadKeys(
            ColRef groupByCol,
            std::vector<u8>& actFlagVec,
            BinMatrix& compressKeys, block codeSeed);

        void concatColumns(
            ColRef groupByCol,
            std::vector<ColRef> avgCol,
            std::vector<u8>& actFlag,
            BinMatrix& compressKeys,
            BinMatrix& ret);

        void concatColumns(
            BinMatrix& data,
            BinMatrix& groupByData,
            BinMatrix& actFlag,
            BinMatrix& ret);


        macoro::task<> getControlBits(
            BinMatrix& keys,
            coproto::Socket& sock,
            BinMatrix& out);

        static AggTree::Operator getAddCircuit(
            std::vector<OmJoin::Offset>& offsets,
            oc::BetaLibrary::Optimized op);

        void getOutput(
            SharedTable& out,
            std::vector<ColRef> avgCol,
            ColRef groupByCol,
            BinMatrix& data,
            std::vector<OmJoin::Offset>& offsets);


        macoro::task<> updateActiveFlag(
            BinMatrix& actFlag,
            BinMatrix& choice,
            BinMatrix& out,
            coproto::Socket& sock);


    };


    //Table average(
    //    ColRef groupByCol,
    //    std::vector<ColRef> avgCol,
    //    bool removeInactiveFlag = false);


    void populateOutTable(
        Table& out,
        std::vector<ColRef> avgCol,
        ColRef groupByCol,
        u64 nOutRows);
}