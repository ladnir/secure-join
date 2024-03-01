#include "secure-join/AggTree/AggTree.h"

#include "secure-join/AggTree/PlainAggTree.h"
#include "secure-join/AggTree/PerfectShuffle.h"
#include "cryptoTools/Common/TestCollection.h"
#include "coproto/Socket/LocalAsyncSock.h"
#include "secure-join/CorGenerator/CorGenerator.h"
#include "secure-join/GMW/Gmw.h"
#include "secure-join/Util/Util.h"
#include "AggTree_Tests.h"


//#include "helper.h"
//#include "AggTreeTests.h"
//#include "BinEval_Tests.h"
using namespace oc;
using namespace secJoin;
using secJoin::span;
void eval(BetaCircuit& cir,
    u64 numTrials,
    u64 numShares,
    bool mock,
    bool debug,
    u64 printIdx)
{
    u64 numInputs = cir.mInputs.size();
    u64 numOutputs = cir.mOutputs.size();

    std::vector<std::vector<BitVector>> inputs(numShares);
    std::vector<std::vector<BitVector>> outputs(numShares);
    std::vector<std::array<BinMatrix, 2>> sInputs(numInputs);
    PRNG prng(ZeroBlock);

    auto comm = coproto::LocalAsyncSocket::makePair();

    for (u64 t = 0; t < numTrials; ++t)
    {
        CorGenerator gen[2];
        Gmw bin[2];


        for (u64 i = 0; i < 2; ++i)
        {
            gen[i].init(comm[i].fork(), prng, i, 1<<18, mock);
            bin[i].init(numShares, cir, gen[i]);
        }

        for (u64 i = 0; i < numInputs; ++i)
        {

            Matrix<u8> in(numShares, oc::divCeil(cir.mInputs[i].size(), 8));

            for (u64 j = 0; j < numShares; ++j)
            {
                inputs[j].resize(numInputs);
                inputs[j][i].resize(cir.mInputs[i].size());
                inputs[j][i].randomize(prng);
                memcpy(in[j].data(), inputs[j][i].data(), inputs[j][i].sizeBytes());
            }

            share(in, cir.mInputs[i].size(), sInputs[i][0], sInputs[i][1], prng);

            for (u64 j = 0; j < 2; ++j)
            {
                bin[j].setInput<u8>(i, sInputs[i][j]);
            }
        }

        auto mR = macoro::sync_wait(macoro::when_all_ready(
            gen[0].start(),
            gen[1].start(),
            bin[0].run(comm[0]),
            bin[1].run(comm[1])
        ));

        std::get<0>(mR).result();
        std::get<1>(mR).result();

        for (u64 j = 0; j < numShares; ++j)
        {
            outputs[j].resize(numOutputs);
            for (u64 i = 0; i < numOutputs; ++i)
                outputs[j][i].resize(cir.mOutputs[i].size());

            cir.evaluate(inputs[j], outputs[j], false);
        }

        for (u64 i = 0; i < numOutputs; ++i)
        {
            std::array<BinMatrix, 2> sOut;
            for (u64 j = 0; j < 2; ++j)
            {
                sOut[j].resize(numShares, cir.mOutputs[i].size());
                bin[j].getOutput<u8>(i, sOut[j]);
            }

            auto out = reveal(sOut[0], sOut[1]);

            for (u64 j = 0; j < numShares; ++j)
            {
                BitVector oj((u8*)out[j].data(), cir.mOutputs[i].size());
                if (oj != outputs[j][i])
                {
                    std::cout << "exp " << outputs[j][i] << std::endl;
                    std::cout << "act " << oj << std::endl;
                    throw RTE_LOC;
                }
            }
        }
    }
}

void perfectShuffle_32_Test()
{
    u64 mN = 100;
    PRNG prng(ZeroBlock);

    for (u64 i = 0; i < mN; ++i)
    {
        std::array<u16, 2> x = prng.get();

        auto y = cPerfectShuffle(x[0], x[1]);

        BitVector X((u8*)&x, 32);
        BitVector Y((u8*)&y, 32);

        // abcd efgh ijkl mnop ABCD EFGH IJKL MNOP,
        // aAbB cCdD eEfF gGhH iIjJ kKlL mMnN oOpP
        for (u64 j = 0; j < 32; ++j)
            if (X[j] != Y[(j * 2) % 32 + (j * 2) / 32])
                throw RTE_LOC;


        if (x != cPerfectUnshuffle(y))
            throw RTE_LOC;

    }

}

void perfectShuffle_span_Test()
{
    u64 mN = 100, m = 132;
    PRNG prng(ZeroBlock);

    for (u64 i = 0; i < mN; ++i)
    {
        BitVector X[2];
        X[0].resize(m - m / 2); X[0].randomize(prng);
        X[1].resize(m / 2);	 X[1].randomize(prng);
        BitVector Y(m);

        BitVector XX[2];
        XX[0].resize(X[0].size());
        XX[1].resize(X[1].size());
        cPerfectShuffle(X[0].getSpan<u8>(), X[1].getSpan<u8>(), Y.getSpan<u8>());

        // X0 = abcd efgh ijkl mnop 
        // X1 = ABCD EFGH IJKL MNOP,
        // Y  = aAbB cCdD eEfF gGhH iIjJ kKlL mMnN oOpP
        for (u64 j = 0; j < m; ++j)
        {
            if (Y[j] != X[j & 1][j / 2])
                throw RTE_LOC;
        }

        cPerfectUnshuffle(Y.getSpan<u8>(), XX[0].getSpan<u8>(), XX[1].getSpan<u8>());

        if (XX[0] != X[0])
            throw RTE_LOC;
        if (XX[1] != X[1])
            throw RTE_LOC;
    }
}

void perfectShuffle_128_Test()
{
#ifdef ENABLE_SSE
    u64 mN = 100;
    PRNG prng(ZeroBlock);

    u64 m = 128;
    for (u64 i = 0; i < mN; ++i)
    {
        std::array<u64, 2> x = prng.get();

        auto y = ssePerfectShuffle(x[0], x[1]);

        BitVector X((u8*)&x, m);
        BitVector Y((u8*)&y, m);

        // abcd efgh ijkl mnop ABCD EFGH IJKL MNOP,
        // aAbB cCdD eEfF gGhH iIjJ kKlL mMnN oOpP
        for (u64 j = 0; j < m; ++j)
            if (X[j] != Y[(j * 2) % m + (j * 2) / m])
                throw RTE_LOC;


        if (x != ssePerfectUnshuffle(y))
            throw RTE_LOC;

    }
#endif
}

