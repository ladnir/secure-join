#include "AltModWPrf_Test.h"
#include "secure-join/Prf/AltModPrfProto.h"
#include "secure-join/Prf/AltModSimd.h"
#include "secure-join/Prf/mod3.h"
#include "cryptoTools/Crypto/PRNG.h"
#include "cryptoTools/Common/Matrix.h"

#include "cryptoTools/Common/TestCollection.h"
#include "secure-join/Util/Util.h"
#include "secure-join/Prf/F3LinearCode.h"
#include "secure-join/Prf/ConvertToF3.h"
#include "secure-join/AggTree/PerfectShuffle.h"

using namespace secJoin;


void mult(oc::DenseMtx& C, std::vector<u64>& X, std::vector<u64>& Y)
{
	for (u64 i = 0; i < C.rows(); ++i)
	{
		Y[i] = 0;
		for (u64 j = 0; j < C.cols(); ++j)
		{
			Y[i] += C(i, j) * X[j];
		}
	}
}


void F2LinearCode_test(const oc::CLP& cmd)
{
	auto n = cmd.getOr("n", 1ull << 10);

	F2LinearCode code;

	oc::Matrix<u8> g(128, sizeof(block));
	PRNG prng(oc::CCBlock);
	prng.get(g.data(), g.size());
	code.init(g);



	auto mult = [&](block x) {
		block ret = oc::ZeroBlock;
		auto iter = oc::BitIterator(x.data());
		for (u64 i = 0; i < 128; ++i)
		{
			if (*iter++)
			{
				ret ^= *(block*)g.data(i);
			}
		}
		return ret;
		};

	std::vector<block> x(n), y(n);

	for (u64 t = 0; t < 4; ++t)
	{
		prng.get(x.data(), x.size());
		for (u64 i = 0; i < n / 128; ++i)
		{
			code.encodeN<128>(x.data() + i * 128, y.data() + i * 128);
		}
		for (u64 i = 0; i < n; ++i)
		{
			block act;
			auto exp = mult(x[i]);
			code.encode(x[i], act);

			if (exp != act)
				throw RTE_LOC;

			if (exp != y[i])
				throw RTE_LOC;
		}
	}

}


	void mod3BitDecompostion(oc::MatrixView<u16> u, oc::MatrixView<block> u0, oc::MatrixView<block> u1)
	{
		if (u.rows() != u0.rows())
			throw RTE_LOC;
		if (u.rows() != u1.rows())
			throw RTE_LOC;

		if (oc::divCeil(u.cols(), 128) != u0.cols())
			throw RTE_LOC;
		if (oc::divCeil(u.cols(), 128) != u1.cols())
			throw RTE_LOC;

		u64 n = u.rows();
		u64 m = u.cols();

		oc::AlignedUnVector<u8> temp(oc::divCeil(m * 2, 8));
		for (u64 i = 0; i < n; ++i)
		{
			auto iter = temp.data();

			assert(m % 4 == 0);

			for (u64 k = 0; k < m; k += 4)
			{
				assert(u[i][k + 0] < 3);
				assert(u[i][k + 1] < 3);
				assert(u[i][k + 2] < 3);
				assert(u[i][k + 3] < 3);

				// 00 01 10 11 20 21 30 31
				*iter++ =
					(u[i][k + 0] << 0) |
					(u[i][k + 1] << 2) |
					(u[i][k + 2] << 4) |
					(u[i][k + 3] << 6);
			}

			span<u8> out0((u8*)u0.data(i), temp.size() / 2);
			span<u8> out1((u8*)u1.data(i), temp.size() / 2);
			perfectUnshuffle(temp, out0, out1);

#ifndef NDEBUG
			for (u64 j = 0; j < out0.size(); ++j)
			{
				if (out0[j] & out1[j])
					throw RTE_LOC;
			}
#endif
		}
	}


void AltModWPrf_mod3BitDecompostion_test()
{
	u64 n = 256;
	u64 m = 128;


	oc::Matrix<u16> u(n, m);
	oc::Matrix<oc::block> u0(n, m / 128);
	oc::Matrix<oc::block> u1(n, m / 128);

	mod3BitDecompostion(u, u0, u1);


	for (u64 i = 0; i < n; ++i)
	{
		auto iter0 = oc::BitIterator((u8*)u0.data(i));
		auto iter1 = oc::BitIterator((u8*)u1.data(i));
		for (u64 j = 0; j < m; ++j)
		{
			auto uu0 = u(i, j) & 1;
			auto uu1 = (u(i, j) >> 1) & 1;

			if (uu0 != *iter0++)
				throw RTE_LOC;
			if (uu1 != *iter1++)
				throw RTE_LOC;
		}
	}
}

