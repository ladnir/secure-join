#pragma once
#include "secure-join/Defines.h"
#include "cryptoTools/Common/Matrix.h"
#include "libOTe/Tools/Tools.h"
#include "Trim.h"

namespace secJoin
{
    struct TBinMatrix;

    // represents a binary matrix. Each row represents a
    // bit vector of data.
    struct BinMatrix
    {
        using iterator = span<u8>::iterator;
        using const_iterator = span<u8>::iterator;
        using reverse_iterator = std::reverse_iterator<iterator>;
        using const_reverse_iterator = std::reverse_iterator<const_iterator>;
        using value_type = u8;
        using pointer = value_type*;
        using size_type = u64;

        oc::Matrix<u8> mData;

        u64 mBitCount = 0;

        BinMatrix() = default;
        BinMatrix(const BinMatrix&) = default;
        BinMatrix(BinMatrix&&) = default;
        BinMatrix& operator=(const BinMatrix&) = default;
        BinMatrix& operator=(BinMatrix&&) = default;

        BinMatrix(u64 rows, u64 bits, u64 aligment = 1)
        {
            resize(rows, bits, aligment);
        }

        void resize(u64 rows, u64 bits, u64 aligment = 1, oc::AllocType alloc = oc::AllocType::Zeroed)
        {
            mData.resize(rows, oc::divCeil(bits, 8 * aligment), alloc);
            mBitCount = bits;
        }

        // keep the number of entries the same but change the bit count;
        void reshape(u64 bitCount)
        {
            if (bitCount > mData.cols() * 8)
                throw RTE_LOC;
            mBitCount = bitCount;
        }

        u64 bitsPerEntry() const
        {
            return mBitCount;
        }

        u64 bytesPerEntry() const
        {
            return mData.cols();
        }

        u64 numEntries() const
        {
            return mData.rows();
        }

        u64 rows() const { return numEntries(); }
        u64 cols() const { return bytesPerEntry(); }
        u8* data()
        {
            return mData.data();
        }
        const u8* data() const
        {
            return mData.data();
        }

        u8* data(u64 i)
        {
            return mData.data(i);
        }
        const u8* data(u64 i) const
        {
            return mData.data(i);
        }

        u64 size() const
        {
            return mData.size();
        }

        auto begin()
        {
            return mData.begin();
        }
        auto end()
        {
            return mData.end();
        }

        span<u8> operator[](u64 i) { return mData[i]; }
        span<const u8> operator[](u64 i) const { return mData[i]; }

        u8& operator()(u64 i) { return mData(i); }
        u8& operator()(u64 i, u64 j) { return mData(i, j); }

        const u8& operator()(u64 i) const { return mData(i); }
        const u8& operator()(u64 i, u64 j) const { return mData(i, j); }

        void transpose(oc::MatrixView<u8> dst) const;
        void transpose(TBinMatrix& dst) const;
        TBinMatrix transpose() const;

        void setZero()
        {
            if (mData.size())
                memset(mData.data(), 0, mData.size());
        }

        bool operator==(const BinMatrix& b) const
        {
            if (numEntries() != b.numEntries())
                return false;
            if (bitsPerEntry() != b.bitsPerEntry())
                return false;

            if (size() == b.size() && bitsPerEntry() == bytesPerEntry() * 8)
            {
                return std::memcmp(data(), b.data(), size()) == 0;
            }
            else
            {
                return areEqual<u8>(mData, b.mData, bitsPerEntry());
                // not impl
                // throw RTE_LOC;
            }
        }
        bool operator!=(const BinMatrix& b) const
        {
            return !(*this == b);
        }

        void trim()
        {
            ::secJoin::trim(mData, mBitCount);
        }

        oc::MatrixView<u8> subMatrix(u64 rowIdx, u64 count)
        {
            if (rowIdx >= mData.rows())
                throw RTE_LOC;
            if (rowIdx + count > mData.rows())
                throw RTE_LOC;
            return oc::MatrixView<u8>(mData.data(rowIdx), count, mData.cols());
        }

        oc::MatrixView<u8> subMatrix(u64 rowIdx)
        {
            return subMatrix(rowIdx, mData.rows() - rowIdx);
        }

        oc::MatrixView<const u8> subMatrix(u64 rowIdx, u64 count) const
        {
            if (rowIdx >= mData.rows())
                throw RTE_LOC;
            if (rowIdx + count > mData.rows())
                throw RTE_LOC;
            return oc::MatrixView<const u8>(mData.data(rowIdx), count, mData.cols());
        }

        oc::MatrixView<const u8> subMatrix(u64 rowIdx) const
        {
            return subMatrix(rowIdx, mData.rows() - rowIdx);
        }

        operator oc::MatrixView<u8>()
        {
            return mData;
        }
        operator oc::MatrixView<const u8>() const
        {
            return oc::MatrixView<const u8>(mData.data(), mData.rows(), mData.cols());
        }
    };

    // struct BinMatrixView : oc::MatrixView<u8>
    // {
    //     u64 mBitCount = 0;

    //     BinMatrixView() = default;
    //     BinMatrixView(const BinMatrixView&) = default;
    //     BinMatrixView(BinMatrixView&&) = default;
    //     BinMatrixView& operator=(const BinMatrixView&) = default;
    //     BinMatrixView& operator=(BinMatrixView&&) = default;

    //     BinMatrixView(const BinMatrix& m)
    //         : oc::MatrixView<u8>(m.mData)
    //         , mBitCount(m.mBitCount)
    //     {}

