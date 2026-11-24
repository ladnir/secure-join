#include "BitInjection.h"
#include <cstring>

namespace secJoin
{


	template<u64 Bytes>
	static void unpackWords(span<const u8> in, span<u32> out)
	{
		for (u64 i = 0; i < out.size(); ++i)
		{
			u32 word = 0;
			std::memcpy(&word, in.data() + i * Bytes, Bytes);
			out[i] = word;
		}
	}

	inline void unpack(span<const u8> in, u64 bitCount, span<u32> out)
	{
		auto bytes = oc::divCeil(bitCount, 8);
		if (out.size() * bytes != in.size())
			throw RTE_LOC;

		switch (bytes)
		{
		case 0: std::fill(out.begin(), out.end(), 0); break;
		case 1: unpackWords<1>(in, out); break;
		case 2: unpackWords<2>(in, out); break;
		case 3: unpackWords<3>(in, out); break;
		case 4: osuCrypto::copyBytes(out, in); break;
		default: throw RTE_LOC;
		}
	}
	inline void pack(span<const u32> in, u64 bitCount, span<u8> out)
	{

		if (bitCount == 32)
			osuCrypto::copyBytes(out, in);
		else
		{
			auto n = oc::divCeil(bitCount, 8);
			if (in.size() * n != out.size())
				throw RTE_LOC;

			auto s = in.data();
			auto iter = out.begin();
			for (u64 j = 0; j < in.size(); ++j)
			{
				std::copy((u8 const*)s, (u8 const*)s + n, iter);
				iter += n;
				++s;
			}
		}
	}

	void BitInject::preprocess()
	{
		mHasPreprocessing = true;
		if (mRecvReq.size())
			mRecvReq.start();
		else if (mMod2F4Req.size())
			mMod2F4Req.start();
		else
		{
			mHasPreprocessing = false;
			throw std::runtime_error("BitInject::ini() must be called before preprocess() " LOCATION);
		}
	}

	// convert each bit of the binary secret sharing `in`
	 // to integer Z_{2^outBitCount} arithmetic sharings.
	 // Each row of `in` should have `mInBitCount` bits.
	 // out will therefore have dimension `in.rows()` rows 
	 // and `mInBitCount` columns.
	macoro::task<> BitInject::bitInjection(
		const oc::Matrix<u8>& in,
		u64 outBitCount,
		oc::Matrix<u32>& out,
		coproto::Socket& sock)
	{
		auto in2 = oc::Matrix<u8>{};
		auto ec = macoro::result<void>{};
		auto recvs = std::vector<OtRecv>{};
		auto send = OtSend{};
		auto i = u64{ 0 };
		auto k = u64{ 0 };
		auto m = u64{ 0 };
		auto diff = oc::BitVector{};
		auto buff = oc::AlignedUnVector<u8>{};
		auto updates = oc::AlignedUnVector<u32>{};
		auto mask = u32{};

		if (outBitCount > 32)
			throw std::runtime_error("outBitCount exceeds 32 bits. " LOCATION);

		if (mInBitCount > in.cols() * 8)
			throw std::runtime_error("mInBitCount longer than the row size. " LOCATION);

		if (in.rows() != mRowCount)
			throw std::runtime_error("row count does not match init(). " LOCATION);

		if (hasRequest() == false)
			throw std::runtime_error("request must be called first. " LOCATION);

		//if (hasPreprocessing() == false)
		//    pre = preprocess() | macoro::make_eager();


		out.resize(in.rows(), mInBitCount);
		mask = outBitCount == 32 ? ~u32{0} : ((u32{1} << outBitCount) - 1);

		if (mRole)
		{
			//if (hasPreprocessing() == false)
			//    throw RTE_LOC;
			if (mRecvReq.size() < in.rows() * mInBitCount)
				throw RTE_LOC;

			while (i < out.size())
			{
				recvs.emplace_back();
				co_await mRecvReq.get(recvs.back());

				m = std::min<u64>(recvs.back().size(), out.size() - i);
				recvs.back().mChoice.resize(m);
				//recvs.back().mMsg.resize(m);

				diff.reserve(m);
				for (u64 j = 0; j < m; )
				{
					auto row = i / mInBitCount;
					auto off = i % mInBitCount;
					auto rem = std::min<u64>(m - j, mInBitCount - off);

					diff.append((u8*)&in(row, 0), rem, off);


					//std::cout << "r " << i << " " << recvs.back().mChoice[j] << " " << recvs.back().mMsg[j] << std::endl;
					i += rem;
					j += rem;

				}

				diff ^= recvs.back().mChoice;
				recvs.back().mChoice ^= diff;
				co_await sock.send(std::move(diff));
			}

			i = 0; k = 0;
			while (i < out.size())
			{
				m = recvs[k].mChoice.size();
				buff.resize(m * oc::divCeil(outBitCount, 8));
				co_await sock.recv(buff);
				updates.resize(m);
				unpack(buff, outBitCount, updates);

				for (u64 j = 0; j < m; ++j, ++i)
				{
					//recvs[k].mMsg[j].set<u32>(0, 0);

					if (recvs[k].mChoice[j])
						out(i) = (recvs[k].mMsg[j].get<u32>(0) + updates[j]) & mask;
					else
						out(i) = recvs[k].mMsg[j].get<u32>(0) & mask;
				}

				++k;
			}
		}
		else
		{

			//if (hasPreprocessing() == false)
			//    throw RTE_LOC;
			if (mMod2F4Req.size() < in.rows() * mInBitCount)
				throw RTE_LOC;

			while (i < out.size())
			{
				co_await mMod2F4Req.get(send);;

				m = std::min<u64>(send.size(), out.size() - i);
				diff.resize(m);
				co_await sock.recv(diff);;

				updates.resize(m);
				for (u64 j = 0; j < m; ++j, ++i)
				{
					//std::cout << "s " << i << " " << send.mMsg[j][0] << " " << send.mMsg[j][1] << std::endl;

					auto row = i / mInBitCount;
					auto off = i % mInBitCount;

					auto y = (u8)*oc::BitIterator((u8*)&in(row, 0), off);
					auto b = (u8)diff[j];
					auto m0 = send.mMsg[j][b];
					auto m1 = send.mMsg[j][b ^ 1];

					auto v0 = m0.get<u32>(0);
					auto v1 = v0 + (-2 * y + 1);
					out(i) = (-v0 + y) & mask;
					updates[j] = (v1 - m1.get<u32>(0)) & mask;
				}

				buff.resize(m * oc::divCeil(outBitCount, 8));
				pack(updates, outBitCount, buff);

				co_await sock.send(std::move(buff));;
			}
		}

	}

} // namespace secJoin