void AltModWPrf_sampleMod3_test(const oc::CLP& cmd)
{
	{
		// The 65th output block crosses the 128-block random buffer refill.
		PRNG prng(oc::ZeroBlock);
		const auto firstBlock = prng.mBlockIdx;
		std::array<block, 65> msb, lsb;
		sampleMod3Lookup(prng, msb, lsb);
		// This seed uses two 128-block fills and one 8-block rejection refill.
		if (prng.mBlockIdx != firstBlock + 2 * 128 + 8 || prng.mBytesIdx != 0)
			throw RTE_LOC;
		if (msb.front() != block(0x4891092ad0281014ull, 0xb804758096382277ull) ||
			lsb.front() != block(0x1648668104450b62ull, 0x439b084468069488ull) ||
			msb.back() != block(0x850452644220150bull, 0x089706388b014672ull) ||
			lsb.back() != block(0x0a9a08102c530234ull, 0x360850075462380dull))
			throw RTE_LOC;
	}
	std::array<std::array<u8, 5>, 256>const mod3TableFull{ {
	{  0, 0, 0, 0, 0},{  1, 0, 0, 0, 0},{  2, 0, 0, 0, 0},{  0, 1, 0, 0, 0},{  1, 1, 0, 0, 0},{  2, 1, 0, 0, 0},{  0, 2, 0, 0, 0},{  1, 2, 0, 0, 0},
	{  2, 2, 0, 0, 0},{  0, 0, 1, 0, 0},{  1, 0, 1, 0, 0},{  2, 0, 1, 0, 0},{  0, 1, 1, 0, 0},{  1, 1, 1, 0, 0},{  2, 1, 1, 0, 0},{  0, 2, 1, 0, 0},
	{  1, 2, 1, 0, 0},{  2, 2, 1, 0, 0},{  0, 0, 2, 0, 0},{  1, 0, 2, 0, 0},{  2, 0, 2, 0, 0},{  0, 1, 2, 0, 0},{  1, 1, 2, 0, 0},{  2, 1, 2, 0, 0},
	{  0, 2, 2, 0, 0},{  1, 2, 2, 0, 0},{  2, 2, 2, 0, 0},{  0, 0, 0, 1, 0},{  1, 0, 0, 1, 0},{  2, 0, 0, 1, 0},{  0, 1, 0, 1, 0},{  1, 1, 0, 1, 0},
	{  2, 1, 0, 1, 0},{  0, 2, 0, 1, 0},{  1, 2, 0, 1, 0},{  2, 2, 0, 1, 0},{  0, 0, 1, 1, 0},{  1, 0, 1, 1, 0},{  2, 0, 1, 1, 0},{  0, 1, 1, 1, 0},
	{  1, 1, 1, 1, 0},{  2, 1, 1, 1, 0},{  0, 2, 1, 1, 0},{  1, 2, 1, 1, 0},{  2, 2, 1, 1, 0},{  0, 0, 2, 1, 0},{  1, 0, 2, 1, 0},{  2, 0, 2, 1, 0},
	{  0, 1, 2, 1, 0},{  1, 1, 2, 1, 0},{  2, 1, 2, 1, 0},{  0, 2, 2, 1, 0},{  1, 2, 2, 1, 0},{  2, 2, 2, 1, 0},{  0, 0, 0, 2, 0},{  1, 0, 0, 2, 0},
	{  2, 0, 0, 2, 0},{  0, 1, 0, 2, 0},{  1, 1, 0, 2, 0},{  2, 1, 0, 2, 0},{  0, 2, 0, 2, 0},{  1, 2, 0, 2, 0},{  2, 2, 0, 2, 0},{  0, 0, 1, 2, 0},
	{  1, 0, 1, 2, 0},{  2, 0, 1, 2, 0},{  0, 1, 1, 2, 0},{  1, 1, 1, 2, 0},{  2, 1, 1, 2, 0},{  0, 2, 1, 2, 0},{  1, 2, 1, 2, 0},{  2, 2, 1, 2, 0},
	{  0, 0, 2, 2, 0},{  1, 0, 2, 2, 0},{  2, 0, 2, 2, 0},{  0, 1, 2, 2, 0},{  1, 1, 2, 2, 0},{  2, 1, 2, 2, 0},{  0, 2, 2, 2, 0},{  1, 2, 2, 2, 0},
	{  2, 2, 2, 2, 0},{  0, 0, 0, 0, 1},{  1, 0, 0, 0, 1},{  2, 0, 0, 0, 1},{  0, 1, 0, 0, 1},{  1, 1, 0, 0, 1},{  2, 1, 0, 0, 1},{  0, 2, 0, 0, 1},
	{  1, 2, 0, 0, 1},{  2, 2, 0, 0, 1},{  0, 0, 1, 0, 1},{  1, 0, 1, 0, 1},{  2, 0, 1, 0, 1},{  0, 1, 1, 0, 1},{  1, 1, 1, 0, 1},{  2, 1, 1, 0, 1},
	{  0, 2, 1, 0, 1},{  1, 2, 1, 0, 1},{  2, 2, 1, 0, 1},{  0, 0, 2, 0, 1},{  1, 0, 2, 0, 1},{  2, 0, 2, 0, 1},{  0, 1, 2, 0, 1},{  1, 1, 2, 0, 1},
	{  2, 1, 2, 0, 1},{  0, 2, 2, 0, 1},{  1, 2, 2, 0, 1},{  2, 2, 2, 0, 1},{  0, 0, 0, 1, 1},{  1, 0, 0, 1, 1},{  2, 0, 0, 1, 1},{  0, 1, 0, 1, 1},
	{  1, 1, 0, 1, 1},{  2, 1, 0, 1, 1},{  0, 2, 0, 1, 1},{  1, 2, 0, 1, 1},{  2, 2, 0, 1, 1},{  0, 0, 1, 1, 1},{  1, 0, 1, 1, 1},{  2, 0, 1, 1, 1},
	{  0, 1, 1, 1, 1},{  1, 1, 1, 1, 1},{  2, 1, 1, 1, 1},{  0, 2, 1, 1, 1},{  1, 2, 1, 1, 1},{  2, 2, 1, 1, 1},{  0, 0, 2, 1, 1},{  1, 0, 2, 1, 1},
	{  2, 0, 2, 1, 1},{  0, 1, 2, 1, 1},{  1, 1, 2, 1, 1},{  2, 1, 2, 1, 1},{  0, 2, 2, 1, 1},{  1, 2, 2, 1, 1},{  2, 2, 2, 1, 1},{  0, 0, 0, 2, 1},
	{  1, 0, 0, 2, 1},{  2, 0, 0, 2, 1},{  0, 1, 0, 2, 1},{  1, 1, 0, 2, 1},{  2, 1, 0, 2, 1},{  0, 2, 0, 2, 1},{  1, 2, 0, 2, 1},{  2, 2, 0, 2, 1},
	{  0, 0, 1, 2, 1},{  1, 0, 1, 2, 1},{  2, 0, 1, 2, 1},{  0, 1, 1, 2, 1},{  1, 1, 1, 2, 1},{  2, 1, 1, 2, 1},{  0, 2, 1, 2, 1},{  1, 2, 1, 2, 1},
	{  2, 2, 1, 2, 1},{  0, 0, 2, 2, 1},{  1, 0, 2, 2, 1},{  2, 0, 2, 2, 1},{  0, 1, 2, 2, 1},{  1, 1, 2, 2, 1},{  2, 1, 2, 2, 1},{  0, 2, 2, 2, 1},
	{  1, 2, 2, 2, 1},{  2, 2, 2, 2, 1},{  0, 0, 0, 0, 2},{  1, 0, 0, 0, 2},{  2, 0, 0, 0, 2},{  0, 1, 0, 0, 2},{  1, 1, 0, 0, 2},{  2, 1, 0, 0, 2},
	{  0, 2, 0, 0, 2},{  1, 2, 0, 0, 2},{  2, 2, 0, 0, 2},{  0, 0, 1, 0, 2},{  1, 0, 1, 0, 2},{  2, 0, 1, 0, 2},{  0, 1, 1, 0, 2},{  1, 1, 1, 0, 2},
	{  2, 1, 1, 0, 2},{  0, 2, 1, 0, 2},{  1, 2, 1, 0, 2},{  2, 2, 1, 0, 2},{  0, 0, 2, 0, 2},{  1, 0, 2, 0, 2},{  2, 0, 2, 0, 2},{  0, 1, 2, 0, 2},
	{  1, 1, 2, 0, 2},{  2, 1, 2, 0, 2},{  0, 2, 2, 0, 2},{  1, 2, 2, 0, 2},{  2, 2, 2, 0, 2},{  0, 0, 0, 1, 2},{  1, 0, 0, 1, 2},{  2, 0, 0, 1, 2},
	{  0, 1, 0, 1, 2},{  1, 1, 0, 1, 2},{  2, 1, 0, 1, 2},{  0, 2, 0, 1, 2},{  1, 2, 0, 1, 2},{  2, 2, 0, 1, 2},{  0, 0, 1, 1, 2},{  1, 0, 1, 1, 2},
	{  2, 0, 1, 1, 2},{  0, 1, 1, 1, 2},{  1, 1, 1, 1, 2},{  2, 1, 1, 1, 2},{  0, 2, 1, 1, 2},{  1, 2, 1, 1, 2},{  2, 2, 1, 1, 2},{  0, 0, 2, 1, 2},
	{  1, 0, 2, 1, 2},{  2, 0, 2, 1, 2},{  0, 1, 2, 1, 2},{  1, 1, 2, 1, 2},{  2, 1, 2, 1, 2},{  0, 2, 2, 1, 2},{  1, 2, 2, 1, 2},{  2, 2, 2, 1, 2},
	{  0, 0, 0, 2, 2},{  1, 0, 0, 2, 2},{  2, 0, 0, 2, 2},{  0, 1, 0, 2, 2},{  1, 1, 0, 2, 2},{  2, 1, 0, 2, 2},{  0, 2, 0, 2, 2},{  1, 2, 0, 2, 2},
	{  2, 2, 0, 2, 2},{  0, 0, 1, 2, 2},{  1, 0, 1, 2, 2},{  2, 0, 1, 2, 2},{  0, 1, 1, 2, 2},{  1, 1, 1, 2, 2},{  2, 1, 1, 2, 2},{  0, 2, 1, 2, 2},
	{  1, 2, 1, 2, 2},{  2, 2, 1, 2, 2},{  0, 0, 2, 2, 2},{  1, 0, 2, 2, 2},{  2, 0, 2, 2, 2},{  0, 1, 2, 2, 2},{  1, 1, 2, 2, 2},{  2, 1, 2, 2, 2},
	{  0, 2, 2, 2, 2},{  1, 2, 2, 2, 2},{  2, 2, 2, 2, 2},{  0, 0, 0, 0, 0},{  0, 0, 0, 0, 0},{  0, 0, 0, 0, 0},{  0, 0, 0, 0, 0},{  0, 0, 0, 0, 0},
	{  0, 0, 0, 0, 0},{  0, 0, 0, 0, 0},{  0, 0, 0, 0, 0},{  0, 0, 0, 0, 0},{  0, 0, 0, 0, 0},{  0, 0, 0, 0, 0},{  0, 0, 0, 0, 0},{  0, 0, 0, 0, 0},
	} };


	for (u64 n : {1ull, 4ull, 123ull/*, 1ull << cmd.getOr("nn", 16)*/})
	{

		u64 t = cmd.getOr("t", 1);
		PRNG prng(block(22132, cmd.getOr("s", 1)));

		bool failed = false;

		for (u64 i = 0; i < 256; ++i)
		{
			auto vals = mod3TableFull[i];
			auto lsb = mod3TableLsb[i];
			auto msb = mod3TableMsb[i];
			if (i < 243)
			{
				if (mod3TableV[i] != 5)
					throw RTE_LOC;


				for (u64 j = 0; j < 5; ++j)
				{
					if ((vals[j] & 1) != ((lsb >> j) & 1))
						throw RTE_LOC;

					if (((vals[j] >> 1) & 1) != ((msb >> j) & 1))
						throw RTE_LOC;
				}
			}
			else
			{
				if (mod3TableV[i] != 0)
					throw RTE_LOC;

				for (u64 j = 0; j < 5; ++j)
					if (vals[j] != 0)
						throw RTE_LOC;

				if (lsb)
					throw RTE_LOC;
				if (msb)
					throw RTE_LOC;
			}
		}


		for (u64 tt = 0; tt < t; ++tt)
		{
			oc::AlignedUnVector<block> lsb(n), msb(n);
			sampleMod3Lookup(prng, msb, lsb);
			std::array<int, 3> counts{ 0,0,0 };

			for (u64 i = 0;i < n;++i)
			{
				for (u64 j = 0;j < 128; ++j)
				{
					auto lsbj = bit(lsb[i], j);
					auto msbj = bit(msb[i], j);
					u64 v = lsbj + 2 * msbj;

					if (v > 2)
						throw RTE_LOC;

					++counts[v];
				}
			}

			u64 N = n * 128;
			u64 exp = N / 3;
			u64 eps = 2 * std::sqrt(N);
			for (auto c : counts)
				if ((c < (exp - eps) || c > (exp + eps)) && n > 200)
				{
					//std::cout << "{ " << counts[0] << ", " << counts[1] << ", " << counts[2] << "}" << std::endl;
					//std::cout << "exp = " << exp << " eps = " << eps << std::endl;
					failed = true;
				}

			//std::cout << "lookup3 { " << counts[0] << ", " << counts[1] << ", " << counts[2] << "}" << std::endl;

		}
		if (failed)
			throw RTE_LOC;
	}
}