void perfectShuffle_1024_Test()
{
#ifdef ENABLE_SSE

    u64 mN = 100;
    PRNG prng(ZeroBlock);

    u64 m = 1024;
    for (u64 i = 0; i < mN; ++i)
    {
        std::array<std::array<oc::block, 4>, 2> x = prng.get(), x2;
        std::array<oc::block, 8> y;
        ssePerfectShuffle(x[0].data(), x[1].data(), y.data());

        BitVector X((u8*)&x, m);
        BitVector Y((u8*)&y, m);

        // abcd efgh ijkl mnop ABCD EFGH IJKL MNOP,
        // aAbB cCdD eEfF gGhH iIjJ kKlL mMnN oOpP
        for (u64 j = 0; j < m; ++j)
            if (X[j] != Y[(j * 2) % m + (j * 2) / m])
                throw RTE_LOC;

        ssePerfectUnshuffle(y.data(), x2[0].data(), x2[1].data());
        if (x != x2)
            throw RTE_LOC;
    }
#endif
}


void perfectShuffle_sseSpan_Test()
{
#ifdef ENABLE_SSE
    u64 mN = 40, m = 13203;
    PRNG prng(ZeroBlock);

    for (u64 i = 0; i < mN; ++i)
    {
        BitVector X[2];
        X[0].resize(m - m / 2); X[0].randomize(prng);
        X[1].resize(m / 2);	 X[1].randomize(prng);
        BitVector Y(m);

        BitVector XX[2];
        XX[0].resize(X[0].size());
        XX[1].resize(X[1].size());
        ssePerfectShuffle(X[0].getSpan<u8>(), X[1].getSpan<u8>(), Y.getSpan<u8>());

        // X0 = abcd efgh ijkl mnop 
        // X1 = ABCD EFGH IJKL MNOP,
        // Y  = aAbB cCdD eEfF gGhH iIjJ kKlL mMnN oOpP
        for (u64 j = 0; j < m; ++j)
        {
            if (Y[j] != X[j & 1][j / 2])
                throw RTE_LOC;
        }

        ssePerfectUnshuffle(Y.getSpan<u8>(), XX[0].getSpan<u8>(), XX[1].getSpan<u8>());

        if (XX[0] != X[0])
        {
            std::cout << X[0] << std::endl;
            std::cout << XX[0] << std::endl;
            std::cout << (X[0] ^ XX[0]) << std::endl;
            throw RTE_LOC;
        }
        if (XX[1] != X[1])
            throw RTE_LOC;
    }
#endif
}


//namespace
//{
//    bool operator!=(oc::span<i64> l, oc::span<i64> mR)
//    {
//        return l.size() != mR.size() ||
//            std::memcmp(l.data(), mR.data(), l.size()) != 0;
//    }
//
//    //std::string hex(oc::span<i64> d)
//    //{
//    //    std::stringstream ss;
//    //    for (auto dd : d)
//    //        ss << std::hex << std::setw(2 * sizeof(i64)) << std::setfill('0') << dd;
//    //    return ss.str();
//    //}
//}

using Level = AggTree::Level;
using SplitLevel = AggTree::SplitLevel;


void AggTree_plain_Test()
{
    PTree tree;
    u64 mN = 421;

    auto op = [](
        const oc::BitVector& left,
        const oc::BitVector& right)
    {
        auto mR = left;
        mR.getSpan<u32>()[0] += right.getSpan<u32>()[0];
        return mR;
    };

    std::vector<BitVector> s(mN), pre(mN), suf(mN);
    BitVector c(mN);

    PRNG prng(ZeroBlock);
    c.randomize(prng);
    c[0] = 0;

    for (u64 i = 0; i < mN; ++i)
    {
        s[i].resize(32);
        s[i].getSpan<u8>()[0] = prng.get();

        pre[i] = s[i];
        if (c[i])
        {
            pre[i] = op(pre[i - 1], s[i]);
        }

        //std::cout << i << " " << c[i] << " " << s[i].getSpan<u32>()[0] << " => " << pre[i].getSpan<u32>()[0] << std::endl;
    }

    tree.init(s, c, op);

    for (u64 i = 0; i < mN; ++i)
    {
        if (tree.mPre[i] != pre[i])
        {
            //std::cout << tree.mPre[i].getSpan<u32>()[0] << std::endl;;
            throw RTE_LOC;
        }
    }
}

void AggTree_levelReveal_Test()
{

    PRNG prng(ZeroBlock);

    //auto op = [](
    //    const oc::BitVector& left,
    //    const oc::BitVector& right)
    //{
    //    return left ^ right;
    //};


    //u64 mN = oc::roundUpTo(361, 16);
    for (u64 mN : { 8ull, 256ull, 361ull, 24223ull })
    {
        u64 m = 11;

        {
            BinMatrix preBits(mN, 1);
            BinMatrix preVals(mN, m);
            BinMatrix sufBits(mN, 1);
            BinMatrix sufVals(mN, m);

            BinMatrix preBitsEven(oc::divCeil(mN, 2), 1);
            BinMatrix preValsEven(oc::divCeil(mN, 2), m);
            BinMatrix sufBitsEven(oc::divCeil(mN, 2), 1);
            BinMatrix sufValsEven(oc::divCeil(mN, 2), m);

            BinMatrix preBitsOdd(mN - preBitsEven.numEntries(), 1);
            BinMatrix preValsOdd(mN - preValsEven.numEntries(), m);
            BinMatrix sufBitsOdd(mN - sufBitsEven.numEntries(), 1);
            BinMatrix sufValsOdd(mN - sufValsEven.numEntries(), m);

            prng.get(preBits.data(), preBits.size()); preBits.trim();
            prng.get(preVals.data(), preVals.size()); preVals.trim();
            prng.get(sufBits.data(), sufBits.size()); sufBits.trim();
            prng.get(sufVals.data(), sufVals.size()); sufVals.trim();

            for (u64 i = 0; i < mN; i += 2)
            {
                preBitsEven(i / 2) = preBits(i);
                sufBitsEven(i / 2) = sufBits(i);
                if (i + 1 != mN)
                {
                    preBitsOdd(i / 2) = preBits(i + 1);
                    sufBitsOdd(i / 2) = sufBits(i + 1);
                }

                for (u64 j = 0; j < preVals.bytesPerEntry(); ++j)
                {
                    preValsEven(i / 2, j) = preVals(i, j);
                    sufValsEven(i / 2, j) = sufVals(i, j);
                    if (i + 1 != mN)
                    {
                        preValsOdd(i / 2, j) = preVals(i + 1, j);
                        sufValsOdd(i / 2, j) = sufVals(i + 1, j);
                    }
                }
            }

            auto preBitsEvenShare = share(preBitsEven, prng);
            auto preValsEvenShare = share(preValsEven, prng);
            auto sufBitsEvenShare = share(sufBitsEven, prng);
            auto sufValsEvenShare = share(sufValsEven, prng);

            auto preBitsOddShare = share(preBitsOdd, prng);
            auto preValsOddShare = share(preValsOdd, prng);
            auto sufBitsOddShare = share(sufBitsOdd, prng);
            auto sufValsOddShare = share(sufValsOdd, prng);

            std::array<SplitLevel, 2> tvs;

            for (u64 p = 0; p < 2; ++p)
            {
                tvs[p][0].mPreBit = preBitsEvenShare[p].transpose();
                tvs[p][0].mPreVal = preValsEvenShare[p].transpose();
                tvs[p][0].mSufBit = sufBitsEvenShare[p].transpose();
                tvs[p][0].mSufVal = sufValsEvenShare[p].transpose();
                tvs[p][1].mPreBit = preBitsOddShare[p].transpose();
                tvs[p][1].mPreVal = preValsOddShare[p].transpose();
                tvs[p][1].mSufBit = sufBitsOddShare[p].transpose();
                tvs[p][1].mSufVal = sufValsOddShare[p].transpose();
            }

            PLevel leaves;
            PLevel even;
            even.reveal(tvs[0][0], tvs[1][0]);
            leaves.reveal(tvs[0], tvs[1]);


            for (u64 i = 0; i < mN; ++i)
            {
                if (leaves.mPreBit[i] != preBits(i))
                    throw RTE_LOC;
                if (leaves.mSufBit[i] != sufBits(i))
                    throw RTE_LOC;

                if (i < preValsEven.numEntries())
                {
                    for (u64 j = 0; j < preVals.bytesPerEntry(); ++j)
                    {
                        if (even.mPreVal[i].getSpan<u8>()[j] != preValsEven(i, j))
                        {
                            std::cout << "exp " << oc::BitVector(&preValsEven(i, 0), m) << std::endl;
                            std::cout << "act " << even.mPreVal[i] << std::endl;
                            throw RTE_LOC;
                        }
                        if (even.mSufVal[i].getSpan<u8>()[j] != sufValsEven(i, j))
                            throw RTE_LOC;
                    }
                }

                for (u64 j = 0; j < preVals.bytesPerEntry(); ++j)
                {
                    if (leaves.mPreVal[i].getSpan<u8>()[j] != preVals(i, j))
                    {
                        std::cout << "exp " << oc::BitVector(&preVals(i, 0), m) << std::endl;
                        std::cout << "act " << leaves.mPreVal[i] << std::endl;
                        throw RTE_LOC;
                    }
                    if (leaves.mSufVal[i].getSpan<u8>()[j] != sufVals(i, j))
                        throw RTE_LOC;
                }
            }

        }
    }
}


