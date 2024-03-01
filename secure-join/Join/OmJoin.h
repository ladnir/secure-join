#pragma once
#include "secure-join/Join/Table.h"
#include "cryptoTools/Circuit/BetaLibrary.h"
#include "secure-join/GMW/Gmw.h"
#include "secure-join/AggTree/AggTree.h"
#include "secure-join/Sort/RadixSort.h"

namespace secJoin
{
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

		u64 mKeyIndex = -1;

		u64 mPartyIdx = -1;

		void init(
			JoinQuerySchema schema,
			CorGenerator& ole);


		macoro::task<> updateActiveFlag(
			BinMatrix& data,
			BinMatrix& choice,
			BinMatrix& out,
			u64 flagBitIndex,
			PRNG&prng,
			coproto::Socket& sock);


		// output a combined table that has the leftColumn
		// concatenated with the rightColumn (doubling the
		// number of rows). THe left column will have a
		// zero appended as its LSB while the right gets
		// a one appended.
		BinMatrix loadKeys(
			ColRef leftJoinCol,
			ColRef rightJoinCol);

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
			BinMatrix& out,
			PRNG& prng);

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

		// static void appendControlBits(const BinMatrix &controlBits, const BinMatrix &data, BinMatrix &out);

		static void getOutput(
			BinMatrix& data,
			span<ColRef> selects,
			ColRef& left,
			Table& out,
			std::vector<Offset>& offsets);

		static AggTree::Operator getDupCircuit();

		static macoro::task<> print(
			const BinMatrix& data,
			const BinMatrix& control,
			coproto::Socket& sock,
			int role,
			std::string name,
			std::vector<OmJoin::Offset>& offsets);

		// leftJoinCol should be unique
		macoro::task<> join(
			JoinQuery query,
			Table& out,
			PRNG& prng,
			coproto::Socket& sock);
	};

}