void AltModWPrf_AMult_test(const oc::CLP& cmd)
{
	F3AccPermCode c;
	u64 k = cmd.getOr("k", 128);
	u64 n = cmd.getOr("n", k / 2);
	u64 l = cmd.getOr("l", 1 << 4);
	u64 p = cmd.getOr("p", 0);

	c.init(k, n, p);

	//std::cout << c << std::endl;
	{
		auto m = c.getMatrix();
		std::vector<u8> v(k), y(n);
		PRNG prng(oc::ZeroBlock);
		for (u64 j = 0; j < k;++j)
			v[j] = prng.get<u8>() % 3;

		auto vv = v;
		auto yy = y;
		c.encode<u8>(vv, y);

		for (u64 i = 0; i < n; ++i)
		{
			for (u64 j = 0; j < k;++j)
			{
				yy[i] = (yy[i] + (v[j] * m(i, j))) % 3;
			}
		}

		if (yy != y)
			throw RTE_LOC;
	}

	{
		oc::Matrix<block>msb(k, l), lsb(k, l);
		oc::Matrix<block>msbOut(n, l), lsbOut(n, l);
		PRNG prng(oc::ZeroBlock);
		sampleMod3Lookup(prng, msb, lsb);


		auto msb2 = msb;
		auto lsb2 = lsb;
		c.encode(msb2, lsb2, msbOut, lsbOut);

		for (u64 i = 0; i < l * 128; ++i)
		{
			std::vector<u8> v(k), y(n), yy(n);
			for (u64 j = 0; j < k; ++j)
				v[j] = bit(lsb[j].data(), i) + bit(msb[j].data(), i) * 2;
			for (u64 j = 0; j < n; ++j)
				y[j] = bit(lsbOut[j].data(), i) + bit(msbOut[j].data(), i) * 2;

			c.encode<u8>(v, yy);

			if (y != yy)
			{
				std::cout << "v  ";
				for (u64 j = 0; j < k; ++j)
					std::cout << (int)v[j] << " ";
				std::cout << std::endl;
				std::cout << "y  ";
				for (u64 j = 0; j < n; ++j)
					std::cout << (int)y[j] << " ";
				std::cout << std::endl << "yy ";
				for (u64 j = 0; j < n; ++j)
					std::cout << (int)yy[j] << " ";
				std::cout << std::endl;
				throw RTE_LOC;
			}
		}
	}
}



    struct block256
    {
        std::array<oc::block, 2> mData;

        void operator^=(const block256& x)
        {
            mData[0] = mData[0] ^ x.mData[0];
            mData[1] = mData[1] ^ x.mData[1];
        }
        block256 operator&(const block256& x) const
        {
            block256 r;
            r.mData[0] = mData[0] & x.mData[0];
            r.mData[1] = mData[1] & x.mData[1];
            return r;
        }

        block256 operator^(const block256& x) const
        {
            auto r = *this;
            r ^= x;
            return r;
        }

        block256 rotate(u64 i) const
        {
            auto xx = *(std::bitset<256>*)this;
            auto low = xx >> i;
            auto hgh = xx << (256 - i);

            auto m = hgh ^ low;
			return std::bit_cast<block256>(m);
        }

        bool operator==(const block256& x) const
        {
            return std::memcmp(this, &x, sizeof(x)) == 0;
        }
        bool operator!=(const block256& x) const
        {
            return std::memcmp(this, &x, sizeof(x)) != 0;
        }

        oc::block& operator[](u64 i) { return mData[i]; }
    };

void AltModWPrf_BMult_test(const oc::CLP& cmd)
{
	u64 n = 1ull << cmd.getOr("nn", 12);
	u64 n128 = n / 128;
	oc::Matrix<oc::block> v(256, n128);
	std::vector<block256> V(n);
	PRNG prng(oc::ZeroBlock);
	prng.get(V.data(), V.size());

	for (u64 i = 0; i < n; ++i)
	{
		for (u64 j = 0; j < 256; ++j)
		{
			*oc::BitIterator((u8*)&v(j, 0), i) = bit(V[i], j);
		}
	}
	std::vector<oc::block> y(n);
	AltModPrf::compressB(v, y);

	//oc::Matrix<u64> B(128, 256);
	//for (u64 i = 0; i < 128; ++i)
	//{
	//    u64 j = 0;
	//    for (; j < 128; ++j)
	//        B(i, j) = j == i ? 1 : 0;
	//    for (; j < B.cols(); ++j)
	//    {
	//        B(i, j) = *oc::BitIterator((u8*)&AltModWPrf::mB[i], j - 128);
	//    }
	//}

	for (u64 i = 0; i < n; ++i)
	{
		//auto Y = AltModPrf::compress(V[i]);
		//if (y[i] != Y)
		//	throw RTE_LOC;

		auto Y = oc::ZeroBlock;
		AltModPrf::mBCode.encode(V[i].mData[1], Y);

		{
			auto w = V[i];
			oc::AlignedArray<block, 128> bw;

			for (u64 i = 0; i < 128; ++i)
			{
				//bw[0][i] = B[i].mData[0] & w.mData[0];
				bw[i] = AltModPrf::mB[i] & w.mData[1];
			}
			oc::transpose128(bw.data());
			//oc::transpose128(bw[1].data());

			block r = oc::ZeroBlock;//w[0];
			//memset(&r, 0, sizeof(r));
			//for (u64 i = 0; i < 128; ++i)
			//    r = r ^ bw[0][i];
			for (u64 i = 0; i < 128; ++i)
				r = r ^ bw[i];

			//oc::block b = oc::ZeroBlock;
			//for (u64 ii = 0; ii < 128; ++ii)
			//{
			//    if (bit(V[i].mData[1], ii))
			//    {
			//        b = b ^ AltModWPrf::mB[ii];
			//    }
			//}

			//if (Y != b)
			//    throw RTE_LOC;
			if (r != Y)
				throw RTE_LOC;
		}
		//Y = Y ^ V[i].mData[0];
		Y = Y ^ V[i].mData[0];
		if (y[i] != Y)
			throw RTE_LOC;
	}
}

void AltModWPrf_correction_test(const oc::CLP& cmd)
{
	//u64 n = (1 << 16) + 321;
	//u64 m = 2;
	//bool mock = false;
	//oc::Matrix<block>
	//	x0(n, m), x1(n, m),
	//	yLsb0(n, m), yLsb1(n, m),
	//	yMsb0(n, m), yMsb1(n, m),
	//	yLsb(n, m), yMsb(n, m);

	//PRNG prng(oc::CCBlock);

	//prng.get<block>(x0);
	//prng.get<block>(x1);
	//auto sock = coproto::LocalAsyncSocket::makePair();
	//CorGenerator gen[2];
	//gen[0].init(sock[0].fork(), prng, 0, 10, 1 << 16, mock);
	//gen[1].init(sock[1].fork(), prng, 1, 10, 1 << 16, mock);

	//auto recvReq = gen[0].request<TritOtRecv>(x0.size() * 128);
	//auto sendReq = gen[1].request<TritOtSend>(x1.size() * 128);

	//macoro::sync_wait(macoro::when_all_ready(
	//	keyMultCorrectionRecv(recvReq, x0, yLsb0, yMsb0, sock[0], true),
	//	keyMultCorrectionSend(sendReq, x1, yLsb1, yMsb1, sock[1], true)
	//));

	//// y = y0 + y1
	//mod3Add(yMsb, yLsb, yMsb0, yLsb0, yMsb1, yLsb1);

	//for (u64 i = 0; i < x0.size(); ++i)
	//{
	//	if (yMsb(i) != oc::ZeroBlock)
	//		throw RTE_LOC;


	//	if (yLsb(i) != (x0(i) & x1(i)))
	//		throw RTE_LOC;


	//}
}



