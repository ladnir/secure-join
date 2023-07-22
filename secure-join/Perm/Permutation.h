#pragma once
#include <cryptoTools/Common/Defines.h>
#include <cryptoTools/Common/Matrix.h>
#include <cryptoTools/Network/Channel.h>
#include <cryptoTools/Crypto/PRNG.h>


#include <iostream>
#include "secure-join/Defines.h"

namespace secJoin
{
	
	using PRNG = oc::PRNG;

	struct Xor
	{
		template<typename T>
		auto operator()(const T& x, const T& y) const { return x ^ y; }
	};

	class Perm
	{
	public:
		std::vector<u32> mPerm;

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
			mPerm.resize(n);
			for (u64 i = 0; i < n; ++i)
				mPerm[i] = i;
		}

		//initializing permutation with vector 
		Perm(std::vector<u32> perm) : mPerm(std::move(perm)) {}
		// Perm(const std::vector<u64>& perm) : mPerm(perm.begin(), perm.end()) {}
		Perm(u64 s, std::vector<u32> perm) : mPerm(perm.begin(), perm.end()) {
			assert(size() == s);
		}


		void randomize(u64 n, PRNG& prng)
		{
			mPerm.resize(n);
			for (u64 i = 0; i < size(); i++)
				mPerm[i] = i;

			assert(n < ~u32(0));
			for (u64 i = 0; i < size(); i++)
			{
				auto idx = (prng.get<u32>() % (size() - i)) + i;
				std::swap(mPerm[i], mPerm[idx]);
			}
		}

		Perm operator*(const Perm& rhs) const { return composeSwap(rhs); }

		// A.composeSwap(B) computes the permutation BoA
		Perm composeSwap(const Perm& rsh)const;
		
		// A.compose(B) computes the permutation AoB
		Perm compose(const Perm& rhs) const;

		// return A^-1.
		Perm inverse()const;


		u64 size() const { return mPerm.size(); }


