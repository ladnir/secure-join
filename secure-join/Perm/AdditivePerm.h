#pragma once
#include "secure-join/Defines.h"
#include <vector>
#include "ComposedPerm.h"
#include "Permutation.h"
#include "cryptoTools/Common/Timer.h"

namespace secJoin
{
    // A protocol for being able to permute an input vector x by and XOR 
    // secret sharing of a permutation pi. This will be done by secret sharing
    // pi as mRandPi^-1 o mRho. mRandPi will be random and represented as a
    // ComposedPerm. mRho will be public. We can therefore we can first (locally)
    // permute by mRho and then permute by mRandPi^-1. The effect will be to 
    // permute by pi.
    // 
    // We will compute mRho by permuting mShare (the XOR shares of pi) by mRandPi
    // and reveal the result as mRho.
    class AdditivePerm final : public oc::TimerAdapter
    {
    public:

        // The XOR shares of the permutation pi.
        std::vector<u32> mShare;

        // A random permutation that will be used to mask "mShare". pi = mRandPi^-1 o mRho.
        ComposedPerm mRandPi;

        // The public share of the secret shared permutation pi = mRandPi^-1 o mRho.
        Perm mRho;

        // True if mRho has been computed.
        bool mIsSetup = false;

        // When true and insecure but faster version of the protocol is used.
        bool mInsecureMock = false;

        // returns true if the preprocessing has been run for a chosen permutation.
        // In particular, if the public share mRho has been computed.
        bool isSetup() const { return mIsSetup; }

        AdditivePerm() = default;
        AdditivePerm(const AdditivePerm&) = delete;
        AdditivePerm(AdditivePerm&& o) noexcept 
        {
            *this = std::move(o);
        }


        AdditivePerm& operator=(const AdditivePerm& o) = delete;
        AdditivePerm& operator=(AdditivePerm&& o) noexcept
        {
            mShare = std::move(o.mShare);
            mRandPi = std::move(o.mRandPi);
            mRho = std::move(o.mRho);
            mIsSetup = std::exchange(o.mIsSetup, 0);
            mInsecureMock = std::exchange(o.mInsecureMock, 0);
            return *this;
        }


        // initialize the permutation to be the given size. partyIdx should be in {0,1}.
        // bytesPer can be set to the number of bytes you will want to permute. This can
        // later be set using setBytePerRow(). AltModKeyGen can be set if you want to explicitly 
        // control of the AltMod keygen should be performed.
        void init2(u8 partyIdx, u64 size, u64 bytesPer = 0, macoro::optional<bool> AltModKeyGen = {});

        // Set the XOR shared of the permutation.
        void setShares(span<u32> shares)
        {
            if (shares.size() != size())
                throw RTE_LOC;
            mShare.resize(size());
            memcpy<u32,u32>(mShare, shares);
        }

        // set the AltMod permutation protocol key OTs. These should be AltMod::KeySize OTs in both directions.
        void setKeyOts(
            AltModPrf::KeyType& key,
            std::vector<oc::block>& rk,
            std::vector<std::array<oc::block, 2>>& sk)
        {
            mRandPi.setKeyOts(key, rk, sk);
        }

        // Set the number of bytes we will be permuting. This will cause any correlated randomness
        // to be thrown out.
        void setBytePerRow(u64 bytesPer) { mRandPi.setBytePerRow(bytesPer + 4 * bool(mRho.size())); }

        // generate the masking (replicated) permutation mRandPi
        // and then reveal mRhoPP = mRandPi(mShares).
        //
        // We can then apply our main permutation (mShares)
        // or an input vector x by computing
        //
        // t = mRho(x)
        // y = mRandPi^-1(t)
        //
        // mRho is public and mRandPi is replicated so we
        // have protocols for both.
        //
        macoro::task<> setup(
            coproto::Socket& chl,
            PRNG& prng);

        u64 size() const { return mRandPi.size(); }

        template <typename T>
        macoro::task<> apply(
            PermOp op,
            oc::span<const T> in,
            oc::span<T> out,
            PRNG& prng,
            coproto::Socket& chl);

        template <typename T>
        macoro::task<> apply(
            PermOp op,
            oc::MatrixView<const T> in,
            oc::MatrixView<T> out,
            PRNG& prng,
            coproto::Socket& chl);


        macoro::task<> apply(
            PermOp op,
            BinMatrix& in,
            BinMatrix& out,
            PRNG& prng,
            coproto::Socket& chl);

