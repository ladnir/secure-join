#pragma once

#include "secure-join/Perm/PermCorrelation.h"
#include "secure-join/Perm/AdditivePerm.h"
namespace secJoin
{


    // A shared permutation where P0 holds pi_0 and P1 holds pi_1
    // such that the combined permutation is pi = pi_1 o pi_0.
    class ComposedPerm
    {
    public:
        // {0,1} to determine which party permutes first
        u64 mPartyIdx = -1;

        // The permutation protocol for mPi
        PermCorSender mPermSender;

        // The permutation protocol for the other share.
        PermCorReceiver mPermReceiver;

        ComposedPerm() = default;
        ComposedPerm(const ComposedPerm&) = default;
        ComposedPerm(ComposedPerm&&) noexcept = default;
        ComposedPerm& operator=(const ComposedPerm&) = default;
        ComposedPerm& operator=(ComposedPerm&&) noexcept = default;

        // initializing with a random permutation.
        ComposedPerm(u8 partyIdx, PermCorSender&& s, PermCorReceiver&& r)
            : mPartyIdx(partyIdx)
            , mPermSender(std::move(s))
            , mPermReceiver(std::move(r))
        { }

        Perm& permShare() { return mPermSender.mPerm; }
        const Perm& permShare() const { return mPermSender.mPerm; }

        // the size of the permutation that is being shared.
        u64 size() const { return mPermSender.size(); }

        // return the amount of correlated randomness 
        // that is remaining.
        u64 corSize() const { return std::min<u64>(mPermSender.corSize(), mPermReceiver.corSize()); }

        // initialize the permutation to have the given size.
        // partyIdx should be in {0,1}, n is size, bytesPer can be
        // set to how many bytes the user wants to permute. AltModKeyGen
        // can be set if the user wants to control if the AltMod kets 
        // should be sampled or not.
        void init(u8 partyIdx, PermCorSender&& s, PermCorReceiver&& r)
        {
            mPartyIdx = partyIdx;
            mPermSender = std::move(s);
            mPermReceiver = std::move(r);
        }

        // permute the input data by the secret shared permutation. op
        // control if the permutation is applied directly or its inverse.
        // in/out are the input and output shared. Correlated randomness 
        // must have been requested using request().
        template<typename T>
        macoro::task<> apply(
            PermOp op,
            oc::MatrixView<const T> in,
            oc::MatrixView<T> out,
            coproto::Socket& );

        // dst = *this o permShares
        macoro::task<> compose(
            AdditivePerm& permShares,
            AdditivePerm& dst,
            coproto::Socket& sock)
        {
            dst.mShare.resize(permShares.size());
            return apply<u32>(
                PermOp::Regular,
                { permShares.mShare.data(), permShares.size(), 1 },
                { dst.mShare.data(), dst.size(), 1 },
                sock);
        }



        // derandomize this permutation to equal newPerm.
        // This will reveal the difference between
        // the old permutation and the new perm.
        macoro::task<> derandomize(
            AdditivePerm& newPerm,
            coproto::Socket& chl);

        macoro::task<> validate(coproto::Socket& sock)
        {
            MC_BEGIN(macoro::task<>, this, &sock);

            if (mPartyIdx)
            {
                MC_AWAIT(mPermSender.validate(sock));
                MC_AWAIT(mPermReceiver.validate(sock));
            }
            else
            {
                MC_AWAIT(mPermReceiver.validate(sock));
                MC_AWAIT(mPermSender.validate(sock));
            }

            MC_END();
        }
    };


    template<>
    macoro::task<> ComposedPerm::apply<u8>(
        PermOp op,
        oc::MatrixView<const u8> in,
        oc::MatrixView<u8> out,
        coproto::Socket& chl);

    template<typename T>
    macoro::task<> ComposedPerm::apply(
        PermOp op,
        oc::MatrixView<const T> in,
        oc::MatrixView<T> out,
        coproto::Socket& chl)
    {
        return apply<u8>(op, matrixCast<const u8>(in), matrixCast<u8>(out), chl);
    }

    static_assert(std::is_move_constructible<ComposedPerm>::value, "ComposedPerm is missing its move ctor");
    static_assert(std::is_move_assignable<ComposedPerm>::value, "ComposedPerm is missing its move ctor");



    inline void genPerm(Perm& p, ComposedPerm& p0, ComposedPerm& p1, u64 sizeBytes, PRNG& prng)
    {

        // pi = sp1 o sp0
        // sp1 = pi o sp0^-1
        Perm sp0(p.size(), prng);
        Perm sp1 = p.compose(sp0.inverse());

        std::array<PermCorSender, 2> sendPerms;
        std::array<PermCorReceiver, 2> recvPerms;

        genPerm(sp0, sendPerms[0], recvPerms[0], sizeBytes, prng);
        genPerm(sp1, sendPerms[1], recvPerms[1], sizeBytes, prng);

        p0 = ComposedPerm(0, std::move(sendPerms[0]), std::move(recvPerms[1]));
        p1 = ComposedPerm(1, std::move(sendPerms[1]), std::move(recvPerms[0]));
    }


    inline void validate(const Perm& pi, const ComposedPerm& comPerm0, const ComposedPerm& comPerm1)
    {
        if (comPerm0.mPartyIdx != 0)
            throw RTE_LOC;
        if (comPerm1.mPartyIdx != 1)
            throw RTE_LOC;

        if (pi != comPerm1.permShare().compose(comPerm0.permShare()))
            throw RTE_LOC;

        validate(comPerm0.mPermSender, comPerm1.mPermReceiver);
        validate(comPerm1.mPermSender, comPerm0.mPermReceiver);
    }


}