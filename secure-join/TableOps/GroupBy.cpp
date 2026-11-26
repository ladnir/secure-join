#include "GroupBy.h"

namespace secJoin 
{


	// Removes the groupBy, activeFlag & compressKeys from the data
	// keys = groupByData + ActiveFlag
	// compressKeys = encode(keys);
	void GroupBy::extractKeyInfo(
		BinMatrix& data,
		BinMatrix& grpByData,
		BinMatrix& compressKeys,
		BinMatrix& actFlag,
		const std::vector<OmJoin::Offset>& offsets)
	{
		u64 n = data.rows();
		u64 groupBySize = offsets[0].mSize;
		u64 compressKeySize = offsets[1].mSize;
		u64 actFlagSize = offsets[2].mSize;

		grpByData.resize(n, groupBySize);
		compressKeys.resize(n, compressKeySize);
		actFlag.resize(n, actFlagSize);

		for (u64 i = 0; i < n; ++i)
		{
			osuCrypto::copyBytes(grpByData[i], data[i].subspan(offsets[0].mStart / 8, grpByData.bytesPerEntry()));
			osuCrypto::copyBytes(compressKeys[i], data[i].subspan(offsets[1].mStart / 8, compressKeys.bytesPerEntry()));
			
			*oc::BitIterator((u8*)actFlag.data(i), 0) =
				*oc::BitIterator((u8*)data.data(i), offsets[2].mStart);
		}

		// Discarding the key information
		data.reshape(offsets[0].mStart);
	}

	// Compute the compressKeys from the keys
	// compressKeys is used to generate the sorting Perm & getting controlBits
	// keys = groupByData + ActiveFlag
	// compressKeys = encode(keys);
	void GroupBy::loadKeys(
		ColRef groupByCol,
		std::vector<u8>& actFlagVec,
		BinMatrix& compressKeys, block codeSeed)
	{
		const auto& grpByData = groupByCol.mCol.mData;
		auto rows = grpByData.rows();
		auto grpByBits = grpByData.bitsPerEntry();
		auto grpByBytes = grpByData.bytesPerEntry();
		auto compressedSize = mStatSecParam + 2 * oc::log2ceil(rows);

		// Appending ActiveFlagBit to the key
		BinMatrix keys(rows, grpByBits + 1);
		assert(rows == actFlagVec.size());
		for (u64 i = 0; i < keys.rows(); ++i)
		{
			osuCrypto::copyBytes(keys[i].subspan(0, grpByBytes), grpByData[i]);
			*oc::BitIterator((u8*)keys.data(i), grpByBits) =
				*oc::BitIterator((u8*)&actFlagVec[0] + i, 0);
		}

		if (keys.bitsPerEntry() <= compressedSize)
		{
			compressKeys = std::move(keys);
		}
		else
		{
			PRNG prng(codeSeed);
			oc::LinearCode code;
			code.random(prng, keys.bitsPerEntry(), compressedSize);

			compressKeys.resize(rows, compressedSize);

			for (u64 i = 0; i < rows; ++i)
				code.encode(keys.data(i), compressKeys.data(i));

		}
	}


