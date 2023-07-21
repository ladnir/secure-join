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
        oc::Matrix<u8> mDelta;

        DLpnPermSender() = default;
        DLpnPermSender(const DLpnPermSender&) = default;
        DLpnPermSender(DLpnPermSender&&) noexcept = default;
        DLpnPermSender& operator=(const DLpnPermSender&) = default;
        DLpnPermSender& operator=(DLpnPermSender&&) noexcept = default;

        // Sender apply: permute a remote x by our pi and get shares as output.
        template <typename T>
        macoro::task<> apply(
            const Perm& pi,
            oc::MatrixView<T> sout,

            oc::PRNG& prng,
            coproto::Socket& chl,
            bool invPerm,
            OleGenerator& ole);

        // Sender apply: permute a secret shared input x by our pi and get shares as output
        template <typename T>
        macoro::task<> apply(
            const Perm& pi,
            oc::MatrixView<const T> in,
            oc::MatrixView<T> sout,

            oc::PRNG& prng,
            coproto::Socket& chl,
            bool invPerm,
            OleGenerator& ole
        );

        void setKeyOts(std::vector<std::array<oc::block, 2>>& sk);
        macoro::task<> genKeyOts(OleGenerator& ole, coproto::Socket& chl);


        bool hasPreprocessing() const { return mHasPreprocess; }
        bool hasSetup() const { return mDelta.size(); }


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
            bool invPerm,
            OleGenerator& ole);

        macoro::task<> validateShares(coproto::Socket& sock, const Perm& p);

    };


    class DLpnPermReceiver
    {
    public:
        static constexpr auto mDebug = DLpnPrfSender::mDebug;

        DLpnPrfSender mSender;

        bool mHasPreprocess = false;
        oc::Matrix<u8> mA, mB;

        DLpnPermReceiver() = default;
        DLpnPermReceiver(const DLpnPermReceiver&) = default;
        DLpnPermReceiver(DLpnPermReceiver&&) noexcept = default;
        DLpnPermReceiver& operator=(const DLpnPermReceiver&) = default;
        DLpnPermReceiver& operator=(DLpnPermReceiver&&) noexcept = default;

        void setKeyOts(oc::block& key, std::vector<oc::block>& rk);
        macoro::task<> genKeyOts(OleGenerator& ole, coproto::Socket& chl);


        // Receiver apply: permute a secret shared input x by the other party's pi and get shares as output
        template <typename T>
        macoro::task<> apply(
            oc::MatrixView<const T> in,
            oc::MatrixView<T> sout,
            oc::PRNG& prng,
            coproto::Socket& chl,
            OleGenerator& ole
        );


        bool hasPreprocessing() const { return mHasPreprocess; }
        bool hasSetup() const { return mA.size(); }


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
        oc::MatrixView<u8> sout,
        oc::PRNG& prng,
        coproto::Socket& chl,
        bool invPerm,
        OleGenerator& ole);

    template <typename T>
    macoro::task<> DLpnPermSender::apply(
        const Perm& pi,
        oc::MatrixView<T> sout,
        oc::PRNG& prng,
        coproto::Socket& chl,
        bool invPerm,
        OleGenerator& ole)
    {
        oc::MatrixView<u8> oo((u8*)sout.data(), sout.rows(), sout.cols() * sizeof(T));
        return apply<u8>(pi, sout, prng, chl, invPerm, ole);
    }

    // Generic version of below method
    template <>
    macoro::task<> DLpnPermSender::apply<u8>(
        const Perm& pi,
        oc::MatrixView<const u8> in,
        oc::MatrixView<u8> sout,
        oc::PRNG& prng,
        coproto::Socket& chl,
        bool invPerm,
        OleGenerator& ole);

    // Generic version of below method
    template <typename T>
    macoro::task<> DLpnPermSender::apply(
        const Perm& pi,
        oc::MatrixView<const T> in,
        oc::MatrixView<T> sout,
        oc::PRNG& prng,
        coproto::Socket& chl,
        bool invPerm,
        OleGenerator& ole)
    {
        oc::MatrixView<const u8> xx((u8*)in.data(), in.rows(), in.cols() * sizeof(T));
        oc::MatrixView<u8> oo((u8*)sout.data(), sout.rows(), sout.cols() * sizeof(T));
        return apply<u8>(pi, xx, oo, prng, chl, invPerm, ole);
    }


    template <>
    macoro::task<> DLpnPermReceiver::apply<u8>(
        oc::MatrixView<const u8> in,
        oc::MatrixView<u8> sout,
        oc::PRNG& prng,
        coproto::Socket& chl,
        OleGenerator& ole
        );

    // Generic version of below method
    template <typename T>
    macoro::task<> DLpnPermReceiver::apply(
        oc::MatrixView<const T> in,
        oc::MatrixView<T> sout,
        oc::PRNG& prng,
        coproto::Socket& chl,
        OleGenerator& ole)
    {
        oc::MatrixView<const u8> xx((u8*)in.data(), in.rows(), in.cols() * sizeof(T));
        oc::MatrixView<u8> oo((u8*)sout.data(), sout.rows(), sout.cols() * sizeof(T));
        return apply<u8>(xx, oo, prng, chl, ole);
    }

}