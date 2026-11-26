#include "AltModPrf.h"
#include "AltModSimd.h"


namespace secJoin
{

	auto makeAltModWPrfB() {
		std::array<block, 128> r;
		osuCrypto::setBytes(r, 0);
		PRNG prng(block(234532451234512134, 214512345123455437));
		for (u64 i = 0; i < r.size(); ++i)
		{
			r[i] = prng.get();
		}
		return r;
	};
	const std::array<block, 128> AltModPrf::mB = makeAltModWPrfB();

	auto makeAltModWPrfBCode()
	{
		oc::Matrix<u8> g(128, sizeof(block)), gt(128, sizeof(block));
		g.resize(128, sizeof(block));
		for (u64 i = 0; i < 128; ++i)
			osuCrypto::copyBytes(g[i], AltModPrf::mB[i]);
		oc::transpose({ g.data(), g.rows(), g.cols() }, gt);
		F2LinearCode r;
		r.init(gt);
		return r;
	}
	const F2LinearCode AltModPrf::mBCode = makeAltModWPrfBCode();

	auto makeAltModWPrfACode() {

		F3AccPermCode r;
		r.init(AltModPrf::KeySize, AltModPrf::MidSize);
		return r;
	};
	const F3AccPermCode AltModPrf::mACode = makeAltModWPrfACode();

	auto makeAltModWPrfBExpanded()
	{
		std::array<std::array<u8, 128>, 128> r;
		for (u64 i = 0; i < AltModPrf::mB.size(); ++i)
		{
			auto iter0 = oc::BitIterator((u8*)&AltModPrf::mB[i]);
			for (u64 j = 0; j < r[i].size(); ++j)
				r[i][j] = *iter0++;
		}
		return r;
	}
	const std::array<std::array<u8, 128>, 128> AltModPrf::mBExpanded = makeAltModWPrfBExpanded();


	auto makeAltModWPrfGCode()
	{
		constexpr u64 expandInput2W = (AltModPrf::KeySize - 128) / 128;
		std::array<F2LinearCode, expandInput2W> expandInput2Code;

		oc::Matrix<u8> g(128, sizeof(block));
		PRNG prng(block(45245327478243784, 28874799389237822));
		for (auto i : stdv::iota(0ull, expandInput2W))
		{
			prng.get(g.data(), g.size());
			expandInput2Code[i].init(g);
		}
		return expandInput2Code;
	}
	const std::array<F2LinearCode, 3> AltModPrf::mGCode = makeAltModWPrfGCode();

	void AltModPrf::compressB(
		oc::MatrixView<block> v,
		span<block> y)
	{
		if(v.rows() != MidSize)
			throw RTE_LOC;
		if(v.cols() != divCeil(y.size(), 128))
			throw RTE_LOC;

		// round down
		auto n = y.size();

		// tt and yy will be buffers where we transpose the 256 bit input
		oc::AlignedArray<block, 128> tt, yy;

		// the first 128 bit input
		block* v0Iter = v[0].data();

		// the second 128 bit input
		block* v1Iter = v[128].data();

		// how far we have to step to get to the next bit.
		auto vStep = v.cols();

		for (u64 i = 0, ii = 0; i < n; i += 128, ++ii)
		{
			// load the first 128 bits and transpose.
			for (u64 j = 0; j < 128; ++j)
				yy[j] = v0Iter[j * vStep];
			++v0Iter;
			oc::transpose128(yy);

			// load the second 128 bits and transpose.
			for (u64 j = 0; j < 128; ++j)
				tt[j] = v1Iter[j * vStep];
			++v1Iter;
			oc::transpose128(tt.data());

			// B is systematic so what we can do is compute
			// y = B * v = yy + mBCode * tt
			auto m = std::min<u64>(n - i, 128);
			auto yIter = y.data() + i;
			for (u64 j = 0; j < m; ++j)
			{
				AltModPrf::mBCode.encode(tt[j], tt[j]);
				yIter[j] = yy[j] ^ tt[j];
			}
		}
	}

	void  AltModPrf::setKey(AltModPrf::KeyType k)
	{
		mExpandedKey = k;
	}

	void AltModPrf::expandInputAes(span<const block> x, oc::MatrixView<block> xt)
	{
		auto n = x.size();
		if (xt.rows() != AltModPrf::KeySize)
			throw RTE_LOC;
		if (xt.cols() != oc::divCeil(n, 128))
			throw RTE_LOC;

		for (u64 i = 0, k = 0; i < n; ++k)
		{
			static_assert(AltModPrf::KeySize % 128 == 0);
			auto m = std::min<u64>(128, n - i);
			auto xIter = x.data() + k * 128;

			for (u64 q = 0; q < AltModPrf::KeySize / 128; ++q)
			{
				auto tweak = block(q, q);
				oc::AlignedArray<block, 128> t;
				if (q == 0)
				{
					for (u64 j = 0;j < m; ++j)
					{
						t[j] = xIter[j];
					}
				}
				else
				{
					for (u64 j = 0;j < m; ++j)
					{
						t[j] = xIter[j] ^ tweak;
					}
					oc::mAesFixedKey.hashBlocks(t, t);
				}

				oc::transpose128(t.data());

				auto xtk = &xt(q * 128, k);
				auto step = xt.cols();
				for (u64 j = 0;j < 128; ++j)
				{
					assert(xtk == &xt(q * 128 + j, k));
					*xtk = t[j];
					xtk += step;
				}
			}

			i += 128;
		}
	}

