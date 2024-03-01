#include "AltModComposedPerm.h"

namespace secJoin
{

    void AltModComposedPerm::preprocess()
    {
        mPermSender.preprocess();
        mPermReceiver.preprocess();
    }

    macoro::task<> AltModComposedPerm::generate(
        coproto::Socket& chl,
        PRNG& prng_,
        Perm perm,
        ComposedPerm& dst)
    {
        if (mPermSender.mPrfRecver.mInputSize == 0)
            throw RTE_LOC;
        MC_BEGIN(macoro::task<>, this, &chl, p = std::move(perm), &dst,
            prng = PRNG(prng_.get<oc::block>()),
            chl2 = coproto::Socket{ },
            prng2 = prng_.fork(),
            t0 = macoro::task<>{},
            t1 = macoro::task<>{}
        );

#ifndef NDEBUG
        p.validate();
#endif

        chl2 = chl.fork();
        dst.mPartyIdx = mPartyIdx;

        if (mPartyIdx)
        {
            t0 = mPermSender.generate(std::move(p), prng, chl, dst.mPermSender);
            t1 = mPermReceiver.generate(prng2, chl2, dst.mPermReceiver);
        }
        else
        {
            t0 = mPermReceiver.generate(prng2, chl, dst.mPermReceiver);
            t1 = mPermSender.generate(std::move(p), prng, chl2, dst.mPermSender);
        }

        MC_AWAIT(macoro::when_all_ready(std::move(t0), std::move(t1)));

        MC_END();
    }
}
