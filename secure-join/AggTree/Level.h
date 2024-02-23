#pragma once
#include "secure-join/Util/Matrix.h"
#include "cryptoTools/Common/BitVector.h"
#include <vector>

namespace secJoin
{

    enum AggTreeType : char
    {
        Prefix = 1,
        Suffix = 2,
        Full = 3
    };

    struct AggTreeParam
    {
        // the number of real inputs
        u64 mN = 0;

        bool mCompleteTree = false;
        // the number of inputs rounded upto a power of 2 or multiple of 16
        u64 mN16 = 0;

        // log2 floor of mN16
        //u64 mLogfn = 0;

        //// log2 ceiling of mN16
        //u64 mLogn = 0;

        // the number of parents in the second level.
        u64 mR = 0;

        std::vector<u64> mLevelSizes;
        //// the number of leaves in the first level.
        //u64 mN0 = 0;

        //// the number of leaves in the second level.
        //u64 mN1 = 0;

        // Here we compute various tree size parameters such as the depth
        // and the number of leaves on the lowest two levels (mN0,mN1).
        //
        // for example, if we simply use mN16=mN=5, we get:
        //
        //        *
        //    *       *
        //  *   *   *   *
        // * *
        //
        // mLogfn = 2
        // mLogn  = 3
        // mR     = 1     one parent one the second level (second partial).
        // mN0    = 2     two leaves on the first level (first partial).
        // mN1    = 3     three leaves on the first level (first partial).
        //
        void computeTreeSizes(u64 n_)
        {
            mN = n_;

            // the number of entries rounded up to a multiple of 16 or power of 2
            mN16 = std::min<u64>(1ull << oc::log2ceil(mN), oc::roundUpTo(mN, 16));

            // log 2 floor
            auto mLogfn = oc::log2floor(mN16);
            // log 2 ceiling
            auto mLogn = oc::log2ceil(mN16);
             
            mCompleteTree = mLogfn == mLogn;

            mLevelSizes.resize(mLogn+1);

            // the size of the second level
            auto secondPartial = (1ull << mLogfn);

            // the number of parents in the second level.
            mR = mN16 - secondPartial;

            // the size of the first level.
            mLevelSizes[0] = mR ? 2 * mR : mN16;

            // the number of leaves on the second level (if any)
            mLevelSizes[1] = 1ull << mLogfn; //mN16 - mLevelSizes[0];

            if (mCompleteTree)
                mLevelSizes[1] = mLevelSizes[0] / 2;

            for (u64 i = 2; i < mLevelSizes.size(); ++i)
                mLevelSizes[i] = mLevelSizes[i - 1] / 2;

            assert(mLevelSizes.back() == 1);
            assert(mR % 8 == 0);
        }
    };

    struct AggTreeLevel
    {
        TBinMatrix mPreVal, mSufVal, mPreBit, mSufBit;

        void resize(u64 n, u64 bitCount, AggTreeType type)
        {
            if (type & AggTreeType::Prefix)
            {
                mPreVal.resize(n, bitCount, sizeof(oc::block));
                mPreBit.resize(n, 1, sizeof(oc::block));
            }
            if (type & AggTreeType::Suffix)
            {
                mSufVal.resize(n, bitCount, sizeof(oc::block));
                mSufBit.resize(n, 1, sizeof(oc::block));
            }
        }
    };

    // an agg tree level that stores the left and right children
    // in separate arrays.
    struct AggTreeSplitLevel
    {
        std::array<AggTreeLevel, 2> mLeftRight;

        void resize(u64 n, u64 bitCount, AggTreeType type)
        {
            auto n0 = oc::divCeil(n, 2);
            auto n1 = n - n0;
            mLeftRight[0].resize(n0, bitCount, type);
            mLeftRight[1].resize(n1, bitCount, type);

            mLeftRight[0].mPreBit.resize(n0, 1, sizeof(oc::block));
            mLeftRight[1].mPreBit.resize(n1, 1, sizeof(oc::block));
        }

        u64 bitsPerEntry() const
        {
            return std::max(
                mLeftRight[0].mPreVal.bitsPerEntry(),
                mLeftRight[0].mSufVal.bitsPerEntry());
        }
        u64 size() const { return std::max(prefixSize(), suffixSize()); }
        u64 prefixSize() const { return mLeftRight[0].mPreBit.numEntries() + mLeftRight[1].mPreBit.numEntries(); }
        u64 suffixSize() const { return mLeftRight[0].mSufBit.numEntries() + mLeftRight[1].mSufBit.numEntries(); }

