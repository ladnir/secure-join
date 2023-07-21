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
        Perm mPerm;
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
            , mPerm(std::move(perm))
        {}
        ComposedPerm(u64 n, u8 partyIdx, PRNG& prng)
            : mPartyIdx(partyIdx)
            , mPerm(n, prng)
        {}

        void setKeyOts(
            oc::block& key,
            std::vector<oc::block>& rk,
            std::vector<std::array<oc::block, 2>>& sk)
        {
            mSender.setKeyOts(sk);
            mReceiver.setKeyOts(key, rk);
        }

        macoro::task<> genKeyOts(OleGenerator& ole, coproto::Socket& chl)
        {
            MC_BEGIN(macoro::task<>, this, &ole, &chl);

            if ((int)ole.mRole)
            {
                MC_AWAIT(
                    macoro::when_all_ready(
                        mSender.genKeyOts(ole, chl),
                        mReceiver.genKeyOts(ole, chl))
                );
            }
            else
            {
                MC_AWAIT(
                    macoro::when_all_ready(
                        mReceiver.genKeyOts(ole, chl),
                        mSender.genKeyOts(ole, chl))
                );
            }
            MC_END();
        }

        u64 size() const
        {
            return mPerm.size();
        }

        void init(u64 n, u8 partyIdx, PRNG& prng)
        {
            mPartyIdx = partyIdx;
            mPerm.randomize(n, prng);
        }

        macoro::task<> preprocess(
            u64 n,
            u64 bytesPer,
            coproto::Socket& chl,
            OleGenerator& ole,
            PRNG& prng)
        {
            MC_BEGIN(macoro::task<>, this, &ole, &chl, &prng, n, bytesPer,
                chl2 = coproto::Socket{}
            );
            chl2 = chl.fork();
            if ((int)ole.mRole)
            {
                MC_AWAIT(
                    macoro::when_all_ready(
                        mSender.preprocess(n, bytesPer, prng, chl, ole),
                        mReceiver.preprocess(n, bytesPer, prng, chl2, ole))
                );
            }
            else
            {
                MC_AWAIT(
                    macoro::when_all_ready(
                        mReceiver.preprocess(n, bytesPer, prng, chl, ole),
                        mSender.preprocess(n, bytesPer, prng, chl2, ole))
                );
            }
            MC_END();
        }

        template<typename T>
        macoro::task<> apply(
            oc::MatrixView<const T> in,
            oc::MatrixView<T> out,
            coproto::Socket& chl,
            OleGenerator& ole,
            bool inv = false)
        {
            if (out.rows() != in.rows() ||
                out.cols() != in.cols())
                throw RTE_LOC;

            if (out.rows() != mPerm.size())
                throw RTE_LOC;

            if (mPartyIdx > 1)
                throw RTE_LOC;

            MC_BEGIN(macoro::task<>, in, out, &chl, &ole, inv,
                prng = oc::PRNG(ole.mPrng.get()),
                this,
                soutperm = oc::Matrix<T>{}
            );

            soutperm.resize(in.rows(), in.cols());
            if ((inv ^ bool(mPartyIdx)) == true)
            {
                if (mIsSecure)
                {
                    MC_AWAIT(mReceiver.apply<T>(in, soutperm, prng, chl, ole));
                    MC_AWAIT(mSender.apply<T>(mPerm, soutperm, out, prng, chl, inv, ole));
                }
                else
                {
                    MC_AWAIT(InsecurePerm::apply<T>(in, soutperm, prng, chl, ole));
                    MC_AWAIT(InsecurePerm::apply<T>(mPerm, soutperm, out, prng, chl, inv, ole));
                }
            }
            else
            {
                if (mIsSecure)
                {
                    MC_AWAIT(mSender.apply<T>(mPerm, in, soutperm, prng, chl, inv, ole));
                    MC_AWAIT(mReceiver.apply<T>(soutperm, out, prng, chl, ole));
                }
                else
                {
                    MC_AWAIT(InsecurePerm::apply<T>(mPerm, in, soutperm, prng, chl, inv, ole));
                    MC_AWAIT(InsecurePerm::apply<T>(soutperm, out, prng, chl, ole));
                }
            }

            MC_END();
        }

    };

}