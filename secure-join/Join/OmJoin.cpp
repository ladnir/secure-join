#include "OmJoin.h"
#include "libOTe/Tools/LinearCode.h"

namespace secJoin
{

	macoro::task<> sampleCompressionSeed(u64 party, PRNG& prng, coproto::Socket& sock, block& seed)
	{
		if (party == 0)
		{
			seed = prng.get<block>();
			co_await sock.send(coproto::copy(seed));
		}
		else
			co_await sock.recv(seed);
	}

	// output a combined table that has the leftColumn
	// concatenated with the rightColumn (doubling the
	// number of rows). THe left column will have a
	// zero appended as its LSB while the right gets
	// a one appended.
	BinMatrix OmJoin::loadKeys(
		ColRef leftJoinCol,
		ColRef rightJoinCol, block codeSeed)
	{
		auto rows0 = leftJoinCol.mCol.mData.rows();
		auto rows1 = rightJoinCol.mCol.mData.rows();
		auto compressesSize = mStatSecParam + oc::log2ceil(rows0) + oc::log2ceil(rows1);
		auto bits = leftJoinCol.mCol.getBitCount();

		if (leftJoinCol.mCol.getBitCount() != rightJoinCol.mCol.getBitCount())
			throw RTE_LOC;

		if (leftJoinCol.mCol.getBitCount() <= compressesSize)
		{

			auto size0 = leftJoinCol.mCol.mData.size();
			BinMatrix keys(rows0 + rows1, bits);

			auto k = span<u8>(keys);
			auto d0 = k.subspan(0, size0);
			auto d1 = k.subspan(size0);
			assert(d1.data() + d1.size() == k.data() + k.size());
			osuCrypto::copyBytes(d0, leftJoinCol.mCol.mData);
			osuCrypto::copyBytes(d1, rightJoinCol.mCol.mData);
			return keys;

		}
		else
		{
			PRNG prng(codeSeed);
			oc::LinearCode code;
			code.random(prng, bits, compressesSize);

			BinMatrix keys(rows0 + rows1, compressesSize);

			for (u64 i = 0; i < rows0; ++i)
				code.encode(
					leftJoinCol.mCol.mData.data(i),
					keys.data(i));

			for (u64 i = 0, j = rows0; i < rows1; ++i, ++j)
				code.encode(
					rightJoinCol.mCol.mData.data(i),
					keys.data(j));

			return keys;
		}
	}

	// this circuit compares two inputs for equality with the exception that
	// the first bit is ignored.
	oc::BetaCircuit OmJoin::getControlBitsCircuit(u64 bitCount)
	{
		oc::BetaCircuit cd;
		oc::BetaBundle a1(bitCount);
		oc::BetaBundle a2(bitCount);
		oc::BetaBundle out(1);
		auto bits = a1.mWires.size();
		BetaBundle temp(bits);

		cd.addInputBundle(a1);
		cd.addInputBundle(a2);
		cd.addOutputBundle(out);

		if (bits == 1)
			temp[0] = out[0];
		else
			cd.addTempWireBundle(temp);

		for (u64 i = 0; i < bits; ++i)
		{
			cd.addGate(a1.mWires[i], a2.mWires[i],
				oc::GateType::Nxor, temp.mWires[i]);
		}

		auto levels = oc::log2ceil(bits);
		for (u64 i = 0; i < levels; ++i)
		{
			auto step = 1ull << i;
			auto size = bits / 2 / step;
			BetaBundle temp2(size);
			if (size == 1)
				temp2[0] = out[0];
			else
				cd.addTempWireBundle(temp2);

			for (u64 j = 0; j < size; ++j)
			{
				cd.addGate(
					temp[2 * j + 0],
					temp[2 * j + 1],
					oc::GateType::And,
					temp2[j]
				);
			}

			temp = std::move(temp2);
		}
		return cd;
	}

