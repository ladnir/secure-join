#pragma once

#include "secure-join/Defines.h"
#include "secure-join/Prf/AltModPrf.h"
#include "secure-join/Perm/Permutation.h"

namespace secJoin
{

    class AltModPermSender
    {
    public:
        bool mDebug = false;

        // The AltMod prf protocol
        AltModPrfReceiver mRecver;

        // true if we have setup that has not been derandimized to a chosen permutation.
        bool mHasRandomSetup = false;

        // The random permutation that is used for preprocessing. If we dont do 
        // preprocessing then this will be empty.
        Perm mPrePerm;

        // The chosen permutation that the sender is holding. 
        const Perm* mPi = nullptr;

        // The shares hold by the sender. mDelta ^ mB = mPi(mA)
        oc::Matrix<oc::block> mDelta;

        // The number of bytes per row that the called requested.
        u64 mBytesPerRow = 0;

        // The size of the permutation.
        u64 mNumElems = 0;

        // The number of bytes that have been used to permute the user's data. 
        // This allows us to perform setup one and permute multiple inputs shares.
        u64 mByteOffset = 0;

        // If the sender passed in the permutation mPi by value, we store it here.
        Perm mPermStorage;

        macoro::optional<bool> mKeyGen;

        AltModPermSender() = default;
        AltModPermSender(const AltModPermSender&) = default;
        AltModPermSender(AltModPermSender&&) noexcept = default;
        AltModPermSender& operator=(const AltModPermSender&) = default;
        AltModPermSender& operator=(AltModPermSender&&) noexcept = default;

        // initialize this sender to have a permutation of size n, where 
        // bytesPerRow bytes can be permuted per position. keyGen can be 
        // set if the caller wants to explicitly ask to perform AltMod keygen or not.
        void init(u64 n, u64 bytesPerRow = 0, macoro::optional<bool> keyGen = {});

        void setBytePerRow(u64 bytesPer)
        {
            if (mNumElems == 0)
                throw std::runtime_error("call init first. " LOCATION);


            // invalidate any setup we have
            clearCorrelatedRandomness();

            mBytesPerRow = bytesPer;
            mByteOffset = bytesPer;

            mRecver.init(oc::divCeil(bytesPer, sizeof(oc::block)) * mNumElems, mRecver.mDoKeyGen);
        }

        bool hasPreprocessing() const;

        // clears the internal state
        void clear();

        // sets the permutation of the sender. p will be stored internally.
        // This will clear any derandomized correlated randomness.
        void setPermutation(Perm&& p);

        // sets the permutation of the sender
        // This will clear any derandomized correlated randomness.
        void setPermutation(const Perm& p);

        // clear the permutation and any of its correlated randomness.
        void clearPermutation()
        {
            clearCorrelatedRandomness();
            mPi = nullptr;
        }

        void clearCorrelatedRandomness()
        {
            mHasRandomSetup = 0;
            mByteOffset = mBytesPerRow;
            mDelta.resize(0, 0);
        }

        // permute a remote x by our permutation and get shares as output. 
        // the permutation must have been set and correlated randomness requested.
        template <typename T>
        macoro::task<> apply(
            PermOp op,
            oc::MatrixView<T> sout,
            PRNG& prng,
            coproto::Socket& chl);

        // permute a secret shared input x by our pi and get shares as output
        // the permutation must have been set and correlated randomness requested.
        template <typename T>
        macoro::task<> apply(
            PermOp op,
            oc::MatrixView<const T> in,
            oc::MatrixView<T> sout,
            PRNG& prng,
            coproto::Socket& chl
        );

        // set the AltMod key OTs
        void setKeyOts(std::vector<std::array<oc::block, 2>>& sk);

        // return true if we have preprocessing that has not yet been derandomized.
        bool hasRandomSetup() const { return mHasRandomSetup; }

        //returns true if we have requested correlated randomness
        bool hasRequest() const { return mRecver.hasRequest(); }

