#pragma once
// © 2022 Visa.
// Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the "Software"), to deal in the Software without restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions:
// 
// The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.
// 
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.


#include "secure-join/config.h"
#include "secure-join/Defines.h"
#include "secure-join/GMW/Circuit.h"
#include "secure-join/CorGenerator/CorGenerator.h"
#include "secure-join/Util/Matrix.h"

#include <cryptoTools/Network/Channel.h>
#include <cryptoTools/Common/Matrix.h>
#include <cryptoTools/Common/Timer.h>
#include <list>

namespace secJoin
{
    using block = oc::block;
    enum class OtExtType
    {
        IKNP,
        Silent,
        InsecureMock
    };
    class Gmw : public oc::TimerAdapter
    {
    public:

        struct Debug
        {
            bool mDebug = false;
            std::vector<block> mA, mB, mC, mD;
            std::list<std::array<std::vector<block>,2>> mU, mW;
            oc::Matrix<int> mVals;
            oc::Matrix<block> mWords;
        };


        Debug mO;

        // allow the circuit to be reordered into levels
        // based on their AND depth.
        BetaCircuit::LevelizeType mLevelize = BetaCircuit::LevelizeType::Reorder;

        // the number of SIMD circuits to eval.
        u64 mN = 0;
        // the number of 128 chunks to evaluate
        u64 mN128 = 0;

        // when setting the wires memory, this records how many remain unmapped.
        u64 mRemainingMappings = 0;

        u64 mRole = -1;

        OtExtType mOtExtType;
        
        // points to the i'th wire memory.
        std::vector<block*> mWords;

        // internal wire memory
        std::vector<oc::Matrix<block>> mMem;

        // total number of rounds.
        u64 mNumRounds;

        // the circuit being evaluated
        BetaCircuit mCir;

        // the remaining gates to be evaluated.
        span<oc::BetaGate> mGates;

        // the index of the circuit to print if debugging is enabled.
        u64 mDebugPrintIdx = -1;

        // the current print statement if debugging is enabled.
        BetaCircuit::PrintIter mPrint;

        // the binary triples used in by the protocol.
        BinOleRequest mTriples;

        bool mHasPreprocessing = 0;

        bool mHasRequest = 0;

        macoro::task<> mPreproTask;

        // initialize this gmw to eval n copies of the circuit cir.
        // use correlations provided by ole.
        void init(
            u64 n,
            const BetaCircuit& cir);

        void init(
            u64 n,
            const BetaCircuit& cir,
            CorGenerator& gen)
        {
            init(n, cir);
            request(gen);
        }

        bool hasRequest()
        {
            return mHasRequest;
        }

        void request(CorGenerator& gen);


        bool hasPreprocessing() const
        {
            return mHasPreprocessing;
        }

        // set the i'th input. There should be mN rows of `input`, each row holding
        // mCur.mInput[i].size() bits (rounded up to 8 * sizeof(T)).
        template<typename T>
        void setInput(u64 i, oc::MatrixView<T> input)
        {
            static_assert(std::is_trivially_copyable<T>::value, "expecting trivial");
            oc::MatrixView<u8> ii((u8*)input.data(), input.rows(), input.cols() * sizeof(T));
            implSetInput(i, ii, sizeof(T));
        }
        
        void setInput(u64 i, BinMatrix input)
        {
            setInput<u8>(i, input.mData);
        }

        // Set the i'th input to zero. Useful when only one party has input i (in plaintext).
        void setZeroInput(u64 i);


        void mapInput(u64 i,  TBinMatrix& d)
        {
            if (mCir.mInputs.size() <= i)
                throw std::runtime_error("GMW mapInput for an index that does not exist. " LOCATION);
            if (mCir.mInputs[i].size() != d.bitsPerEntry())
                throw std::runtime_error("GMW mapInput called with incorrect number of wires. " LOCATION);
            if (mN != d.numEntries())
                throw std::runtime_error("GMW mapInput called with incorrect number of inputs. " LOCATION);
            if (d.bytesPerRow() % sizeof(block))
                throw std::runtime_error("the alignment of the data must be at least sizeof(block). " LOCATION);

            for (u64 j = 0; j < d.bitsPerEntry(); ++j)
                map(mCir.mInputs[i][j], (block*)d[j].data());
        }



        void mapOutput(u64 i, TBinMatrix& d)
        {
            if (mCir.mOutputs.size() <= i)
                throw std::runtime_error("GMW mapOutput for an index that does not exist. " LOCATION);
            if (mCir.mOutputs[i].size() != d.bitsPerEntry())
                throw std::runtime_error("GMW mapOutput called with incorrect number of wires. " LOCATION);
            if (mN != d.numEntries())
                throw std::runtime_error("GMW mapOutput called with incorrect number of inputs. " LOCATION);
            if (d.bytesPerRow() % sizeof(block))
                throw std::runtime_error("the alignment of the data must be at least sizeof(block). " LOCATION);

            for (u64 j = 0; j < d.bitsPerEntry(); ++j)
                map(mCir.mOutputs[i][j], (block*)d[j].data());
        }