//void AggTree_toPackedBin_Test()
//{
//
//    PRNG prng(ZeroBlock);
//
//    //auto op = [](
//    //    const oc::BitVector& left,
//    //    const oc::BitVector& right)
//    //{
//    //    return left ^ right;
//    //};
//
//
//    //u64 mN = oc::roundUpTo(361, 16);
//    for (u64 mN : { 8ull, 256ull, 361ull, 24223ull })
//    {
//        u64 startIdx = mN / 2;
//        auto numRows = mN;
//        u64 m = 11;
//        AggTree tree;
//
//        BinMatrix in(mN + startIdx, m);
//        TBinMatrix dst(mN, m);
//
//        prng.get(in.data(), in.size());
//        in.trim();
//
//        tree.toPackedBin(in, dst, startIdx, numRows);
//
//        auto act = dst.transpose();
//
//        for (u64 i = 0; i < mN; ++i)
//        {
//
//            for (u64 j = 0; j < in.bytesPerEntry(); ++j)
//            {
//                if (act(i, j) != in(i + startIdx, j))
//                    throw RTE_LOC;
//            }
//        }
//    }
//}

BinMatrix perfectShuffle(const BinMatrix& x0, const BinMatrix& x1)
{
    if (x0.numEntries() != x1.numEntries() &&
        x0.numEntries() != (x1.numEntries() + 1))
        throw RTE_LOC;
    if (x0.bitsPerEntry() != x1.bitsPerEntry())
        throw RTE_LOC;

    BinMatrix mR(x0.numEntries() + x1.numEntries(), x0.bitsPerEntry());
    for (u64 i = 0; i < x0.numEntries(); ++i)
        memcpy(mR.data(i * 2), x0.data(i), mR.bytesPerEntry());
    for (u64 i = 0; i < x1.numEntries(); ++i)
        memcpy(mR.data(i * 2 + 1), x1.data(i), mR.bytesPerEntry());
    return mR;
}