	macoro::task<> OmJoin::getControlBits(
		BinMatrix& data,
		u64 keyByteOffset,
		u64 keyBitCount,
		coproto::Socket& sock,
		BinMatrix& out)
	{
		auto sKeys = BinMatrix{};
		auto n = u64{};
		auto keyByteSize = u64{};

		n = data.numEntries();
		keyByteSize = oc::divCeil(keyBitCount, 8);
		sKeys.resize(data.rows() + 1, keyBitCount);
		for (u64 i = 0; i < n; ++i)
		{
			osuCrypto::copyBytes(sKeys[i+1], data[i].subspan(keyByteOffset, keyByteSize));
		}

		mControlBitGmw.setInput(0, sKeys.subMatrix(0, n));
		mControlBitGmw.setInput(1, sKeys.subMatrix(1, n));

		co_await mControlBitGmw.run(sock);

		out.resize(n, 1);
		mControlBitGmw.getOutput(0, out);
		out.mData(0) = 0;

	}

	void OmJoin::concatColumns(
		BinMatrix& dst,
		span<BinMatrix*> cols,
		span<Offset> offsets)
	{
		for (u64 j = 0; j < cols.size(); ++j)
		{
			if (cols[j] == nullptr)
				continue;

			auto n = cols[j]->rows();
			if (!n)
				continue;
			assert(n <= dst.rows());
			assert(offsets[j].mStart % 8 == 0);

			auto d0 = span<u8>(dst).subspan(offsets[j].mStart / 8);

			auto src = span<u8>(*cols[j]);
			auto size = cols[j]->bytesPerEntry();
			assert(divCeil(offsets[j].mSize, 8) == size);
			for (u64 i = 1; i < n; ++i)
			{
				osuCrypto::copyBytes(d0.subspan(0, size), src.subspan(0, size));
				src = src.subspan(size);
				d0 = d0.subspan(dst.cols());
			}
			osuCrypto::copyBytes(d0.subspan(0, size), src.subspan(0, size));

		}
	}

	// concatinate all the columns in `select` that are part of the left table.
	// Then append `numDummies` empty rows to the end.
	void OmJoin::concatColumns(
		ColRef leftJoinCol,
		span<ColRef> selects,
		BinMatrix& keys,
		BinMatrix& ret,
		u8 role
	)
	{
		u64 m = selects.size();
		u64 n0 = leftJoinCol.mCol.rows();

		std::vector<BinMatrix*> left;

		for (u64 i = 0; i < m; ++i)
		{
			if (&leftJoinCol.mTable == &selects[i].mTable)
			{
				assert(oc::divCeil(selects[i].mCol.getBitCount(), 8) == selects[i].mCol.getByteCount());
				assert(selects[i].mCol.rows() == n0);

				left.emplace_back(&selects[i].mCol.mData);
			}
		}

		left.emplace_back(nullptr);
		left.emplace_back(&keys);

		ret.resize(keys.rows(), mDataBitsPerEntry);
		concatColumns(ret, left, mOffsets);

		if (role)
		{
			//auto flagBit = mDataBitsPerEntry - 1;;
			auto flagByte = mOffsets[mOffsets.size() - 2].mStart / 8;
			for (u64 i = 0; i < n0; ++i)
			{
				ret(i, flagByte) = 1;
			}
		}
	}

	// the aggTree and unpermute setps gives us `data` which looks like
	//     L
	//     L'
	// where L' are the matching rows copied from L for each row of R.
	// That is, L' || R is the result we want.
	// getOutput(...) unpack L' in `data` and write the result to `out`.
	void OmJoin::getOutput(
		BinMatrix& data,
		span<ColRef> selects,
		ColRef& left,
		Table& out,
		std::vector<Offset>& offsets)
	{
		u64 nL = left.mCol.rows();
		u64 nR = data.rows() - nL;
		u64 m = selects.size();

		std::vector<u64> sizes; //, dSteps(m);
		std::vector<span<u8>> srcs, dsts;

		out.mColumns.resize(selects.size());
		// std::cout << "start row " << nL << std::endl;
		// u64 leftOffset = 0;
		u64 rightOffset = 0, k = 0;
		for (u64 i = 0; i < m; ++i)
		{
			out[i].mCol.mName = selects[i].mCol.mName;
			out[i].mCol.mBitCount = selects[i].mCol.mBitCount;
			out[i].mCol.mType = selects[i].mCol.mType;
			auto& oData = out[i].mCol.mData;

			if (&left.mTable == &selects[i].mTable)
			{
				oData.resize(nR, selects[i].mCol.getBitCount());

				sizes.push_back(oData.bytesPerEntry());
				dsts.push_back(oData);
				srcs.push_back(span<u8>(&data(nL, rightOffset), data.data() + data.size()));
				rightOffset += sizes.back();

				assert(rightOffset == oc::divCeil(offsets[k].mStart + offsets[k].mSize, 8));
				++k;
			}
			else
			{
				oData = selects[i].mCol.mData;
			}
		}
		assert(rightOffset == data.bytesPerEntry() - 1);
		assert(rightOffset * 8 == offsets[k].mStart);
		assert(8 == offsets[k].mSize);
		out.mIsActive.resize(nR);
		sizes.push_back(1);
		dsts.push_back(out.mIsActive);
		srcs.push_back(span<u8>(&data(nL, rightOffset), data.data() + data.size()));

		auto srcStep = data.cols();
		for (u64 i = 0; i < nR; ++i)
		{
			for (u64 j = 0; j < sizes.size(); ++j)
			{
				if (i)
				{
					dsts[j] = dsts[j].subspan(sizes[j]);
					srcs[j] = srcs[j].subspan(srcStep);
				}
				osuCrypto::copyBytes(dsts[j].subspan(0, sizes[j]), srcs[j].subspan(0, sizes[j]));
			}
		}
	}


