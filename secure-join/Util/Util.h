#pragma once
#include "cryptoTools/Common/Matrix.h"
#include "cryptoTools/Crypto/PRNG.h"
#include "secure-join/Defines.h"
#include <vector>
#include "secure-join/Perm/AdditivePerm.h"
#include "secure-join/Join/Table.h"

namespace secJoin
{

    using oc::Matrix;
    inline oc::Matrix<oc::block> reveal(oc::MatrixView<oc::block>x0, oc::MatrixView<oc::block>x1)
    {
        oc::Matrix<oc::block>ret(x0.rows(), x0.cols());
        for (u64 i = 0; i < ret.size(); ++i)
            ret(i) = x0(i) ^ x1(i);
        return ret;
    }
    inline oc::Matrix<oc::block> reveal(std::array<oc::Matrix<oc::block>, 2>& x)
    {
        return reveal(x[0], x[1]);
    }

    template<typename T>
    inline std::array<oc::Matrix<T>, 2> xorShare(oc::MatrixView<T> d, PRNG& prng)
    {
        std::array<oc::Matrix<T>, 2> ret;

        ret[0].resize(d.rows(), d.cols());
        ret[1].resize(d.rows(), d.cols());
        prng.get(ret[0].data(), ret[0].size());
        for (u64 i = 0; i < d.rows(); ++i)
        {
            for (u64 j = 0; j < d.cols(); ++j)
            {
                ret[1](i, j) = d(i, j) ^ ret[0](i, j);
            }
        }
        return ret;
    }

    inline void share(const Matrix<u32>& x, Matrix<u32>& x0, Matrix<u32>& x1, PRNG& prng)
    {
        x0.resize(x.rows(), x.cols());
        x1.resize(x.rows(), x.cols());
        prng.get(x0.data(), x0.size());

        for (u64 i = 0; i < x.size(); ++i)
            x1(i) = x(i) - x0(i);
    }

    inline void share(const span<u32> x, std::vector<u32>& x0, std::vector<u32>& x1, PRNG& prng)
    {
        x0.resize(x.size());
        x1.resize(x.size());
        prng.get(x0.data(), x0.size());
        for (u64 i = 0; i < x.size(); ++i)
            x1[i] = x[i] - x0[i];
    }

    inline void share(const Matrix<u8>& x, u64 bitCount, Matrix<u8>& x0, Matrix<u8>& x1, PRNG& prng)
    {
        if (x.cols() != oc::divCeil(bitCount, 8))
            throw RTE_LOC;

        x0.resize(x.rows(), x.cols());
        x1.resize(x.rows(), x.cols());
        prng.get(x0.data(), x0.size());
        for (u64 i = 0; i < x.size(); ++i)
            x1(i) = x(i) ^ x0(i);

        if (bitCount % 8)
        {
            auto mask = (1 << (bitCount % 8)) - 1;
            for (u64 i = 0; i < x.rows(); ++i)
            {
                x0[i].back() &= mask;
                x1[i].back() &= mask;
            }
        }
    }


    inline void share(const BinMatrix& x, BinMatrix& x0, BinMatrix& x1, PRNG& prng)
    {
        auto bitCount = x.bitsPerEntry();
        x0.resize(x.rows(), x.bitsPerEntry());
        x1.resize(x.rows(), x.bitsPerEntry());
        prng.get(x0.data(), x0.size());
        for (u64 i = 0; i < x.size(); ++i)
            x1(i) = x(i) ^ x0(i);

        if (bitCount % 8)
        {
            auto mask = (1 << (bitCount % 8)) - 1;
            for (u64 i = 0; i < x.rows(); ++i)
            {
                x0[i].back() &= mask;
                x1[i].back() &= mask;
            }
        }
    }

    inline void share(const Matrix<u8>& x, Matrix<u8>& x0, Matrix<u8>& x1, PRNG& prng)
    {
        share(x, x.cols() * 8, x0, x1, prng);
    }
    inline Perm reveal(const AdditivePerm& x0, const AdditivePerm& x1)
    {
        if (x0.mRho != x1.mRho)
            throw RTE_LOC;
        Perm p(x0.size());

        {
            for (u64 i = 0; i < p.size(); ++i)
                p.mPi[i] = x0.mShare[i] ^ x1.mShare[i];
        }
        // else
        //{
        //     for (u64 i = 0; i < p.size(); ++i)
        //         p.mPi[i] = x0.mShare[i] + x1.mShare[i];
        // }

        // p.validate();
        return p;
    }

    inline Matrix<u32> reveal(const Matrix<u32>& x0, const Matrix<u32>& x1)
    {
        Matrix<u32> r(x0.rows(), x0.cols());
        for (u64 i = 0; i < r.size(); ++i)
            r(i) = x0(i) + x1(i);
        return r;
    }