void AltModWPrf_convertToF3_test(const oc::CLP& cmd)
{
	u64 n = (1 << 14) + 321;
	bool mock = true;
	oc::AlignedUnVector<block>
		x0(n), x1(n),
		yLsb0(n), yLsb1(n),
		yMsb0(n), yMsb1(n),
		yLsb(n), yMsb(n);

	PRNG prng(oc::CCBlock);

	prng.get<block>(x0);
	prng.get<block>(x1);
	auto sock = coproto::LocalAsyncSocket::makePair();
	CorGenerator gen[2];
	gen[0].init(sock[0].fork(), prng, 0, 10, 1 << 14, mock);
	gen[1].init(sock[1].fork(), prng, 1, 10, 1 << 14, mock);

	ConvertToF3Sender sender;
	ConvertToF3Recver recver;

	sender.init(n * 128, gen[0]);
	recver.init(n * 128, gen[1]);

	macoro::sync_wait(macoro::when_all_ready(
		sender.convert(x0, sock[0], yMsb0, yLsb0),
		recver.convert(x1, sock[1], yMsb1, yLsb1),
		gen[0].start(),
		gen[1].start()
	));

	// y = y0 + y1
	mod3Add(yMsb, yLsb, yMsb0, yLsb0, yMsb1, yLsb1);

	for (u64 i = 0; i < x0.size() * 128; ++i)
	{
		if (bit(yMsb.data(), i))
			throw RTE_LOC;


		if (bit(yLsb.data(), i) != (bit(x0.data(), i) ^ bit(x1.data(), i)))
			throw RTE_LOC;

	}
}

void AltModWPrf_keyMult_test(const oc::CLP& cmd)
{
	u64 n = cmd.getOr("n", 1 << 12) + 31;
	PRNG prng(oc::CCBlock);
	bool mock = true;

	auto sock = coproto::LocalAsyncSocket::makePair();

	for (auto shared : { false, true })
	{
		CorGenerator gen[2];
		gen[0].init(sock[0].fork(), prng, 0, 10, 1 << 10, mock);
		gen[1].init(sock[1].fork(), prng, 1, 10, 1 << 10, mock);
		AltModKeyMultSender sender;
		AltModKeyMultReceiver recver;

		std::vector<std::array<block, 2>> sendOTSender(AltModPrf::KeySize);
		std::vector<block> recvOTRecver(AltModPrf::KeySize);
		AltModPrf::KeyType sendKey, recvKey, key;
		prng.get(sendOTSender.data(), sendOTSender.size());
		sendKey = prng.get();
		recvKey = prng.get();

		for (u64 i = 0; i < AltModPrf::KeySize; ++i)
		{
			recvOTRecver[i] = sendOTSender[i][bit(recvKey, i)];
		}

		if (shared)
		{
			key = sendKey ^ recvKey;
			sender.init(gen[0], sendKey, sendOTSender);
			recver.init(gen[1], recvKey, recvOTRecver);
		}
		else
		{
			key = recvKey;
			sender.init(gen[0], {}, sendOTSender);
			recver.init(gen[1], recvKey, recvOTRecver);
		}

		oc::Matrix<block> x(AltModPrf::KeySize, divCeil(n, 128)), yMsb0, yMsb1, yLsb0, yLsb1;
		block mask = oc::AllOneBlock;
		for (u64 i = (n % 128); i < 128; ++i)
			*oc::BitIterator(&mask, i) = 0;

		for (u64 i = 0; i < x.rows(); ++i)
		{
			prng.get(x[i].data(), x[i].size());
			x[i].back() &= mask;
		}

		macoro::sync_wait(macoro::when_all_ready(
			gen[0].start(),
			gen[1].start(),
			sender.mult(x, {}, yLsb0, yMsb0, sock[0]),
			recver.mult(n, yLsb1, yMsb1, sock[1])
		));


		if (yMsb0.rows() != AltModPrf::KeySize)
			throw RTE_LOC;
		if (yMsb1.rows() != AltModPrf::KeySize)
			throw RTE_LOC;
		if (yLsb0.rows() != AltModPrf::KeySize)
			throw RTE_LOC;
		if (yLsb1.rows() != AltModPrf::KeySize)
			throw RTE_LOC;
		if (yMsb0.cols() != x.cols())
			throw RTE_LOC;
		if (yMsb1.cols() != x.cols())
			throw RTE_LOC;
		if (yLsb0.cols() != x.cols())
			throw RTE_LOC;
		if (yLsb1.cols() != x.cols())
			throw RTE_LOC;

		auto yLsb = yLsb0;
		auto yMsb = yLsb0;

		mod3Add(yMsb, yLsb, yMsb0, yLsb0, yMsb1, yLsb1);

		for (u64 i = 0; i < yLsb.rows(); ++i)
		{
			auto ki = bit(key, i);
			for (u64 j = 0; j < x.cols();++j)
			{
				if (yMsb(i, j) != oc::ZeroBlock)
					throw RTE_LOC;

				if (ki == 0)
				{
					if (yLsb(i, j) != oc::ZeroBlock)
						throw RTE_LOC;
				}
				else
				{
					if (yLsb(i, j) != x(i, j))
						throw RTE_LOC;
				}
			}

		}
	}
}

void AltModWPrf_keyMultF3_test(const oc::CLP& cmd)
{

	u64 n = cmd.getOr("n", 1 << 12) + 31;
	PRNG prng(oc::CCBlock);
	bool mock = true;

	auto sock = coproto::LocalAsyncSocket::makePair();

	for (auto shared : { false, true })
	{
		CorGenerator gen[2];
		gen[0].init(sock[0].fork(), prng, 0, 10, 1 << 10, mock);
		gen[1].init(sock[1].fork(), prng, 1, 10, 1 << 10, mock);
		AltModKeyMultSender sender;
		AltModKeyMultReceiver recver;

		std::vector<std::array<block, 2>> sendOTSender(AltModPrf::KeySize);
		std::vector<block> recvOTRecver(AltModPrf::KeySize);
		AltModPrf::KeyType sendKey, recvKey, key;
		prng.get(sendOTSender.data(), sendOTSender.size());
		sendKey = prng.get();
		recvKey = prng.get();

		for (u64 i = 0; i < AltModPrf::KeySize; ++i)
		{
			recvOTRecver[i] = sendOTSender[i][bit(recvKey, i)];
		}

		if (shared)
		{
			key = sendKey ^ recvKey;
			sender.init(gen[0], sendKey, sendOTSender);
			recver.init(gen[1], recvKey, recvOTRecver);
		}
		else
		{
			key = recvKey;
			sender.init(gen[0], {}, sendOTSender);
			recver.init(gen[1], recvKey, recvOTRecver);
		}

		oc::Matrix<block> 
			xLsb(AltModPrf::KeySize, divCeil(n, 128)),
			xMsb(AltModPrf::KeySize, divCeil(n, 128)),
			yMsb0, yMsb1, yLsb0, yLsb1;
		block mask = oc::AllOneBlock;
		for (u64 i = (n % 128); i < 128; ++i)
			*oc::BitIterator(&mask, i) = 0;

		for (u64 i = 0; i < xLsb.rows(); ++i)
		{
			prng.get(xLsb[i].data(), xLsb[i].size());
			prng.get(xMsb[i].data(), xMsb[i].size());

			// make sure its not 3...
			for (u64 j = 0; j < xLsb.cols(); ++j)
				xLsb(i, j) = xLsb(i, j) & ~xMsb(i, j);

			xLsb[i].back() &= mask;
			xMsb[i].back() &= mask;
		}

		macoro::sync_wait(macoro::when_all_ready(
			gen[0].start(),
			gen[1].start(),
			sender.mult(xLsb, xMsb, yLsb0, yMsb0, sock[0]),
			recver.mult(n, yLsb1, yMsb1, sock[1])
		));


		if (yMsb0.rows() != AltModPrf::KeySize)
			throw RTE_LOC;
		if (yMsb1.rows() != AltModPrf::KeySize)
			throw RTE_LOC;
		if (yLsb0.rows() != AltModPrf::KeySize)
			throw RTE_LOC;
		if (yLsb1.rows() != AltModPrf::KeySize)
			throw RTE_LOC;
		if (yMsb0.cols() != xLsb.cols())
			throw RTE_LOC;
		if (yMsb1.cols() != xLsb.cols())
			throw RTE_LOC;
		if (yLsb0.cols() != xLsb.cols())
			throw RTE_LOC;
		if (yLsb1.cols() != xLsb.cols())
			throw RTE_LOC;

		auto yLsb = yLsb0;
		auto yMsb = yLsb0;

		mod3Add(yMsb, yLsb, yMsb0, yLsb0, yMsb1, yLsb1);

		for (u64 i = 0; i < yLsb.rows(); ++i)
		{
			auto ki = bit(key, i);
			for (u64 j = 0; j < xMsb.cols();++j)
			{

				if (ki == 0)
				{
					if (yMsb(i, j) != oc::ZeroBlock)
						throw RTE_LOC;
					if (yLsb(i, j) != oc::ZeroBlock)
						throw RTE_LOC;
				}
				else
				{
					if (yMsb(i, j) != xMsb(i, j))
						throw RTE_LOC;

					if (yLsb(i, j) != xLsb(i, j))
						throw RTE_LOC;
				}
			}

		}
	}

}


