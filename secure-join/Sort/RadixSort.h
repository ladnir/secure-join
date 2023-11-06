#pragma once

#include "secure-join/Perm/ComposedPerm.h"
#include "secure-join/Perm/AdditivePerm.h"
#include "secure-join/Defines.h"
#include "secure-join/CorGenerator/CorGenerator.h"
#include "secure-join/Sort/BitInjection.h"

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

        // has request been called.
        bool mHasRequest = false;

        // has preprocess been called.
        bool mHasPrepro = false;

        // The number of item we are sorting.
        u64 mSize = 0;

        // The bit count of the items we are sorting.
        u64 mBitCount = 0;

        // The requested amount of preprocessing that the output permutation should have.
        u64 mBytesPerElem = 0;

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

        // This will hold the correlated randomness for each round of the radix sort 
        // protocol. We will preprocess the correlated randomness on demand so its ready
        // just in time.
        struct Round
        {
            Round() = default;
            Round(const Round&) = delete;
            Round(Round&&) = default;
            Round&operator=(Round&&) = default;



            void init(
                u64 idx,
                u64 role, u64 size,
                u64 permutationByteSize,
                bool AltModKeyGen,
                u64 expandedBitsSize,
                oc::BetaCircuit& mIndexToOneHotCircuit,
                oc::BetaCircuit& mArith2BinCir,
                bool debug)
            {
                mIdx = idx;
                mRole = role;
                mPermBytes = permutationByteSize;
                mExpandedBitSize = expandedBitsSize;
                mPerm.init2(role, size, permutationByteSize, AltModKeyGen);
                mBitInject.init(1, expandedBitsSize);
                mIndexToOneHotGmw.init(size, mIndexToOneHotCircuit);
                mArithToBinGmw.init(size, mArith2BinCir);
                mReady = std::make_unique<macoro::async_manual_reset_event>();
                mDebug = debug;
            }

            void request(CorGenerator& gen)
            {
                if (mPermBytes)
                    mPerm.request(gen);

                mBitInject.request(gen);
                mIndexToOneHotGmw.request(gen);
                mArithToBinGmw.request(gen);

                if (mRole)
                {
                    mHadamardSumRecvOts = gen.recvOtRequest(mExpandedBitSize);
                    mHadamardSumSendOts = gen.sendOtRequest(mExpandedBitSize);
                }
                else
                {
                    mHadamardSumSendOts = gen.sendOtRequest(mExpandedBitSize);
                    mHadamardSumRecvOts = gen.recvOtRequest(mExpandedBitSize);
                }
            }

            macoro::task<> preprocess();


            std::unique_ptr<macoro::async_manual_reset_event> mReady;
            u64 mRole = -1;
            u64 mIdx = -1;
            u64 mPermBytes = 0;
            u64 mExpandedBitSize = 0;
            bool mPreproDone = false;
            AdditivePerm mPerm;
            BitInject mBitInject;
            Gmw mIndexToOneHotGmw, mArithToBinGmw;
            OtRecvRequest mHadamardSumRecvOts;
            OtSendRequest mHadamardSumSendOts;
            bool mDebug = false;
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
            u64 bytesPerElem = 0);

        // Once init it called, this will request the required correlated randomness
        // from CorGenerator. To start the generation of the randomness, call preprocess().
        void request(CorGenerator& gen);

        // Start the generation of the requested correlated randomness.
        macoro::task<> preprocess(
            coproto::Socket& comm,
            PRNG& prng);

        using Matrix32 = oc::Matrix<u32>;

        RadixSort() = default;
        RadixSort(RadixSort&&) = default;

        // returns true if request() has been called.
        bool hasRequest()
        {
            return mHasRequest;
        }

        bool hasPreprocessing()
        {
            return mHasPrepro;
        }

        macoro::task<> hadamardSumSend(
            Matrix32& s,
            std::vector<u32>& shares,
            BinMatrix& f,
            OtRecvRequest& req,
            coproto::Socket& comm);
        macoro::task<> hadamardSumRecv(
            Matrix32& s,
            std::vector<u32>& shares,
            BinMatrix& f,
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