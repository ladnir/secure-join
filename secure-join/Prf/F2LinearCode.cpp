#include "F2LinearCode.h"
#include <fstream>
#include <cryptoTools/Common/BitVector.h>
#include <cryptoTools/Common/Log.h>
#define BITSET
#include <bitset>
#include <iomanip>

#include <cryptoTools/Common/MatrixView.h>
#include <string>
#include <sstream>
#include <cassert>

namespace secJoin
{
    void F2LinearCode::init(oc::Matrix<u8> g)
    {
        //mCodewordBitSize = g.cols() * 8;
        mInputByteSize = oc::divCeil(g.rows(), 8);

        mG.resize(g.rows(), g.cols());
        mG.setZero();
        for (u64 i = 0; i < g.rows(); ++i)
        {
            memcpy(mG[i], g[i]);
        }

        generateSubcodes();
    }

    //void F2LinearCode::random(PRNG& prng, u64 inputSize, u64 outputSize)
    //{
    //    mG.resize(inputSize, codewordBlkSize());

    //    prng.get(mG.data(), mG.size());

    //    generateMod8Table();

    //}

    // For the i'th input byte, we will generate a subcode Ci.
    //  Ci will contain 26 codewords. We will enumerate these codewords
    // in the table Ti. Ti[x] will be the codeword.
    //
    // The final codeword is then compute as T1[x1] + T2[x2] + ... + Tm[xm]
    void F2LinearCode::generateSubcodes()
    {
        if (mG.rows() > sLinearCodePlainTextMaxSize * 8)
        {
            std::cout << "The encode function assumes that the plaintext word"
                " size is less than " << sLinearCodePlainTextMaxSize << ". If"
                " this is not the case, raise the limit" << std::endl;
            throw std::runtime_error(LOCATION);
        }

        // each byte of the input will be a subcode.
        // we round up the number of subcodes to 8
        // so that the encode function is simpler to 
        // implement.
        auto numsubcodes = oc::roundUpTo(oc::divCeil(mG.rows(), 8), 8);

        // we round the code length up to a power of 2. IDK why.
        auto codeSize = oc::divCeil(mG.cols(), sizeof(block));
        mSubcodes.resize(256 * numsubcodes, codeSize);
        mSubcodes.setZero();

        for (u64 i = 0; i < numsubcodes; ++i)
        {
            oc::MatrixView<u8> Ti((u8*)mSubcodes.data(i * 256), 256, mSubcodes.cols() * sizeof(block));
            oc::MatrixView<u8> Gi(mG.data(i * 8), 8, mG.cols());

            for (u64 j = 1; j < 256; ++j)
            {
                for (u64 k = 0; k < 8; ++k)
                {
                    auto jk = j & (1 << k);
                    if (jk)
                    {
                        for (u64 l = 0; l < Gi.cols(); ++l)
                            Ti(j, l) = Ti(j, l) ^ Gi(k, l);
                    }
                }
            }
        }
    }


//    void F2LinearCode::encode(
//        const span<block>& plaintxt,
//        const span<block>& codeword)
//    {
//#ifndef NDEBUG
//        if (static_cast<u64>(plaintxt.size()) != oc::divCeil(mG.rows(), 128) ||
//            static_cast<u64>(codeword.size()) < oc::divCeil(mG.cols(), sizeof(block)))
//            throw std::runtime_error(LOCATION);
//#endif
//        encode((u8*)plaintxt.data(), (u8*)codeword.data());
//    }
//
//
//    void F2LinearCode::encode(
//        const span<u8>& plaintxt,
//        const span<u8>& codeword)
//    {
//#ifndef NDEBUG
//        if (static_cast<u64>(plaintxt.size()) != oc::divCeil(mG.rows(), 8) ||
//            static_cast<u64>(codeword.size()) < mG.cols())
//            throw std::runtime_error(LOCATION);
//#endif
//        encode(plaintxt.data(), codeword.data());
//    }


}