void AltModWPrf_mod2Ole_test(const oc::CLP& cmd)
{


	u64 n = cmd.getOr("n", 128);
	u64 m = cmd.getOr("m", 128);
	auto m128 = oc::divCeil(m, 128);


	u64 printI = cmd.getOr("i", -1);
	u64 printJ = cmd.getOr("j", -1);

	PRNG prng0(oc::ZeroBlock);
	PRNG prng1(oc::OneBlock);
	oc::Timer timer;

	AltModWPrfSender sender;
	AltModWPrfReceiver recver;

	//sender.mPrintI = printI;
	//sender.mPrintJ = printJ;
	//recver.mPrintI = printI;
	//recver.mPrintJ = printJ;

	sender.setTimer(timer);
	recver.setTimer(timer);

	oc::Matrix<u16> u(n, m);
	std::array<oc::Matrix<u16>, 2> us;
	us[0].resize(n, m);
	us[1].resize(n, m);
	for (u64 i = 0; i < u.rows(); ++i)
	{
		for (u64 j = 0; j < u.cols(); ++j)
		{

			u(i, j) = prng0.get<u8>() % 3;
			us[0](i, j) = prng0.get<u8>() % 3;
			us[1](i, j) = u8(u(i, j) + 3 - us[0](i, j)) % 3;
			assert((u8(us[0](i, j) + us[1](i, j)) % 3) == u(i, j));
		}
	}


	//auto us = xorShare(u, prng0);

	std::array<oc::Matrix<oc::block>, 2> u0s, u1s;
	u0s[0].resize(n, m128);
	u0s[1].resize(n, m128);
	u1s[0].resize(n, m128);
	u1s[1].resize(n, m128);
	mod3BitDecompostion(us[0], u0s[0], u1s[0]);
	mod3BitDecompostion(us[1], u0s[1], u1s[1]);

	//if (i == printI && j == printJ)
	if (printI < n)
	{
		auto i = printI;
		auto j = printJ;
		std::cout << "\nu(" << i << ", " << j << ") \n"
			<< "    = " << u(i, j) << "\n"
			<< "    = " << us[0](i, j) << " + " << us[1](i, j) << "\n"
			<< "    = " << bit(u1s[0](i, 0), j) << bit(u0s[0](i, 0), j) << " + "
			<< bit(u1s[1](i, 0), j) << bit(u0s[1](i, 0), j) << std::endl;
	}

	//auto u0s = share(u0, prng0);
	//auto u1s = share(u1, prng0);
	std::array<oc::Matrix<oc::block>, 2> outs;
	outs[0].resize(n, m128);
	outs[1].resize(n, m128);

	auto sock = coproto::LocalAsyncSocket::makePair();


	CorGenerator ole0, ole1;
	auto chls = coproto::LocalAsyncSocket::makePair();
	ole0.init(chls[0].fork(), prng0, 0, 1, 1 << 18, cmd.getOr("mock", 1));
	ole1.init(chls[1].fork(), prng1, 1, 1, 1 << 18, cmd.getOr("mock", 1));


	//sender.init(n, ole0);
	//recver.init(n, ole1);
	//    sender.request(ole0);
	//    recver.request(ole1);
	sender.mMod2OleReq = ole0.binOleRequest(u0s[0].rows() * u0s[0].cols() * 256);
	recver.mMod2OleReq = ole1.binOleRequest(u0s[0].rows() * u0s[0].cols() * 256);
	sender.mUseMod2F4Ot = false;
	recver.mUseMod2F4Ot = false;
	//auto s0 = sender.mOleReq_.start() | macoro::make_eager();
	//auto s1 = recver.mOleReq_.start() | macoro::make_eager();
	//sender.mHasOleRequest = true;
	//recver.mHasOleRequest = true;

	auto r = macoro::sync_wait(macoro::when_all_ready(
		sender.mod2Ole(u0s[0], u1s[0], outs[0], sock[0]),
		recver.mod2Ole(u0s[1], u1s[1], outs[1], sock[1]),
		ole0.start(),
		ole1.start()
	));

	std::get<0>(r).result();
	std::get<1>(r).result();
	std::get<2>(r).result();
	std::get<3>(r).result();

	auto out = reveal(outs);


	for (u64 i = 0; i < n; ++i)
	{
		auto iter = oc::BitIterator((u8*)out[i].data());
		for (u64 j = 0; j < m; ++j)
		{
			u8 uij = u(i, j);
			u8 exp = uij % 2;
			u8 act = *iter++;
			if (exp != act)
			{
				std::cout << "i " << i << " j " << j << "\n"
					<< "act " << int(act) << " = "
					<< *oc::BitIterator((u8*)&outs[0](i, 0), j) << " ^ "
					<< *oc::BitIterator((u8*)&outs[1](i, 0), j) << std::endl
					<< "exp " << int(exp) << " = " << u(i, j) << " = " << us[0](i, j) << " + " << us[1](i, j) << std::endl;
				throw RTE_LOC;
			}
		}
	}
}


void AltModWPrf_mod2OtF4_test(const oc::CLP& cmd)
{


	u64 n = cmd.getOr("n", 12802);
	u64 m = cmd.getOr("m", 128);
	auto m128 = oc::divCeil(m, 128);


	u64 printI = cmd.getOr("i", -1);
	u64 printJ = cmd.getOr("j", -1);

	PRNG prng0(oc::ZeroBlock);
	PRNG prng1(oc::OneBlock);
	oc::Timer timer;

	AltModWPrfSender sender;
	AltModWPrfReceiver recver;

	//sender.mPrintI = printI;
	//sender.mPrintJ = printJ;
	//recver.mPrintI = printI;
	//recver.mPrintJ = printJ;

	sender.setTimer(timer);
	recver.setTimer(timer);

	oc::Matrix<u16> u(n, m);
	std::array<oc::Matrix<u16>, 2> us;
	us[0].resize(n, m);
	us[1].resize(n, m);
	for (u64 i = 0; i < u.rows(); ++i)
	{
		for (u64 j = 0; j < u.cols(); ++j)
		{

			u(i, j) = prng0.get<u8>() % 3;
			us[0](i, j) = prng0.get<u8>() % 3;
			us[1](i, j) = u8(u(i, j) + 3 - us[0](i, j)) % 3;
			assert((u8(us[0](i, j) + us[1](i, j)) % 3) == u(i, j));
		}
	}


	//auto us = xorShare(u, prng0);

	std::array<oc::Matrix<oc::block>, 2> u0s, u1s;
	u0s[0].resize(n, m128);
	u0s[1].resize(n, m128);
	u1s[0].resize(n, m128);
	u1s[1].resize(n, m128);
	mod3BitDecompostion(us[0], u0s[0], u1s[0]);
	mod3BitDecompostion(us[1], u0s[1], u1s[1]);

	//if (i == printI && j == printJ)
	if (printI < n)
	{
		auto i = printI;
		auto j = printJ;
		std::cout << "\nu(" << i << ", " << j << ") \n"
			<< "    = " << u(i, j) << "\n"
			<< "    = " << us[0](i, j) << " + " << us[1](i, j) << "\n"
			<< "    = " << bit(u1s[0](i, 0), j) << bit(u0s[0](i, 0), j) << " + "
			<< bit(u1s[1](i, 0), j) << bit(u0s[1](i, 0), j) << std::endl;
	}

	//auto u0s = share(u0, prng0);
	//auto u1s = share(u1, prng0);
	std::array<oc::Matrix<oc::block>, 2> outs;
	outs[0].resize(n, m128);
	outs[1].resize(n, m128);

	auto sock = coproto::LocalAsyncSocket::makePair();


	CorGenerator ole0, ole1;
	auto chls = coproto::LocalAsyncSocket::makePair();
	ole0.init(chls[0].fork(), prng0, 0, 1, 1 << 18, cmd.getOr("mock", 1));
	ole1.init(chls[1].fork(), prng1, 1, 1, 1 << 18, cmd.getOr("mock", 1));


	//sender.init(n, ole0);
	//recver.init(n, ole1);
	//    sender.request(ole0);
	//    recver.request(ole1);
	sender.mMod2F4Req = ole0.request<F4BitOtSend>(u0s[0].rows() * u0s[0].cols() * 128);
	recver.mMod2F4Req = ole1.request<F4BitOtRecv>(u0s[0].rows() * u0s[0].cols() * 128);

	//auto s0 = sender.mOleReq_.start() | macoro::make_eager();
	//auto s1 = recver.mOleReq_.start() | macoro::make_eager();
	//sender.mHasOleRequest = true;
	//recver.mHasOleRequest = true;

	auto r = macoro::sync_wait(macoro::when_all_ready(
		sender.mod2OtF4(u0s[0], u1s[0], outs[0], sock[0]),
		recver.mod2OtF4(u0s[1], u1s[1], outs[1], sock[1]),
		ole0.start(),
		ole1.start()
	));

	std::get<0>(r).result();
	std::get<1>(r).result();
	std::get<2>(r).result();
	std::get<3>(r).result();

	auto out = reveal(outs);


	for (u64 i = 0; i < n; ++i)
	{
		auto iter = oc::BitIterator((u8*)out[i].data());
		for (u64 j = 0; j < m; ++j)
		{
			u8 uij = u(i, j);
			u8 exp = uij % 2;
			u8 act = *iter++;
			if (exp != act)
			{
				std::cout << "i " << i << " j " << j << "\n"
					<< "act " << int(act) << " = "
					<< *oc::BitIterator((u8*)&outs[0](i, 0), j) << " ^ "
					<< *oc::BitIterator((u8*)&outs[1](i, 0), j) << std::endl
					<< "exp " << int(exp) << " = " << u(i, j) << " = " << us[0](i, j) << " + " << us[1](i, j) << std::endl;
				throw RTE_LOC;
			}
		}
	}
}