void AggTree_dup_pre_levelReveal_Test()
{

    PRNG prng(ZeroBlock);

    u64 m = 11;
    for (u64 mN : { 8ull, 256ull, 361ull, 24223ull })
    {

        BinMatrix preBits(mN, 1);
        BinMatrix preVals(mN, m);
        BinMatrix sufBits(mN, 1);
        BinMatrix sufVals(mN, m);

        BinMatrix preBitsEven(oc::divCeil(mN, 2), 1);
        BinMatrix preValsEven(oc::divCeil(mN, 2), m);
        BinMatrix sufBitsEven(oc::divCeil(mN, 2), 1);
        BinMatrix sufValsEven(oc::divCeil(mN, 2), m);

        BinMatrix preBitsOdd(mN - preBitsEven.numEntries(), 1);
        BinMatrix preValsOdd(mN - preValsEven.numEntries(), m);
        BinMatrix sufBitsOdd(mN - sufBitsEven.numEntries(), 1);
        BinMatrix sufValsOdd(mN - sufValsEven.numEntries(), m);

        prng.get(preBits.data(), preBits.size()); preBits.trim();
        prng.get(preVals.data(), preVals.size()); preVals.trim();
        prng.get(sufBits.data(), sufBits.size()); sufBits.trim();
        prng.get(sufVals.data(), sufVals.size()); sufVals.trim();

        for (u64 i = 0; i < mN; i += 2)
        {
            preBitsEven(i / 2) = preBits(i);
            sufBitsEven(i / 2) = sufBits(i);
            if (i + 1 != mN)
            {
                preBitsOdd(i / 2) = preBits(i + 1);
                sufBitsOdd(i / 2) = sufBits(i + 1);
            }

            for (u64 j = 0; j < preVals.bytesPerEntry(); ++j)
            {
                preValsEven(i / 2, j) = preVals(i, j);
                sufValsEven(i / 2, j) = sufVals(i, j);
                if (i + 1 != mN)
                {
                    preValsOdd(i / 2, j) = preVals(i + 1, j);
                    sufValsOdd(i / 2, j) = sufVals(i + 1, j);
                }
            }
        }

        auto preBitsEvenShare = share(preBitsEven, prng);
        auto preValsEvenShare = share(preValsEven, prng);
        auto sufBitsEvenShare = share(sufBitsEven, prng);
        auto sufValsEvenShare = share(sufValsEven, prng);

        auto preBitsOddShare = share(preBitsOdd, prng);
        auto preValsOddShare = share(preValsOdd, prng);
        auto sufBitsOddShare = share(sufBitsOdd, prng);
        auto sufValsOddShare = share(sufValsOdd, prng);

        std::array<SplitLevel, 2> tvs;

        for (u64 p = 0; p < 2; ++p)
        {
            tvs[p][0].mPreBit = preBitsEvenShare[p].transpose();
            tvs[p][0].mPreVal = preValsEvenShare[p].transpose();
            tvs[p][0].mSufBit = sufBitsEvenShare[p].transpose();
            tvs[p][0].mSufVal = sufValsEvenShare[p].transpose();
            tvs[p][1].mPreBit = preBitsOddShare[p].transpose();
            tvs[p][1].mPreVal = preValsOddShare[p].transpose();
            tvs[p][1].mSufBit = sufBitsOddShare[p].transpose();
            tvs[p][1].mSufVal = sufValsOddShare[p].transpose();
        }

        PLevel leaves;
        PLevel even;
        even.reveal(tvs[0][0], tvs[1][0]);
        leaves.reveal(tvs[0], tvs[1]);


        for (u64 i = 0; i < mN; ++i)
        {
            if (leaves.mPreBit[i] != preBits(i))
                throw RTE_LOC;
            if (leaves.mSufBit[i] != sufBits(i))
                throw RTE_LOC;

            if (i < preValsEven.numEntries())
            {
                for (u64 j = 0; j < preVals.bytesPerEntry(); ++j)
                {
                    if (even.mPreVal[i].getSpan<u8>()[j] != preValsEven(i, j))
                    {
                        std::cout << "exp " << oc::BitVector(&preValsEven(i, 0), m) << std::endl;
                        std::cout << "act " << even.mPreVal[i] << std::endl;
                        throw RTE_LOC;
                    }
                    if (even.mSufVal[i].getSpan<u8>()[j] != sufValsEven(i, j))
                        throw RTE_LOC;
                }
            }

            for (u64 j = 0; j < preVals.bytesPerEntry(); ++j)
            {
                if (leaves.mPreVal[i].getSpan<u8>()[j] != preVals(i, j))
                {
                    std::cout << "exp " << oc::BitVector(&preVals(i, 0), m) << std::endl;
                    std::cout << "act " << leaves.mPreVal[i] << std::endl;
                    throw RTE_LOC;
                }
                if (leaves.mSufVal[i].getSpan<u8>()[j] != sufVals(i, j))
                    throw RTE_LOC;
            }
        }
    }
}



void AggTree_dup_singleSetLeaves_Test()
{

    PRNG prng(ZeroBlock);


    u64 m = 11;
    u64 mLogn, mLogfn, mN16, mR, mN0;;
    for (u64 mN : { 8ull, 256ull, 361ull, 24223ull })
    {
        {
            mN16 = mN;
            mLogn = oc::log2ceil(mN);
            mLogfn = oc::log2floor(mN);
            if (mLogn != mLogfn)
            {
                mN16 = oc::roundUpTo(mN, 16);
                mLogn = oc::log2ceil(mN16);
                mLogfn = oc::log2floor(mN16);

            }

            mR = mN16 - (1ull << mLogfn);
            mN0 = mR ? 2 * mR : mN16;
            //mN1 = mN16 - mN0;

        }

        for (auto type : { AggTree::Type::Prefix, AggTree::Type::Suffix,AggTree::Type::Full })
        {
            //auto nPow = 1ull << oc::log2ceil(mN);

            for (auto level : { 0,1 })
            {
                SplitLevel tvs;

                BinMatrix s(mN, m), expS;
                BinMatrix c(mN, 1), expPreC, expSufC;
                prng.get(s.data(), s.size());
                s.trim();
                for (u64 i = 1; i < mN; ++i)
                    c(i) = prng.getBit();
                //c(i) = 1;

                if (mLogn == mLogfn)
                {
                    if (level)
                        continue;

                    tvs.resize(mN, m, type);
                    tvs.setLeafVals(s, c, 0, 0);

                    expS = s;
                    expPreC = c;
                    expPreC.resize(mN, 1);
                    expSufC = c;
                    for (u64 i = 1; i < expSufC.size(); ++i)
                        expSufC(i - 1) = expSufC(i);
                    expSufC(expSufC.size() - 1) = 0;
                }
                else
                {
                    if (level == 0)
                    {
                        //std::cout << "exp clast " << (int)c(mN0) << " @ " << mN0 << std::endl;
                        tvs.resize(mN0, m, type);
                        tvs.setLeafVals(s, c, 0, 0);

                        expS = s; expS.resize(mN0, m);
                        expPreC = c; expPreC.resize(mN0, 1);
                        expSufC.resize(mN0, 1);
                        for (u64 i = 0; i < expSufC.size(); ++i)
                            expSufC(i) = c(i + 1);
                    }
                    else
                    {
                        auto nn = 1ull << mLogfn;
                        tvs.resize(nn, m, type);
                        tvs.setLeafVals(s, c, mN0, mR);

                        expS.resize(nn, m);
                        expPreC.resize(nn, 1);

                        memcpy<u8, u8>(expS.subMatrix(mR, mN - mN0), s.subMatrix(mN0));
                        memcpy<u8, u8>(expPreC.subMatrix(mR, mN - mN0), c.subMatrix(mN0));

                        expSufC = expPreC; expSufC.resize(nn, 1);
                        for (u64 i = mR + 1; i < expSufC.size(); ++i)
                            expSufC(i - 1) = expSufC(i);
                        expSufC(expSufC.size() - 1) = 0;
                    }
                }

                auto& even = tvs[0];
                auto& odd = tvs[1];

                auto preBit = perfectShuffle(even.mPreBit.transpose(), odd.mPreBit.transpose());
                auto sufBit = perfectShuffle(even.mSufBit.transpose(), odd.mSufBit.transpose());
                auto preVal = perfectShuffle(even.mPreVal.transpose(), odd.mPreVal.transpose());
                auto sufVal = perfectShuffle(even.mSufVal.transpose(), odd.mSufVal.transpose());

                bool failed = false;
                for (u64 i = 0; i < expS.numEntries(); ++i)
                {
                    if ((type & AggTree::Type::Prefix) &&
                        preBit(i) != expPreC(i))
                    {
                        std::cout << (int)preBit(i) << " != " << (int)expPreC(i) << std::endl;
                        throw RTE_LOC;
                    }

                    if ((type & AggTree::Type::Suffix))
                    {

                        //std::cout << "suf " << i << " " << (int)sufBit(i) << " != " << (int)expSufC(i) << std::endl;
                        if (sufBit(i) != expSufC(i))
                        {
                            failed = true;
                            throw RTE_LOC;
                        }
                    }

                    for (u64 j = 0; j < s.bytesPerEntry(); ++j)
                    {

                        if ((type & AggTree::Type::Prefix) &&
                            preVal(i, j) != expS(i, j))
                            throw RTE_LOC;
                        if ((type & AggTree::Type::Suffix) &&
                            sufVal(i, j) != expS(i, j))
                            throw RTE_LOC;
                    }
                }

                if (failed)
                    throw RTE_LOC;
            }
        }
    }
}

