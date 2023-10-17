#include "PlainAggTree.h"


namespace secJoin
{
    //void PTreeNew::init(
    //    u64 n,
    //    u64 bitCount,
    //    PRNG& prng,
    //    std::function<
    //    oc::BitVector(
    //        const oc::BitVector&,
    //        const oc::BitVector&)
    //    > op)
    //{
    //    BinMatrix s(n, bitCount);
    //    oc::BitVector c(n);

    //    for (u64 i = 0; i < n; ++i)
    //    {
    //        //s[i].resize(bitCount);

    //        u64 t = (i % bitCount);
    //        u64 v = (i / bitCount) + 1;
    //        for (u64 j = 0; j < std::min<u64>(bitCount, 64); ++j)
    //        {
    //            *oc::BitIterator(s.data(i), (j + t) % bitCount) 
    //                = *oc::BitIterator((u8*)&v, j);
    //        }
    //        //s[i].randomize(prng);
    //        if (i)
    //            c[i] = prng.getBit();
    //    }

    //    init(s, c, op);
    //}

    //void PTreeNew::init(const BinMatrix& s, const oc::BitVector& c, std::function<oc::BitVector(const oc::BitVector&, const oc::BitVector&)> op)
    //{
    //    loadLeaves(s, c);
    //    upstream(op);
    //    downstream(op);
    //    leaves(op);
    //}

    //void PTreeNew::leaves(std::function<oc::BitVector(const oc::BitVector&, const oc::BitVector&)> op)
    //{
    //    mPre.resize(n, mInput.bitsPerEntry());
    //    mSuf.resize(n, mInput.bitsPerEntry());
    //    mFull.resize(n, mInput.bitsPerEntry());
    //    BinMatrix
    //        expPre(n, mInput.bitsPerEntry()),
    //        expSuf(n, mInput.bitsPerEntry());

    //    oc::BitVector
    //        pre, suf, expPrei(mInput.bitsPerEntry()), expSufi(mInput.bitsPerEntry()),
    //        in, inn;

    //    for (u64 i = 0; i < n; ++i)
    //    {
    //        auto q = i < n0 ? 0 : 1;
    //        auto w = i < n0 ? i : i - r;

    //        auto& ll = mLevels[q].mDown;


    //        in.resize(0);
    //        in.append(mInput.data(i), mInput.bitsPerEntry());
    //        pre.resize(0);
    //        pre.append(ll.mPreVal.data(w), mInput.bitsPerEntry());
    //        suf.resize(0);
    //        suf.append(ll.mPreVal.data(w), mInput.bitsPerEntry());
    //        auto ii = n - 1 - i;
    //        inn.resize(0);
    //        inn.append(mInput.data(ii), mInput.bitsPerEntry());

    //        expPrei = mCtrl[i] ? op(expPrei, in) : in;
    //        memcpy(expPre.data(i), expPrei.data(), expPrei.sizeBytes());

    //        u64 c = i ? mCtrl[ii + 1] : 0;
    //        expSufi = c ? op(inn, expSufi) : inn;
    //        memcpy(expSuf.data(i), expSufi.data(), expSufi.sizeBytes());


    //        pre = ll.mPreBit(w) ? op(pre, in) : in;
    //        auto full = ll.mPreBit(w) ? pre : in;
    //        full = ll.mSufBit(w) ? op(full, suf) : full;
    //        suf = ll.mSufBit(w) ? op(in, suf) : in;

    //        //mPre[i] = ll.mPreBit(w) ? op(ll.mPreVal[w], mInput[i]) : mInput[i];
    //        //mSuf[i] = ll.mSufBit(w) ? op(mInput[i], ll.mSufVal[w]) : mInput[i];
    //        //mFull[i] = ll.mPreBit(w) ? mPre[i] : mInput[i];
    //        //mFull[i] = ll.mSufBit(w) ? op(mFull[i], ll.mSufVal[w]) : mFull[i];

    //        memcpy(mSuf.data(i), suf.data(), suf.sizeBytes());
    //        memcpy(mPre.data(i), pre.data(), pre.sizeBytes());
    //        memcpy(mFull.data(i), full.data(), full.sizeBytes());

    //    }

    //    //for (u64 i = 0; i < n; ++i)
    //    //{
    //    //    u64 c = mCtrl[i];
    //    //}

    //    if (mPre != expPre)
    //        throw RTE_LOC;
    //    if (mSuf != expSuf)
    //        throw RTE_LOC;
    //}

