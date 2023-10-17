#include "secure-join/Defines.h"
#include "Level.h"

#include "cryptoTools/Crypto/PRNG.h"
#include "cryptoTools/Common/BitVector.h"
#include "cryptoTools/Common/Matrix.h"

#include <vector>
#include <functional>
#include <sstream>

namespace secJoin
{


	//// plaintext version of the agg tree.
	//struct PTreeNew
	//{
	//	// upstream  (0) and downstream (1) levels.
	//	struct LevelPair
	//	{
	//		PLevelNew mUp, mDown;
	//		auto& operator[](int i)
	//		{
	//			return i ? mDown : mUp;
	//		}
	//	};

	//	std::vector<LevelPair> mLevels;

	//	//std::vector<oc::BitVector> mPre, mSuf, mFull, mInput;
	//	BinMatrix mPre, mSuf, mFull, mInput;
	//	oc::BitVector mCtrl;


	//	u64 bitCount;
	//	u64 n;
	//	u64 mN16;
	//	u64 mLogn;
	//	u64 mLogfn, r, mN0, mN1;

	//	void init(u64 n, u64 bitCount, PRNG& prng,
	//		std::function<oc::BitVector(const oc::BitVector&, const oc::BitVector&)> op);


	//	std::array<BinMatrix, 2> shareVals(PRNG& prng)
	//	{
	//		std::array<BinMatrix, 2> ret;
	//		ret[0].resize(mInput.size(), mInput.bitsPerEntry());
	//		ret[1].resize(mInput.size(), mInput.bitsPerEntry());

	//		prng.get(ret[0].data(), ret[0].size());

	//		for (u64 i = 0; i < mInput.size(); ++i)
	//		{
	//			memcpy(ret[1].data(i), mInput.data(i), mInput.bytesPerEntry());

	//			for (u64 j = 0; j < ret[1].bytesPerEntry(); ++i)
	//			{
	//				ret[1](i, j) ^= ret[0](i, j);
	//			}
	//		}

	//		return ret;
	//	}


	//	std::array<BinMatrix, 2> shareBits(PRNG& prng)
	//	{
	//		std::array<BinMatrix, 2> ret;
	//		ret[0].resize(mInput.size(), 1);
	//		ret[1].resize(mInput.size(), 1);

	//		for (u64 i = 0; i < mInput.size(); ++i)
	//		{
	//			ret[0](i) = prng.getBit();
	//			ret[1](i) = ret[0](i) ^ mCtrl[i];
	//		}

	//		//oc::Matrix<i64> v(ret[0].rows(), 1);
	//		//for (u64 i = 0; i < v.rows(); ++i)
	//		//	v(i) = mCtrl[i];

	//		//share(v, ret[0].bitCount(), ret[0], ret[1], prng);

	//		return ret;
	//	}

	//	void loadLeaves(
	//		const BinMatrix& s,
	//		const oc::BitVector& c);

	//	void upstream(
	//		std::function<oc::BitVector(const oc::BitVector&, const oc::BitVector&)> op);


	//	void downstream(
	//		std::function<oc::BitVector(const oc::BitVector&, const oc::BitVector&)> op);

	//	void leaves(
	//		std::function<oc::BitVector(const oc::BitVector&, const oc::BitVector&)> op);

	//	void init(
	//		const BinMatrix& s,
	//		const oc::BitVector& c,
	//		std::function<oc::BitVector(const oc::BitVector&, const oc::BitVector&)> op);
	//};





	inline void share(oc::MatrixView<u8> d, u64 bits, BinMatrix& x0, BinMatrix& x1, PRNG& prng)
	{
		x0.resize(d.rows(), bits);
		x1.resize(d.rows(), bits);
		prng.get(x0.data(), x0.size());
		x0.trim();
		for (u64 i = 0; i < d.rows(); ++i)
		{
			for (u64 j = 0; j < x0.bytesPerEntry(); ++j)
			{
				x1(i, j) = d(i, j) ^ x0(i, j);
			}
		}
	}



	inline std::array<BinMatrix,2> share(BinMatrix d, PRNG& prng)
	{
		std::array<BinMatrix, 2> r;
		share(d.mData, d.bitsPerEntry(), r[0], r[1], prng);
		return r;
	}

