#pragma once

#include "secure-join/Defines.h"
#include "secure-join/Prf/DLpnPrf.h"
#include "secure-join/Perm/Permutation.h"

namespace secJoin
{

    class DLpnPerm
    {
    public:
        static constexpr auto mDebug = DLpnPrfSender::mDebug;

        DLpnPrfReceiver mRecver;
        DLpnPrfSender mSender;

        bool mHasPreprocess = false;
        Perm mPrePerm;
        oc::Matrix<u8> mA, mDelta, mB;

        DLpnPerm() = default;
        DLpnPerm(const DLpnPerm&) = default;
        DLpnPerm(DLpnPerm&&) noexcept = default;
        DLpnPerm& operator=(const DLpnPerm&) = default;
        DLpnPerm& operator=(DLpnPerm&&) noexcept = default;

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

        // Receiver apply: permute a secret shared input x by the other party's pi and get shares as output
        template <typename T>
        macoro::task<> apply(
            oc::MatrixView<const T> in,
            oc::MatrixView<T> sout,

            oc::PRNG& prng,
            coproto::Socket& chl,
            OleGenerator& ole
        );



        void setupDlpnSender(oc::block& key, std::vector<oc::block>& rk);
        void setupDlpnReceiver(std::vector<std::array<oc::block, 2>>& sk);

        macoro::task<> setupDlpnSender(OleGenerator& ole);
        macoro::task<> setupDlpnReceiver(OleGenerator& ole);

        bool hasSetup() const { return mA.size() + mDelta.size(); }


        // generate the preprocessing when all inputs are unknown.
        macoro::task<> preprocess(
            bool permHolder,
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

        // generate the preprocessing when the other party hold pi.
        macoro::task<> setup(
            u64 totElements,
            u64 bytesPerRow,
            oc::PRNG& prng,
            coproto::Socket& chl,
            OleGenerator& ole);

        macoro::task<> validateShares(coproto::Socket& sock);
        macoro::task<> validateShares(coproto::Socket& sock, const Perm& p);

    };

    template <>
    macoro::task<> DLpnPerm::apply<u8>(
        const Perm& pi,
        oc::MatrixView<u8> sout,
        oc::PRNG& prng,
        coproto::Socket& chl,
        bool invPerm,
        OleGenerator& ole);

    template <typename T>
    macoro::task<> DLpnPerm::apply(
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
    macoro::task<> DLpnPerm::apply<u8>(
        const Perm& pi,
        oc::MatrixView<const u8> in,
        oc::MatrixView<u8> sout,
        oc::PRNG& prng,
        coproto::Socket& chl,
        bool invPerm,
        OleGenerator& ole);

    // Generic version of below method
    template <typename T>
    macoro::task<> DLpnPerm::apply(
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
    macoro::task<> DLpnPerm::apply<u8>(
        oc::MatrixView<const u8> in,
        oc::MatrixView<u8> sout,
        oc::PRNG& prng,
        coproto::Socket& chl,
        OleGenerator& ole
        );

    // Generic version of below method
    template <typename T>
    macoro::task<> DLpnPerm::apply(
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