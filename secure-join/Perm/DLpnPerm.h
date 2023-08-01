#pragma once

#include "secure-join/Defines.h"
#include "secure-join/Prf/DLpnPrf.h"
#include "secure-join/Perm/Permutation.h"

namespace secJoin
{

    class DLpnPermSender
    {
    public:
        static constexpr auto mDebug = DLpnPrfSender::mDebug;

        DLpnPrfReceiver mRecver;

        bool mHasPreprocess = false;
        Perm mPrePerm;
        const Perm* mPi = nullptr;
        Perm mPermStorage;
        oc::Matrix<oc::block> mDelta;
        u64 mByteOffset = 0, mSetupSize = 0;

        DLpnPermSender() = default;
        DLpnPermSender(const DLpnPermSender&) = default;
        DLpnPermSender(DLpnPermSender&&) noexcept = default;
        DLpnPermSender& operator=(const DLpnPermSender&) = default;
        DLpnPermSender& operator=(DLpnPermSender&&) noexcept = default;



        void clear()
        {
            mHasPreprocess = false;
            mPrePerm.clear();
            mPi = nullptr;
            mPermStorage.clear();
            mDelta.resize(0, 0);
            mSetupSize = 0;
            mHasPreprocess = 0;
        }

        void setPermutation(Perm&& p)
        {
            mPermStorage = std::move(p);
            setPermutation(mPermStorage);
        }

        void setPermutation(const Perm& p)
        {
            if (mHasPreprocess)
            {
                if (p.size() != mDelta.rows())
                    throw RTE_LOC;
            }
            else
            {
                mByteOffset = mSetupSize;
            }
            mPi = &p;
        }

        // Sender apply: permute a remote x by our pi and get shares as output.
        template <typename T>
        macoro::task<> apply(
            const Perm& pi,
            PermOp op,
            oc::MatrixView<T> sout,
            oc::PRNG& prng,
            coproto::Socket& chl,
            OleGenerator& ole);

        // Sender apply: permute a secret shared input x by our pi and get shares as output
        template <typename T>
        macoro::task<> apply(
            const Perm& pi, 
            PermOp op,
            oc::MatrixView<const T> in,
            oc::MatrixView<T> sout,
            oc::PRNG& prng,
            coproto::Socket& chl,
            OleGenerator& ole
        );

        // Sender apply: permute a remote x by our pi and get shares as output.
        template <typename T>
        macoro::task<> apply(
            PermOp op,
            oc::MatrixView<T> sout,
            oc::PRNG& prng,
            coproto::Socket& chl,
            OleGenerator& ole);

        // Sender apply: permute a secret shared input x by our pi and get shares as output
        template <typename T>
        macoro::task<> apply(
            PermOp op,
            oc::MatrixView<const T> in,
            oc::MatrixView<T> sout,
            oc::PRNG& prng,
            coproto::Socket& chl,
            OleGenerator& ole
        );

        void setKeyOts(std::vector<std::array<oc::block, 2>>& sk);
        macoro::task<> genKeyOts(OleGenerator& ole, coproto::Socket& chl);


        bool hasPreprocessing() const { return mHasPreprocess; }
        u64 remainingSetup() const { return mSetupSize - mByteOffset; }
        bool hasSetup(u64 numBytes) const { return remainingSetup() >= numBytes; }


        // generate the preprocessing when all inputs are unknown.
        macoro::task<> preprocess(
            u64 totElements,
            u64 bytesPerRow,
            oc::PRNG& prng,
            coproto::Socket& chl,
            OleGenerator& ole);

        // generate the preprocessing for us holding pi.
        macoro::task<> setup(
            const Perm& pi,
            u64 bytesPerRow,
            oc::PRNG& prng,
            coproto::Socket& chl,
            OleGenerator& ole);

        macoro::task<> validateShares(coproto::Socket& sock, Perm p);

    };


    class DLpnPermReceiver
    {
    public:
        static constexpr auto mDebug = DLpnPrfSender::mDebug;

        DLpnPrfSender mSender;

        bool mHasPreprocess = false;
        oc::Matrix<oc::block> mA, mB;
        u64 mByteOffset = 0, mSetupSize = 0;

        DLpnPermReceiver() = default;
        DLpnPermReceiver(const DLpnPermReceiver&) = default;
        DLpnPermReceiver(DLpnPermReceiver&&) noexcept = default;
        DLpnPermReceiver& operator=(const DLpnPermReceiver&) = default;
        DLpnPermReceiver& operator=(DLpnPermReceiver&&) noexcept = default;

        void setKeyOts(oc::block& key, std::vector<oc::block>& rk);
        macoro::task<> genKeyOts(OleGenerator& ole, coproto::Socket& chl);