        AggTreeLevel& operator[](u64 i)
        {
            return mLeftRight[i];
        }

        // load `src` and `controlBits` into this (split) level
        // at the starting index `offset`.
        void setLeafVals(
            oc::MatrixView<const u8> src,
            span<const u8> controlBits,
            u64 offset)
        {
            auto srcSize = src.rows();
            auto bitCount = bitsPerEntry();
            auto byteCount = oc::divCeil(bitCount, 8);
            auto n = size();
            ;
            auto available = std::min<u64>(srcSize, n);
            auto available2 = available & ~1ull;
            if (src.cols() < byteCount)
                throw RTE_LOC;
            if (controlBits.size() != srcSize && controlBits.size() != srcSize + 1)
                throw RTE_LOC;
            if (offset & 1)
                throw RTE_LOC;
            // if (size() > offset + available)
            //	throw RTE_LOC;

            if (prefixSize())
            {
                std::array<BinMatrix, 2> vals;
                vals[0].resize(oc::divCeil(n, 2), bitCount);
                vals[1].resize(n / 2, bitCount);
                u64 i = 0, d = offset / 2;

                // for the first even number of rows split te values into left and right
                for (; i < available2; i += 2, ++d)
                {
                    memcpy(vals[0][d], src[i + 0].subspan(0, byteCount));
                    memcpy(vals[1][d], src[i + 1].subspan(0, byteCount));

                    assert(mLeftRight[0].mPreBit.size() > d / 8);
                    assert(mLeftRight[1].mPreBit.size() > d / 8);

                    *oc::BitIterator(mLeftRight[0].mPreBit.data(), d) = controlBits[i + 0];
                    *oc::BitIterator(mLeftRight[1].mPreBit.data(), d) = controlBits[i + 1];
                }

                // special case for when we have an odd number of rows.
                if (available & 1)
                {
                    assert(i == src.rows() - 1);
                    memcpy(vals[0][d], src[i].subspan(0, byteCount));
                    memset(vals[1][d], 0);
                    assert(mLeftRight[0].mPreBit.size() > d / 8);
                    *oc::BitIterator(mLeftRight[0].mPreBit.data(), d) = controlBits[i];
                    *oc::BitIterator(mLeftRight[1].mPreBit.data(), d) = 0;

                    ++d;
                }

                // if there is space left over fill with zeros
                for (; d < n / 2; ++d)
                {
                    memset(vals[0][d], 0);
                    memset(vals[1][d], 0);
                    *oc::BitIterator(mLeftRight[0].mPreBit.data(), d) = 0;
                    *oc::BitIterator(mLeftRight[1].mPreBit.data(), d) = 0;
                }

                // transpose
                vals[0].transpose(mLeftRight[0].mPreVal);
                vals[1].transpose(mLeftRight[1].mPreVal);
            }

            if (suffixSize())
            {

                // if (preSize)
                //{
                //	mLeftRight[0].mSufVal = mLeftRight[0].mPreVal;
                //	mLeftRight[1].mSufVal = mLeftRight[1].mPreVal;

                //	auto n0 = mLeftRight[0].mPreBit.size() - 1;
                //	auto n1 = mLeftRight[1].mPreBit.size() - 1;
                //	for (u64 i = 0; i < n0; ++i)
                //	{
                //		mLeftRight[0].mSufBit(i) =
                //			mLeftRight[0].mPreBit(i) >> 1 |
                //			mLeftRight[0].mPreBit(i + 1) << 7;
                //	}

                //	for (u64 i = 0; i < n1; ++i)
                //	{
                //		mLeftRight[1].mSufBit(i) =
                //			mLeftRight[1].mPreBit(i) >> 1 |
                //			mLeftRight[1].mPreBit(i + 1) << 7;
                //	}

                //	mLeftRight[0].mSufBit(n0) = mLeftRight[0].mPreBit(n0) >> 1;
                //	mLeftRight[1].mSufBit(n1) = mLeftRight[1].mPreBit(n1) >> 1;
                //}
                // else
                {
                    std::array<BinMatrix, 2> vals;
                    vals[0].resize(oc::divCeil(n, 2), bitCount);
                    vals[1].resize(n / 2, bitCount);

                    auto n = available / 2;
                    u64 i = 0, d = offset / 2;
                    // std::cout << "d " << d << std::endl;
                    for (; i < available2 - 2; i += 2, ++d)
                    {
                        memcpy(vals[0][d], src[i + 0].subspan(0, byteCount));
                        memcpy(vals[1][d], src[i + 1].subspan(0, byteCount));
                        *oc::BitIterator(mLeftRight[0].mSufBit.data(), d) = controlBits[i + 1];
                        *oc::BitIterator(mLeftRight[1].mSufBit.data(), d) = controlBits[i + 2];
                    }

                    {
                        memcpy(vals[0][d], src[i + 0].subspan(0, byteCount));
                        memcpy(vals[1][d], src[i + 1].subspan(0, byteCount));

                        auto cLast = 0;
                        if (i + 2 < controlBits.size())
                        {
                            cLast = controlBits[i + 2];
                            // std::cout << "act clast " << d*2+1<< ": " << (int)cLast << " = c[" << i + 2 << "]" << std::endl;
                        }

                        *oc::BitIterator(mLeftRight[0].mSufBit.data(), d) = controlBits[i + 1];
                        *oc::BitIterator(mLeftRight[1].mSufBit.data(), d) = cLast;

                        i += 2;
                        ++d;
                    }

                    if (available & 1)
                    {
                        memcpy(vals[0][d], src[i + 0].subspan(0, byteCount));
                        memset(vals[1][d], 0);

                        auto cLast = 0;
                        if (i + 1 < controlBits.size())
                        {
                            cLast = controlBits[i + 1];
                            std::cout << "act clast* " << (int)cLast << std::endl;
                        }

                        *oc::BitIterator(mLeftRight[0].mSufBit.data(), d) = cLast;
                        *oc::BitIterator(mLeftRight[1].mSufBit.data(), d) = 0;

                        ++d;
                    }

                    // if there is space left over fill with zeros
                    for (; d < n / 2; ++d)
                    {
                        memset(vals[0][d], 0);
                        memset(vals[1][d], 0);
                        *oc::BitIterator(mLeftRight[0].mPreBit.data(), d) = 0;
                        *oc::BitIterator(mLeftRight[1].mPreBit.data(), d) = 0;
                    }

                    vals[0].transpose(mLeftRight[0].mSufVal);
                    vals[1].transpose(mLeftRight[1].mSufVal);
                }
            }
        }