	// concatinate all the columns in `average` that are part of the table.
	// Then append 1's the end for the count
	// Then append the groupByCol 
	// Then concatinate the compressKeys
	// Then append ActFlag to the end
	// keys = groupByData + ActiveFlag
	// compressKeys = encode(keys);
	void GroupBy::concatColumns(
		ColRef groupByCol,
		std::vector<ColRef> avgCol,
		std::vector<u8>& actFlagVec,
		BinMatrix& compressKeys,
		BinMatrix& ret)
	{
		u64 m = avgCol.size();
		u64 n0 = groupByCol.mCol.rows();
		u64 rowByteSize = 0;

		std::vector<BinMatrix*> data;

		for (u64 i = 0; i < m; ++i)
		{
			if (&groupByCol.mTable == &avgCol[i].mTable)
			{
				auto bytes = oc::divCeil(avgCol[i].mCol.getBitCount(), 8);
				assert(avgCol[i].mCol.rows() == n0);
				assert(mOffsets[i].mStart == rowByteSize * 8);
				assert(mOffsets[i].mSize == avgCol[i].mCol.mData.bitsPerEntry());
				assert(mOffsets[i].mName == avgCol[i].mCol.mName);

				data.emplace_back(&avgCol[i].mCol.mData);
				rowByteSize += bytes;
			}
			else
			{
				std::string temp("Average table is not same as groupby table\n");
				throw std::runtime_error(temp + LOCATION);
			}
		}

		// Adding a Columns of 1's for calculating average
		BinMatrix ones(n0, sizeof(oc::u64) * 8);

		// Adding 1's in only party column
		if (mPartyIdx)
		{
			for (oc::u64 i = 0; i < n0; i++)
				ones(i, 0) = 1;
		}

		data.emplace_back(&ones);
		rowByteSize += sizeof(oc::u64);

		// Adding the groupBy Cols
		data.emplace_back(&groupByCol.mCol.mData);
		rowByteSize += groupByCol.mCol.mData.bytesPerEntry();

		// Adding the compress Keys
		data.emplace_back(&compressKeys);
		rowByteSize += compressKeys.bytesPerEntry();


		// All columns size + ActFlag Size
		ret.resize(n0, (rowByteSize + 1) * 8);
		OmJoin::concatColumns(ret, data, mOffsets);

		// Adding the ActFlag 
		for (u64 i = 0; i < ret.rows(); i++)
		{
			ret(i, rowByteSize) = actFlagVec[i];
			//            *oc::BitIterator((u8*)ret.data(i), rowByteSize) =
			//                    *oc::BitIterator((u8*)&actFlagVec[0] + i, 0);
		}




	}


	// Call this concat columns when you want to apply PermBackwards
	// All the average data is in data
	// GroupBy Data & updated Active Flag is also present
	void GroupBy::concatColumns(
		BinMatrix& data,
		BinMatrix& groupByData,
		BinMatrix& actFlag,
		BinMatrix& ret)
	{
		auto rows = data.rows();
		assert(groupByData.rows() == rows);
		assert(actFlag.rows() == rows);

		std::vector<BinMatrix*> cols;
		cols.emplace_back(&data);
		cols.emplace_back(&groupByData);
		cols.emplace_back(&actFlag);

		std::vector<OmJoin::Offset> tempOffsets =
		{ OmJoin::Offset{ 0, data.bitsPerEntry(), "data" },
		  OmJoin::Offset{ data.bytesPerEntry() * 8, groupByData.bitsPerEntry(), "GroupBy" },
		  OmJoin::Offset{ (data.bytesPerEntry() + groupByData.bytesPerEntry()) * 8,
						  actFlag.bitsPerEntry(), "Act Flag" }
		};

		auto bits = (data.bytesPerEntry() + groupByData.bytesPerEntry() + actFlag.bytesPerEntry()) * 8;
		ret.resize(rows, bits);
		OmJoin::concatColumns(ret, cols, tempOffsets);

	}




	// Active Flag = (Controlbits)^-1 & Active Flag
	macoro::task<> GroupBy::updateActiveFlag(
		BinMatrix& actFlag,
		BinMatrix& choice,
		BinMatrix& out,
		coproto::Socket& sock)
	{

		mUpdateActiveFlagGmw.setInput(0, choice);
		mUpdateActiveFlagGmw.setInput(1, actFlag);

		co_await mUpdateActiveFlagGmw.run(sock);

		out.resize(actFlag.rows(), 1);
		mUpdateActiveFlagGmw.getOutput(0, out);

	}


	oc::BetaCircuit updateActiveFlagCir(u64 aSize, u64 bSize, u64 cSize)
	{
		// Current Assumption is Act flag & Control Bit is 1 bit
		assert(aSize == 1);
		assert(aSize == bSize);
		assert(bSize == cSize);

		BetaCircuit cd;

		BetaBundle a(aSize);
		BetaBundle b(bSize);
		BetaBundle c(cSize);

		a.mWires.resize(aSize);
		b.mWires.resize(bSize);
		c.mWires.resize(cSize);

		cd.addInputBundle(a);
		cd.addInputBundle(b);
		cd.addOutputBundle(c);

		for (u64 i = 0; i < aSize; i++)
			cd.addGate(a.mWires[i], b.mWires[i], oc::GateType::na_And, c.mWires[i]);

		return cd;
	}

