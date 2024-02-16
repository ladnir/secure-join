#pragma once

#include "secure-join/Perm/ComposedPerm.h"
#include "secure-join/Perm/AltModComposedPerm.h"
#include "secure-join/Perm/AdditivePerm.h"
#include "secure-join/Defines.h"
#include "secure-join/CorGenerator/CorGenerator.h"
#include "secure-join/Sort/BitInjection.h"
#include "secure-join/GMW/Gmw.h"

#include "cryptoTools/Circuit/BetaLibrary.h"
#include "cryptoTools/Common/Log.h"
#include "coproto/Socket/Socket.h"
#include "cryptoTools/Common/Timer.h"

namespace secJoin
{

    inline bool operator>(const oc::BitVector& v0, const oc::BitVector& v1)
    {
        if (v0.size() != v1.size())
            throw RTE_LOC;
        for (u64 i = v0.size() - 1; i < v0.size(); --i)
        {
            if (v0[i] > v1[i])
                return true;
            if (v1[i] > v0[i])
                return false;
        }

        return false;
    }

    class RadixSort : public oc::TimerAdapter
    {
    public:
        // the number of preprocessing's ahead of the main phase
        u64 mPreProLead = 4;

        // run debugging check (insecure).
        bool mDebug = false;

        // mock the sorting protocol (insecure).
        bool mInsecureMock = false;

        // The number of item we are sorting.
        u64 mSize = 0;

        // The bit count of the items we are sorting.
        u64 mBitCount = 0;

        // The bit step size of the genBitPerm protocol.
        u64 mL = 2;

        // A zero one flag denoting the party index.
        u64 mRole = -1;

        // The current size of the one hot circuit.
        u64 mIndexToOneHotCircuitBitCount = 0;

        // A circuit that maps an index x into a unit vector x s.t. v_x=1.
        oc::BetaCircuit mIndexToOneHotCircuit;

        // A circuit that takes an input two values x0,x1 and return y=x0+x1.
        oc::BetaCircuit mArith2BinCir;

        // has the pre perm started
        bool mPrePermStarted = false;

        // This will hold the correlated randomness for each round of the radix sort 
        // protocol. We will preprocess the correlated randomness on demand so its ready
        // just in time.
        struct Round
        {
            // a flag that tells the round when it can start preprocessing.
            std::unique_ptr<macoro::async_manual_reset_event> mStartPrepro, mPrePermReady;

            // role of this party
            u64 mRole = -1;

            // index of the round.
            u64 mIdx = -1;

            // perm size
            u64 mSize = 0;

            // number of bytes that the perm should be
            u64 mPermBytes = 0;

            // 1 << mL, the size of the "one-hot" key
            u64 mExpandedBitSize = 0;

            //
            //bool mPrecomputePerm = false;
            //bool mPreproDone = false;

            // the sorting permutation for this round.
            // We will preprocess this as a random perm
            // and then derandomize it to the sorting perm.
            ComposedPerm mPerm;

            // the protocol to generate the mPrePerm.
            AltModComposedPerm mPrePermGen;

            // the bit injection protocol for converting exanded
            // sort value one-hot count u32-vector.
            BitInject mBitInject;

            // the Gmw protocol that converts an index i
            // onto a one-hot vector 00010000
            // where the 1 is at index i.
            Gmw mIndexToOneHotGmw;
            
            // This will convert the u32 version of
            // the permutation (computed by the sum
            // of one-hot vectors) into a binary sharing.
            Gmw mArithToBinGmw;

            // OT used to multiply a binary sharing by an aithmetic share
            OtRecvRequest mHadamardSumRecvOts;

            // OT used to multiply a binary sharing by an aithmetic share
            OtSendRequest mHadamardSumSendOts;
            
            // run debugging checks.
            bool mDebug = false;

            bool mPerPermStarted = false;

            // the socket that the random perm will be generated on.
            //coproto::Socket mChl;

            // the PRNG that the random perm will be generated with.
            //PRNG mPrng;

            Round() = default;
            Round(const Round&) = delete;
            Round(Round&&) = default;
            Round&operator=(Round&&) = default;

            // init the round.
            void init(
                u64 idx,
                u64 role, 
                u64 size,
                u64 permutationByteSize,
                u64 expandedBitsSize,
                CorGenerator& cor,
                oc::BetaCircuit& mIndexToOneHotCircuit,
                oc::BetaCircuit& mArith2BinCir,
                bool debug)
            {
                mIdx = idx;
                mRole = role;
                mSize = size;
                mPermBytes = permutationByteSize;
                mExpandedBitSize = expandedBitsSize;

                if(permutationByteSize)
                    mPrePermGen.init(role, size, permutationByteSize, cor);
                mBitInject.init(1, expandedBitsSize, cor);
                mIndexToOneHotGmw.init(size, mIndexToOneHotCircuit, cor);
                mArithToBinGmw.init(size, mArith2BinCir, cor);
                mStartPrepro = std::make_unique<macoro::async_manual_reset_event>();
                mPrePermReady = std::make_unique<macoro::async_manual_reset_event>();
                mDebug = debug;

                if (mRole)
                {
                    mHadamardSumRecvOts = cor.recvOtRequest(mExpandedBitSize);
                    mHadamardSumSendOts = cor.sendOtRequest(mExpandedBitSize);
                }
                else
                {
                    mHadamardSumSendOts = cor.sendOtRequest(mExpandedBitSize);
                    mHadamardSumRecvOts = cor.recvOtRequest(mExpandedBitSize);
                }
            }