    //void PTreeNew::downstream(std::function<oc::BitVector(const oc::BitVector&, const oc::BitVector&)> op)
    //{
    //    assert(mLevels.back().mUp.numEntries() == 1);
    //    mLevels.back().mDown = mLevels.back().mUp;

    //    for (u64 j = mLevels.size() - 1; j != 0; --j)
    //    {
    //        auto& parent = mLevels[j].mDown;
    //        auto& childDn = mLevels[j - 1].mDown;
    //        auto& childUp = mLevels[j - 1].mUp;
    //        u64 end = childDn.numEntries() / 2;
    //        childDn.mPreBit = childUp.mPreBit;
    //        childDn.mSufBit = childUp.mSufBit;
    //        auto bv = [&](auto& d, u64 i) {
    //            return oc::BitVector(d.data(i), d.bitsPerEntry()); 
    //        };
    //        auto write = [](auto& d, u64 i, auto&& v)
    //        {
    //            assert(d.bitsPerEntry() == v.size());
    //            memcpy(d.data(i), v.data(), v.sizeBytes());
    //        };
    //        for (u64 i = 0; i < end; ++i)
    //        {
    //            {
    //                auto v = bv(parent.mPreVal, i);
    //                auto v0 = bv(childUp.mPreVal, i * 2);
    //                auto v1 = bv(childUp.mPreVal, i * 2 + 1);
    //                //auto d0 = bv(childDn.mPreVal.data(i * 2));
    //                //auto d1 = bv(childDn.mPreVal.data(i * 2 + 1));
    //                auto p0 = childUp.mPreBit(i * 2);

    //                assert(v.size());
    //                assert(v0.size());
    //                assert(v1.size());

    //                write(childDn.mPreVal, i * 2 + 0, p0 ? op(v, v0) : v0);
    //                write(childDn.mPreVal, i * 2 + 1, v);
    //                //auto d0 = v;

    //                //assert(d0.sizeBytes() == childDn.mPreVal.bytesPerEntry());
    //                //assert(d1.sizeBytes() == childDn.mPreVal.bytesPerEntry());

    //                //auto dd0 = childDn.mPreVal.data(i * 2);
    //                //auto dd1 = childDn.mPreVal.data(i * 2+1);
    //                //memcpy(dd0, d0.data(), d0.sizeBytes());
    //                //memcpy(dd1, d1.data(), d1.sizeBytes());
    //            }

    //            {
    //                auto v =  bv(parent.mSufVal ,i);
    //                auto v0 = bv(childUp.mSufVal, i * 2);
    //                auto v1 = bv(childUp.mSufVal, i * 2 + 1);
    //                //auto d0 = bv(childDn.mSufVal.data(i * 2));
    //                //auto d1 = bv(childDn.mSufVal.data(i * 2 + 1));
    //                auto p1 = childUp.mSufBit(i * 2 + 1);

    //                auto d0 = p1 ? op(v1, v) : v1;
    //                auto d1 = v;

    //                write(childDn.mSufVal, i * 2 + 0, d0);
    //                write(childDn.mSufVal, i * 2 + 1, d1);

    //                //auto dd0 = childDn.mSufVal.data(i * 2);
    //                //auto dd1 = childDn.mSufVal.data(i * 2 + 1);
    //                //memcpy(dd0, d0.data(), d0.sizeBytes());
    //                //memcpy(dd1, d1.data(), d1.sizeBytes());
    //            }
    //        }
    //    }
    //}

    //void PTreeNew::upstream(std::function<oc::BitVector(const oc::BitVector&, const oc::BitVector&)> op)
    //{
    //    for (u64 j = 1; j < mLevels.size(); ++j)
    //    {
    //        u64 end = mLevels[j - 1].mUp.numEntries() / 2;
    //        auto& child = mLevels[j - 1].mUp;
    //        auto& parent = mLevels[j].mUp;
    //        auto bv = [&](auto& d, u64 i) {
    //            return oc::BitVector(d.data(i), d.bitsPerEntry()); 
    //        };

    //        auto write = [](auto& d, u64 i, auto& v)
    //        {
    //            assert(d.bitsPerEntry() == v.size());
    //            memcpy(d.data(i), v.data(), v.sizeBytes());
    //        };
    //        for (u64 i = 0; i < end; ++i)
    //        {
    //            {
    //                auto v0 = bv(child.mPreVal, 2 * i);
    //                auto v1 = bv(child.mPreVal, 2 * i + 1);
    //                auto p0 = (child.mPreBit(2 * i));
    //                auto p1 = (child.mPreBit(2 * i + 1));