	void GroupBy::init(
		ColRef groupByCol,
		std::vector<ColRef> avgCol,
		CorGenerator& ole,
		bool remDummiesFlag)
	{
		u64 rows = groupByCol.mCol.rows();
		u64 keySize = groupByCol.mCol.getBitCount() + 1;

		u64 compressKeySize = std::min<u64>(
			keySize,
			mStatSecParam + 2 * oc::log2ceil(rows));

		mPartyIdx = ole.partyIdx();

		u64 dataBitsPerEntry = 0;

		mOffsets.clear();
		mOffsets.reserve(avgCol.size() + 1);
		u64 aggTreeBitCount = 0;

		for (u64 i = 0; i < avgCol.size(); ++i)
		{
			auto bytes = avgCol[i].mCol.getByteCount();
			mOffsets.emplace_back(
				OmJoin::Offset{
						dataBitsPerEntry,
					avgCol[i].mCol.getBitCount(),
					avgCol[i].mCol.mName });

			dataBitsPerEntry += bytes * 8;
			aggTreeBitCount += avgCol[i].mCol.getBitCount();
		}
		// Columns for ones
		mOffsets.emplace_back(OmJoin::Offset{ dataBitsPerEntry, sizeof(oc::u64) * 8, "count*" });
		dataBitsPerEntry += sizeof(oc::u64) * 8;
		aggTreeBitCount += sizeof(oc::u64) * 8;

		// Initialize the AggTree before adding the keys info to the offsets
		std::vector<OmJoin::Offset> aggTreeOffsets;
		for (u64 i = 0; i < mOffsets.size(); i++)
			aggTreeOffsets.push_back(mOffsets[i]);

		mOffsets.emplace_back(OmJoin::Offset{ dataBitsPerEntry, groupByCol.mCol.getBitCount(), "GroupBy" });
		dataBitsPerEntry += groupByCol.mCol.getByteCount() * 8;

		mOffsets.emplace_back(OmJoin::Offset{ dataBitsPerEntry, compressKeySize, "CompressKey*" });
		dataBitsPerEntry += oc::divCeil(compressKeySize, 8) * 8;

		mOffsets.emplace_back(OmJoin::Offset{ dataBitsPerEntry, 1, "ActFlag" });
		dataBitsPerEntry += 8;

		mSort.init(mPartyIdx, rows, compressKeySize, ole);

		// in the forward direction we will permute the group col, a flag,
		// compress keys and all of the average columns. In the
		// backwards direction, we will unpermute the all the above
		// columns except the compress keys. Therefore, in total we will permute:
		u64 permForward = oc::divCeil(dataBitsPerEntry, 8) + sizeof(u32);
		u64 permBackward = ((remDummiesFlag == false) * oc::divCeil(dataBitsPerEntry - compressKeySize, 8))
			+ (mInsecurePrint == true) * 1; // Permuting the control Bits

		mPerm.init(mPartyIdx, rows, permForward + permBackward, ole);

		mControlBitGmw.init(rows, OmJoin::getControlBitsCircuit(compressKeySize), ole);

		auto addCir = getAddCircuit(aggTreeOffsets, oc::BetaLibrary::Optimized::Depth);
		mAggTree.init(rows, aggTreeBitCount, AggTreeType::Suffix, addCir, ole);

		auto cir = updateActiveFlagCir(1, 1, 1);
		mUpdateActiveFlagGmw.init(rows, cir, ole);


		if (remDummiesFlag)
		{
			mRemoveInactive.emplace();
			auto cols = groupByCol.mTable.getColumnInfo();
			mRemoveInactive->init(rows, cols, ole);
		}
	}