        // returns the amount of correlated randomness that is left in bytes.
        u64 remainingSetup() const { return mBytesPerRow - mByteOffset; }

        // returns true if there is enough coreelated randomness to permute numBytes bytes.
        bool hasSetup(u64 numBytes) const { return remainingSetup() >= numBytes; }

        // Request the required correlated randomness from CorGenerator. Init must be called first.
        void request(CorGenerator& ole);

        // Invoke the preprocessing protocol. This will sample a random permutation
        // that we can later derandomize.
        macoro::task<> preprocess();

        // for debugging, check that the correlated randomness is correct.
        macoro::task<> validateShares(coproto::Socket& sock, Perm p);

        // Generate the correlated randomness for the permutation pi. pi will either
        // be mPi if it is already known or it will be mPrePerm.
        macoro::task<> setup(
            PRNG& prng,
            coproto::Socket& chl);
    };


    class AltModPermReceiver
    {
    public:
        bool mDebug = false;

        // The AltMod prf sender protocol.
        AltModPrfSender mSender;

        // true if we have preprocessing that has not been derandomized to a chosen permutation.
        bool mHasRandomSetup = false;

        // The shares held by the receiver. mDelta ^ mB = mPi(mA)
        oc::Matrix<oc::block> mA, mB;

        // The number of bytes per row that the user requested.
        u64 mBytesPerRow = 0;

        // The size of the permutation.
        u64 mNumElems = 0;

        // The number of bytes that have been used to permute the user's data. 
        // This allows us to perform setup one and permute multiple inputs shares.
        u64 mByteOffset = 0;

        macoro::optional<bool> mKeyGen;

        AltModPermReceiver() = default;
        AltModPermReceiver(const AltModPermReceiver&) = default;
        AltModPermReceiver(AltModPermReceiver&&) noexcept = default;
        AltModPermReceiver& operator=(const AltModPermReceiver&) = default;
        AltModPermReceiver& operator=(AltModPermReceiver&&) noexcept = default;

        void init(u64 n, u64 bytesPer = 0, macoro::optional<bool> keyGen = {})
        {
            clearCorrelatedRandomness();
            mNumElems = n;
            mBytesPerRow = bytesPer;
            mByteOffset = bytesPer;
            mKeyGen = keyGen;
        }

        void setBytePerRow(u64 bytesPer)
        {
            if (mNumElems == 0)
                throw std::runtime_error("call init first. " LOCATION);

            clearCorrelatedRandomness();
            mBytesPerRow = bytesPer;
            mByteOffset = bytesPer;
        }

        void setKeyOts(AltModPrf::KeyType& key, std::vector<oc::block>& rk);

        bool hasRequest() const { return mSender.hasRequest(); }

        bool hasPreprocessing() const { return mSender.hasPreprocessing(); }


        void clear()
        {
            mSender.clear();
            mA.resize(0, 0);
            mB.resize(0, 0);
            mByteOffset = 0;
            mNumElems = 0;
            mBytesPerRow = 0;
            mHasRandomSetup = 0;
        }


        void clearCorrelatedRandomness()
        {
            mHasRandomSetup = 0;
            mByteOffset = mBytesPerRow;
            mA.resize(0, 0);
            mB.resize(0, 0);
        }


        // Receiver apply: permute a secret shared input x by the other party's pi and get shares as output
        template <typename T>
        macoro::task<> apply(
            PermOp op,
            oc::MatrixView<const T> in,
            oc::MatrixView<T> sout,
            PRNG& prng,
            coproto::Socket& chl
        );

        // If we have derandomized random correlations, then clear
        void clearPermutation() {
            clearCorrelatedRandomness();
        }

        // returns true if we have preprocessing for a random permutation. 
        // This random permutation can be later derandomized to a chosen perm.
        bool hasRandomSetup() const { return mHasRandomSetup; }

        // returns how must correlated randomness is left in bytes. 
        u64 remainingSetup() const { return mBytesPerRow - mByteOffset; }