        //macoro::task<> composeSwap(
        //    AdditivePerm& pi,
        //    AdditivePerm& dst,
        //    PRNG& prng,
        //    coproto::Socket& chl);


        macoro::task<> compose(
            AdditivePerm& pi,
            AdditivePerm& dst,
            PRNG& prng,
            coproto::Socket& chl);


        template <typename T>
        macoro::task<> mockApply(
            PermOp op,
            oc::MatrixView<const T> in,
            oc::MatrixView<T> out,
            PRNG& prng,
            coproto::Socket& chl);

        void request(CorGenerator& ole);

        macoro::task<> preprocess();

        void clearPermutation()
        {
            mRandPi.clearPermutation();
        }

        //returns true if we have requested correlated randomness
        bool hasRequest()
        {
            return mRandPi.hasRequest();
        }

        // return true if we have permutation setup that has not been derandomized.
        bool hasRandomSetup()
        {
            return mRandPi.hasRandomSetup();
        }

        bool hasSetup(u64 bytes) const
        {
            return mRandPi.mSender.hasSetup(bytes + sizeof(u32) * !mIsSetup);
        }

        void clear()
        {
            mRandPi.clear();
            mRho.clear();
            mShare.clear();
            mIsSetup = false;
        }
    };

    static_assert(std::is_move_constructible<AdditivePerm>::value, "AdditivePerm is missing its move ctor");
    static_assert(std::is_move_assignable<AdditivePerm>::value, "AdditivePerm is missing its move ctor");

    template <typename T>
    macoro::task<> AdditivePerm::apply(
        PermOp op,
        oc::span<const T> in,
        oc::span<T> out,
        PRNG& prng,
        coproto::Socket& chl)
    {
        return apply<T>(
            op,
            oc::MatrixView<const T>(in.data(), in.size(), 1),
            oc::MatrixView<T>(out.data(), out.size(), 1),
            prng, chl);
    }


    template <typename T>
    macoro::task<> AdditivePerm::apply(
        PermOp op,
        oc::MatrixView<const T> in,
        oc::MatrixView<T> out,
        PRNG& prng,
        coproto::Socket& chl)
    {
        if (out.rows() != in.rows())
            throw RTE_LOC;
        if (out.cols() != in.cols())
            throw RTE_LOC;
        if (out.rows() != size())
            throw RTE_LOC;

        MC_BEGIN(macoro::task<>, this, in, out, &prng, &chl, op,
            temp = oc::Matrix<T>{},
            soutInv = oc::Matrix<T>{},
            tt = char{});

        if (mInsecureMock)
        {
            MC_AWAIT(mockApply<T>(op, in, out, prng, chl));
            MC_RETURN_VOID();
        }

        if (isSetup() == false)
            MC_AWAIT(setup(chl, prng));


        MC_AWAIT(chl.send(std::move(tt)));
        MC_AWAIT(chl.recv(tt));

        if (op == PermOp::Inverse)
        {
            temp.resize(in.rows(), in.cols());
            MC_AWAIT(mRandPi.apply<T>(PermOp::Regular, in, temp, chl, prng));
            mRho.apply<T>(temp, out, PermOp::Inverse);
        }
        else
        {
            // Local Permutation of [x]
            temp.resize(in.rows(), in.cols());
            mRho.apply<T>(in, temp, PermOp::Regular);
            MC_AWAIT(mRandPi.apply<T>(PermOp::Inverse, temp, out, chl, prng));
        }

        MC_END();
    }


    template <typename T>
    macoro::task<> AdditivePerm::mockApply(
        PermOp op,
        oc::MatrixView<const T> in,
        oc::MatrixView<T> out,
        PRNG& prng,
        coproto::Socket& chl)
    {
        if (mInsecureMock == false)
            throw RTE_LOC;
        if (out.rows() != in.rows())
            throw RTE_LOC;
        if (out.cols() != in.cols())
            throw RTE_LOC;
        if (out.rows() != size())
            throw RTE_LOC;

        MC_BEGIN(macoro::task<>, this, in, out, &prng, &chl, op,
            temp = oc::Matrix<T>{},
            soutInv = oc::Matrix<T>{});

        if (mIsSetup == false)
            MC_AWAIT(setup(chl, prng));

        mRho.apply<T>(in, out, op);

        MC_END();

    }
}