	// Assumptions: 
	// 1) Both Average Col & Group by Col are not null
	// 2) Currently one group by column is supported
	macoro::task<> GroupBy::groupBy(
		ColRef groupByCol,
		std::vector<ColRef> avgCol,
		SharedTable& out,
		oc::PRNG& prng,
		coproto::Socket& sock)
	{

		auto compressKeys = BinMatrix{};
		auto sortedgroupByData = BinMatrix{};
		auto data = BinMatrix{};
		auto temp = BinMatrix{};
		auto actFlag = BinMatrix{};
		auto controlBits = BinMatrix{};
		auto tempVec = std::vector<u8>{};
		auto dataOffsets = std::vector<OmJoin::Offset>{};
		auto tempOffsets = std::vector<OmJoin::Offset>{};
		auto prepro = macoro::eager_task<>{};
		auto sPerm = AdditivePerm{};
		auto perm = ComposedPerm{};
		auto tempNum = u64{};

		block codeSeed = oc::ZeroBlock;
		if (groupByCol.mCol.getBitCount() + 1 > mOffsets[mOffsets.size() - 2].mSize)
			co_await sampleCompressionSeed(mPartyIdx, prng, sock, codeSeed);
		loadKeys(groupByCol, groupByCol.mTable.mIsActive, compressKeys, codeSeed);

		if (mInsecurePrint)
		{
			std::cout << "------------- Average Starts here ---------- " << std::endl;
			tempOffsets = { OmJoin::Offset{ 0, compressKeys.bitsPerEntry(), "Compress Key*" } };
			co_await OmJoin::print(compressKeys, controlBits, sock, mPartyIdx, "Compress keys", tempOffsets);
		}

		mSort.mInsecureMock = mInsecureMockSubroutines;
		mSort.preprocess();
		prepro = mSort.genPrePerm(sock, prng) | macoro::make_eager();

		// get the stable sorting permutation sPerm
		co_await mSort.genPerm(compressKeys, sPerm, sock, prng);

		mPerm.preprocess();
		co_await prepro;

		// Concat Columns
		concatColumns(groupByCol, avgCol, groupByCol.mTable.mIsActive, compressKeys, data);
		if (mInsecurePrint)
			co_await OmJoin::print(data, controlBits, sock, mPartyIdx, "preSort", mOffsets);


		// Apply the sortin permutation.
		temp.resize(data.numEntries(), data.bitsPerEntry());

		co_await mPerm.generate(sock, prng, data.rows(), perm);

		co_await perm.derandomize(sPerm, sock);

		co_await perm.apply<u8>(PermOp::Inverse, data, temp, sock);
		std::swap(data, temp);

		if (mInsecurePrint)
			co_await OmJoin::print(data, controlBits, sock, mPartyIdx, "sort", mOffsets);

		// Removing keys info from the offsets 
		dataOffsets.resize(mOffsets.size() - 3);

		for (u64 i = 0; i < mOffsets.size() - 3; i++)
			dataOffsets[i] = mOffsets[i];

		// All the Key Related information is in tempOffSet
		tempOffsets.resize(3);
		for (u64 i = 0; i < 3; i++)
			tempOffsets[i] = mOffsets[mOffsets.size() - 3 + i];

		// Take out the Keys + activeflag + compressKeys from the data
		// After this data only has avgCols & count*
		extractKeyInfo(data, sortedgroupByData, compressKeys, actFlag, tempOffsets);

		// compare adjacent keys. controlBits[i] = 1 if k[i]==k[i-1].
		co_await getControlBits(compressKeys, sock, controlBits);

		if (mInsecurePrint)
			co_await OmJoin::print(data, controlBits, sock, mPartyIdx, "control", dataOffsets);

		co_await mAggTree.apply(data, controlBits, sock, prng, temp);
		std::swap(data, temp);

		if (mInsecurePrint)
			co_await OmJoin::print(data, controlBits, sock, mPartyIdx, "agg-data", dataOffsets);

		co_await updateActiveFlag(actFlag, controlBits, temp, sock);
		std::swap(actFlag, temp);

		if (mInsecurePrint)
		{
			tempOffsets = { OmJoin::Offset{ 0, actFlag.bitsPerEntry(), "Act Flag" } };
			co_await OmJoin::print(actFlag, controlBits, sock, mPartyIdx, "isActive", tempOffsets);
		}

		// Concating Columns either for Rand Perm or Backward Perm
		// Discarding the compress key bcoz we don't it anymore
		tempNum = data.bytesPerEntry();
		concatColumns(data, sortedgroupByData, actFlag, temp);
		std::swap(data, temp);

		// Adding GroupBy Col
		dataOffsets.emplace_back(OmJoin::Offset{ tempNum * 8, sortedgroupByData.bitsPerEntry(), "GroupBy" });
		// Adding ActFlag Offset
		dataOffsets.emplace_back(
			OmJoin::Offset{ (tempNum + sortedgroupByData.bytesPerEntry()) * 8, actFlag.bitsPerEntry(), "Act Flag" });

		co_await perm.apply<u8>(PermOp::Regular, data, data, sock);
		getOutput(out, avgCol, groupByCol, data, dataOffsets);

		if (mRemoveInactive)
		{
			co_await mRemoveInactive->apply(out, out, sock, prng);
		}
	}