        void clear()
        {
            mA.resize(0, 0);
            mB.resize(0, 0);
            mByteOffset = 0;
            mSetupSize = 0;
            mHasPreprocess = 0;
        }

        // Receiver apply: permute a secret shared input x by the other party's pi and get shares as output
        template <typename T>
        macoro::task<> apply(
            PermOp op,
            oc::MatrixView<const T> in,
            oc::MatrixView<T> sout,
            oc::PRNG& prng,
            coproto::Socket& chl,
            OleGenerator& ole
        );

        void clearPermutation() {
            if (!mHasPreprocess)
            {
                mByteOffset = mSetupSize;
            }
        }

        bool hasPreprocessing() const { return mHasPreprocess; }

        u64 remainingSetup() const { return mSetupSize - mByteOffset; }
        bool hasSetup(u64 numBytes) const { return remainingSetup() >= numBytes; }


        // generate the preprocessing when all inputs are unknown.
        macoro::task<> preprocess(
            u64 totElements,
            u64 bytesPerRow,
            oc::PRNG& prng,
            coproto::Socket& chl,
            OleGenerator& ole);

        // generate the preprocessing when the other party hold pi.
        macoro::task<> setup(
            u64 totElements,
            u64 bytesPerRow,
            oc::PRNG& prng,
            coproto::Socket& chl,
            OleGenerator& ole);

        macoro::task<> validateShares(coproto::Socket& sock);

    };

    template <>
    macoro::task<> DLpnPermSender::apply<u8>(
        const Perm& pi,
        PermOp op,
        oc::MatrixView<u8> sout,
        oc::PRNG& prng,
        coproto::Socket& chl,
        OleGenerator& ole);

    template <typename T>
    macoro::task<> DLpnPermSender::apply(
        const Perm& pi,
        PermOp op,
        oc::MatrixView<T> sout,
        oc::PRNG& prng,
        coproto::Socket& chl,
        OleGenerator& ole)
    {
        return apply<u8>(pi, op, matrixCast<u8>(sout), prng, chl, ole);
    }


    template <>
    macoro::task<> DLpnPermSender::apply<u8>(
        PermOp op,
        oc::MatrixView<u8> sout,
        oc::PRNG& prng,
        coproto::Socket& chl,
        OleGenerator& ole);

    template <typename T>
    macoro::task<> DLpnPermSender::apply(
        PermOp op,
        oc::MatrixView<T> sout,
        oc::PRNG& prng,
        coproto::Socket& chl,
        OleGenerator& ole)
    {
        return apply<u8>(pi, op, matrixCast<u8>(sout), prng, chl, ole);
    }

    // Generic version of below method
    template <>
    macoro::task<> DLpnPermSender::apply<u8>(
        const Perm& pi,
        PermOp op,
        oc::MatrixView<const u8> in,
        oc::MatrixView<u8> sout,
        oc::PRNG& prng,
        coproto::Socket& chl,
        OleGenerator& ole);

    // Generic version of below method
    template <typename T>
    macoro::task<> DLpnPermSender::apply(
        const Perm& pi,
        PermOp op,
        oc::MatrixView<const T> in,
        oc::MatrixView<T> sout,
        oc::PRNG& prng,
        coproto::Socket& chl,
        OleGenerator& ole)
    {
        return apply<u8>(pi, op, matrixCast<const u8>(in), matrixCast<u8>(sout), prng, chl, ole);
    }

    // Generic version of below method
    template <>
    macoro::task<> DLpnPermSender::apply<u8>(
        PermOp op,
        oc::MatrixView<const u8> in,
        oc::MatrixView<u8> sout,
        oc::PRNG& prng,
        coproto::Socket& chl,
        OleGenerator& ole);

    // Generic version of below method
    template <typename T>
    macoro::task<> DLpnPermSender::apply(
        PermOp op,
        oc::MatrixView<const T> in,
        oc::MatrixView<T> sout,
        oc::PRNG& prng,
        coproto::Socket& chl,
        OleGenerator& ole)
    {
        return apply<u8>(op, matrixCast<const u8>(in), matrixCast<u8>(out), prng, chl, ole);
    }

    template <>
    macoro::task<> DLpnPermReceiver::apply<u8>(
        PermOp op,
        oc::MatrixView<const u8> in,
        oc::MatrixView<u8> sout,
        oc::PRNG& prng,
        coproto::Socket& chl,
        OleGenerator& ole
        );

    // Generic version of below method
    template <typename T>
    macoro::task<> DLpnPermReceiver::apply(
        PermOp op,
        oc::MatrixView<const T> in,
        oc::MatrixView<T> sout,
        oc::PRNG& prng,
        coproto::Socket& chl,
        OleGenerator& ole)
    {
        return apply<u8>(op, matrixCast<const u8>(in), matrixCast<u8>(out), prng, chl, ole);
    }



}