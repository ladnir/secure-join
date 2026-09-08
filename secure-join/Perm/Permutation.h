#pragma once
#include <cryptoTools/Common/Defines.h>
#include <cryptoTools/Common/Matrix.h>
#include <cryptoTools/Network/Channel.h>
#include <cryptoTools/Crypto/PRNG.h>
#include <algorithm>
#include <stdexcept>


#include <iostream>
#include "secure-join/Defines.h"
#include "secure-join/Util/Matrix.h"
namespace secJoin
{
	
	enum class PermOp
	{
		Regular,
		Inverse
	};

	// a plaintext permutation.
	class Perm
	{
	public:
		std::vector<u32> mPi;

		Perm() = default;
		Perm(const Perm&) = default;
		Perm(Perm&&) noexcept = default;
		Perm& operator=(const Perm&) = default;
		Perm& operator=(Perm&&) noexcept = default;

		//initializing perm with prng
		Perm(u64 n, PRNG& prng)
		{
			randomize(n, prng);
		}

		Perm(u64 n)
		{
			mPi.resize(n);
			for (u64 i = 0; i < n; ++i)
				mPi[i] = i;
		}

		//initializing permutation with vector 
		Perm(std::vector<u32> perm) : mPi(std::move(perm)) {}

		Perm(u64 s, std::vector<u32> perm) : mPi(perm.begin(), perm.end()) {
			assert(size() == s);
		}

		// sample a random permutation of size n
		void randomize(u64 n, PRNG& prng)
		{
			if (n >= ~u32(0))
				throw std::invalid_argument("Perm::randomize exceeds 32-bit permutation capacity");
			mPi.resize(n);
			for (u64 i = 0; i < size(); i++)
				mPi[i] = i;

			// PRNG implements the 64-bit UniformRandomBitGenerator interface.
			// std::shuffle uses unbiased bounded sampling. Reducing a random
			// u32 modulo the remaining size biases the permutation and is unsafe
			// when the permutation hides positions that are subsequently opened.
			std::shuffle(mPi.begin(), mPi.end(), prng);
		}

		// A.compose(B) computes the permutation AoB
		Perm compose(const Perm& rhs) const;

		// return A^-1.
		Perm inverse()const;

		u64 size() const { return mPi.size(); }

		// dst[i] = src[perm[i]]
		template <typename T>
		void apply(oc::MatrixView<const T> src, oc::MatrixView<T> dst, PermOp op = PermOp::Regular) const
		{
			if (src.rows() != size())
				throw RTE_LOC;
			if (dst.rows() != size())
				throw RTE_LOC;
			if (src.cols() != dst.cols())
				throw RTE_LOC;;
			auto cols = src.cols();

			if (op == PermOp::Regular)
			{
				if (std::is_trivially_constructible<T>::value)
				{

					auto d = dst.data();
					auto size8 = size() / 8 * 8;
					u64 i = 0;
					for (; i < size8; i += 8)
					{
						const T* __restrict  s0 = src.data(mPi.data()[i + 0]);
						const T* __restrict  s1 = src.data(mPi.data()[i + 1]);
						const T* __restrict  s2 = src.data(mPi.data()[i + 2]);
						const T* __restrict  s3 = src.data(mPi.data()[i + 3]);
						const T* __restrict  s4 = src.data(mPi.data()[i + 4]);
						const T* __restrict  s5 = src.data(mPi.data()[i + 5]);
						const T* __restrict  s6 = src.data(mPi.data()[i + 6]);
						const T* __restrict  s7 = src.data(mPi.data()[i + 7]);
						T* __restrict  d0 = d + 0 * cols;
						T* __restrict  d1 = d + 1 * cols;
						T* __restrict  d2 = d + 2 * cols;
						T* __restrict  d3 = d + 3 * cols;
						T* __restrict  d4 = d + 4 * cols;
						T* __restrict  d5 = d + 5 * cols;
						T* __restrict  d6 = d + 6 * cols;
						T* __restrict  d7 = d + 7 * cols;

						assert(d7 + cols <= dst.data() + dst.size());
						assert(s0 + cols <= src.data() + src.size());
						assert(s1 + cols <= src.data() + src.size());
						assert(s2 + cols <= src.data() + src.size());
						assert(s3 + cols <= src.data() + src.size());
						assert(s4 + cols <= src.data() + src.size());
						assert(s5 + cols <= src.data() + src.size());
						assert(s6 + cols <= src.data() + src.size());
						assert(s7 + cols <= src.data() + src.size());

						std::copy(s0, s0 + cols, d0);
						std::copy(s1, s1 + cols, d1);
						std::copy(s2, s2 + cols, d2);
						std::copy(s3, s3 + cols, d3);
						std::copy(s4, s4 + cols, d4);
						std::copy(s5, s5 + cols, d5);
						std::copy(s6, s6 + cols, d6);
						std::copy(s7, s7 + cols, d7);

						d = d7 + cols;
					}

					for (; i < size(); i++)
					{
						osuCrypto::copyBytes(dst[i], src[mPi[i]]);
					}
				}
				else
				{
					auto d = dst.data();
					for (u64 i = 0; i < size(); i++)
					{
						auto s = &src(mPi[i], 0);
						for (u64 j = 0; j < cols; ++j)
							d[j] = s[j];
						d += cols;
					}
				}
			}
			else
			{
				if (std::is_trivially_constructible<T>::value)
				{
					auto s = src.data();
					auto size8 = size() / 8 * 8;
					u64 i = 0;
					for (; i < size8; i += 8)
					{
						T* __restrict d0 = dst.data(mPi.data()[i + 0]);
						T* __restrict d1 = dst.data(mPi.data()[i + 1]);
						T* __restrict d2 = dst.data(mPi.data()[i + 2]);
						T* __restrict d3 = dst.data(mPi.data()[i + 3]);
						T* __restrict d4 = dst.data(mPi.data()[i + 4]);
						T* __restrict d5 = dst.data(mPi.data()[i + 5]);
						T* __restrict d6 = dst.data(mPi.data()[i + 6]);
						T* __restrict d7 = dst.data(mPi.data()[i + 7]);
						const T* __restrict  s0 = s + 0 * cols;
						const T* __restrict  s1 = s + 1 * cols;
						const T* __restrict  s2 = s + 2 * cols;
						const T* __restrict  s3 = s + 3 * cols;
						const T* __restrict  s4 = s + 4 * cols;
						const T* __restrict  s5 = s + 5 * cols;
						const T* __restrict  s6 = s + 6 * cols;
						const T* __restrict  s7 = s + 7 * cols;

						assert(s7 + cols <= src.data() + src.size());
						assert(d0 + cols <= dst.data() + dst.size());
						assert(d1 + cols <= dst.data() + dst.size());
						assert(d2 + cols <= dst.data() + dst.size());
						assert(d3 + cols <= dst.data() + dst.size());
						assert(d4 + cols <= dst.data() + dst.size());
						assert(d5 + cols <= dst.data() + dst.size());
						assert(d6 + cols <= dst.data() + dst.size());
						assert(d7 + cols <= dst.data() + dst.size());

						std::copy(s0, s0 + cols, d0);
						std::copy(s1, s1 + cols, d1);
						std::copy(s2, s2 + cols, d2);
						std::copy(s3, s3 + cols, d3);
						std::copy(s4, s4 + cols, d4);
						std::copy(s5, s5 + cols, d5);
						std::copy(s6, s6 + cols, d6);
						std::copy(s7, s7 + cols, d7);

						s = s7 + cols;
					}

					for (; i < size(); i++)
					{
						osuCrypto::copyBytes(dst[mPi[i]], src[i]);
					}
				}
				else
				{
					auto s = src.data();
					for (u64 i = 0; i < size(); i++)
					{
						auto d = &dst(mPi[i], 0);
						for (u64 j = 0; j < cols; ++j)
							d[j] = s[j];
						s += cols;
					}
				}
			}
		}