void AltModWPrf_mod3_test(const oc::CLP& cmd)
{


	PRNG prng(oc::ZeroBlock);
	u64 n = 100;
	for (u64 i = 0; i < n;++i)
	{
		u8 x = prng.get<u8>() % 3;
		u8 y = prng.get<u8>() % 3;
		auto a = (x >> 1) & 1;
		auto b = x & 1;
		auto c = (y >> 1) & 1;
		auto d = y & 1;

		auto ab = a ^ b;
		auto z1 = (1 ^ d ^ b) * (ab ^ c);
		auto z0 = (1 ^ a ^ c) * (ab ^ d);
		auto e = (x + y) % 3;
		if (z0 != (e & 1))
			throw RTE_LOC;
		if (z1 != (e >> 1))
			throw RTE_LOC;
	}


	//     c
	// ab  0  1   // msb = bc+a(1+c)  = bc + a + ac
	// 00  0  0          = a + (b+a)c =
	// 01  0  1          = 
	// 10  1  0    
	// 
	//     0  1   // lsb = b(1+c) + (1+b+a)c
	// 00  0  1          = b + (1 + a) c
	// 01  1  0 
	// 10  0  0
	for (u64 i = 0; i < n;++i)
	{
		u8 x = prng.get<u8>() % 3;
		u8 c = prng.get<u8>() % 2;
		auto a = (x >> 1) & 1;
		auto b = x & 1;

		//auto ab = a ^ b;
		//auto z1 = (1 ^ b) * (ab ^ c);
		//auto z0 = (1 ^ a ^ c) * (ab);
		auto z1 = a ^ (a ^ b) * c;
		auto z0 = b ^ (1 ^ a) * c;
		auto e = (x + c) % 3;
		if (z0 != (e & 1))
			throw RTE_LOC;
		if (z1 != (e >> 1))
			throw RTE_LOC;

		oc::block A = oc::block::allSame(-a);
		oc::block B = oc::block::allSame(-b);
		oc::block C = oc::block::allSame(-c);
		oc::block Z0 = oc::block::allSame(-z0);
		oc::block Z1 = oc::block::allSame(-z1);

		mod3Add(
			span<oc::block>(&A, 1), span<oc::block>(&B, 1),
			span<oc::block>(&A, 1), span<oc::block>(&B, 1),
			span<oc::block>(&C, 1));

		if (Z0 != B)
			throw RTE_LOC;
		if (Z1 != A)
			throw RTE_LOC;
	}


	for (u64 i = 0; i < n;++i)
	{
		u8 x = prng.get<u8>() % 3;
		u8 y0 = prng.get<u8>() % 2;
		auto x1 = (x >> 1) & 1;
		auto x0 = x & 1;

		auto z = (x - y0 + 3) % 3;

		auto z1 = (z >> 1) & 1;
		auto z0 = z & 1;

		oc::block X0 = oc::block::allSame(-x0);
		oc::block X1 = oc::block::allSame(-x1);
		oc::block Y0 = oc::block::allSame(-y0);
		oc::block Z0 = oc::block{};
		oc::block Z1 = oc::block{};

		mod3Sub(
			span<oc::block>(&Z1, 1), span<oc::block>(&Z0, 1),
			span<oc::block>(&X1, 1), span<oc::block>(&X0, 1),
			span<oc::block>(&Y0, 1));

		if (Z0 != oc::block::allSame(-z0))
			throw RTE_LOC;
		if (Z1 != oc::block::allSame(-z1))
			throw RTE_LOC;
	}

}


void AltModWPrf_plain_test()
{

	u64 len = 128;
	u64 n = AltModPrf::KeySize;
	u64 m = 256;
	u64 t = 128;
	PRNG prng(oc::ZeroBlock);
	AltModPrf::KeyType kk = prng.get();
	oc::block xx = prng.get();

	AltModPrf::KeyType x;
	AltModPrf::expandInput(xx, x);
	//if (x.size() > 1) {

	//    for (u64 i = 0; i < x.size(); ++i)
	//        x[i] = xx ^ oc::block(i, i);
	//    oc::mAesFixedKey.hashBlocks<x.size() - 1>(x.data() + 1, x.data() + 1);
	//}
	//else
	//    x[0] = xx;

	AltModPrf prf;

	prf.setKey(kk);


	auto y = prf.eval(xx);

	oc::Matrix<u64> B(t, m);
	std::vector<u64> X(n), K(n), H(n), U(m), W(m), Y(t);
	for (u64 i = 0; i < n; ++i)
	{
		X[i] = *oc::BitIterator((u8*)&x, i);
		K[i] = *oc::BitIterator((u8*)&prf.mExpandedKey, i);
		H[i] = X[i] & K[i];

		//if (i < 20)
		//    std::cout << "H[" << i << "] = " << (H[i]) << " = " << K[i] << " * " << X[i] << std::endl;

	}
	for (u64 i = 0; i < t; ++i)
	{
		u64 j = 0;
		for (; j < 128; ++j)
			B(i, j) = j == i ? 1 : 0;
		for (; j < m; ++j)
		{
			B(i, j) = *oc::BitIterator((u8*)&AltModPrf::mB[i], j - 128);
		}
	}

	AltModPrf::mACode.encode<u64>(H, U);

	for (u64 i = 0; i < m; ++i)
	{
		W[i] = (U[i] % 3) % 2;
	}
	for (u64 i = 0; i < t; ++i)
	{
		for (u64 j = 0; j < m; ++j)
		{
			Y[i] ^= B(i, j) * W[j];
		}

		if (Y[i] != (u8)*oc::BitIterator((u8*)&y, i))
		{
			throw RTE_LOC;
		}
	}

	{
		std::vector<block> x(len), y(len);
		prng.get(x.data(), x.size());
		{
			oc::Matrix<block> ex(4 * 128, len / 128), ext(len, 4);
			prf.expandInput(x, ex);
			oc::transpose(ex, ext);


			//for (u64 i = 0; i < 128; ++i)
			//{
			//	std::cout << ex(i, 0) << std::endl;
			//}

			for (u64 i = 0; i < len; ++i)
			{
				AltModPrf::KeyType exp;
				prf.expandInput(x[i], exp);
				for (u64 j = 0; j < 4; ++j)
					if (ext[i][j] != exp[j])
					{
						std::cout << i << " " << j << " single " << exp[j] << " multi " << ext[i][j] << std::endl;
						throw RTE_LOC;
					}
			}
		}
		prf.eval(x, y);
		for (u64 i = 0; i < len; ++i)
		{
			auto exp = prf.eval(x[i]);
			if (y[i] != exp)
				throw RTE_LOC;
		}
	}
}

    struct block256m3
    {
        //std::array<oc::block, 2> mData;
        std::array<u8, 256> mData;
	};