	// plaintext version of the agg tree.
	struct PTree : public AggTreeParam
	{
		// upstream  (0) and downstream (1) levels.
		struct LevelPair
		{
			PLevel mUp, mDown;
			auto& operator[](int i)
			{
				return i ? mDown : mUp;
			}
		};

		std::vector<LevelPair> mLevels;

		std::vector<oc::BitVector> mPre, mSuf, mFull, mInput;
		oc::BitVector mCtrl;


		u64 bitCount;
		//u64 n;
		//u64 mN16;
		//u64 mLogn;
		//u64 mLogfn, r, mN0, mN1;
		std::function<std::string(const oc::BitVector&)> mFormatter = [](const oc::BitVector& bv) {
			std::stringstream ss;
			ss << bv;
			return ss.str();
		};

		void init(u64 n, u64 bitCount, PRNG& prng,
			std::function<oc::BitVector(const oc::BitVector&, const oc::BitVector&)> op,
			std::function<std::string(const oc::BitVector&)> formatter = {}
			)
		{

			std::vector<oc::BitVector> s(n);
			oc::BitVector c(n);

			for (u64 i = 0; i < n; ++i)
			{
				s[i].resize(bitCount);

				u64 t = (i % bitCount);
				u64 v = (i / bitCount) + 1;
				for (u64 j = 0; j < std::min<u64>(bitCount, 64); ++j)
					s[i][(j + t) % bitCount] = *oc::BitIterator((u8*)&v, j);
				//s[i].randomize(prng);
				if (i)
					c[i] = prng.getBit();
			}

			init(s, c, op, std::move(formatter));
		}


		std::array<BinMatrix, 2> shareVals(PRNG& prng)
		{
			std::array<BinMatrix, 2> ret;
			ret[0].resize(mInput.size(), mInput[0].size());
			ret[1].resize(mInput.size(), mInput[0].size());

			oc::Matrix<u8> v(ret[0].numEntries(), ret[0].bytesPerEntry());
			for (u64 i = 0; i < v.rows(); ++i)
				memcpy(v[i].data(), mInput[i].data(), mInput[i].sizeBytes());

			share(v, ret[0].bitsPerEntry(), ret[0], ret[1], prng);

			return ret;
		}


		std::array<BinMatrix, 2> shareBits(PRNG& prng)
		{
			std::array<BinMatrix, 2> ret;
			ret[0].resize(mInput.size(), 1);
			ret[1].resize(mInput.size(), 1);

			oc::Matrix<u8> v(ret[0].numEntries(), 1);
			for (u64 i = 0; i < v.rows(); ++i)
				v(i) = mCtrl[i];

			share(v, ret[0].bitsPerEntry(), ret[0], ret[1], prng);

			return ret;
		}

		void loadLeaves(
			const std::vector<oc::BitVector>& s,
			const oc::BitVector& c)
		{

			mInput = s;
			mCtrl = c;

			bitCount = s[0].size();
			computeTreeSizes(s.size());

			mLevels.resize(mLogn + 1);
			for (u64 j = 0; j < 2; ++j)
			{
				mLevels[0][j].resize(mN0);
				if (mR)
					mLevels[1][j].resize(1ull << mLogfn);

				for (u64 i = mR ? 2 : 1; i < mLevels.size(); ++i)
				{
					auto nn = mLevels[i - 1][j].size() / 2;
					mLevels[i][j].resize(nn);
				}
			}

			for (u64 i = 0; i < mN; ++i)
			{
				auto q = i < mN0 ? 0 : 1;
				auto w = i < mN0 ? i : i - mR;

				mLevels[q].mUp.mPreVal[w] = s[i];
				mLevels[q].mUp.mSufVal[w] = s[i];
				if (i)
					mLevels[q].mUp.mPreBit[w] = c[i];
				else
					mLevels[q].mUp.mPreBit[w] = 0;
				if (i != mN - 1)
					mLevels[q].mUp.mSufBit[w] = c[i + 1];
				else
					mLevels[q].mUp.mSufBit[w] = 0;

				//std::cout << s[i] << " " << mLevels[q].mUp.mPreBit[w] << " " << mLevels[q].mUp.mSufBit[w] << std::endl;
			}
			//std::cout << "\n";

			for (u64 i = mN; i < mN16; ++i)
			{
				auto q = i < mN0 ? 0 : 1;
				auto w = i < mN0 ? i : i - mR;

				mLevels[q].mUp.mPreVal[w].resize(bitCount);
				mLevels[q].mUp.mSufVal[w].resize(bitCount);
				mLevels[q].mUp.mPreBit[w] = 0;
				mLevels[q].mUp.mSufBit[w] = 0;
			}
		}
		void upstream(
			std::function<oc::BitVector(const oc::BitVector&, const oc::BitVector&)> op)
		{
			for (u64 j = 1; j < mLevels.size(); ++j)
			{
				u64 end = mLevels[j - 1].mUp.size() / 2;
				auto& child = mLevels[j - 1].mUp;
				auto& parent = mLevels[j].mUp;

				for (u64 i = 0; i < end; ++i)
				{
					{
						auto v0 = child.mPreVal[2 * i];
						auto v1 = child.mPreVal[2 * i + 1];
						auto p0 = child.mPreBit[2 * i];
						auto p1 = child.mPreBit[2 * i + 1];

						parent.mPreVal[i] = p1 ? op(v0, v1) : v1;
						parent.mPreBit[i] = p1 * p0;

					}

					{
						auto v0 = child.mSufVal[2 * i];
						auto v1 = child.mSufVal[2 * i + 1];
						auto p0 = child.mSufBit[2 * i];
						auto p1 = child.mSufBit[2 * i + 1];

						parent.mSufVal[i] = p0 ? op(v0, v1) : v0;
						parent.mSufBit[i] = p1 * p0;

						//std::cout << parent.mSufVal[i] << " " << parent.mSufBit[i] << " = " << p0 <<" * " << p1 << std::endl;

					}
				}
				//std::cout << std::endl;
			}

		}