void AggTree_dup_setLeaves_Test()
{

    PRNG prng(ZeroBlock);

    auto op = [](
        const oc::BitVector& left,
        const oc::BitVector& right)
    {
        return left ^ right;
    };


    //u64 mN = oc::roundUpTo(361, 16);
    for (u64 mN : { 8ull, 256ull, 361ull, 24223ull })
    {
        u64 m = 11;

        PTree tree;


        tree.init(mN, m, prng, op);
        auto s = tree.shareVals(prng);
        auto c = tree.shareBits(prng);

        //for (u64 i = 0; i < mN; ++i)
        //    std::cout << "v " << i << " " << tree.mInput[i] << " " << (int)s[0](i,0) << " " << (int)s[1](i,0) << std::endl;

        for (auto type : { AggTree::Type::Prefix, AggTree::Type::Suffix,AggTree::Type::Full })
        {
            std::array<SplitLevel, 2> tvs[2];

            for (u64 p = 0; p < 2; ++p)
            {
                if (tree.mCompleteTree)
                {
                    tvs[p][0].resize(tree.mN16, m, type);
                    tvs[p][0].setLeafVals(s[p], c[p], 0, 0);
                }
                else
                {
                    tvs[p][0].resize(tree.mLevelSizes[0], m, type);
                    tvs[p][1].resize(tree.mLevelSizes[1], m, type);

                    tvs[p][0].setLeafVals(s[p], c[p], 0, 0);
                    tvs[p][1].setLeafVals(s[p], c[p], tree.mLevelSizes[0], tree.mR);
                }
            }

            PLevel leaves[2];

            if (tree.mCompleteTree)
            {
                leaves[0].reveal(tvs[0][0], tvs[1][0]);
            }
            else
            {

                leaves[0].reveal(tvs[0][0], tvs[1][0]);
                leaves[1].reveal(tvs[0][1], tvs[1][1]);
            }

            for (u64 i = 0; i < tree.mN16; ++i)
            {
                auto q = i < tree.mLevelSizes[0] ? 0 : 1;
                auto w = i < tree.mLevelSizes[0] ? i : i - tree.mR;

                if (type & AggTree::Type::Prefix)
                {

                    auto v0 = leaves[q].mPreVal[w];
                    auto b0 = leaves[q].mPreBit[w];

                    auto expV = tree.mLevels[q].mUp.mPreVal[w];
                    auto expB = tree.mLevels[q].mUp.mPreBit[w];
                    if (expV != v0)
                    {
                        std::cout << "\nexp " << (expV) << std::endl;
                        std::cout << "act " << (v0) << std::endl;
                        throw RTE_LOC;
                    }

                    if (expB != (b0))
                    {
                        std::cout << "\nexp " << expB << std::endl;
                        std::cout << "act " << b0 << std::endl;
                        throw RTE_LOC;
                    }
                }

                if (type & AggTree::Type::Suffix)
                {
                    auto v0 = leaves[q].mSufVal[w];
                    auto b0 = leaves[q].mSufBit[w];

                    auto expV = tree.mLevels[q].mUp.mSufVal[w];
                    auto expB = tree.mLevels[q].mUp.mSufBit[w];
                    if (expV != v0)
                    {
                        std::cout << "\nexp " << (expV) << std::endl;
                        std::cout << "act " << (v0) << std::endl;
                        throw RTE_LOC;
                    }

                    if (expB != (b0))
                    {
                        std::cout << "\nexp " << expB << std::endl;
                        std::cout << "act " << b0 << std::endl;
                        throw RTE_LOC;
                    }
                }
            }
        }
    }
}


void AggTree_dup_upstream_cir_Test(const oc::CLP& cmd)
{
    for (auto type : { AggTree::Type::Prefix, AggTree::Type::Suffix,AggTree::Type::Full })
    {
        u64 bitCount = 10;

        AggTree t0;
        auto op = [](
            oc::BetaCircuit& cir,
            const oc::BetaBundle& left,
            const oc::BetaBundle& right,
            oc::BetaBundle& out)
        {
            oc::BetaLibrary lib;
            oc::BetaBundle temp(left.size());
            cir.addTempWireBundle(temp);
            lib.add_build(cir, left, right, out, temp, oc::BetaLibrary::IntType::Unsigned, oc::BetaLibrary::Optimized::Depth);
            cir.addCopy(left, out);
        };
        auto cir = t0.upstreamCir(bitCount, type, op);

        cir.levelByAndDepth(BetaCircuit::LevelizeType::Reorder);

        eval(cir, 10, 10, true, ~0ull, cmd.getOr("mock", 1));
    }

}

