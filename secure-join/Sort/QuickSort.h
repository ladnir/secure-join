//#pragma once
//#include "secure-join/Defines.h"
//#include "secure-join/Perm/ComposedPerm.h"
//
//namespace secJoin
//{
//
//
//    class QuickSort : public oc::TimerAdapter
//    {
//    public:
//        // run debugging check (insecure).
//        bool mDebug = false;
//
//        // mock the sorting protocol (insecure).
//        bool mInsecureMock = false;
//
//        // has request been called.
//        bool mHasRequest = false;
//
//        // has preprocess been called.
//        bool mHasPrepro = false;
//
//        // The number of item we are sorting.
//        u64 mSize = 0;
//
//        // The bit count of the items we are sorting.
//        u64 mBitCount = 0;
//
//        // the bit count with the index of the item prepended.
//        u64 mExtendedBitCount = 0;
//
//        // The requested amount of preprocessing that the output permutation should have.
//        u64 mBytesPerElem = 0;
//
//        // A zero one flag denoting the party index.
//        u64 mRole = -1;
//
//        std::vector<BinOleRequest> mOleRequests;
//
//        // Sets various parameters for the protocol. role should be 0,1. n is the list size, bitCount is
//        // the number of bits per element. bytesPerElem is an optional parameter
//        // that will initialize the output permutation with enough correlated
//        // randomness to permute elements with bytesPerElem bytes.
//        void init(
//            u64 role,
//            u64 n,
//            u64 bitCount,
//            u64 bytesPerElem = 0);
//
//        // Once init it called, this will request the required correlated randomness
//        // from CorGenerator. To start the generation of the randomness, call preprocess().
//        void request(CorGenerator& gen);
//
//        // Start the generation of the requested correlated randomness.
//        macoro::task<> preprocess(
//            coproto::Socket& comm,
//            PRNG& prng);
//
//        // returns true if request() has been called.
//        bool hasRequest()
//        {
//            return mHasRequest;
//        }
//
//        bool hasPreprocessing()
//        {
//            return mHasPrepro;
//        }
//
//        macoro::task<> mockSort(
//            const BinMatrix& k,
//            AdditivePerm& dst,
//            coproto::Socket& comm);
//
//    };
//}
