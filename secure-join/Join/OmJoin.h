#pragma once
#include "secure-join/Join/Table.h"
#include "cryptoTools/Circuit/BetaLibrary.h"
#include "secure-join/GMW/Gmw.h"
#include "secure-join/AggTree/AggTree.h"
#include "secure-join/Sort/RadixSort.h"

namespace secJoin
{
	using SharedTable = Table;
	using SharedColumn = Column;

	struct OmJoin : public oc::TimerAdapter
	{
		bool mInsecurePrint = true, mInsecureMockSubroutines = false;

		// statical security parameter.
		u64 mStatSecParam = 40;

		struct Offset
		{
			u64 mStart = 0, mSize = 0;
			std::string mName;
		};

		static macoro::task<> updateActiveFlag(
			BinMatrix& data,
			BinMatrix& choice,
			BinMatrix& out,
			OleGenerator& ole,
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
		static macoro::task<> getControlBits(
			BinMatrix& data,
			u64 keyByteOffset,
			u64 keyBitCount,
			coproto::Socket& sock,
			BinMatrix& out,
			OleGenerator& ole);

		// concatinate all the columns
		// Then append `numDummies` empty rows to the end.
		static void concatColumns(
			BinMatrix& dst,
			span<BinMatrix*> cols);

		// gather all of the columns from the left table and concatinate them
		// together. Append dummy rows after that. Then add the column of keys
		// to that. So it will look something like:
		//     L | kL
		//     0 | kR
		static void concatColumns(
			ColRef leftJoinCol,
			span<ColRef> selects,
			u64 numDummies,
			BinMatrix& keys,
			u64& keyOffset,
			BinMatrix& out,
			u8 role,
			std::vector<Offset>& offsets);

		// static void appendControlBits(const BinMatrix &controlBits, const BinMatrix &data, BinMatrix &out);

		static void getOutput(
			BinMatrix& data,
			// oc::MatrixView<i64> revealBits,
			span<ColRef> selects,
			ColRef& left,
			SharedTable& out,
			std::vector<Offset>& offsets);

		static AggTree::Operator getDupCircuit();

		// leftJoinCol should be unique
		macoro::task<> join(
			ColRef leftJoinCol,
			ColRef rightJoinCol,
			std::vector<ColRef> selects,
			SharedTable& out,
			oc::PRNG& prng,
			OleGenerator& ole,
			coproto::Socket& sock);
	};

}