        void map(u64 wire, block* data)
        {
            if (mWords[wire])
                throw RTE_LOC;
            if ((u64)data % sizeof(block))
                throw RTE_LOC;

            mWords[wire] = data;
            assert(mRemainingMappings);
            --mRemainingMappings;
        }

        void finalizeMapping()
        {
            for (u64 i = 0; i < mCir.mInputs.size(); ++i)
                for (u64 j = 0; j < mCir.mInputs[i].size(); ++j)
                    if (mWords[mCir.mInputs[i][j]] == nullptr)
                        throw std::runtime_error("GMW input wire not set. input: " + std::to_string(i) + "\n" + LOCATION);

            //for (u64 i = 0; i < mCir.mOutputs.size(); ++i)
            //    for (u64 j = 0; j < mCir.mOutputs[i].size(); ++j)
            //        if (mWords[mCir.mOutputs[i][j]] == nullptr)
            //            throw RTE_LOC;

            if (mRemainingMappings)
            {
                mMem.emplace_back();
                mMem.back().resize(mRemainingMappings, mN128, oc::AllocType::Uninitialized);
                for (u64 i = 0, j = 0; i < mCir.mWireCount; ++i)
                {
                    if (mWords[i] == nullptr)
                        map(i, mMem.back().data(j++));
                }

                assert(mRemainingMappings == 0);
            }
        }



        macoro::task<> preprocess();

        // run the gmw protocol.
        coproto::task<> run(CorGenerator& gen, coproto::Socket& chl, PRNG& prng);

        coproto::task<> run(coproto::Socket& chl);

        void getOutput(u64 i, BinMatrix& out)
        {
            if (i > mCir.mOutputs.size() || out.bitsPerEntry() != mCir.mOutputs[i].size())
                throw RTE_LOC;
            getOutput(i, out.mData);
        }

        // get the i'th output. There should be mN rows of `out`, each row holding
        // mCur.mOutput[i].size() bits (rounded up to 8 * sizeof(T)).
        template<typename T>
        void getOutput(u64 i, oc::MatrixView<T> out)
        {
            static_assert(std::is_trivially_copyable<T>::value, "expecting trivial");
            oc::MatrixView<u8> ii((u8*)out.data(), out.rows(), out.cols() * sizeof(T));
            implGetOutput(i, ii, sizeof(T));
        }


        void implSetInput(u64 i, oc::MatrixView<u8> input, u64 alignment);
        void implGetOutput(u64 i, oc::MatrixView<u8> out, u64 alignment);

        oc::MatrixView<u8> getInputView(u64 i);
        oc::MatrixView<u8> getOutputView(u64 i);
        oc::MatrixView<u8> getMemView(BetaBundle& wires);


        u64 numRounds()
        {
            return mNumRounds;
        }


        block* multSendP1(block* x, oc::GateType gt,
            block* a, block* sendIter);
        block* multSendP2(block* x, oc::GateType gt,
            block* c, block* sendIter);


        block* multRecvP1(block* x, block* z, oc::GateType gt,
            block* b, block* recvIter);
        block* multRecvP2(block* x,  block* z,
            block* c,
            block* d, 
            block* recvIter);


        block* multSend(block* x, block* y, oc::GateType gt,
            block* a,
            block* c,
            block* sendIter,
            u64 idx)
        {
            if (idx == 0)
                return multSendP1(x, y, gt, a, c, sendIter);
            else
                return multSendP2(x, y, a, c, sendIter);
        }
        block* multSendP1(block* x, block* y, oc::GateType gt,
            block* a,
            block* c,
            block* sendIter);
        block* multSendP2(block* x, block* y,
            block* a,
            block* c,
            block* sendIter);


        block* multRecv(block* x, block* y, block* z, oc::GateType gt,
            block* b,
            block* c,
            block* d,
            block* recvIter,
            u64 idx)
        {
            if (idx == 0)
                return multRecvP1(x, y, z, gt, b, c, d, recvIter);
            else
                return multRecvP2(x, y, z, b, c, d, recvIter);
        }

        block* multRecvP1(block* x, block* y, block* z, oc::GateType gt,
            block* b,
            block* c,
            block* d,
            block* recvIter);
        block* multRecvP2(block* x, block* y, block* z,
            block* b,
            block* c,
            block* d,
            block* recvIter);
    };
}