        // load the leaf values and control bits.
        // src are the values, controlBits are ...
        // leaves are where we will write the results.
        // They are separated into left and right children.
        //
        // sIdx means that we should start copying values from
        // src, controlBits at row sIdx.
        //
        // dIdx means that we should start writing results to
        // leaf index dIdx.
        //
        // We require dIdx to be a multiple of 8 and therefore
        // we will pad the overall tree to be a multiple of 16.
        // We will assign zero to the padded control bits.
        void setLeafVals(
            const BinMatrix& src,
            const BinMatrix& controlBits,
            u64 sIdx,
            u64 dIdx)
        {
            auto srcSize = src.numEntries() - sIdx;
            auto dstSize = size() - dIdx;

            auto available = std::min<u64>(srcSize, dstSize);
            auto availableC = available + (controlBits.size() > sIdx + available);

            setLeafVals(
                src.subMatrix(sIdx, available),
                controlBits.subMatrix(sIdx, availableC),
                dIdx);
        }

        AggTreeType type() const
        {
            if (mLeftRight[0].mPreVal.size() == 0)
                return AggTreeType::Suffix;
            if (mLeftRight[0].mSufVal.size() == 0)
                return AggTreeType::Prefix;
            return AggTreeType::Full;
        }

        void copy(const AggTreeSplitLevel& src, u64 srcOffset, u64 destOffset, u64 dstSize)
        {
            // AggTreeSplitLevel is split into left and right and we require that the offset begin at
            // a byte boundary. Therefore srcOffset must be a multiple of 2 * 8.
            if (srcOffset % 16)
                throw RTE_LOC;
            // AggTreeSplitLevel is split into left and right and we require that the offset begin at
            // a byte boundary. Therefore destOffset must be a multiple of 2 * 8.
            if (destOffset % 16)
                throw RTE_LOC;
            auto srcSize = src.size() - srcOffset;

            if (srcSize % 16)
                throw RTE_LOC;
            if (dstSize < srcSize + destOffset)
                throw RTE_LOC;
            // auto dstSize = srcSize + destOffset;

            resize(dstSize, src.bitsPerEntry(), src.type());

            auto doCopy = [destOffset, srcOffset, srcSize](const auto& src, auto& dst)
                {
                    for (u64 i = 0; i < src.bitsPerEntry(); ++i)
                    {
                        auto d = &dst.mData(i, destOffset / 16);
                        auto s = &src.mData(i, srcOffset / 16);
                        memcpy(d, s, srcSize / 16);
                    }
                };
            for (u64 j = 0; j < 2; ++j)
            {
                doCopy(src.mLeftRight[j].mPreBit, mLeftRight[j].mPreBit);
                doCopy(src.mLeftRight[j].mPreVal, mLeftRight[j].mPreVal);
                doCopy(src.mLeftRight[j].mSufBit, mLeftRight[j].mSufBit);
                doCopy(src.mLeftRight[j].mSufVal, mLeftRight[j].mSufVal);
            }
        }