void AltModProtoCheck(AltModWPrfSender& sender, AltModWPrfReceiver& recver)
{
	auto x = recver.mDebugInput;
	std::array<std::vector<block>, 2> xShares;
	auto n = x.size();

	if (sender.mDebugInput.size())
	{
		xShares[0] = x;
		xShares[1].resize(n);
		for (u64 i = 0; i < n; ++i)
		{
			xShares[1][i] = sender.mDebugInput[i];
			x[i] = x[i] ^ sender.mDebugInput[i];
		}
	}

	oc::Matrix<block> xt(AltModPrf::KeySize, oc::divCeil(n, 128));
	AltModPrf::expandInput(x, xt);

	auto key = sender.getKey();
	if (recver.getKey())
	{
		key = key ^ *recver.getKey();
	}

	for (u64 ii = 0; ii < n; ++ii)
	{

		std::array<u16, AltModPrf::KeySize> h;
		AltModPrf::KeyType X;
		std::array< AltModPrf::KeyType, 2> XShares;
		AltModPrf::expandInput(x[ii], X);
		if (sender.mDebugInput.size())
		{
			AltModPrf::expandInput(xShares[0][ii], XShares[0]);
			AltModPrf::expandInput(xShares[1][ii], XShares[1]);
		}

		//auto kIter = oc::BitIterator((u8*)key.data());
		//auto xIter = oc::BitIterator((u8*)X.data());
		//std::array<oc::BitIterator, 2> xSharesIter{
		//	oc::BitIterator((u8*)X0.data()),
		//	oc::BitIterator((u8*)X1.data())
		//};

		bool failed = false;
		//if (sender.mDebugInput.size())
		//{
		//	for (auto j = 0; j < 2;++j)
		//	{
		//		if (ii < 10)
		//		{
		//			std::cout << "x[" << ii << "][" << j << "] " << XShares[j] << std::endl;
		//		}

		//		auto& rkx0 = j ? recver.mKeyMultSender.mDebugEk0 : recver.mKeyMultRecver.mDebugEk0;
		//		auto& rkx1 = j ? recver.mKeyMultSender.mDebugEk1 : recver.mKeyMultRecver.mDebugEk1;
		//		auto& skx0 = j ? sender.mKeyMultRecver.mDebugEk0 : sender.mKeyMultSender.mDebugEk0;
		//		auto& skx1 = j ? sender.mKeyMultRecver.mDebugEk1 : sender.mKeyMultSender.mDebugEk1;


		//		for (u64 i = 0; i < AltModWPrf::KeySize; ++i)
		//		{

		//			if (bit(X, i) != bit(xt[i].data(), ii))
		//				throw RTE_LOC;

		//			u8 xi = bit(XShares[j], i);
		//			u8 ki = bit(key, i);
		//			h[i] = ki & xi;

		//			assert(rkx0.cols() == oc::divCeil(x.size(), 128));
		//			auto r0 = bit(rkx0.data(i), ii);
		//			auto r1 = bit(rkx1.data(i), ii);
		//			auto s0 = bit(skx0.data(i), ii);
		//			auto s1 = bit(skx1.data(i), ii);

		//			auto s = 2 * s1 + s0;
		//			auto r = 2 * r1 + r0;

		//			//auto neg = (3 - r) % 3;
		//			auto act = (s + r) % 3;
		//			if (act != h[i])
		//				failed = true;
		//			//throw RTE_LOC;

		//		}
		//	}
		//}

		for (u64 i = 0; i < AltModPrf::KeySize; ++i)
		{

			if (bit(X, i) != bit(xt[i].data(), ii))
				throw RTE_LOC;

			u8 xi = bit(X, i);
			u8 ki = bit(key, i);
			h[i] = ki & xi;

			assert(recver.mDebugEk0.cols() == oc::divCeil(x.size(), 128));
			auto r0 = bit(recver.mDebugEk0.data(i), ii);
			auto r1 = bit(recver.mDebugEk1.data(i), ii);
			auto s0 = bit(sender.mDebugEk0.data(i), ii);
			auto s1 = bit(sender.mDebugEk1.data(i), ii);


			auto s = 2 * s1 + s0;
			auto r = 2 * r1 + r0;

			//auto neg = (3 - r) % 3;
			auto act = (s + r) % 3;
			if (act != h[i])
				failed = true;
			//throw RTE_LOC;

			//++kIter;
			//++xIter;
		}

		if (failed)
			throw RTE_LOC;

		block256m3 u;
		{

			std::array<u8, AltModPrf::KeySize> out;
			for (u64 i = 0; i < AltModPrf::KeySize; ++i)
				out[i] = h[i];
			AltModPrf::mACode.encode<u8>(out, u.mData);
		}
		//AltModPrf::mtxMultA(h, u);

		for (u64 i = 0; i < 256; ++i)
		{
			auto r0 = bit(recver.mDebugU0.data(i), ii);
			auto r1 = bit(recver.mDebugU1.data(i), ii);
			auto s0 = bit(sender.mDebugU0.data(i), ii);
			auto s1 = bit(sender.mDebugU1.data(i), ii);

			auto s = 2 * s1 + s0;
			auto r = 2 * r1 + r0;

			if ((s + r) % 3 != u.mData[i])
			{
				throw RTE_LOC;
			}

		}

		block256 w;
		for (u64 i = 0; i < u.mData.size(); ++i)
		{
			*oc::BitIterator((u8*)&w, i) = u.mData[i] % 2;

			auto v0 = bit(sender.mDebugV(i, 0), ii);
			auto v1 = bit(recver.mDebugV(i, 0), ii);

			if ((v0 ^ v1) != u.mData[i] % 2)
			{
				throw RTE_LOC;
			}
		}


		//    auto yy = sender.mPrf.compress(w);

		//    y = sender.mPrf.eval(x[ii]);
		//else
		//    y = sender.mPrf.eval(x[ii]);

		//auto yy = (y0[ii] ^ y1[ii]);
		//if (yy != y)
		//{
		//    std::cout << "i   " << ii << std::endl;
		//    std::cout << "act " << yy << std::endl;
		//    std::cout << "exp " << y << std::endl;
		//    throw RTE_LOC;
		//}
	}
}

void AltModWPrf_proto_test(const oc::CLP& cmd)
{


	u64 n = cmd.getOr("n", 1024);
	bool noCheck = cmd.isSet("nc");
	bool debug = cmd.isSet("debug");

	oc::Timer timer;

	AltModWPrfSender sender;
	AltModWPrfReceiver recver;
	sender.mDebug = debug;
	recver.mDebug = debug;

	sender.setTimer(timer);
	recver.setTimer(timer);

	std::vector<oc::block> x(n);
	std::vector<oc::block> y0(n), y1(n);

	auto sock = coproto::LocalAsyncSocket::makePair();

	PRNG prng0(oc::ZeroBlock);
	PRNG prng1(oc::OneBlock);

	AltModPrf dm(prng0.get());

	CorGenerator ole0, ole1;
	ole0.init(sock[0].fork(), prng0, 0, 1, 1 << 18, cmd.getOr("mock", 1));
	ole1.init(sock[1].fork(), prng1, 1, 1, 1 << 18, cmd.getOr("mock", 1));

	prng0.get(x.data(), x.size());


	if (cmd.isSet("doKeyGen") == false)
	{
		std::vector<oc::block> rk(AltModPrf::KeySize);
		std::vector<std::array<oc::block, 2>> sk(AltModPrf::KeySize);
		for (u64 i = 0; i < AltModPrf::KeySize; ++i)
		{
			sk[i][0] = oc::block(i, 0);
			sk[i][1] = oc::block(i, 1);
			rk[i] = oc::block(i, *oc::BitIterator((u8*)&dm.mExpandedKey, i));
		}
		sender.init(n, ole0, AltModPrfKeyMode::SenderOnly, AltModPrfInputMode::ReceiverOnly, dm.getKey(), rk);
		recver.init(n, ole1, AltModPrfKeyMode::SenderOnly, AltModPrfInputMode::ReceiverOnly, {}, sk);
	}
	else
	{
		sender.init(n, ole0);
		recver.init(n, ole1);
	}

	auto r = coproto::sync_wait(coproto::when_all_ready(
		sender.evaluate({}, y0, sock[0], prng0),
		recver.evaluate(x, y1, sock[1], prng1),
		ole0.start(),
		ole1.start()
	));

	std::get<0>(r).result();
	std::get<1>(r).result();
	std::get<2>(r).result();
	std::get<3>(r).result();


	if (cmd.isSet("v"))
	{
		std::cout << timer << std::endl;
		std::cout << sock[0].bytesReceived() / 1000.0 << " " << sock[0].bytesSent() / 1000.0 << " kB " << std::endl;
	}

	if (noCheck)
		return;


	AltModPrf prf(sender.getKey());
	std::vector<block> y(x.size());
	prf.eval(x, y);
	for (u64 ii = 0; ii < n; ++ii)
	{
		if (debug)
		{
			AltModProtoCheck(sender, recver);
		}

		auto yy = (y0[ii] ^ y1[ii]);
		if (yy != y[ii])
		{
			std::cout << "i   " << ii << std::endl;
			std::cout << "act " << yy << std::endl;
			std::cout << "exp " << y[ii] << std::endl;
			throw RTE_LOC;
		}
	}
}