	void GroupBy::getOutput(
		SharedTable& out,
		std::vector<ColRef> avgCol,
		ColRef groupByCol,
		BinMatrix& data,
		std::vector<OmJoin::Offset>& offsets)
	{

		u64 nEntries = data.numEntries();
		populateOutTable(out, avgCol, groupByCol, nEntries);

		out.mIsActive.resize(nEntries);

		for (u64 i = 0; i < data.numEntries(); i++)
		{

			// Copying the average columns
			for (u64 j = 0; j < offsets.size() - 2; j++)
			{
				osuCrypto::copyBytes(
					out.mColumns[j + 1].mData[i],
					data[i].subspan(offsets[j].mStart / 8,
						out.mColumns[j + 1].getByteCount()));

			}

			// Storing the Group By Column
			osuCrypto::copyBytes(
				out.mColumns[0].mData[i],
				data[i].subspan(offsets[offsets.size() - 2].mStart / 8,
					out.mColumns[0].getByteCount()));


			// Adding Active Flag
			out.mIsActive[i] = data(i, offsets[offsets.size() - 1].mStart / 8);
		}

	}

	AggTree::Operator GroupBy::getAddCircuit(std::vector<OmJoin::Offset>& offsets,
		oc::BetaLibrary::Optimized op)
	{
		return [&, op](
			oc::BetaCircuit& cd,
			const oc::BetaBundle& left,
			const oc::BetaBundle& right,
			oc::BetaBundle& out)
			{
				u64 currIndex = 0;
				for (u64 i = 0; i < offsets.size(); i++)
				{
					auto size = offsets[i].mSize;
					// std::cout << "Offset size is " <<  size << std::endl;
					auto beginIndex = currIndex;
					auto endIndex = currIndex + size;
					BetaBundle a, b, c;

					a.mWires.reserve(size);
					b.mWires.reserve(size);
					c.mWires.reserve(size);
					for (u64 j = beginIndex; j < endIndex; j++)
					{
						a.mWires.emplace_back(left[j]);
						b.mWires.emplace_back(right[j]);
						c.mWires.emplace_back(out[j]);
					}

					BetaBundle t(op == oc::BetaLibrary::Optimized::Size ? 4 : size * 2);
					cd.addTempWireBundle(t);
					osuCrypto::BetaLibrary::add_build(cd, a, b, c, t, oc::BetaLibrary::IntType::TwosComplement, op);

					// std::cout << "Circuit Gen for " << offsets[i].mName << std::endl;
					currIndex = endIndex;
				}

			};
	}

	macoro::task<> GroupBy::getControlBits(
		BinMatrix& keys,
		coproto::Socket& sock,
		BinMatrix& out)
	{
		auto cir = oc::BetaCircuit{};
		auto sKeys = BinMatrix{};
		auto n = u64{};
		auto keyBitCount = u64{};

		n = keys.numEntries();
		keyBitCount = keys.bitsPerEntry();

		sKeys.resize(n + 1, keyBitCount);
		osuCrypto::copyBytes(sKeys.subMatrix(1), keys.subMatrix(0, n));

		mControlBitGmw.setInput(0, sKeys.subMatrix(0, n));
		mControlBitGmw.setInput(1, sKeys.subMatrix(1, n));

		co_await mControlBitGmw.run(sock);

		out.resize(n, 1);
		mControlBitGmw.getOutput(0, out);
		out.mData(0) = 0;

	}







}