    //     BinMatrixView(const oc::MatrixView<u8>& m)
    //         : oc::MatrixView<u8>(m)
    //         , mBitCount(m.cols() * 8)
    //     {}

    //     BinMatrixView& operator=(const BinMatrix& m)
    //     {
    //         (oc::MatrixView<u8>&)(*this) = (m.mData);
    //         mBitCount = (m.mBitCount);

    //     }

    //     BinMatrixView& operator=(const oc::MatrixView<u8>& m)
    //     {
    //         (oc::MatrixView<u8>&)(*this) = m;
    //         mBitCount = (m.cols() * 8);
    //     }

    //     u64 bitsPerEntry() const { return mBitCount; }
    // };

    // represents a binary matrix in bit transpose format.
    // the i'th row shorts the i'th bit of the elements.
    struct TBinMatrix
    {

        TBinMatrix() = default;
        TBinMatrix(TBinMatrix&&) = default;
        TBinMatrix(const TBinMatrix&) = default;
        TBinMatrix& operator=(TBinMatrix&&) = default;
        TBinMatrix& operator=(const TBinMatrix&) = default;
        TBinMatrix(u64 rows, u64 bits, u64 alignment = 1)
        {
            resize(rows, bits, alignment);
        }

        oc::Matrix<u8> mData;
        u64 mEntryCount = 0;
        void resize(u64 rows, u64 bits, u64 alignment = 1)
        {
            mData.resize(bits, oc::roundUpTo(oc::divCeil(rows, 8), alignment));
            mEntryCount = rows;
        }

        void reshape(u64 shareCount)
        {
            if (shareCount > mData.cols() * sizeof(*mData.data()) * 8)
                throw RTE_LOC;

            mEntryCount = shareCount;
        }

        u64 bitsPerEntry() const
        {
            return mData.rows();
        }

        u64 numEntries() const
        {
            return mEntryCount;
        }

        u8* data() { return mData.data(); }
        u8* data(u64 bitIdx) { return mData.data(bitIdx); }
        u64 size() const { return mData.size(); }
        u64 bytesPerRow() const { return mData.cols(); }

        u8& operator()(u64 i) { return mData(i); }
        u8& operator()(u64 i, u64 j) { return mData(i, j); }

        const u8& operator()(u64 i) const { return mData(i); }
        const u8& operator()(u64 i, u64 j) const { return mData(i, j); }

        template <typename T>
        u64 simdWidth() const
        {
            assert(bytesPerRow() % sizeof(T) == 0);
            return bytesPerRow() / sizeof(T);
        }

        void trim()
        {
            ::secJoin::trim(mData, mEntryCount);
        }

        span<u8> operator[](u64 i) { return mData[i]; }
        span<u8 const> operator[](u64 i) const { return mData[i]; }

        void transpose(BinMatrix& dst) const
        {
            if (dst.bitsPerEntry() != bitsPerEntry() ||
                dst.numEntries() != numEntries())
                dst.resize(numEntries(), bitsPerEntry());

            oc::transpose(mData, dst.mData);
        }

        BinMatrix transpose() const
        {
            BinMatrix r;
            transpose(r);
            return r;
        }
    };

    inline void BinMatrix::transpose(TBinMatrix& dst) const
    {
        if (dst.bitsPerEntry() != bitsPerEntry() ||
            dst.numEntries() != numEntries())
            dst.resize(numEntries(), bitsPerEntry());

        oc::transpose(mData, dst.mData);
    }

    inline void BinMatrix::transpose(oc::MatrixView<u8> dst) const
    {
        if (dst.rows() != bitsPerEntry() ||
            dst.cols() < oc::divCeil(numEntries(), 8))
            throw RTE_LOC;

        oc::transpose(mData, dst);
    }

    inline TBinMatrix BinMatrix::transpose() const
    {
        TBinMatrix r;
        transpose(r);
        return r;
    }


    template<typename T, typename = void>
    struct has_cols
        : std::false_type
    {};
    template<typename T>
    struct has_cols <T, std::void_t<decltype(std::declval<T>().cols() == u64{0}) >>
        : std::true_type
    {};


    template<typename T, typename U>
    oc::MatrixView<T> matrixCast(U&& u)
    {
        using UT = decltype(*u.data());
        static_assert(sizeof(UT) % sizeof(T) == 0 || sizeof(UT) > sizeof(T));

        if constexpr (has_cols<U>::value)
        {

            if (sizeof(UT) > sizeof(T))
            {
                if (sizeof(UT) * u.cols() % sizeof(T))
                    throw RTE_LOC;
            }
            return oc::MatrixView<T>((T*)u.data(), u.rows(), u.cols() * sizeof(UT) / sizeof(T));
        }
        else
        {
            static_assert(sizeof(UT) % sizeof(T) == 0);
            return oc::MatrixView<T>((T*)u.data(), u.size(), sizeof(UT) / sizeof(T));
        }
    }



    template<typename T, typename U>
    oc::span<T> spanCast(U&& u)
    {
        using UT = decltype(*u.data());

        static_assert(sizeof(UT) % sizeof(T) == 0 || sizeof(UT) > sizeof(T));
        if (sizeof(UT) > sizeof(T))
        {
            if (sizeof(UT) * u.size() % sizeof(T))
                throw RTE_LOC;
        }
        return oc::span<T>((T*)u.data(), u.size() * sizeof(UT) / sizeof(T));
    }
}