            void preprocess();

            macoro::task<> preGenPerm(coproto::Socket sock, PRNG prng);

        };

        // The correlated randomness for each round.
        std::vector<Round> mRounds;

        // Sets various parameters for the protocol. role should be 0,1. n is the list size, bitCount is
        // the number of bits per element. bytesPerElem is an optional parameter
        // that will initialize the output permutation with enough correlated
        // randomness to permute elements with bytesPerElem bytes.
        void init(
            u64 role,
            u64 n,
            u64 bitCount,
            CorGenerator& gen);

        // Once init it called, this will request the required correlated randomness
        // from CorGenerator. To start the generation of the randomness, call preprocess().
        //void request();

        void preprocess();

        // Start the generation of the requested correlated randomness.
        macoro::task<> genPrePerm(
            coproto::Socket& comm,
            PRNG& prng);

        using Matrix32 = oc::Matrix<u32>;

        RadixSort() = default;
        RadixSort(RadixSort&&) = default;


        macoro::task<> hadamardSumSend(
            const Matrix32& s,
            const BinMatrix& f,
            std::vector<u32>& shares,
            OtRecvRequest& req,
            coproto::Socket& comm);

        macoro::task<> hadamardSumRecv(
            const Matrix32& s,
            const BinMatrix& f,
            std::vector<u32>& shares,
            OtSendRequest& req,
            coproto::Socket& comm);

        // compute dst = sum_i f.col(i) * s.col(i) where * 
        // is the hadamard (component-wise) product. 
        macoro::task<> hadamardSum(
            Round& round,
            BinMatrix& f,
            Matrix32& s,
            AdditivePerm& dst,
            coproto::Socket& comm);

        // from each row, we generate a series of sharing flag bits
        // f.col(0) ,..., f.col(n) where f.col(i) is one if k=i.
        // Computes the same function as genValMask but is more efficient
        // due to the use a binary secret sharing.
        macoro::task<> genValMasks2(
            Round& round,
            u64 bitCount,
            const BinMatrix& k,
            Matrix32& f,
            BinMatrix& fBin,
            coproto::Socket& comm);


        // Generate a permutation dst which will be the inverse of the
        // permutation that permutes the keys k into sorted order. 
        macoro::task<> genBitPerm(
            Round& round,
            u64 keyBitCount,
            const BinMatrix& k,
            AdditivePerm& dst,
            coproto::Socket& comm);


        // get 'size' columns of k starting at column index 'begin'
        // Assumes 'size <= 8'. 
        BinMatrix extract(u64 begin, u64 size, const BinMatrix& k);


        // generate the (inverse) permutation that sorts the keys k.
        macoro::task<> genPerm(
            const BinMatrix& k,
            AdditivePerm& dst,
            coproto::Socket& comm,
            PRNG& prng);

        //// sort `src` based on the key `k`. The sorted values are written to `dst`
        //// and the sorting (inverse) permutation is written to `dstPerm`.
        //BinMatrix sort(
        //	u64 keyBitCount,
        //	const BinMatrix& k,
        //	const BinMatrix& src,
        //	CorGenerator& gen,
        //	coproto::Socket& comm);

        //// sort `src` based on the key `k`. The sorted values are written to `dst`
        //// and the sorting (inverse) permutation is written to `dstPerm`.
        //void sort(
        //	const BinMatrix& k,
        //	const BinMatrix& src,
        //	BinMatrix& dst,
        //	ComposedPerm& dstPerm,
        //	CorGenerator& gen,
        //	coproto::Socket& comm);

        // this circuit takes as input a index i\in {0,1}^L and outputs
        // a binary vector o\in {0,1}^{2^L} where is one at index i.
        void initIndexToOneHotCircuit(u64 L);


        void initArith2BinCircuit(u64 n);

        // compute a running sum. replace each element f(i,j) with the sum all previous 
        // columns f(*,1),...,f(*,j-1) plus the elements of f(0,j)+....+f(i-1,j).
        static void aggregateSum(const Matrix32& f, Matrix32& s, u64 partyIdx);

        macoro::task<> mockSort(
            const BinMatrix& k,
            AdditivePerm& dst,
            coproto::Socket& comm);

        macoro::task<> checkHadamardSum(
            BinMatrix& f,
            Matrix32& s,
            span<u32> dst,
            coproto::Socket& comm,
            bool additive);

        macoro::task<> checkGenValMasks(
            u64 bitCount,
            const BinMatrix& k,
            BinMatrix& f,
            coproto::Socket& comm,
            bool check);

        macoro::task<> checkGenValMasks(
            u64 L,
            const BinMatrix& k,
            Matrix32& f,
            coproto::Socket& comm);

        macoro::task<> checkAggregateSum(
            const Matrix32& f0,
            Matrix32& s0,
            coproto::Socket& comm
        );

        macoro::task<std::vector<Perm>> debugGenPerm(
            const BinMatrix& k,
            coproto::Socket& comm);
    };


    bool lessThan(span<const u8> l, span<const u8> r);
    Perm sort(const BinMatrix& x);

}