void AggTree_xor_upstream_Test(const oc::CLP& cmd)
{
    for (auto type : { AggTree::Type::Prefix, AggTree::Type::Suffix,AggTree::Type::Full })
    {


        auto opp = [](
            const oc::BitVector& left,
            const oc::BitVector& right)
        {
            return left ^ right;
        };
        auto op = [](
            oc::BetaCircuit& cir,
            const oc::BetaBundle& left,
            const oc::BetaBundle& right,
            oc::BetaBundle& out)
        {
            for (u64 i = 0; i < left.size(); ++i)
                cir.addGate(left[i], right[i], oc::GateType::Xor, out[i]);
        };
        auto comm = coproto::LocalAsyncSocket::makePair();

        u64 mN = 311;
        u64 m = 11;

        PRNG prng(oc::ZeroBlock);
        AggTree t0, t1;
        PTree tree;
        tree.init(mN, m, prng, opp);

        Level root[2];

        //auto mN16 = tree.mN16;
        auto mLogn = tree.mLevelSizes.size()-1;
        //auto mLogfn = tree.mLogfn;
        auto s = tree.shareVals(prng);
        auto c = tree.shareBits(prng);

        std::array<std::vector<SplitLevel>, 2> tvs;
        tvs[0].resize(mLogn);
        tvs[1].resize(mLogn);

        CorGenerator g0, g1;

        g0.init(comm[0].fork(), prng, 0, 1<<18, cmd.getOr("mock", 1));
        g1.init(comm[1].fork(), prng, 1, 1<<18, cmd.getOr("mock", 1));


        t0.init(mN, m, type, op, g0);
        t1.init(mN, m, type, op, g1);

        macoro::sync_wait(macoro::when_all_ready(
            g0.start(),
            g1.start(),
            t0.upstream(s[0], c[0], comm[0], prng, root[0], tvs[0]),
            t1.upstream(s[1], c[1], comm[1], prng, root[1], tvs[1])
        ));

        if (tvs[0].size() != mLogn)
            throw RTE_LOC;

        std::vector<PLevel> levels(mLogn);
        for (u64 i = 0; i < levels.size(); ++i)
        {
            levels[i].reveal(tvs[0][i], tvs[1][i]);
        }
        levels.emplace_back();
        levels.back().reveal(root[0], root[1]);

        for (u64 j = 0; j < tree.mLevels.size(); ++j)
        {
            auto& exp = tree.mLevels[j].mUp;
            for (u64 i = 0; i < exp.size(); ++i)
            {
                if (type & AggTreeType::Prefix)
                {

                    if (levels[j].mPreVal[i] != exp.mPreVal[i])
                    {
                        std::cout << "\n i" << i << std::endl;
                        std::cout << "exp " << exp.mPreVal[i] << std::endl;
                        std::cout << "act " << levels[j].mPreVal[i] << std::endl;
                        throw RTE_LOC;
                    }
                    if (levels[j].mPreBit[i] != exp.mPreBit[i])
                        throw RTE_LOC;
                }

                if (type & AggTreeType::Suffix)
                {

                    if (levels[j].mSufVal[i] != exp.mSufVal[i])
                    {
                        std::cout << "\n i" << i << std::endl;
                        std::cout << "exp " << exp.mSufVal[i] << std::endl;
                        std::cout << "act " << levels[j].mSufVal[i] << std::endl;
                        throw RTE_LOC;
                    }
                    if (levels[j].mSufBit[i] != exp.mSufBit[i])
                        throw RTE_LOC;
                }
            }
        }
    }

}

namespace {

    void compare(oc::BetaCircuit& c0, oc::BetaCircuit& c1)
    {
        u64 numTrials = 10;
        using namespace oc;

        u64 numInputs = c0.mInputs.size();
        u64 numOutputs = c0.mOutputs.size();

        if (numInputs != c1.mInputs.size())
            throw std::runtime_error(LOCATION);
        if (numOutputs != c1.mOutputs.size())
            throw std::runtime_error(LOCATION);

        std::vector<BitVector> inputs(numInputs);
        std::vector<BitVector> output0(numOutputs), output1(numOutputs);
        PRNG prng(ZeroBlock);

        for (u64 t = 0; t < numTrials; ++t)
        {
            for (u64 i = 0; i < numInputs; ++i)
            {
                if (c0.mInputs[i].size() != c1.mInputs[i].size())
                    throw RTE_LOC;

                inputs[i].resize(c0.mInputs[i].size());
                inputs[i].randomize(prng);
            }
            for (u64 i = 0; i < numOutputs; ++i)
            {
                if (c0.mOutputs[i].size() != c1.mOutputs[i].size())
                    throw RTE_LOC;
                output0[i].resize(c0.mOutputs[i].size());
                output1[i].resize(c0.mOutputs[i].size());
            }

            c0.evaluate(inputs, output0, false);
            //std::cout << "\mN";
            c1.evaluate(inputs, output1, false);

            for (u64 i = 0; i < numOutputs; ++i)
            {
                if (output0[i] != output1[i])
                {
                    for (u64 j = 0; j < output0[i].size(); ++j)
                        std::cout << (j / 10);
                    std::cout << std::endl;
                    for (u64 j = 0; j < output0[i].size(); ++j)
                        std::cout << (j % 10);
                    std::cout << std::endl;
                    std::cout << output0[i] << std::endl;
                    std::cout << output1[i] << std::endl;
                    std::cout << (output0[i] ^ output1[i]) << std::endl;

                    throw RTE_LOC;
                }
                //for (u64 j = 0; j < numShares; ++j)
                //{
                //	BitVector oj((u8*)out[j].data(), cir.mOutputs[i].size());
                //	if (oj != outputs[j][i])
                //	{
                //		std::cout << "exp " << outputs[j][i] << std::endl;
                //		std::cout << "act " << oj << std::endl;
                //		throw RTE_LOC;
                //	}
                //}
            }
        }
    }

}

void AggTree_dup_pre_downstream_cir_Test(const oc::CLP& cmd)
{
    u64 bitCount = 1;

    AggTree t0;
    //t0.mDebug = true;
    auto op = [](
        oc::BetaCircuit& cir,
        const oc::BetaBundle& left,
        const oc::BetaBundle& right,
        oc::BetaBundle& out)
    {
        cir.addCopy(left, out);
    };
    auto cir = t0.downstreamCir(bitCount, op, AggTree::Type::Prefix);

    auto c1 = cir;
    cir.levelByAndDepth();
    compare(c1, cir);
    eval(cir, 10, 10, false, ~0ull, cmd.getOr("mock", 1));
}

// std::string hex(const oc::span<u8>& bb)
// {
//     std::stringstream ss;
//     for (u64 i = 0; i < bb.size(); ++i)
//         ss << std::hex << std::setw(2) << std::setfill('0') << int(bb[i]);
//     return ss.str();
// }

