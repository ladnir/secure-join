#pragma once
#include "secure-join/Perm/LowMCPerm.h"
#include "secure-join/Perm/InsecurePerm.h"
#include "secure-join/GMW/Gmw.h"
#include "secure-join/Perm/AltModPerm.h"

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
        AltModPermSender mSender;

        // The permutation protocol for the other share.
        AltModPermReceiver mReceiver;

        // A flag that skips the actual protocol and insecurely permutes the data.
        bool mIsSecure = true;

        ComposedPerm() = default;
        ComposedPerm(const ComposedPerm&) = default;
        ComposedPerm(ComposedPerm&&) noexcept = default;
        ComposedPerm& operator=(const ComposedPerm&) = default;
        ComposedPerm& operator=(ComposedPerm&&) noexcept = default;

        //initializing the permutation
        ComposedPerm(Perm perm, u8 partyIdx, u64 rowSize = 0)
        {
            init2(partyIdx, perm.size(), rowSize);
            mSender.setPermutation(std::move(perm));
        }

        // initializing with a random permutation.
        ComposedPerm(u64 n, u8 partyIdx, PRNG& prng, u64 rowSize = 0)
        {
            init2(partyIdx, n, rowSize);
            samplePermutation(prng);
        }

        // set the AltMod permutation protocol key OTs. These should be AltMod::KeySize OTs in both directions.
        void setKeyOts(
            AltModPrf::KeyType& key,
            std::vector<oc::block>& rk,
            std::vector<std::array<oc::block, 2>>& sk);

        // the size of the permutation that is being shared.
        u64 size() const { return mSender.mNumElems; }

        // initialize the permutation to have the given size.
        // partyIdx should be in {0,1}, n is size, bytesPer can be
        // set to how many bytes the user wants to permute. AltModKeyGen
        // can be set if the user wants to control if the AltMod kets 
        // should be sampled or not.
        void init2(u8 partyIdx, u64 n, u64 bytesPer = 0, macoro::optional<bool> AltModKeyGen = {});

        // Clear the set permutations and any correlated randomness 
        // that is assoicated to them.
        void clearPermutation() 
        {
            mSender.clearPermutation();
            mReceiver.clearPermutation();
        }

        // returns true if there is permutation setup that has not been derandomized
        // to a user chosen permutation.
        bool hasRandomSetup() const { return mSender.hasRandomSetup(); }

        //returns true if we have requested correlated randomness
        bool hasRequest() const { return mSender.hasRequest(); }

        // return true if the permutation share has been set.
        bool hasPermutation() const { return mSender.mPi; }

        // Samples a random permutation share.
        void samplePermutation(PRNG& prng) {
            if (!size())
                throw std::runtime_error("init must be called first");
            Perm perm(size(), prng);
            mSender.setPermutation(std::move(perm));
        }

        // request the required correlated randomness. init() must be called first.
        void request(
            CorGenerator& ole);

        // request the required correlated randomness. init() must be called first.
        void setBytePerRow(u64 bytesPer);

        // Generate the required correlated randomness.
        macoro::task<> preprocess();

        // generate the permutation correlation
        macoro::task<> setup(
            coproto::Socket& chl,
            PRNG& prng);

        // permute the input data by the secret shared permutation. op
        // control if the permutation is applied directly or its inverse.
        // in/out are the input and output shared. Correlated randomness 
        // must have been requested using request().
        template<typename T>
        macoro::task<> apply(
            PermOp op,
            oc::MatrixView<const T> in,
            oc::MatrixView<T> out,
            coproto::Socket& chl,
            PRNG& prng
        );


        void clear()
        {
            mPartyIdx = -1;
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
        PRNG& prng);

    template<typename T>
    macoro::task<> ComposedPerm::apply(
        PermOp op,
        oc::MatrixView<const T> in,
        oc::MatrixView<T> out,
        coproto::Socket& chl,
        PRNG& prng)
    {
        return apply<u8>(op, matrixCast<const u8>(in), matrixCast<u8>(out), chl, prng);
    }

    static_assert(std::is_move_constructible<ComposedPerm>::value, "ComposedPerm is missing its move ctor");
    static_assert(std::is_move_assignable<ComposedPerm>::value, "ComposedPerm is missing its move ctor");

}