	AggTree::Operator OmJoin::getDupCircuit()
	{
		return [](
			oc::BetaCircuit& c,
			const oc::BetaBundle& left,
			const oc::BetaBundle& right,
			oc::BetaBundle& out)
			{
				for (u64 i = 0; i < left.size(); ++i)
					c.addCopy(left[i], out[i]);
			};
	}

	macoro::task<> OmJoin::print(
		const BinMatrix& data,
		const BinMatrix& control,
		coproto::Socket& sock,
		int role,
		std::string name,
		std::vector<OmJoin::Offset>& offsets)
	{
		auto D = BinMatrix{};
		auto C = BinMatrix{};

		if (role)
		{
			co_await sock.send(data);

			if (control.size())
				co_await sock.send(control);
		}
		else
		{
			D.resize(data.numEntries(), data.bytesPerEntry() * 8);
			co_await sock.recv(D);
			if (control.size())
				C.resize(control.numEntries(), control.bitsPerEntry());
			if (control.size())
				co_await sock.recv(C);


			for (u64 i = 0; i < D.size(); ++i)
				D(i) ^= data(i);
			for (u64 i = 0; i < C.size(); ++i)
				C(i) ^= control(i);

			std::cout << name << std::endl << "        ";
			for (auto o : offsets)
				std::cout << o.mName << "  ";
			std::cout << std::endl;
			oc::BitVector bv;
			for (u64 i = 0; i < D.numEntries(); ++i)
			{
				std::cout << i << ": " << (C.size() ? (int)C(i) : -1) << " ~ ";// << hex(D[i]) << std::endl;
				for (auto o : offsets)
				{
					assert(D.bitsPerEntry() >= o.mSize + o.mStart);
					bv.resize(0);
					bv.append(D[i].data(), o.mSize, o.mStart);
					trimSpan(bv.getSpan<u8>(), bv.size());
					std::cout << bv.hex() << " ";
					///bv.resize(o.mSize);
					//b/v.getSpan<u8>().back() = 0;
				}
				std::cout << std::endl;
			}
		}
	}

	// Active Flag = LFlag & Controlbits
	macoro::task<> OmJoin::updateActiveFlag(
		BinMatrix& data,
		BinMatrix& choice,
		BinMatrix& out,
		u64 flagBitIndex,
		coproto::Socket& sock)
	{
		auto temp = BinMatrix{};
		auto offset = u64{};

		assert(flagBitIndex % 8 == 0);
		offset = flagBitIndex / 8;

		temp.resize(data.rows(), 1);
		for (u64 i = 0; i < data.rows(); ++i)
			temp(i) = data(i, offset);

		mUpdateActiveFlagGmw.setInput(0, choice);
		mUpdateActiveFlagGmw.setInput(1, temp);


		co_await mUpdateActiveFlagGmw.run(sock);

		mUpdateActiveFlagGmw.getOutput(0, temp);
		mUpdateActiveFlagGmw = {};


		out.resize(data.rows(), data.bitsPerEntry());
		for (u64 i = 0; i < data.rows(); ++i)
		{
			osuCrypto::copyBytes(out[i].subspan(0, offset), data[i].subspan(0, offset));
			out(i, offset) = temp(i);
		}
	}