template<typename Op, typename OpCir>
void AggTree_xor_downstream_Test(Op op, OpCir opCir, u64 mN, bool mock)
{

    for (auto type : { AggTree::Type::Prefix, AggTree::Type::Suffix,AggTree::Type::Full })
    {
        auto com = coproto::LocalAsyncSocket::makePair();

        u64 m = 32;

        std::array<CorGenerator, 2> g;

        PRNG prng(ZeroBlock);
        g[0].init(com[0].fork(), prng, 0, 1<<18, mock);
        g[1].init(com[1].fork(), prng, 1, 1<<18, mock);

        AggTree t[2];

        PTree tree;
        auto format = [](const oc::BitVector& bv)
        {
            return hex(bv.getSpan<u8>());
        };
        tree.init(mN, m, prng, op, format);

        auto s = tree.shareVals(prng);
        auto c = tree.shareBits(prng);

        //std::cout << tree.print(AggTree::Type::Prefix) << std::endl;

        t[0].init(mN, m, type, opCir, g[0]);
        t[1].init(mN, m, type, opCir, g[1]);

        std::array<Level, 2> root, root2;
        std::array<SplitLevel, 2> preSuf;
        std::array<std::vector<SplitLevel>, 2> upLevels, dwLevels;
        auto mLogn = t[0].mDownGmw.size();
        upLevels[0].resize(mLogn);
        upLevels[1].resize(mLogn);
        dwLevels[0].resize(mLogn);
        dwLevels[1].resize(mLogn);

        //t[0].mDebug = true;
        //t[1].mDebug = true;


        auto start = macoro::make_eager(macoro::when_all_ready(
            g[0].start(),
            g[1].start()
        ));

        macoro::sync_wait(macoro::when_all_ready(
            t[0].upstream(s[0], c[0], com[0], prng, root[0], upLevels[0]),
            t[1].upstream(s[1], c[1], com[1], prng, root[1], upLevels[1])
        ));
        root2[0] = root[0];
        root2[1] = root[1];

        macoro::sync_wait(macoro::when_all_ready(
            t[0].downstream(s[0], root[0], upLevels[0], preSuf[0], com[0], prng, &dwLevels[0]),
            t[1].downstream(s[1], root[1], upLevels[1], preSuf[1], com[1], prng, &dwLevels[1])
        ));

        g[0].abort();
        g[1].abort();
        macoro::sync_wait(std::move(start));

        std::vector<PLevel> levels(mLogn);
        for (u64 i = 0; i < levels.size(); ++i)
        {
            levels[i].reveal(dwLevels[0][i], dwLevels[1][i]);
        }
        levels.emplace_back();
        levels.back().reveal(root2[0], root2[1]);

        //std::cout << tree.print(AggTree::Type::Prefix, format) << std::endl;


        for (u64 j = levels.size() - 2; j < levels.size(); --j)
        {
            auto& plain = tree.mLevels[j];

            for (u64 i = 0; i < plain.mDown.size(); ++i)
            {
                if (type & AggTreeType::Prefix)
                {

                    auto act = levels[j].mPreVal[i];
                    auto exp = plain.mDown.mPreVal[i];
                    if (exp != act)
                    {
                        std::cout << "\npre lvl " << j << std::endl;
                        std::cout << "i " << i << std::endl;
                        std::cout << "act " << format(act) << std::endl;
                        std::cout << "exp " << format(exp) << std::endl << std::endl;
                        // d1 = p0 ? op(v, v0) : v0;
                        // d0 = v;

                        auto p = i / 2;
                        auto l = i & ~1ull;
                        auto mR = i | 1;
                        std::cout << "pnt exp " << format(tree.mLevels[j + 1].mUp.mPreVal[p]) << " " << tree.mLevels[j + 1].mUp.mPreBit[p] << "  " << p << std::endl;
                        std::cout << "    act " << format(levels[j + 1].mPreVal[p]) << " " << levels[j + 1].mPreBit[p] << std::endl;
                        std::cout << "ch0 exp " << format(plain.mUp.mPreVal[l]) << " " << plain.mUp.mPreBit[l] << "  " << l << (l == i ? " <- " : "") << std::endl;
                        std::cout << "    act " << format(levels[j].mPreVal[l]) << " " << levels[j].mPreBit[l] << std::endl;
                        std::cout << "ch1 exp " << format(plain.mUp.mPreVal[mR]) << " " << plain.mUp.mPreBit[mR] << "  " << mR << (mR == i ? " <- " : "") << std::endl;
                        std::cout << "    act " << format(levels[j].mPreVal[mR]) << " " << levels[j].mPreBit[mR] << std::endl;


                        throw RTE_LOC;
                    }
                }

                if (type & AggTreeType::Suffix)
                {

                    auto act = levels[j].mSufVal[i];
                    auto exp = plain.mDown.mSufVal[i];
                    if (exp != act)
                    {
                        std::cout << "\nsuf lvl " << j << std::endl;
                        std::cout << "i " << i << std::endl;
                        std::cout << "act " << format(act) << std::endl;
                        std::cout << "exp " << format(exp) << std::endl << std::endl;
                        // d1 = p0 ? op(v, v0) : v0;
                        // d0 = v;

                        auto p = i / 2;
                        auto l = i & ~1ull;
                        auto mR = i | 1;
                        std::cout << "pnt exp " << format(tree.mLevels[j + 1].mUp.mSufVal[p]) << " " << tree.mLevels[j + 1].mUp.mSufBit[p] << "  " << p << std::endl;
                        std::cout << "    act " << format(levels[j + 1].mSufVal[p]) << " " << levels[j + 1].mSufBit[p] << std::endl;
                        std::cout << "ch0 exp " << format(plain.mUp.mSufVal[l]) << " " << plain.mUp.mSufBit[l] << "  " << l << (l == i ? " <- " : "") << std::endl;
                        std::cout << "    act " << format(levels[j].mSufVal[l]) << " " << levels[j].mSufBit[l] << std::endl;
                        std::cout << "ch1 exp " << format(plain.mUp.mSufVal[mR]) << " " << plain.mUp.mSufBit[mR] << "  " << mR << (mR == i ? " <- " : "") << std::endl;
                        std::cout << "    act " << format(levels[j].mSufVal[mR]) << " " << levels[j].mSufBit[mR] << std::endl;


                        throw RTE_LOC;
                    }
                }
            }
        }

        auto level00 = upLevels[0][0];
        auto level10 = upLevels[1][0];
        PLevel pre; pre.reveal(preSuf[0], preSuf[1]);
        PLevel val; val.reveal(level00, level10);

        for (u64 i = 0; i < mN; ++i)
        {
            auto q = i < tree.mLevelSizes[0] ? 0 : 1;
            auto w = i < tree.mLevelSizes[0] ? i : i - tree.mR;

            if (type & AggTreeType::Prefix)
            {

                {
                    auto exp = tree.mLevels[q].mDown.mPreVal[w];
                    auto act = pre.mPreVal[i];
                    if (act != exp)
                    {
                        std::cout << "i   " << i << std::endl;
                        std::cout << "exp " << exp << std::endl;
                        std::cout << "act " << act << std::endl;
                        std::cout << "    " << (act ^ exp) << std::endl;

                        throw RTE_LOC;
                    }
                }

                {
                    auto exp = tree.mLevels[q].mUp.mPreVal[w];
                    auto act = val.mPreVal[i];
                    if (act != exp)
                    {
                        std::cout << "i    " << i << std::endl;
                        std::cout << "exp  " << exp << std::endl;
                        std::cout << "act  " << act << std::endl;
                        std::cout << "     " << (act ^ exp) << std::endl;

                        throw RTE_LOC;
                    }
                }

                {
                    auto exp = tree.mLevels[q].mUp.mPreBit[w];
                    auto act = val.mPreBit[i];
                    if (act != exp)
                    {
                        std::cout << "i   " << i << std::endl;
                        std::cout << "exp " << exp << std::endl;
                        std::cout << "act " << act << std::endl;
                        std::cout << "    " << (act ^ exp) << std::endl;

                        throw RTE_LOC;
                    }
                }
            }


            if (type & AggTreeType::Suffix)
            {

                {
                    auto exp = tree.mLevels[q].mDown.mSufVal[w];
                    auto act = pre.mSufVal[i];
                    if (act != exp)
                    {
                        std::cout << "i   " << i << std::endl;
                        std::cout << "exp " << exp << std::endl;
                        std::cout << "act " << act << std::endl;
                        std::cout << "    " << (act ^ exp) << std::endl;

                        throw RTE_LOC;
                    }
                }

                {
                    auto exp = tree.mLevels[q].mUp.mSufVal[w];
                    auto act = val.mSufVal[i];
                    if (act != exp)
                    {
                        std::cout << "i    " << i << std::endl;
                        std::cout << "exp  " << exp << std::endl;
                        std::cout << "act  " << act << std::endl;
                        std::cout << "     " << (act ^ exp) << std::endl;

                        throw RTE_LOC;
                    }
                }

                {
                    auto exp = tree.mLevels[q].mUp.mSufBit[w];
                    auto act = val.mSufBit[i];
                    if (act != exp)
                    {
                        std::cout << "i   " << i << std::endl;
                        std::cout << "exp " << exp << std::endl;
                        std::cout << "act " << act << std::endl;
                        std::cout << "    " << (act ^ exp) << std::endl;

                        throw RTE_LOC;
                    }
                }
            }
        }
    }
}