		void downstream(
			std::function<oc::BitVector(const oc::BitVector&, const oc::BitVector&)> op)
		{
			assert(mLevels.back().mUp.size() == 1);
			mLevels.back().mDown = mLevels.back().mUp;

			for (u64 j = mLevels.size() - 1; j != 0; --j)
			{
				auto& parent = mLevels[j].mDown;
				auto& childDn = mLevels[j - 1].mDown;
				auto& childUp = mLevels[j - 1].mUp;
				u64 end = childDn.size() / 2;
				childDn.mPreBit = childUp.mPreBit;
				childDn.mSufBit = childUp.mSufBit;

				for (u64 i = 0; i < end; ++i)
				{
					{
						auto& v = parent.mPreVal[i];
						auto& v0 = childUp.mPreVal[i * 2];
						auto& v1 = childUp.mPreVal[i * 2 + 1];
						auto& d0 = childDn.mPreVal[i * 2];
						auto& d1 = childDn.mPreVal[i * 2 + 1];
						auto p0 = childUp.mPreBit[i * 2];

						assert(v.size());
						assert(v0.size());
						assert(v1.size());

						d1 = p0 ? op(v, v0) : v0;
						d0 = v;
					}
					{
						auto& v = parent.mSufVal[i];
						//auto& v0 = childUp.mSufVal[i * 2];
						auto& v1 = childUp.mSufVal[i * 2 + 1];
						auto& d0 = childDn.mSufVal[i * 2];
						auto& d1 = childDn.mSufVal[i * 2 + 1];
						auto p1 = childUp.mSufBit[i * 2 + 1];

						d0 = p1 ? op(v1, v) : v1;
						d1 = v;

						//std::cout << d0 << " " << " " << p1 << "\n" << d1 << std::endl;

					}
				}
				//std::cout << std::endl;
			}
		}