		template <typename T>
		oc::Matrix<T> apply(const oc::Matrix<T>& src, PermOp op = PermOp::Regular) const
		{
			oc::Matrix<T> r(src.rows(), src.cols());
			apply<u8>(matrixCast<u8>(src), matrixCast<u8>(r), op);
			return r;
		}


		template <typename T>
		std::vector<T> apply(u64 s, std::vector<T>& src)const
		{
			assert(s == size());
			return apply<T>(src);
		}

		template <typename T>
		std::vector<T> apply(const std::vector<T>& src)const
		{
			std::vector<T> dst(size());

			apply<T>(
				oc::MatrixView<const T>(src.data(), src.size(), 1),
				oc::MatrixView<T>(dst.data(), dst.size(), 1)
				);

			return dst;
		}

		template <typename T>
		void applyInv(oc::MatrixView<const T> src, oc::MatrixView<T> dst) const
		{
			apply<T>(src, dst, PermOp::Inverse);
		}

		template <typename T>
		std::vector<T> applyInv(const std::vector<T>& src) const
		{
			std::vector<T> dst(size());

			applyInv<T>(
				oc::MatrixView<const T>(src.data(), src.size(), 1),
				oc::MatrixView<T>(dst.data(), dst.size(), 1)
				);

			return dst;
		}


		auto& operator[](u64 idx) const
		{
			return mPi[idx];
		}

		bool operator==(const Perm& p) const
		{
			return mPi == p.mPi;
		}
		bool operator!=(const Perm& p) const
		{
			return mPi != p.mPi;
		}

		auto begin() { return mPi.begin(); }
		auto begin() const { return mPi.begin(); }
		auto end() { return mPi.end(); }
		auto end() const { return mPi.end(); }


		void validate()
		{
			std::vector<u8> v(size(), 0);
			for (u64 i = 0; i < size(); ++i)
			{
				if (mPi[i] >= size())
					throw RTE_LOC;
				auto& vv = v[mPi[i]];
				if (vv)
					throw RTE_LOC;

				vv = 1;
			}
		}


		void clear()
		{
			mPi.clear();
		}
	};



	inline std::ostream& operator<<(std::ostream& o, const Perm& p)
	{
		o << "[";
		for (auto pp : p.mPi)
			o << pp << " ";
		o << "]" << std::dec;
		return o;
	}

}
