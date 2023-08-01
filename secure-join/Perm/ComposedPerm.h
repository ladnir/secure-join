#pragma once
#include "secure-join/Perm/LowMCPerm.h"
#include "secure-join/Perm/InsecurePerm.h"
#include "secure-join/GMW/Gmw.h"
#include "secure-join/Perm/DLpnPerm.h"

namespace secJoin
{
    // A shared permutation where P0 holds pi_0 and P1 holds pi_1
    // such that the combined permutation is pi = pi_1 o pi_0.
    class ComposedPerm
    {
    public:
        u64 mPartyIdx = -1;
        Perm mPi;
        DLpnPermSender mSender;
        DLpnPermReceiver mReceiver;
        bool mIsSecure = true;

        ComposedPerm() = default;
        ComposedPerm(const ComposedPerm&) = default;
        ComposedPerm(ComposedPerm&&) noexcept = default;
        ComposedPerm& operator=(const ComposedPerm&) = default;
        ComposedPerm& operator=(ComposedPerm&&) noexcept = default;

        //initializing the permutation
        ComposedPerm(Perm perm, u8 partyIdx)
            : mPartyIdx(partyIdx)
            , mPi(std::move(perm))
        {}

        ComposedPerm(u64 n, u8 partyIdx, PRNG& prng)
            : mPartyIdx(partyIdx)
            , mPi(n, prng)
        {}

        void setKeyOts(
            oc::block& key,
            std::vector<oc::block>& rk,
            std::vector<std::array<oc::block, 2>>& sk);

        macoro::task<> genKeyOts(OleGenerator& ole, coproto::Socket& chl);

        u64 size() const { return mPi.size(); }

        void init(u64 n, u8 partyIdx, PRNG& prng);

        macoro::task<> preprocess(
            u64 n,
            u64 bytesPer,
            coproto::Socket& chl,
            OleGenerator& ole,
            PRNG& prng);

        template<typename T>
        macoro::task<> apply(
            PermOp op,
            oc::MatrixView<const T> in,
            oc::MatrixView<T> out,
            coproto::Socket& chl,
            OleGenerator& ole);


        void clear()
        {
            mPartyIdx = -1;
            mPi.clear();
            mSender.clear();
            mReceiver.clear();
        }
    };



    template<>
    macoro::task<> ComposedPerm::apply<u8>(
        PermOp op,
        oc::MatrixView<const u8> in,
        oc::MatrixView<u8> out,
        coproto::Socket& chl,
        OleGenerator& ole);

    template<typename T>
    macoro::task<> ComposedPerm::apply(
        PermOp op,
        oc::MatrixView<const T> in,
        oc::MatrixView<T> out,
        coproto::Socket& chl,
        OleGenerator& ole)
    {
        return apply<u8>(op, matrixCast<const u8>(in), matrixCast<u8>(out), chl, ole);
    }
}