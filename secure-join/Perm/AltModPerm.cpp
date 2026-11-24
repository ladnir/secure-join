#include "AltModPerm.h"

#include "secure-join/Util/Matrix.h"
namespace secJoin
{
	void xorShare(
		oc::MatrixView<const oc::block> v1_,
		u64 byteOffset,
		oc::MatrixView<const oc::u8> v2,
		oc::MatrixView<oc::u8>& s)
	{
		auto v1 = matrixCast<const u8>(v1_);
		auto r = s.rows();
		auto c = s.cols();
		auto d = s.data();
		auto d2 = v2.data();
		for (oc::u64 i = 0; i < r; ++i)
		{
			auto d1 = v1.data(i) + byteOffset;
			for (oc::u64 j = 0; j < c; ++j)
				*d++ = *d1++ ^ *d2++;
		}
	}

	void xorShare(oc::MatrixView<const u8> v1,
		oc::MatrixView<const oc::u8> v2,
		oc::MatrixView<oc::u8>& s)
	{
		// Checking the dimensions
		if (v1.rows() != v2.rows())
			throw RTE_LOC;
		if (v1.cols() != v2.cols())
			throw RTE_LOC;
		for (oc::u64 i = 0; i < v1.size(); ++i)
			s(i) = v1(i) ^ v2(i);
	}


	// AltMod Receiver calls this setup
	// generate random mDelta such that
	// mDelta ^ mB = pi(mA)
	macoro::task<> AltModPermGenSender::generate(
		Perm pi,
		PRNG& prng,
		coproto::Socket& chl,
		PermCorSender& dst)
	{
		if (mPrfRecver.mInputSize == 0)
			throw RTE_LOC;

		//auto aesCipher = oc::Matrix<oc::block>();
		//auto blocksPerRow = u64{};
		//auto aes = oc::AES();
		//auto key = block();


		if (mN == 0)
			throw std::runtime_error("AltModPermGenSender::init() must be called before setup. " LOCATION);
		if (pi.size() != mN)
			throw std::runtime_error("AltModPermGenSender::generate() permutaiton size does not match. " LOCATION);
		dst.mPerm = std::move(pi);

		auto blocksPerRow = oc::divCeil(mBytesPerRow, sizeof(oc::block));

		// sample a hashing key.
		block key = prng.get();
		oc::AES aes(key);
		co_await chl.send(std::move(key));

		dst.mDelta.resize(mN, blocksPerRow);
		for (u64 i = 0, k = 0; i < mN; i++)
		{
			auto srcIdx = dst.mPerm[i];
			for (u64 j = 0; j < blocksPerRow; j++, ++k)
			{
				// unchecked indexing ok due to the resize above.
				dst.mDelta.data()[k] = oc::block(j, srcIdx);
			}
		}

		// randomized the blocks using AES. We only have a weak PRF.
		aes.ecbEncBlocks(dst.mDelta, dst.mDelta);

		oc::Matrix<block> debugInput;
		if (mDebug)
			debugInput = dst.mDelta;
		co_await mPrfRecver.evaluate(dst.mDelta, dst.mDelta, chl, prng);

		if (mDebug)
		{
			auto debugB = oc::Matrix<oc::block>{};
			auto debugKey = AltModPrf::KeyType{};

			debugB.resize(dst.mDelta.rows(), dst.mDelta.cols());
			co_await chl.recv(debugKey);
			co_await chl.recv(debugB);

			{
				AltModPrf prf;
				prf.setKey(debugKey);
				for (u64 i = 0; i < debugB.size();++i)
				{
					if ((dst.mDelta(i) ^ debugB(i)) != prf.eval(debugInput(i)))
						throw RTE_LOC;
				}
			}
		}
	}

	// AltMod Receiver calls this setup
	// generate random mA, mB such that
	// mDelta ^ mB = pi(mA)
	macoro::task<> AltModPermGenReceiver::generate(
		PRNG& prng,
		coproto::Socket& chl,
		PermCorReceiver& dst)
	{
		if (mN == 0)
			throw std::runtime_error("AltModPermGenReceiver::init() must be called before setup. " LOCATION);

		auto blocksPerRow = oc::divCeil(mBytesPerRow, sizeof(oc::block));

		auto key = block();
		co_await chl.recv(key);


		// B = (AltMod(k,AES(k', pi(0))), ..., (AltMod(k,AES(k', pi(n-1)))))
		dst.mB.resize(mN, blocksPerRow);
		co_await mPrfSender.evaluate({}, dst.mB, chl, prng);

		if (mDebug)
		{
			co_await chl.send(std::move(mPrfSender.getKey()));
			co_await chl.send(coproto::copy(dst.mB));
		}

		// A = (AltMod(k,AES(k', 0)), ..., (AltMod(k,AES(k', n-1))))
		oc::AES aes(key);

		dst.mA.resize(mN, blocksPerRow);
		for (u64 i = 0, k = 0; i < mN; i++)
		{
			for (u64 j = 0; j < blocksPerRow; ++j, ++k)
			{
				// unchecked indexing ok due to the resize above.
				dst.mA.data()[k] = oc::block(j, i);
			}
		}
		aes.ecbEncBlocks(dst.mA, dst.mA);

		auto altMod = AltModPrf{};
		altMod.setKey(mPrfSender.getKey());
		altMod.eval(dst.mA, dst.mA);
	}

}