void AggTree_dup_downstream_Test(const oc::CLP& cmd)
{
    auto opCir = [](
        oc::BetaCircuit& cir,
        const oc::BetaBundle& left,
        const oc::BetaBundle& right,
        oc::BetaBundle& out)
    {
        cir.addCopy(left, out);
    };

    auto op = [](
        const oc::BitVector& left,
        const oc::BitVector& right)
    {
        return left;
    };
    AggTree_xor_downstream_Test(op, opCir, cmd.getOr("n", 2343), cmd.getOr("mock", 1));
}


void AggTree_xor_full_downstream_Test(const oc::CLP& cmd)
{
    auto opCir = [](
        oc::BetaCircuit& cir,
        const oc::BetaBundle& left,
        const oc::BetaBundle& right,
        oc::BetaBundle& out)
    {
        for (u64 i = 0; i < left.size(); ++i)
            cir.addGate(left[i], right[i], oc::GateType::Xor, out[i]);
    };

    auto op = [](
        const oc::BitVector& left,
        const oc::BitVector& right)
    {
        return left ^ right;
    };
    AggTree_xor_downstream_Test(op, opCir, cmd.getOr("n", 2343), cmd.getOr("mock", 1));
}
void AggTree_xor_Partial_downstream_Test(const oc::CLP& cmd)
{
    auto opCir = [](
        oc::BetaCircuit& cir,
        const oc::BetaBundle& left,
        const oc::BetaBundle& right,
        oc::BetaBundle& out)
    {
        for (u64 i = 0; i < left.size(); ++i)
            cir.addGate(left[i], right[i], oc::GateType::Xor, out[i]);
    };

    auto op = [](
        const oc::BitVector& left,
        const oc::BitVector& right)
    {
        return left ^ right;
    };
    AggTree_xor_downstream_Test(op, opCir, cmd.getOr("n", 2343), cmd.getOr("mock", 1));
    AggTree_xor_downstream_Test(op, opCir, cmd.getOr("n", 2343), cmd.getOr("mock", 1));
}


template<typename Op, typename OpCir>
void AggTree_full_Test(Op op, OpCir opCir,u64 mN, bool mock)
{

    for (auto type : { AggTree::Type::Prefix, AggTree::Type::Suffix,AggTree::Type::Full })
    {
        auto com = coproto::LocalAsyncSocket::makePair();

        u64 m = 32;

        std::array<CorGenerator, 2> g;

        PRNG prng(oc::ZeroBlock);
        g[0].init(com[0].fork(), prng, 0, 1<<18, mock);
        g[1].init(com[1].fork(), prng, 1, 1<<18, mock);

        AggTree t[2];



        PTree tree;
        tree.init(mN, m, prng, op);

        auto s = tree.shareVals(prng);
        auto c = tree.shareBits(prng);
        BinMatrix d0(mN, m), d1(mN, m);

        t[0].init(mN, m, type, opCir, g[0]);
        t[1].init(mN, m, type, opCir, g[1]);

        macoro::sync_wait(macoro::when_all_ready(
            g[0].start(),
            g[1].start(),
            t[0].apply(s[0], c[0], com[0], prng, d0),
            t[1].apply(s[1], c[1], com[1], prng, d1)
        ));


        auto dd = reveal(d0, d1);
        for (u64 i = 0; i < mN; ++i)
        {
            BitVector exp;
            switch (type)
            {
            case secJoin::Prefix:
                exp = tree.mPre[i];
                break;
            case secJoin::Suffix:
                exp = tree.mSuf[i];
                break;
            case secJoin::Full:
                exp = tree.mFull[i];
                break;
            default:
                throw RTE_LOC;
                break;
            }

            auto act = BitVector((u8*)&dd(i, 0), m);
            if (act != exp)
            {
                std::cout << "\n" << i << std::endl;
                std::cout << "act " << act << std::endl;
                std::cout << "exp " << exp << std::endl;

                std::cout << "\n";
                //std::cout << "val " << tree.mInput[i] << " " << tree.mLevels[0].mDown.mPreVal[i] << " " << tree.mCtrl[i] << std::endl;
                //std::cout << "act " << tree.mInput[i] << " " << tree.mLevels[o].mDown.mPreVal[i] << " " << tree.mCtrl[i] << std::endl;
                throw RTE_LOC;
            }
        }
    }
}


void AggTree_dup_pre_full_Test(const oc::CLP& cmd)
{
    auto opCir = [](
        oc::BetaCircuit& cir,
        const oc::BetaBundle& left,
        const oc::BetaBundle& right,
        oc::BetaBundle& out)
    {
        cir.addCopy(left, out);
    };

    auto op = [](
        const oc::BitVector& left,
        const oc::BitVector& right)
    {
        return left;
    };

    AggTree_full_Test(op, opCir, cmd.getOr("n", 2343), cmd.getOr("mock", 1));
}


void AggTree_xor_pre_full_Test(const oc::CLP& cmd)
{
    auto opCir = [](
        oc::BetaCircuit& cir,
        const oc::BetaBundle& left,
        const oc::BetaBundle& right,
        oc::BetaBundle& out)
    {
        for (u64 i = 0; i < left.size(); ++i)
            cir.addGate(left[i], right[i], oc::GateType::Xor, out[i]);
    };

    auto op = [](
        const oc::BitVector& left,
        const oc::BitVector& right)
    {
        return left ^ right;
    };

    AggTree_full_Test(op, opCir, cmd.getOr("n", 2343), cmd.getOr("mock",1));
}