	void AltModPrf::expandInputLinear(block x, KeyType& X)
	{
		X[0] = x;
		constexpr const auto rem = KeyType{}.size() - 1;
		for (auto i = 0ull; i < rem; ++i)
			mGCode[i].encode(x, X[i + 1]);
	}

	void AltModPrf::expandInputLinear(span<const block> x, oc::MatrixView<block> xt)
	{
		auto n = x.size();
		if (xt.rows() != AltModPrf::KeySize)
			throw RTE_LOC;
		if (xt.cols() != oc::divCeil(n, 128))
			throw RTE_LOC;


		oc::AlignedArray<block, 128> t;
		auto step = xt.cols();
		for (u64 i = 0; i < n; i += 128)
		{
			static_assert(AltModPrf::KeySize % 128 == 0);
			auto m = std::min<u64>(128, n - i);
			auto xIter = x.data() + i;

			for(u64 j = 0; j < m; ++j)
				t[j] = xIter[j];
			for(u64 j = m; j < 128; ++j)
				t[j] = block(0, 0);

			oc::transpose128(t.data());

			auto k = i / 128;
			auto xtk = &xt(0, k);
			for (u64 j = 0; j < 128; ++j)
			{
				assert(xtk == &xt(j, k));
				*xtk = t[j];
				xtk += step;
			}
		}

		for (u64 q = 0; q < AltModPrf::mGCode.size(); ++q)
		{
			for (u64 i = 0; i < n; i += 128)
			{
				static_assert(AltModPrf::KeySize % 128 == 0);
				auto m = std::min<u64>(128, n - i);
				auto xIter = x.data() + i;

				if (m == 128)
				{
					for (u64 w = 0; w < 16; ++w)
						AltModPrf::mGCode[q].encodeN<8>(xIter + w * 8, t.data() + w * 8);
				}
				else
				{
					for (u64 j = 0; j < m; ++j)
						AltModPrf::mGCode[q].encode(xIter[j], t[j]);
					for (u64 j = m; j < 128; ++j)
						t[j] = oc::ZeroBlock;
				}

				oc::transpose128(t.data());

				auto k = i / 128;
				auto xtk = &xt(q * 128 + 128, k);
				for (u64 j = 0;j < 128; ++j)
				{
					assert(xtk == &xt(q * 128 + 128 + j, k));
					*xtk = t[j];
					xtk += step;
				}
			}
		}
	}

	void AltModPrf::expandInputAes(block x, KeyType& X)
	{
		X[0] = x;
		for (u64 i = 1; i < X.size(); ++i)
			X[i] = x ^ block(i, i);

		constexpr const auto rem = KeyType{}.size() - 1;
		if (rem)
			oc::mAesFixedKey.hashBlocks<rem>(X.data() + 1, X.data() + 1);
	}

	block  AltModPrf::eval(block x)
	{
		block y;
		eval({ &x,1 }, { &y,1 });
		return y;

		// TODO, optimize single evaluation
		//std::array<u16, KeySize> h;
		//AltModPrf::KeyType X;

		//expandInput(x, X);

		//auto kIter = oc::BitIterator((u8*)mExpandedKey.data());
		//auto xIter = oc::BitIterator((u8*)X.data());
		//for (u64 i = 0; i < KeySize; ++i)
		//{
		//	h[i] = *kIter & *xIter;
		//	++kIter;
		//	++xIter;
		//}

		//block256m3 u;
		//mtxMultA(h, u);

		//block256 w;
		//for (u64 i = 0; i < u.mData.size(); ++i)
		//{
		//	*oc::BitIterator((u8*)&w, i) = u.mData[i] % 2;
		//}
		//return compress(w);
	}

	void AltModPrf::eval(span<block> x, span<block> y)
	{
		if(x.size() != y.size()) 
			throw RTE_LOC;

		oc::Matrix<block> xt, xk0, xk1, u0, u1;

		// we need x in a transformed format so that we can do SIMD operations.
		// xt = G * x  or xt = AES.hash(x) mod 2
		xt.resize(AltModPrf::KeySize, oc::divCeil(y.size(), 128));
		AltModPrf::expandInput(x, xt, mInputExpansionMode);

		// xk = (x . k) mod 3
		xk0.resize(AltModPrf::KeySize, oc::divCeil(x.size(), 128), oc::AllocType::Uninitialized);
		xk1.resize(AltModPrf::KeySize, oc::divCeil(x.size(), 128), oc::AllocType::Uninitialized);
		for (u64 i = 0; i < KeySize; ++i)
		{
			// TODO, make this branch free.
			if (bit(mExpandedKey, i))
			{
				osuCrypto::copyBytes(xk0[i], xt[i]);
			}
			else
				osuCrypto::setBytes(xk0[i], 0);

			osuCrypto::setBytes(xk1[i], 0);
		}

		// u = A * xk mod 3
		u0.resize(AltModPrf::MidSize, oc::divCeil(x.size(), 128), oc::AllocType::Uninitialized);
		u1.resize(AltModPrf::MidSize, oc::divCeil(x.size(), 128), oc::AllocType::Uninitialized);
		AltModPrf::mACode.encode(xk1, xk0, u1, u0);

		// y = B * u mod 2
		compressB(u0, y);
	}

}