	void appendChoiceBit(BinMatrix& data, BinMatrix& choice, BinMatrix& out)
	{
		auto n = data.rows();
		auto m = data.bitsPerEntry();
		auto m8 = oc::divCeil(m, 8);
		out.resize(n, m + 8);
		for (u64 i = 0; i < n; ++i)
		{
			osuCrypto::copyBytes(out[i].subspan(0, m8), data[i].subspan(0, m8));
			out(i, m8) = choice(i);
		}
	}

	void OmJoin::init(
		JoinQuerySchema schema,
		CorGenerator& ole,
		bool remDummiesFlag)
	{
		u64 rows = schema.mLeftSize + schema.mRightSize;

		auto keySize = std::min<u64>(
			schema.mKey.mBitCount,
			mStatSecParam + oc::log2ceil(schema.mLeftSize) + oc::log2ceil(schema.mRightSize));

		mPartyIdx = ole.partyIdx();
		mDataBitsPerEntry = 0;
		

		mOffsets.clear();
		mOffsets.reserve(schema.mSelect.size() + 1);
		for (u64 i = 0; i < schema.mSelect.size(); ++i)
		{
			// copy all of the left columns except the key
			if (schema.mSelect[i].mIsLeftColumn)
			{
				auto bytes = oc::divCeil(schema.mSelect[i].getBitCount(), 8);
				assert(bytes == schema.mSelect[i].getByteCount());
				mOffsets.emplace_back(
					Offset{
						mDataBitsPerEntry,
						schema.mSelect[i].getBitCount(),
						schema.mSelect[i].name() });

				mDataBitsPerEntry += bytes * 8;
			}
		}

		mOffsets.emplace_back(Offset{ mDataBitsPerEntry, 8, "LFlag" });
		mDataBitsPerEntry += 8;

		mOffsets.emplace_back(Offset{ mDataBitsPerEntry, keySize, "key*" });
		const u64 payloadBits = mDataBitsPerEntry;
		mDataBitsPerEntry += oc::roundUpTo(keySize, 8);

		//std::cout << "sort " << rows << " rows " << keySize << " bits " << std::endl;
		mSort.init(mPartyIdx, rows, keySize, ole);

		// in the forward direction we will permute the keys, a flag, 
		// and all of the select columns of the left table. In the 
		// backwards direction, we will unpermute the left table select
		// columns. Therefore, in total we will permute:
		u64 permForward = oc::divCeil(mDataBitsPerEntry, 8) + sizeof(u32);
		u64 permBackward = payloadBits / 8;

		mPerm.init(mPartyIdx, rows, permForward + permBackward, ole);

		mControlBitGmw.init(rows, getControlBitsCircuit(keySize), ole);

		mAggTree.init(rows, payloadBits, AggTreeType::Prefix, getDupCircuit(), ole);

		auto cir = *oc::BetaLibrary{}.int_int_bitwiseAnd(1, 1, 1);
		mUpdateActiveFlagGmw.init(rows, cir, ole);

		if (remDummiesFlag)
		{
			mRemoveInactive.emplace(Extract{});

			u64 dateBytesPerEntry = 1; // active flag
			for (u64 i = 0; i < schema.mSelect.size(); ++i)
				dateBytesPerEntry += schema.mSelect[i].getByteCount();

			// Here rows would be only elements in right table bcoz
			// after the unsort entries from the left table are removed
			mRemoveInactive->init(schema.mRightSize, dateBytesPerEntry, ole);
		}

	}