        void reshape(u64 shareCount)
        {
            for (u64 j = 0; j < 2; ++j)
            {
                if (mLeftRight[j].mPreBit.size())
                    mLeftRight[j].mPreBit.reshape(shareCount / 2);
                if (mLeftRight[j].mPreVal.size())
                    mLeftRight[j].mPreVal.reshape(shareCount / 2);
                if (mLeftRight[j].mSufBit.size())
                    mLeftRight[j].mSufBit.reshape(shareCount / 2);
                if (mLeftRight[j].mSufVal.size())
                    mLeftRight[j].mSufVal.reshape(shareCount / 2);
            }
        }

        bool operator!=(const AggTreeSplitLevel& o) const
        {
            auto neq = [](auto& l, auto& r)
                {
                    if (l.bitsPerEntry() != r.bitsPerEntry())
                        return false;
                    if (l.numEntries() != r.numEntries())
                        return false;
                    auto bytes = l.numEntries() / 8;

                    for (u64 i = 0; i < l.bitsPerEntry(); ++i)
                    {
                        for (u64 k = 0; k < bytes; ++k)
                            if (l.mData(i, k) != r.mData(i, k))
                                return true;
                    }
                    return false;
                };
            for (u64 j = 0; j < 2; ++j)
            {
                if (neq(mLeftRight[j].mPreBit, o.mLeftRight[j].mPreBit))
                    return true;
                if (neq(mLeftRight[j].mPreVal, o.mLeftRight[j].mPreVal))
                    return true;
                if (neq(mLeftRight[j].mSufBit, o.mLeftRight[j].mSufBit))
                    return true;
                if (neq(mLeftRight[j].mSufVal, o.mLeftRight[j].mSufVal))
                    return true;
                ;
            }
            return false;
        }
    };

    // struct DLevel
    //{
    //	BinMatrix mPreVal, mSufVal, mPreBit, mSufBit;

    //	void load(std::array<AggTreeLevel, 2>& tvs)
    //	{
    //		//Sh3Converter conv;

    //		throw RTE_LOC;
    //		//auto m = std::max<u64>(tvs[0].mPreVal.bitsPerEntry(), tvs[1].mSufVal.bitsPerEntry());

    //		//BinMatrix preVal[2], sufVal[2], preBit[2], sufBit[2];
    //		//conv.toBinaryMatrix(tvs[0].mPreVal, preVal[0]);
    //		//conv.toBinaryMatrix(tvs[1].mPreVal, preVal[1]);
    //		//conv.toBinaryMatrix(tvs[0].mPreBit, preBit[0]);
    //		//conv.toBinaryMatrix(tvs[1].mPreBit, preBit[1]);
    //		//conv.toBinaryMatrix(tvs[0].mSufVal, sufVal[0]);
    //		//conv.toBinaryMatrix(tvs[1].mSufVal, sufVal[1]);
    //		//conv.toBinaryMatrix(tvs[0].mSufBit, sufBit[0]);
    //		//conv.toBinaryMatrix(tvs[1].mSufBit, sufBit[1]);

    //		//preVal[0].trim();
    //		//preVal[1].trim();
    //		//preBit[0].trim();
    //		//preBit[1].trim();

    //		//sufVal[0].trim();
    //		//sufVal[1].trim();
    //		//sufBit[0].trim();
    //		//sufBit[1].trim();

    //		//mPreVal.resize(preVal[0].rows() + preVal[1].rows(), m);
    //		//mPreBit.resize(preBit[0].rows() + preBit[1].rows(), 1);
    //		//mSufVal.resize(sufVal[0].rows() + sufVal[1].rows(), m);
    //		//mSufBit.resize(sufBit[0].rows() + sufBit[1].rows(), 1);