    inline std::array<oc::Matrix<oc::u8>, 2> share(
        oc::Matrix<oc::u8> v,
        PRNG& prng)
    {
        auto n = v.rows();
        oc::Matrix<oc::u8>
            s0(n, v.cols()),
            s1(n, v.cols());

        prng.get(s0.data(), s0.size());

        for (oc::u64 i = 0; i < v.size(); ++i)
            s1(i) = v(i) ^ s0(i);

        return { s0, s1 };
    }

    inline std::array<oc::Matrix<oc::u32>, 2> share(
        oc::Matrix<oc::u32> v,
        PRNG& prng)
    {
        auto n = v.rows();
        oc::Matrix<oc::u32>
            s0(n, v.cols()),
            s1(n, v.cols());

        prng.get(s0.data(), s0.size());

        for (oc::u64 i = 0; i < v.size(); ++i)
            s1(i) = v(i) - s0(i);

        return { s0, s1 };
    }

    inline std::array<std::vector<u32>, 2> xorShare(
        span<const u32> v,
        PRNG& prng)
    {
        auto n = v.size();
        std::vector<u32>
            s0(n),
            s1(n);

        prng.get(s0.data(), s0.size());
        prng.get((u8*)s0.data(), n * sizeof(u32));

        for (oc::u64 i = 0; i < v.size(); ++i)
            s1[i] = v[i] ^ s0[i];

        return { s0, s1 };
    }


    inline BinMatrix reveal(
        const BinMatrix& v1,
        const BinMatrix& v2)
    {

        // Checking the dimensions
        if (v1.rows() != v2.rows())
            throw RTE_LOC;
        if (v1.bitsPerEntry() != v2.bitsPerEntry())
            throw RTE_LOC;

        BinMatrix s(v1.rows(), v1.bitsPerEntry());

        for (oc::u64 i = 0; i < v1.size(); ++i)
            s(i) = v1(i) ^ v2(i);

        return s;
    }

    inline oc::Matrix<oc::u8> reveal(
        oc::MatrixView<oc::u8> v1,
        oc::MatrixView<oc::u8> v2)
    {

        // Checking the dimensions
        if (v1.rows() != v2.rows())
            throw RTE_LOC;
        if (v1.cols() != v2.cols())
            throw RTE_LOC;

        oc::Matrix<oc::u8> s(v1.rows(), v1.cols());

        for (oc::u64 i = 0; i < v1.size(); ++i)
            s(i) = v1(i) ^ v2(i);

        return s;
    }

    template<typename T>
    inline bool eq(
        const oc::Matrix<T>& v1,
        const oc::Matrix<T>& v2)
    {
        // Checking the dimensions
        if (v1.rows() != v2.rows())
            throw RTE_LOC;
        if (v1.cols() != v2.cols())
            throw RTE_LOC;

        return std::equal(v1.begin(), v1.end(), v2.begin());
    }

    inline void printMatrix(const oc::Matrix<oc::u8>& v1)
    {
        for (u64 i = 0; i < v1.rows(); i++)
        {
            std::cout << i << ": ";
            std::cout << hex(v1[i]) << " ";
            std::cout << std::endl;
        }
    }


    inline Table reveal(const Table& t0, const Table& t1, bool removeNulls = true)
    {
        u64 size = 0;
        if (t0.getColumnInfo() != t1.getColumnInfo() || t0.rows() != t1.rows())
        {
            throw RTE_LOC;
        }
        if (t0.mIsActive.size() && removeNulls)
        {
            for (u64 i = 0; i < t0.rows(); ++i)
            {
                assert((t0.mIsActive[i] ^ t1.mIsActive[i]) < 2);
                size += t0.mIsActive[i] ^ t1.mIsActive[i];
            }
        }
        else
            size = t0.rows();


        Table ret;
        ret.init(size, t0.getColumnInfo());
        if (removeNulls == false)
            ret.mIsActive.resize(size);
        for (u64 i = 0, j = 0; i < t0.rows(); ++i)
        {
            bool isActive = t0.mIsActive[i] ^ t1.mIsActive[i];
            if (removeNulls == false)
            {
                ret.mIsActive[j] = isActive;
            }

            if (isActive)
            {
                for (u64 k = 0; k < t0.mColumns.size(); ++k)
                {
                    for (u64 l = 0;l < ret.mColumns[k].mData.cols(); ++l)
                    {
                        ret.mColumns[k].mData(j, l) =
                            t0.mColumns[k].mData(i, l) ^
                            t1.mColumns[k].mData(i, l);
                    }
                }
                ++j;
            }
        }

        return ret;
    }
}