void AltModWPrf_sharedKey_test(const oc::CLP& cmd)
{


	u64 n = cmd.getOr("n", 1024);
	bool noCheck = cmd.isSet("nc");
	bool debug = cmd.isSet("debug");

	oc::Timer timer;

	AltModWPrfSender sender;
	AltModWPrfReceiver recver;
	sender.mDebug = debug;
	recver.mDebug = debug;

	sender.setTimer(timer);
	recver.setTimer(timer);

	std::vector<oc::block> x(n);
	std::vector<oc::block> y0(n), y1(n);

	auto sock = coproto::LocalAsyncSocket::makePair();

	PRNG prng0(oc::ZeroBlock);
	PRNG prng1(oc::OneBlock);

	AltModPrf dm_(prng0.get());
	AltModPrf::KeyType k = dm_.mExpandedKey;
	AltModPrf::KeyType k1 = prng0.get();
	AltModPrf::KeyType k0 = k1 ^ k;

	CorGenerator ole0, ole1;
	ole0.init(sock[0].fork(), prng0, 0, 1, 1 << 18, cmd.getOr("mock", 1));
	ole1.init(sock[1].fork(), prng1, 1, 1, 1 << 18, cmd.getOr("mock", 1));

	prng0.get(x.data(), x.size());


	if (cmd.isSet("doKeyGen") == false)
	{
		std::vector<oc::block> rk(AltModPrf::KeySize);
		std::vector<std::array<oc::block, 2>> sk(AltModPrf::KeySize);
		for (u64 i = 0; i < AltModPrf::KeySize; ++i)
		{
			sk[i][0] = oc::block(i, 0);
			sk[i][1] = oc::block(i, 1);
			rk[i] = oc::block(i, *oc::BitIterator((u8*)&k1, i));
		}
		sender.init(n, ole0, AltModPrfKeyMode::Shared, AltModPrfInputMode::ReceiverOnly, k1, rk);
		recver.init(n, ole1, AltModPrfKeyMode::Shared, AltModPrfInputMode::ReceiverOnly, k0, sk);
	}
	else
	{
		sender.init(n, ole0, AltModPrfKeyMode::Shared, AltModPrfInputMode::ReceiverOnly, k1);
		recver.init(n, ole1, AltModPrfKeyMode::Shared, AltModPrfInputMode::ReceiverOnly, k0);
	}

	auto r = coproto::sync_wait(coproto::when_all_ready(
		sender.evaluate({}, y0, sock[0], prng0),
		recver.evaluate(x, y1, sock[1], prng1),
		ole0.start(),
		ole1.start()
	));

	std::get<0>(r).result();
	std::get<1>(r).result();
	std::get<2>(r).result();
	std::get<3>(r).result();


	if (cmd.isSet("v"))
	{
		std::cout << timer << std::endl;
		std::cout << sock[0].bytesReceived() / 1000.0 << " " << sock[0].bytesSent() / 1000.0 << " kB " << std::endl;
	}

	if (noCheck)
		return;

	if (debug)
	{
		AltModProtoCheck(sender, recver);
	}

	std::vector<block> y(x.size());
	dm_.eval(x, y);
	for (u64 ii = 0; ii < n; ++ii)
	{

		auto yy = (y0[ii] ^ y1[ii]);
		if (yy != y[ii])
		{
			std::cout << "i   " << ii << std::endl;
			std::cout << "act " << yy << std::endl;
			std::cout << "exp " << y[ii] << std::endl;
			throw RTE_LOC;
		}
	}
}

void mod3Sub(block& zMsb, block& zLsb, block& xMsb, block& xLsb, block& yLsb)
{
	mod3Sub(
		span<block>(&zMsb, 1),
		span<block>(&zLsb, 1),
		span<block>(&xMsb, 1),
		span<block>(&xLsb, 1),
		span<block>(&yLsb, 1));
}
void mod3Add(block& zMsb, block& zLsb, block& xMsb, block& xLsb, block& yMsb, block& yLsb)
{

	mod3Add(
		span<block>(&zMsb, 1),
		span<block>(&zLsb, 1),
		span<block>(&xMsb, 1),
		span<block>(&xLsb, 1),
		span<block>(&yMsb, 1),
		span<block>(&yLsb, 1));
}
void AltModWPrf_shared_test(const oc::CLP& cmd)
{
	oc::Timer timer;
	timer.setTimePoint("param");

	u64 n = cmd.getOr("n", (1ull << 7) + 123);
	bool noCheck = cmd.isSet("nc");
	bool debug = cmd.isSet("debug");

	PRNG prng0(block(cmd.getOr("seed", 0), 0));
	PRNG prng1(block(cmd.getOr("seed", 0), 1));

	AltModWPrfSender sender;
	AltModWPrfReceiver recver;
	sender.mDebug = debug;
	recver.mDebug = debug;

	sender.setTimer(timer);
	recver.setTimer(timer);

	std::vector<oc::block> x(n);
	std::vector<oc::block> y0(n), y1(n);

	auto sock = coproto::LocalAsyncSocket::makePair();
	auto sock2 = coproto::LocalAsyncSocket::makePair();

	AltModPrf dm_(prng0.get());
	AltModPrf::KeyType k = dm_.mExpandedKey;
	AltModPrf::KeyType k1 = prng0.get();
	AltModPrf::KeyType k0 = k1 ^ k;

	oc::AlignedUnVector<block> x0(n), x1(n);
	CorGenerator ole0, ole1;
	ole0.init(std::move(sock2[0]), prng0, 0, 1, 1 << 18, cmd.getOr("mock", 1));
	ole1.init(std::move(sock2[1]), prng1, 1, 1, 1 << 18, cmd.getOr("mock", 1));

	prng0.get(x.data(), x.size());
	prng0.get(x0.data(), x0.size());
	for (u64 i = 0; i < n; ++i)
	{
		//if (i & 1)
		//{
		//	x1[i] = { 0ull, 0ull };
		//	x0[i] = x[i];
		//}
		//else
		//{
		//	x0[i] = { 0ull, 0ull };
		//	x1[i] = x[i];
		//}

		//auto mask = prng0.get<block>();
		//x0[i] = x[i] & mask; //{ 0ull, 0ull };
		//x1[i] = x[i] & (~mask);//^ x0[i];

		x1[i] = x[i] ^ x0[i];


		//if (i < 10)
		//	std::cout << "x[" << i << "] " << x[i] << std::endl;

		if (x[i] != (x0[i] ^ x1[i]))
			throw RTE_LOC;
	}
	timer.setTimePoint("pre");

	if (cmd.isSet("doKeyGen") == false)
	{
		std::vector<oc::block> rk0(AltModPrf::KeySize), rk1(AltModPrf::KeySize);
		std::vector<std::array<oc::block, 2>> sk0(AltModPrf::KeySize), sk1(AltModPrf::KeySize);
		for (u64 i = 0; i < AltModPrf::KeySize; ++i)
		{
			sk1[i][0] = oc::block(i, 0);
			sk1[i][1] = oc::block(i, 1);
			rk1[i] = oc::block(i, *oc::BitIterator((u8*)&k1, i));
			sk0[i][0] = oc::block(i, 0);
			sk0[i][1] = oc::block(i, 1);
			rk0[i] = oc::block(i, *oc::BitIterator((u8*)&k0, i));
		}
		timer.setTimePoint("in0");

		sender.init(n, ole0, AltModPrfKeyMode::Shared, AltModPrfInputMode::Shared, k1, rk1, sk0);
		timer.setTimePoint("in1");
		recver.init(n, ole1, AltModPrfKeyMode::Shared, AltModPrfInputMode::Shared, k0, sk1, rk0);
	}
	else
	{
		sender.init(n, ole0, AltModPrfKeyMode::Shared, AltModPrfInputMode::Shared, k1);
		recver.init(n, ole1, AltModPrfKeyMode::Shared, AltModPrfInputMode::Shared, k0);
	}
	timer.setTimePoint("init");

	auto r = coproto::sync_wait(coproto::when_all_ready(
		sender.evaluate(x0, y0, sock[0], prng0),
		recver.evaluate(x1, y1, sock[1], prng1),
		ole0.start(),
		ole1.start()
	));
	timer.setTimePoint("run");

	std::get<0>(r).result();
	std::get<1>(r).result();
	std::get<2>(r).result();
	std::get<3>(r).result();


	if (cmd.isSet("v"))
	{
		std::cout << timer << std::endl;
		std::cout << sock[0].bytesReceived() / 1000.0 << " " << sock[0].bytesSent() / 1000.0 << " kB " << std::endl;
	}

	if (noCheck)
		return;

	if (debug)
	{
		AltModProtoCheck(sender, recver);
	}

	std::vector<block> y(x.size());
	dm_.eval(x, y);
	for (u64 ii = 0; ii < n; ++ii)
	{
		auto yy = (y0[ii] ^ y1[ii]);
		if (yy != y[ii])
		{
			std::cout << "i   " << ii << std::endl;
			std::cout << "act " << yy << std::endl;
			std::cout << "exp " << y[ii] << std::endl;
			throw RTE_LOC;
		}
	}


}