		// dst[i] = src[perm[i]]
		template <typename T>
		void apply(oc::MatrixView<const T> src, oc::MatrixView<T> dst, bool inverse = false) const
		{
			assert(src.rows() == size());
			assert(src.cols() == dst.cols());
			auto cols = src.cols();

			if (inverse == false)
			{
				if (std::is_trivially_constructible<T>::value)
				{

					auto d = dst.data();
					auto size8 = size() / 8 * 8;
					u64 i = 0;
					for (; i < size8; i += 8)
					{
						const T* __restrict  s0 = &src(mPerm[i + 0], 0);
						const T* __restrict  s1 = &src(mPerm[i + 1], 0);
						const T* __restrict  s2 = &src(mPerm[i + 2], 0);
						const T* __restrict  s3 = &src(mPerm[i + 3], 0);
						const T* __restrict  s4 = &src(mPerm[i + 4], 0);
						const T* __restrict  s5 = &src(mPerm[i + 5], 0);
						const T* __restrict  s6 = &src(mPerm[i + 6], 0);
						const T* __restrict  s7 = &src(mPerm[i + 7], 0);
						T* __restrict  d0 = d + 0 * cols;
						T* __restrict  d1 = d + 1 * cols;
						T* __restrict  d2 = d + 2 * cols;
						T* __restrict  d3 = d + 3 * cols;
						T* __restrict  d4 = d + 4 * cols;
						T* __restrict  d5 = d + 5 * cols;
						T* __restrict  d6 = d + 6 * cols;
						T* __restrict  d7 = d + 7 * cols;

						std::memcpy(d0, s0, cols * sizeof(T));
						std::memcpy(d1, s1, cols * sizeof(T));
						std::memcpy(d2, s2, cols * sizeof(T));
						std::memcpy(d3, s3, cols * sizeof(T));
						std::memcpy(d4, s4, cols * sizeof(T));
						std::memcpy(d5, s5, cols * sizeof(T));
						std::memcpy(d6, s6, cols * sizeof(T));
						std::memcpy(d7, s7, cols * sizeof(T));


						d = d7 + cols;
					}

					for (; i < size(); i++)
					{
						auto s = &src(mPerm[i], 0);
						std::memcpy(d, s, cols * sizeof(T));
						d += cols;
					}
				}
				else
				{
					auto d = dst.data();
					for (u64 i = 0; i < size(); i++)
					{
						auto s = &src(mPerm[i], 0);
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
						T* __restrict d0 = &dst(mPerm[i + 0], 0);
						T* __restrict d1 = &dst(mPerm[i + 1], 0);
						T* __restrict d2 = &dst(mPerm[i + 2], 0);
						T* __restrict d3 = &dst(mPerm[i + 3], 0);
						T* __restrict d4 = &dst(mPerm[i + 4], 0);
						T* __restrict d5 = &dst(mPerm[i + 5], 0);
						T* __restrict d6 = &dst(mPerm[i + 6], 0);
						T* __restrict d7 = &dst(mPerm[i + 7], 0);
						const T* __restrict  s0 = s + 0 * cols;
						const T* __restrict  s1 = s + 1 * cols;
						const T* __restrict  s2 = s + 2 * cols;
						const T* __restrict  s3 = s + 3 * cols;
						const T* __restrict  s4 = s + 4 * cols;
						const T* __restrict  s5 = s + 5 * cols;
						const T* __restrict  s6 = s + 6 * cols;
						const T* __restrict  s7 = s + 7 * cols;

						std::memcpy(d0, s0, cols * sizeof(T));
						std::memcpy(d1, s1, cols * sizeof(T));
						std::memcpy(d2, s2, cols * sizeof(T));
						std::memcpy(d3, s3, cols * sizeof(T));
						std::memcpy(d4, s4, cols * sizeof(T));
						std::memcpy(d5, s5, cols * sizeof(T));
						std::memcpy(d6, s6, cols * sizeof(T));
						std::memcpy(d7, s7, cols * sizeof(T));


						s = s7 + cols;
					}

					for (; i < size(); i++)
					{
						auto d = &dst(mPerm[i], 0);
						std::memcpy(d, s, cols * sizeof(T));
						s += cols;
					}
				}
				else
				{
					auto s = src.data();
					for (u64 i = 0; i < size(); i++)
					{
						auto d = &dst(mPerm[i], 0);
						for (u64 j = 0; j < cols; ++j)
							d[j] = s[j];
						s += cols;
					}
				}
			}
		}

		template <typename T>
		oc::Matrix<T> apply(const oc::Matrix<T>& src, bool inverse = false) const
		{
			oc::Matrix<T> r(src.rows(), src.cols());
			oc::MatrixView<const u8> ss((u8*)src.data(), src.rows(), src.cols() *sizeof(T));
			oc::MatrixView<u8> rr((u8*)r.data(), src.rows(), src.cols() * sizeof(T));
			apply<u8>(ss, rr, inverse);
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
			apply<T>(src, dst, true);
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
			return mPerm[idx];
		}

		bool operator==(const Perm& p) const
		{
			return mPerm == p.mPerm;
		}
		bool operator!=(const Perm& p) const
		{
			return mPerm != p.mPerm;
		}

		auto begin() { return mPerm.begin(); }
		auto begin() const { return mPerm.begin(); }
		auto end() { return mPerm.end(); }
		auto end() const { return mPerm.end(); }


		void validate()
		{
			std::vector<u8> v(size(), 0);
			for (u64 i = 0; i < size(); ++i)
			{
				if (mPerm[i] >= size())
					throw RTE_LOC;
				auto& vv = v[mPerm[i]];
				if (vv)
					throw RTE_LOC;

				vv = 1;
			}
		}

	};



	inline std::ostream& operator<<(std::ostream& o, const Perm& p)
	{
		o << "[";
		for (auto pp : p.mPerm)
			o << std::hex << pp << " ";
		o << "]" << std::dec;
		return o;
	}

}