        // returns true if there is enough correlated randomness to permute input elements of numBytes bytes.
        bool hasSetup(u64 numBytes) const { return remainingSetup() >= numBytes; }

        // Request correlated randomness from CorGenerator. init must be called first.
        void request(CorGenerator& ole);

        // generete preprocessing for a rnadom permutation. This can be derandomized to a chosen perm later.
        macoro::task<> preprocess();

        // For debugging. Check that the correlated randomness is correct.
        macoro::task<> validateShares(coproto::Socket& sock);

        macoro::task<> setup(
            PRNG& prng,
            coproto::Socket& chl);


    };

    //template <>
    //macoro::task<> AltModPermSender::apply<u8>(
    //    const Perm& pi,
    //    PermOp op,
    //    oc::MatrixView<u8> sout,
    //    PRNG& prng,
    //    coproto::Socket& chl,
    //    CorGenerator& ole);

    //template <typename T>
    //macoro::task<> AltModPermSender::apply(
    //    const Perm& pi,
    //    PermOp op,
    //    oc::MatrixView<T> sout,
    //    PRNG& prng,
    //    coproto::Socket& chl,
    //    CorGenerator& ole)
    //{
    //    return apply<u8>(pi, op, matrixCast<u8>(sout), prng, chl, ole);
    //}


    template <>
    macoro::task<> AltModPermSender::apply<u8>(
        PermOp op,
        oc::MatrixView<u8> sout,
        PRNG& prng,
        coproto::Socket& chl);

    //template <typename T>
    //macoro::task<> AltModPermSender::apply(
    //    PermOp op,
    //    oc::MatrixView<T> sout,
    //    PRNG& prng,
    //    coproto::Socket& chl)
    //{
    //    return apply<u8>(op, matrixCast<u8>(sout), prng, chl, ole);
    //}

    // Generic version of below method
    //template <>
    //macoro::task<> AltModPermSender::apply<u8>(
    //    const Perm& pi,
    //    PermOp op,
    //    oc::MatrixView<const u8> in,
    //    oc::MatrixView<u8> sout,
    //    PRNG& prng,
    //    coproto::Socket& chl,
    //    CorGenerator& ole);

    //// Generic version of below method
    //template <typename T>
    //macoro::task<> AltModPermSender::apply(
    //    const Perm& pi,
    //    PermOp op,
    //    oc::MatrixView<const T> in,
    //    oc::MatrixView<T> sout,
    //    PRNG& prng,
    //    coproto::Socket& chl,
    //    CorGenerator& ole)
    //{
    //    return apply<u8>(pi, op, matrixCast<const u8>(in), matrixCast<u8>(sout), prng, chl, ole);
    //}

    // Generic version of below method
    template <>
    macoro::task<> AltModPermSender::apply<u8>(
        PermOp op,
        oc::MatrixView<const u8> in,
        oc::MatrixView<u8> sout,
        PRNG& prng,
        coproto::Socket& chl);

    //// Generic version of below method
    //template <typename T>
    //macoro::task<> AltModPermSender::apply(
    //    PermOp op,
    //    oc::MatrixView<const T> in,
    //    oc::MatrixView<T> sout,
    //    PRNG& prng,
    //    coproto::Socket& chl)
    //{
    //    return apply<u8>(op, matrixCast<const u8>(in), matrixCast<u8>(out), prng, chl);
    //}

    template <>
    macoro::task<> AltModPermReceiver::apply<u8>(
        PermOp op,
        oc::MatrixView<const u8> in,
        oc::MatrixView<u8> sout,
        PRNG& prng,
        coproto::Socket& chl
        );

    // Generic version of below method
    //template <typename T>
    //macoro::task<> AltModPermReceiver::apply(
    //    PermOp op,
    //    oc::MatrixView<const T> in,
    //    oc::MatrixView<T> sout,
    //    PRNG& prng,
    //    coproto::Socket& chl)
    //{
    //    return apply<u8>(op, matrixCast<const u8>(in), matrixCast<u8>(out), prng, chl);
    //}



}