    //		//for (u64 j = 0; j < mPreVal[0].rows(); ++j)
    //		//{
    //		//	for (u64 l = 0; l < 2; ++l)
    //		//	{
    //		//		for (u64 k = 0; k < mPreVal.i64Cols(); ++k)
    //		//			mPreVal.mShares[l](j, k) = preVal[j & 1].mShares[l](j / 2, k);

    //		//		if (mPreBit.rows())
    //		//			mPreBit.mShares[l](j) = preBit[j & 1].mShares[l](j / 2);
    //		//	}
    //		//}
    //		//for (u64 j = 0; j < mSufVal[0].rows(); ++j)
    //		//{
    //		//	for (u64 l = 0; l < 2; ++l)
    //		//	{
    //		//		for (u64 k = 0; k < mSufVal.i64Cols(); ++k)
    //		//			mSufVal.mShares[l](j, k) = sufVal[j & 1].mShares[l](j / 2, k);

    //		//		if (mSufBit.rows())
    //		//			mSufBit.mShares[l](j) = sufBit[j & 1].mShares[l](j / 2);
    //		//	}
    //		//}
    //	}

    //	void load(AggTreeLevel& tvs)
    //	{
    //		//throw RTE_LOC;
    //		//Sh3Converter conv;
    //		tvs.mPreVal.transpose(mPreVal);
    //		tvs.mPreBit.transpose(mPreBit);
    //		tvs.mSufVal.transpose(mSufVal);
    //		tvs.mSufBit.transpose(mSufBit);
    //	}
    //};

    // struct Level
    //{
    //	TBinMatrix mPreVal, mSufVal, mPreBit, mSufBit;

    //	void resize(u64 n, u64 bitCount, Type type)
    //	{
    //		if (type & Type::Prefix)
    //		{
    //			throw RTE_LOC;
    //			//mPreVal.reset(n, bitCount, 4);
    //			//mPreBit.reset(n, 1, 4);
    //		}
    //		if (type & Type::Suffix)
    //		{
    //			throw RTE_LOC;
    //			//mSufVal.reset(n, bitCount, 4);
    //			//mSufBit.reset(n, 1, 4);
    //		}
    //	}

    //};

    struct PLevelNew
    {
        BinMatrix mPreVal, mSufVal;
        BinMatrix mPreBit, mSufBit;

        u64 numEntries() { return mPreVal.numEntries(); }

        void resize(u64 n, u64 elementBitCount)
        {
            mPreVal.resize(n, elementBitCount);
            mPreVal.resize(n, elementBitCount);
            mSufVal.resize(n, elementBitCount);
            mSufVal.resize(n, elementBitCount);
            mPreBit.resize(n, 1);
            mSufBit.resize(n, 1);
        }

        // void load(DLevel* dl);

        // void validate(TBinMatrix& tvs0, TBinMatrix& tvs1, TBinMatrix& tvs2);

        // void validate(AggTreeLevel& tvs0, AggTreeLevel& tvs1, AggTreeLevel& tvs2);

        void reveal(std::array<AggTreeLevel, 2>& tvs0, std::array<AggTreeLevel, 2>& tvs1);
        void reveal(AggTreeLevel& tvs0, AggTreeLevel& tvs1);

        void perfectUnshuffle(PLevelNew& l0, PLevelNew& l1);
        // void load(AggTreeLevel& tvs0);
    };

    struct PLevel
    {
        std::vector<oc::BitVector> mPreVal, mSufVal;
        oc::BitVector mPreBit, mSufBit;

        u64 numEntries() { return mPreVal.size(); }

        u64 size() { return mPreBit.size(); }

        void resize(u64 n)
        {
            mPreVal.resize(n);
            mPreVal.resize(n);
            mSufVal.resize(n);
            mSufVal.resize(n);
            mPreBit.resize(n);
            mSufBit.resize(n);
        }

        // void load(DLevel* dl);

        // void validate(TBinMatrix& tvs0, TBinMatrix& tvs1, TBinMatrix& tvs2);

        // void validate(AggTreeLevel& tvs0, AggTreeLevel& tvs1, AggTreeLevel& tvs2);

        void reveal(AggTreeSplitLevel& tvs0, AggTreeSplitLevel& tvs1);
        void reveal(AggTreeLevel& tvs0, AggTreeLevel& tvs1);

        void perfectUnshuffle(PLevel& l0, PLevel& l1);
        // void load(AggTreeLevel& tvs0);
    };
}