#pragma once
#include "secure-join/Defines.h"
#include <vector>
#include "ComposedPerm.h"
#include "Permutation.h"
#include "cryptoTools/Common/Timer.h"

namespace secJoin
{

    class AdditivePerm final : public oc::TimerAdapter
    {
    public:
        std::vector<u32> mShare;
        ComposedPerm mPi;
        Perm mRho;
        bool mIsSetup = false;
        bool mInsecureMock = false;

        bool isSetup() const { return mIsSetup; }

        AdditivePerm() = default;
        AdditivePerm(const AdditivePerm&) = default;
        AdditivePerm(AdditivePerm&&) noexcept = default;
        AdditivePerm& operator=(const AdditivePerm&) = default;
        AdditivePerm& operator=(AdditivePerm&&) noexcept = default;

        AdditivePerm(span<u32> shares, PRNG& prng, u8 partyIdx);
        void init(u64 size);

        void setupDlpnSender(oc::block& key, std::vector<oc::block>& rk);
        void setupDlpnReceiver(std::vector<std::array<oc::block, 2>>& sk);

        macoro::task<> setupDlpnSender(OleGenerator& ole);
        macoro::task<> setupDlpnReceiver(OleGenerator& ole);

        // generate the masking (replicated) permutation mPi
        // and then reveal mRhoPP = mPi(mShares).
        //
        // We can then apply our main permutation (mShares)
        // or an input vector x by computing
        //
        // t = mRho(x)
        // y = mPi^-1(t)
        //
        // mRho is public and mPi is replicated so we
        // have protocols for both.
        //
        macoro::task<> setup(
            coproto::Socket& chl,
            OleGenerator& ole,
            PRNG& prng);

        u64 size() const { return mShare.size(); }

        template <typename T>
        macoro::task<> apply(
            oc::span<const T> in,
            oc::span<T> out,
            oc::PRNG& prng,
            coproto::Socket& chl,
            OleGenerator& ole,
            bool inv = false);

        template <typename T>
        macoro::task<> apply(
            oc::MatrixView<const T> in,
            oc::MatrixView<T> out,
            oc::PRNG& prng,
            coproto::Socket& chl,
            OleGenerator& ole,
            bool inv = false);


        macoro::task<> apply(
            BinMatrix& in,
            BinMatrix& out,
            oc::PRNG& prng,
            coproto::Socket& chl,
            OleGenerator& ole,
            bool inv = false)
        {
            if (in.cols() != oc::divCeil(in.bitsPerEntry(), 8))
                throw RTE_LOC;
            if (out.cols() != oc::divCeil(out.bitsPerEntry(), 8))
                throw RTE_LOC;
            return apply<u8>(in.mData, out.mData, prng, chl, ole, inv);
        }

        macoro::task<> composeSwap(
            AdditivePerm& pi,
            AdditivePerm& dst,
            oc::PRNG& prng,
            coproto::Socket& chl,
            OleGenerator& gen);



        template <typename T>
        macoro::task<> mockApply(
            oc::MatrixView<const T> in,
            oc::MatrixView<T> out,
            oc::PRNG& prng,
            coproto::Socket& chl,
            OleGenerator& ole,
            bool inv);
    };



    template <typename T>
    macoro::task<> AdditivePerm::apply(
        oc::span<const T> in,
        oc::span<T> out,
        oc::PRNG& prng,
        coproto::Socket& chl,
        OleGenerator& ole,
        bool inv)
    {
        return apply<T>(
            oc::MatrixView<const T>(in.data(), in.size(), 1),
            oc::MatrixView<T>(out.data(), out.size(), 1),
            prng, chl, ole, inv);
    }


    // macoro::task<> AdditivePerm::apply(
    //     BinMatrix& in,
    //     BinMatrix& out,
    //     oc::PRNG& prng,
    //     coproto::Socket& chl,
    //     OleGenerator& ole,
    //     bool inv)
    // {
    //     MC_BEGIN(macoro::task<>, &in, &out, &prng, &chl, &ole, inv);

    //     MC_END();
    // }

    template <typename T>
    macoro::task<> AdditivePerm::apply(
        oc::MatrixView<const T> in,
        oc::MatrixView<T> out,
        oc::PRNG& prng,
        coproto::Socket& chl,
        OleGenerator& ole,
        bool inv)
    {
        if (out.rows() != in.rows())
            throw RTE_LOC;
        if (out.cols() != in.cols())
            throw RTE_LOC;
        if (out.rows() != size())
            throw RTE_LOC;

        MC_BEGIN(macoro::task<>, this, in, out, &prng, &chl, &ole, inv,
            temp = oc::Matrix<T>{},
            soutInv = oc::Matrix<T>{});

        if (mInsecureMock)
        {
            MC_AWAIT(mockApply<T>(in, out, prng, chl, ole, inv));
            MC_RETURN_VOID();
        }

        if (isSetup() == false)
            MC_AWAIT(setup(chl, ole, prng));


        if (inv)
        {
            temp.resize(in.rows(), in.cols());
            MC_AWAIT(mPi.apply<T>(in, temp, chl, ole, false));
            mRho.apply<T>(temp, out, true);
        }
        else
        {
            // Local Permutation of [x]
            temp.resize(in.rows(), in.cols());
            mRho.apply<T>(in, temp);
            MC_AWAIT(mPi.apply<T>(temp, out, chl, ole, true));
        }

        MC_END();
    }


    template <typename T>
    macoro::task<> AdditivePerm::mockApply(
        oc::MatrixView<const T> in,
        oc::MatrixView<T> out,
        oc::PRNG& prng,
        coproto::Socket& chl,
        OleGenerator& ole,
        bool inv)
    {
        if (mInsecureMock == false)
            throw RTE_LOC;
        if (out.rows() != in.rows())
            throw RTE_LOC;
        if (out.cols() != in.cols())
            throw RTE_LOC;
        if (out.rows() != size())
            throw RTE_LOC;

        MC_BEGIN(macoro::task<>, this, in, out, &prng, &chl, &ole, inv,
            temp = oc::Matrix<T>{},
            soutInv = oc::Matrix<T>{});

        if (mIsSetup == false)
            MC_AWAIT(setup(chl, ole, prng));

        mRho.apply<T>(in, out, inv);

        MC_END();

    }
}