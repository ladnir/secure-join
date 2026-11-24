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

    // a helper struct used to track the size of the agg tree. mostly
    // useful when the tree is not full.
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

            mLevelSizes.resize(mLogn + 1);

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

    // A level in the tree. data is stored in transposed format.
    // prefix and suffix may not both be used at the same time
    // depending on the type.
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

        // the size of the level.
        u64 size() const { return std::max(prefixSize(), suffixSize()); }

        // the size of the level if we only consider prefix values.
        u64 prefixSize() const { return mLeftRight[0].mPreBit.numEntries() + mLeftRight[1].mPreBit.numEntries(); }

        // the size of the level if we only consider suffix values.
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

            if (prefixSize())
            {
                std::array<BinMatrix, 2> vals;
                vals[0].resize(oc::divCeil(n, 2), bitCount);
                vals[1].resize(n / 2, bitCount);
                u64 i = 0, d = offset / 2;

                // for the first even number of rows split te values into left and right
                for (; i < available2; i += 2, ++d)
                {
                    osuCrypto::copyBytes(vals[0][d], src[i + 0].subspan(0, byteCount));
                    osuCrypto::copyBytes(vals[1][d], src[i + 1].subspan(0, byteCount));

                    assert(mLeftRight[0].mPreBit.size() > d / 8);
                    assert(mLeftRight[1].mPreBit.size() > d / 8);

                    *oc::BitIterator(mLeftRight[0].mPreBit.data(), d) = controlBits[i + 0];
                    *oc::BitIterator(mLeftRight[1].mPreBit.data(), d) = controlBits[i + 1];
                }

                // special case for when we have an odd number of rows.
                if (available & 1)
                {
                    assert(i == src.rows() - 1);
                    osuCrypto::copyBytes(vals[0][d], src[i].subspan(0, byteCount));
                    osuCrypto::setBytes(vals[1][d], 0);
                    assert(mLeftRight[0].mPreBit.size() > d / 8);
                    *oc::BitIterator(mLeftRight[0].mPreBit.data(), d) = controlBits[i];
                    *oc::BitIterator(mLeftRight[1].mPreBit.data(), d) = 0;

                    ++d;
                }

                // if there is space left over fill with zeros
                for (; d < n / 2; ++d)
                {
                    osuCrypto::setBytes(vals[0][d], 0);
                    osuCrypto::setBytes(vals[1][d], 0);
                    *oc::BitIterator(mLeftRight[0].mPreBit.data(), d) = 0;
                    *oc::BitIterator(mLeftRight[1].mPreBit.data(), d) = 0;
                }

                // transpose
                vals[0].transpose(mLeftRight[0].mPreVal);
                vals[1].transpose(mLeftRight[1].mPreVal);
            }

            if (suffixSize())
            {
                std::array<BinMatrix, 2> vals;
                vals[0].resize(oc::divCeil(n, 2), bitCount);
                vals[1].resize(n / 2, bitCount);

                u64 i = 0, d = offset / 2;
                for (; i < available2; i += 2, ++d)
                {
                    osuCrypto::copyBytes(vals[0][d], src[i].first(byteCount));
                    osuCrypto::copyBytes(vals[1][d], src[i + 1].first(byteCount));
                    *oc::BitIterator(mLeftRight[0].mSufBit.data(), d) = controlBits[i + 1];
                    *oc::BitIterator(mLeftRight[1].mSufBit.data(), d) =
                        i + 2 < controlBits.size() ? controlBits[i + 2] : 0;
                }
                if (available & 1)
                {
                    osuCrypto::copyBytes(vals[0][d], src[i].first(byteCount));
                    *oc::BitIterator(mLeftRight[0].mSufBit.data(), d) =
                        i + 1 < controlBits.size() ? controlBits[i + 1] : 0;
                    *oc::BitIterator(mLeftRight[1].mSufBit.data(), d) = 0;
                    ++d;
                }
                // vals is zero-initialized. Clear the corresponding control bits.
                for (; d < n / 2; ++d)
                {
                    *oc::BitIterator(mLeftRight[0].mSufBit.data(), d) = 0;
                    *oc::BitIterator(mLeftRight[1].mSufBit.data(), d) = 0;
                }

                vals[0].transpose(mLeftRight[0].mSufVal);
                vals[1].transpose(mLeftRight[1].mSufVal);
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

            auto doCopy = [destOffset, srcOffset, srcSize](const auto& src, auto& dst) {
                    if (src.size() == 0)
                        return;
                    auto size = srcSize / 16;
                    auto dOffset = destOffset / 16;
                    auto sOffset = srcOffset / 16;
                    for (u64 i = 0; i < src.bitsPerEntry(); ++i)
                    {
                        auto d = dst.mData[i].subspan(dOffset, size);
                        auto s = src.mData[i].subspan(sOffset, size);
                        osuCrypto::copyBytes(d, s);
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

    // a level of the plaintext agg tree.
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

        void reveal(AggTreeSplitLevel& tvs0, AggTreeSplitLevel& tvs1);

        void reveal(AggTreeLevel& tvs0, AggTreeLevel& tvs1);

        void perfectUnshuffle(PLevel& l0, PLevel& l1);
    };
}