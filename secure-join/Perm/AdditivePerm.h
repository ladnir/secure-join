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

        void setKeyOts(
            oc::block& key,
            std::vector<oc::block>& rk,
            std::vector<std::array<oc::block, 2>>& sk)
        {
            mPi.setKeyOts(key, rk, sk);
        }

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
            CorGenerator& ole,
            PRNG& prng);

        u64 size() const { return mShare.size(); }

        template <typename T>
        macoro::task<> apply(
            PermOp op,
            oc::span<const T> in,
            oc::span<T> out,
            oc::PRNG& prng,
            coproto::Socket& chl,
            CorGenerator& ole);

        template <typename T>
        macoro::task<> apply(
            PermOp op,
            oc::MatrixView<const T> in,
            oc::MatrixView<T> out,
            oc::PRNG& prng,
            coproto::Socket& chl,
            CorGenerator& ole);


        macoro::task<> apply(
            PermOp op,
            BinMatrix& in,
            BinMatrix& out,
            oc::PRNG& prng,
            coproto::Socket& chl,
            CorGenerator& ole);

        macoro::task<> composeSwap(
            AdditivePerm& pi,
            AdditivePerm& dst,
            oc::PRNG& prng,
            coproto::Socket& chl,
            CorGenerator& gen);


        macoro::task<> compose(
            AdditivePerm& pi,
            AdditivePerm& dst,
            oc::PRNG& prng,
            coproto::Socket& chl,
            CorGenerator& gen);


        template <typename T>
        macoro::task<> mockApply(
            PermOp op,
            oc::MatrixView<const T> in,
            oc::MatrixView<T> out,
            oc::PRNG& prng,
            coproto::Socket& chl,
            CorGenerator& ole);


        macoro::task<> preprocess(
            u64 n, 
            u64 bytesPer, 
            coproto::Socket& chl,
            CorGenerator& ole,
            oc::PRNG& prng);

        bool hasSetup(u64 bytes) const
        {
            return mPi.mSender.hasSetup(bytes + sizeof(u32) * !mIsSetup);
        }

        void clear()
        {
            mPi.clear();
            mRho.clear();
            mShare.clear();
        }
    };



    template <typename T>
    macoro::task<> AdditivePerm::apply(
        PermOp op,
        oc::span<const T> in,
        oc::span<T> out,
        oc::PRNG& prng,
        coproto::Socket& chl,
        CorGenerator& ole)
    {
        return apply<T>(
            op,
            oc::MatrixView<const T>(in.data(), in.size(), 1),
            oc::MatrixView<T>(out.data(), out.size(), 1),
            prng, chl, ole);
    }


    template <typename T>
    macoro::task<> AdditivePerm::apply(
        PermOp op,
        oc::MatrixView<const T> in,
        oc::MatrixView<T> out,
        oc::PRNG& prng,
        coproto::Socket& chl,
        CorGenerator& ole)
    {
        if (out.rows() != in.rows())
            throw RTE_LOC;
        if (out.cols() != in.cols())
            throw RTE_LOC;
        if (out.rows() != size())
            throw RTE_LOC;

        MC_BEGIN(macoro::task<>, this, in, out, &prng, &chl, &ole, op,
            temp = oc::Matrix<T>{},
            soutInv = oc::Matrix<T>{},
            tt = char{});

        if (mInsecureMock)
        {
            MC_AWAIT(mockApply<T>(op, in, out, prng, chl, ole));
            MC_RETURN_VOID();
        }

        if (isSetup() == false)
            MC_AWAIT(setup(chl, ole, prng));


        MC_AWAIT(chl.send(std::move(tt)));
        MC_AWAIT(chl.recv(tt));

        if (op == PermOp::Inverse)
        {
            temp.resize(in.rows(), in.cols());
            MC_AWAIT(mPi.apply<T>(PermOp::Regular, in, temp, chl, ole, prng));
            mRho.apply<T>(temp, out, PermOp::Inverse);
        }
        else
        {
            // Local Permutation of [x]
            temp.resize(in.rows(), in.cols());
            mRho.apply<T>(in, temp, PermOp::Regular);
            MC_AWAIT(mPi.apply<T>(PermOp::Inverse, temp, out, chl, ole, prng));
        }

        MC_END();
    }


    template <typename T>
    macoro::task<> AdditivePerm::mockApply(
        PermOp op,
        oc::MatrixView<const T> in,
        oc::MatrixView<T> out,
        oc::PRNG& prng,
        coproto::Socket& chl,
        CorGenerator& ole)
    {
        if (mInsecureMock == false)
            throw RTE_LOC;
        if (out.rows() != in.rows())
            throw RTE_LOC;
        if (out.cols() != in.cols())
            throw RTE_LOC;
        if (out.rows() != size())
            throw RTE_LOC;

        MC_BEGIN(macoro::task<>, this, in, out, &prng, &chl, &ole, op,
            temp = oc::Matrix<T>{},
            soutInv = oc::Matrix<T>{});

        if (mIsSetup == false)
            MC_AWAIT(setup(chl, ole, prng));

        mRho.apply<T>(in, out, op);

        MC_END();

    }
}