		void leaves(
			std::function<oc::BitVector(const oc::BitVector&, const oc::BitVector&)> op)
		{
			mPre.resize(mN);
			mSuf.resize(mN);
			mFull.resize(mN);
			std::vector<oc::BitVector> expPre(mN), expSuf(mN);

			for (u64 i = 0; i < mN; ++i)
			{
				auto q = i < mN0 ? 0 : 1;
				auto w = i < mN0 ? i : i - mR;

				auto& ll = mLevels[q].mDown;

				expPre[i] = mCtrl[i] ? op(expPre[i - 1], mInput[i]) : mInput[i];


				auto ii = mN - 1 - i;
				u64 c = i ? mCtrl[ii + 1] : 0;
				expSuf[ii] = c ? op(mInput[ii], expSuf[ii + 1]) : mInput[ii];
				//u64 c = (ii != n - 1) ? mCtrl[ii+1] : 0;
				//expSuf[ii] = c ? op(mInput[ii], expSuf[ii + 1]) : mInput[ii];
				//std::cout << mInput[ii] << " " << c << " -> "<< expSuf[ii] << std::endl;

				mPre[i] = ll.mPreBit[w] ? op(ll.mPreVal[w], mInput[i]) : mInput[i];
				mSuf[i] = ll.mSufBit[w] ? op(mInput[i], ll.mSufVal[w]) : mInput[i];
				mFull[i] = ll.mPreBit[w] ? mPre[i] : mInput[i];
				mFull[i] = ll.mSufBit[w] ? op(mFull[i], ll.mSufVal[w]) : mFull[i];

				//std::cout << mInput[i] << " " << mCtrl[i] << " -> " << expPre[i] << " " << mPre[i] << std::endl;
			}

			//std::cout << "\n";
			//for (u64 i = 0; i < n; ++i)
			//{
			//	//u64 c = (i != n - 1) ? mCtrl[i + 1] : 0;
			//	//u64 c = mCtrl[i];
			//	//std::cout << mInput[i] << " " << c << " -> " << expSuf[i] << " " << mSuf[i] << "  " << (expSuf[i] ^ mSuf[i]) << std::endl;

			//}

			if (mPre != expPre)
				throw RTE_LOC;
			if (mSuf != expSuf)
				throw RTE_LOC;
		}

		void init(
			const std::vector<oc::BitVector>& s,
			const oc::BitVector& c,
			std::function<oc::BitVector(const oc::BitVector&, const oc::BitVector&)> op,
			std::function<std::string(const oc::BitVector&)> formatter = {})
		{

			if (formatter)
				mFormatter = std::move(formatter);

			loadLeaves(s, c);
			upstream(op);
			downstream(op);
			leaves(op);
		}


		std::string print(AggTreeType type = AggTreeType::Prefix)
		{
			std::stringstream ss;

			if (type == AggTreeType::Prefix)
			{

				for (u64 l = 0; l < mLevels.size(); ++l)
				{
					std::cout << "[ up lvl: " << l  << std::endl;
					for (u64 j = 0; j < mLevels[l].mUp.mPreVal.size(); ++j) {
						std::cout << l << "." << j << " (" << mFormatter(mLevels[l].mUp.mPreVal[j]) << ", " << mLevels[l].mUp.mPreBit[j] << ")";
						auto child = j * 2;
						if (l && mLevels[l - 1].mUp.mPreVal.size() > child)
						{

							std::cout << " = " << mLevels[l - 1].mUp.mPreBit[child + 1] << " ? op(" <<
								mFormatter(mLevels[l - 1].mUp.mPreVal[child]) << ", " <<
								mFormatter(mLevels[l - 1].mUp.mPreVal[child + 1]) << ") : " <<
								mFormatter(mLevels[l - 1].mUp.mPreVal[child + 1]);

							// parent.mPreVal[i] = p1 ? op(v0, v1) : v1;
							// parent.mPreBit[i] = p1 * p0;
						}
						std::cout << std::endl;
					}
					std::cout << "]" << std::endl;
				}


				for (u64 l = mLevels.size()-1; l < mLevels.size(); --l)
				{
					std::cout << "[ dwn lvl: " <<l << std::endl;
					for (u64 j = 0; j < mLevels[l].mDown.mPreVal.size(); ++j) {
						std::cout << l << "." << j << " (" << mFormatter(mLevels[l].mDown.mPreVal[j]) << ", " << mLevels[l].mDown.mPreBit[j] << ")";
						//auto child = j * 2;
						if (l != mLevels.size() -1)
						{
							auto parent = j / 2;
							auto sibling = j ^ 1;
							if (j & 1)
							{
								std::cout << " = " << mLevels[l].mDown.mPreBit[sibling] << " ? op(" <<
									mFormatter(mLevels[l + 1].mDown.mPreVal[parent]) << ", " <<
									mFormatter(mLevels[l].mDown.mPreVal[sibling]) << ") : " <<
									mFormatter(mLevels[l].mDown.mPreVal[sibling]);
							}
							else
							{
								std::cout << " = " << mFormatter(mLevels[l + 1].mDown.mPreVal[parent]);
							}
							//d1 = p0 ? op(v, v0) : v0;
							//d0 = v;
						}
						std::cout << std::endl;
					}
					std::cout << "]" << std::endl;
				}

			}
			else
			{
				throw RTE_LOC;
			}

			return ss.str();
		}
	};

}