	// leftJoinCol should be unique
	macoro::task<> OmJoin::join(
		JoinQuery query,
		Table& out,
		PRNG& prng,
		coproto::Socket& sock)
	{

		auto keys = BinMatrix{};
		auto sPerm = AdditivePerm{};
		auto perm = ComposedPerm{};
		auto controlBits = BinMatrix{};
		auto data = BinMatrix{};
		auto temp = BinMatrix{};
		auto offsets_ = std::vector<Offset>{};
		auto prepro = macoro::eager_task<>{};

		setTimePoint("start");

		// left keys kL followed by the right keys kR
		block codeSeed = oc::ZeroBlock;
		if (query.mLeftKey.mCol.getBitCount() > mOffsets.back().mSize)
		{
			co_await sampleCompressionSeed(mPartyIdx, prng, sock, codeSeed);
		}
		keys = loadKeys(query.mLeftKey, query.mRightKey, codeSeed);
		setTimePoint("load");

		if (mInsecurePrint)
		{
			offsets_ = { Offset{0,keys.bitsPerEntry(), "key"} };
			co_await print(keys, controlBits, sock, mPartyIdx, "keys", offsets_);
		}

		mSort.preprocess();
		prepro = mSort.genPrePerm(sock, prng) | macoro::make_eager();

		// get the stable sorting permutation sPerm
		co_await mSort.genPerm(keys, sPerm, sock, prng);
		setTimePoint("sort");

		//co_await sPerm.validate(sock));

		mPerm.preprocess();
		co_await prepro;

		// gather all of the columns from the left table and concatinate them
		// together. Append dummy rows after that. Then add the column of keys
		// to that. So it will look something like:
		//     L | kL | 1
		//     0 | kR | 0
		concatColumns(query.mLeftKey, query.mSelect, keys, data, mPartyIdx);
		setTimePoint("concat");
		keys.mData = {};


		if (mInsecurePrint)
			co_await print(data, controlBits, sock, mPartyIdx, "preSort", mOffsets);

		// Apply the sortin permutation. What you end up with are the keys
		// in sorted order and the rows of L also in sorted order.
		temp.resize(data.numEntries(), data.bitsPerEntry());

		co_await mPerm.generate(sock, prng, data.rows(), perm);
		setTimePoint("perm cor gen");

		co_await perm.derandomize(sPerm, sock);
		setTimePoint("perm cor derand");

		co_await perm.apply<u8>(PermOp::Inverse, data, temp, sock);
		std::swap(data, temp);
		setTimePoint("applyInv-sort");

		if (mInsecurePrint)
			co_await print(data, controlBits, sock, mPartyIdx, "sort", mOffsets);

		// compare adjacent keys. controlBits[i] = 1 if k[i]==k[i-1].
		// put another way, controlBits[i] = 1 if keys[i] is from the
		// right table and has a matching key from the left table.
		co_await getControlBits(data, mOffsets[mOffsets.size() - 1].mStart / 8, keys.bitsPerEntry(), sock, controlBits);
		setTimePoint("control");

		if (mInsecurePrint)
			co_await print(data, controlBits, sock, mPartyIdx, "control", mOffsets);

		// reshape data so that the key at then end of each row are discarded.
		mOffsets.pop_back();
		data.reshape(mOffsets.back().mStart + mOffsets.back().mSize);
		temp.reshape(mOffsets.back().mStart + mOffsets.back().mSize);

		// duplicate the rows in data that are from L into any matching
		// rows that correspond to R.
		co_await mAggTree.apply(data, controlBits, sock, prng, temp);
		std::swap(data, temp);
		setTimePoint("duplicate");

		if (mInsecurePrint)
			co_await print(data, controlBits, sock, mPartyIdx, "agg", mOffsets);

		// updates which rows have a match after the agg tree duplicates values.
		co_await updateActiveFlag(data, controlBits, temp, mOffsets.back().mStart, sock);
		std::swap(data, temp);

		if (mInsecurePrint)
			co_await print(data, controlBits, sock, mPartyIdx, "isActive", mOffsets);

		// unpermute `data`. What we are left with is
		//     L
		//     L'
		// where L' are the matching rows from L for each row of R.
		// That is, L' || R is the result we want.
		temp.resize(data.numEntries(), data.bitsPerEntry());
		temp.reshape(data.bitsPerEntry());
		temp.setZero();
		co_await perm.apply<u8>(PermOp::Regular, data, temp, sock);
		std::swap(data, temp);
		setTimePoint("apply-sort");

		if (mInsecurePrint)
			co_await print(data, controlBits, sock, mPartyIdx, "unsort", mOffsets);

		// unpack L' in `data` and write the result to `out`.
		getOutput(data, query.mSelect, query.mLeftKey, out, mOffsets);

		if (mRemoveInactive)
		{
			co_await mRemoveInactive->apply(out, out, sock, prng);
		}
	}

}