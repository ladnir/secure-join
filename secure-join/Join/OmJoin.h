#pragma once
#include "secure-join/Join/Table.h"
#include "cryptoTools/Circuit/BetaLibrary.h"
#include "secure-join/GMW/Gmw.h"
#include "secure-join/AggTree/AggTree.h"
#include "secure-join/Sort/RadixSort.h"
#include "secure-join/Util/Util.h"
#include "secure-join/TableOps/Extract.h"
#include "secure-join/Join/JoinQuery.h"

namespace secJoin
{
    // Public randomness sampled after both semi-honest parties have fixed their inputs.
    macoro::task<> sampleCompressionSeed(u64 party, PRNG& prng, coproto::Socket& sock, block& seed);

	// one to many secure join protocol
	struct OmJoin : public oc::TimerAdapter
	{
		// we will pack columns into a matrix `data`.
		// offset tracks where each column starts in the data matrix.
		struct Offset
		{
			u64 mStart = 0, mSize = 0;
			std::string mName;
		};

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

		// the number of bytes that will be stored per for of `data`.
		u64 mDataBitsPerEntry = 0;

		// the offset of the columns in the data matrix.
		std::vector<Offset> mOffsets;

		u64 mPartyIdx = ~0ull;

		// subprotocol to remove dummy rows.
        std::optional<Extract> mRemoveInactive;

		// initialize the protocol with the description of join.
		// leftJoinCol should be unique
		void init(
			JoinQuerySchema schema,
			CorGenerator& ole,
			bool remDummiesFlag = false);


		// perform the actual join protocool. 
		macoro::task<> join(
			JoinQuery query,
			Table& out,
			PRNG& prng,
			coproto::Socket& sock);

		// updates which rows have a match after the agg tree duplicates values.
		macoro::task<> updateActiveFlag(
			BinMatrix& data,
			BinMatrix& choice,
			BinMatrix& out,
			u64 flagBitIndex,
			coproto::Socket& sock);

		// output a combined table that has the leftColumn
		// concatenated with the rightColumn (doubling the
		// number of rows). THe left column will have a
		// zero appended as its LSB while the right gets
		// a one appended.
		BinMatrix loadKeys(
			ColRef leftJoinCol,
			ColRef rightJoinCol, block codeSeed);

		// this circuit compares two inputs for equality with the exception that
		// the first bit is ignored.
		static oc::BetaCircuit getControlBitsCircuit(u64 bitCount);

		// compare each key with the key of the previous row.
		// The keys are stored starting at data[i, keyOffset] and
		// going to the end of each row i.
		macoro::task<> getControlBits(
			BinMatrix& data,
			u64 keyByteOffset,
			u64 keyBitCount,
			coproto::Socket& sock,
			BinMatrix& out);

		// concatinate all the columns
		// Then append `numDummies` empty rows to the end.
		static void concatColumns(
			BinMatrix& dst,
			span<BinMatrix*> cols,
			span<Offset> offsets);

		// gather all of the columns from the left table and concatinate them
		// together. Append dummy rows after that. Then add the column of keys
		// to that. So it will look something like:
		//     L | kL
		//     0 | kR
		void concatColumns(
			ColRef leftJoinCol,
			span<ColRef> selects,
			BinMatrix& keys,
			BinMatrix& out,
			u8 role);


		// extracts the rows from the unpermuted combined table that correspond 
		// to the output.
		static void getOutput(
			BinMatrix& data,
			span<ColRef> selects,
			ColRef& left,
			Table& out,
			std::vector<Offset>& offsets);

		// extracts the rows from the unpermuted combined table that correspond 
		// to the output.
		static  macoro::task<> getOutput(
			BinMatrix& data,
			span<ColRef> selects,
			ColRef& left,
			Table& out,
			std::vector<Offset>& offsets,
			CorGenerator& ole,
			coproto::Socket& sock,
			oc::PRNG& prng,
			bool securePerm,
			Perm& randPerm);


		// returns the circuit that duplicates that left argumement.
		static AggTree::Operator getDupCircuit();

		// prints the combined table.
		static macoro::task<> print(
			const BinMatrix& data,
			const BinMatrix& control,
			coproto::Socket& sock,
			int role,
			std::string name,
			std::vector<OmJoin::Offset>& offsets);

	};

}