    //                auto pre = p1 ? op(v0, v1) : v1;
    //                auto bit = p1 * p0;

    //                write(parent.mPreVal, i, pre);
    //                //assert(parent.mPreVal.bitsPerEntry() == pre.size());
    //                //memcpy(parent.mPreVal.data(i), pre.data(), pre.sizeBytes());
    //                parent.mPreBit(i) = bit;

    //            }

    //            {
    //                auto v0 = bv(child.mSufVal, 2 * i);
    //                auto v1 = bv(child.mSufVal, 2 * i + 1);
    //                auto p0 = child.mSufBit(2 * i);
    //                auto p1 = child.mSufBit(2 * i + 1);

    //                auto suf = p0 ? op(v0, v1) : v0;
    //                auto bit = p1 * p0;

    //                //assert(parent.mSufVal.bitsPerEntry() == suf.size());
    //                //memcpy(parent.mSufVal.data(i), suf.data(), suf.sizeBytes());
    //                write(parent.mSufVal, i, suf);
    //                parent.mSufBit(i) = bit;
    //            }
    //        }
    //    }

    //}

    //void PTreeNew::loadLeaves(
    //    const BinMatrix& s,
    //    const oc::BitVector& c)
    //{

    //    mInput = s;
    //    mCtrl = c;

    //    bitCount = s.bitsPerEntry();
    //    n = s.numEntries();
    //    n16 = n;
    //    logn = oc::log2ceil(n);
    //    logfn = oc::log2floor(n);
    //    if (logn != logfn)
    //    {
    //        n16 = oc::roundUpTo(n, 16);
    //        logn = oc::log2ceil(n16);
    //        logfn = oc::log2floor(n16);

    //    }

    //    r = n16 - (1ull << logfn);
    //    n0 = r ? 2 * r : n16;
    //    n1 = n16 - n0;


    //    mLevels.resize(logn + 1);
    //    for (u64 j = 0; j < 2; ++j)
    //    {
    //        mLevels[0][j].resize(n0, s.bitsPerEntry());
    //        if (r)
    //            mLevels[1][j].resize(1ull << logfn, s.bitsPerEntry());

    //        for (u64 i = r ? 2 : 1; i < mLevels.size(); ++i)
    //        {
    //            auto nn = mLevels[i - 1][j].numEntries() / 2;
    //            assert(nn);
    //            mLevels[i][j].resize(nn, s.bitsPerEntry());
    //        }
    //    }

    //    auto bv = [&](BinMatrix& d, u64 i) {
    //        return oc::BitVector(d.data(i), d.bitsPerEntry()); 
    //    };
    //    auto write = [](auto& d, u64 i, auto&& v)
    //    {
    //        assert(d.bitsPerEntry() == v.size());
    //        memcpy(d.data(i), v.data(), v.sizeBytes());
    //    };

    //    for (u64 i = 0; i < n; ++i)
    //    {
    //        auto q = i < n0 ? 0 : 1;
    //        auto w = i < n0 ? i : i - r;

    //        write(mLevels[q].mUp.mPreVal, w, bv(mInput, i));
    //        write(mLevels[q].mUp.mSufVal, w, bv(mInput, i));

    //        if (i)
    //            mLevels[q].mUp.mPreBit(w) = c[i];
    //        else
    //            mLevels[q].mUp.mPreBit(w) = 0;

    //        if (i != n - 1)
    //            mLevels[q].mUp.mSufBit(w) = c[i + 1];
    //        else
    //            mLevels[q].mUp.mSufBit(w) = 0;

    //        //std::cout << s[i] << " " << mLevels[q].mUp.mPreBit[w] << " " << mLevels[q].mUp.mSufBit[w] << std::endl;
    //    }
    //    //std::cout << "\n";

    //    //for (u64 i = n; i < n16; ++i)
    //    //{
    //    //    auto q = i < n0 ? 0 : 1;
    //    //    auto w = i < n0 ? i : i - r;

    //    //    mLevels[q].mUp.mPreVal[w].resize(bitCount);
    //    //    mLevels[q].mUp.mSufVal[w].resize(bitCount);
    //    //    mLevels[q].mUp.mPreBit[w] = 0;
    //    //    mLevels[q].mUp.mSufBit[w] = 0;
    //    //}
    //}

}