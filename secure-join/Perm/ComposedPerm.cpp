#include "ComposedPerm.h"

namespace secJoin
{

    void ComposedPerm::setKeyOts(oc::block& key, std::vector<oc::block>& rk, std::vector<std::array<oc::block, 2>>& sk)
    {
        mSender.setKeyOts(sk);
        mReceiver.setKeyOts(key, rk);
    }
    macoro::task<> ComposedPerm::genKeyOts(OleGenerator& ole, coproto::Socket& chl)
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
    void ComposedPerm::init(u64 n, u8 partyIdx, PRNG& prng)
    {
        mPartyIdx = partyIdx;
        mPi.randomize(n, prng);
    }
    macoro::task<> ComposedPerm::preprocess(u64 n, u64 bytesPer, coproto::Socket& chl, OleGenerator& ole, PRNG& prng)
    {
        MC_BEGIN(macoro::task<>, this, &ole, &chl, &prng, n, bytesPer,
            chl2 = coproto::Socket{ }
        );

        if (mPi.size() != n)
            throw RTE_LOC;

        mSender.setPermutation(mPi);
        //chl2 = chl.fork();
        TODO("parallel");
        if ((int)ole.mRole)
        {
            //MC_AWAIT(
            //    macoro::when_all_ready(
            //        mSender.preprocess(n, bytesPer, prng, chl, ole),
            //        mReceiver.preprocess(n, bytesPer, prng, chl2, ole))
            //);
            MC_AWAIT(mSender.setup(mPi, bytesPer, prng, chl, ole));
            MC_AWAIT(mReceiver.setup(n, bytesPer, prng, chl, ole));
        }
        else
        {
            //MC_AWAIT(
            //    macoro::when_all_ready(
            //        mReceiver.preprocess(n, bytesPer, prng, chl, ole),
            //        mSender.preprocess(n, bytesPer, prng, chl2, ole))
            //);

            MC_AWAIT(mReceiver.setup(n, bytesPer, prng, chl, ole));
            MC_AWAIT(mSender.setup(mPi, bytesPer, prng, chl, ole));
        }
        MC_END();
    }


    template<>
    macoro::task<> ComposedPerm::apply<u8>(
        PermOp op,
        oc::MatrixView<const u8> in,
        oc::MatrixView<u8> out,
        coproto::Socket& chl,
        OleGenerator& ole)
    {
        if (out.rows() != in.rows() ||
            out.cols() != in.cols())
            throw RTE_LOC;

        if (out.rows() != mPi.size())
            throw RTE_LOC;

        if (mPartyIdx > 1)
            throw RTE_LOC;

        MC_BEGIN(macoro::task<>, in, out, &chl, &ole, op,
            prng = oc::PRNG(ole.mPrng.get()),
            this,
            soutperm = oc::Matrix<u8>{}
        );

        if (mSender.hasSetup(in.cols()) == false)
            mSender.setPermutation(mPi);

        soutperm.resize(in.rows(), in.cols());
        if (((op == PermOp::Inverse) ^ bool(mPartyIdx)) == true)
        {
            if (mIsSecure)
            {
                MC_AWAIT(mReceiver.apply<u8>(op, in, soutperm, prng, chl, ole));
                MC_AWAIT(mSender.apply<u8>(op, soutperm, out, prng, chl, ole));
            }
            else
            {
                MC_AWAIT(InsecurePerm::apply<u8>(in, soutperm, prng, chl, ole));
                MC_AWAIT(InsecurePerm::apply<u8>(mPi, op, soutperm, out, prng, chl, ole));
            }
        }
        else
        {
            if (mIsSecure)
            {
                MC_AWAIT(mSender.apply<u8>(op, in, soutperm, prng, chl, ole));
                MC_AWAIT(mReceiver.apply<u8>(op, soutperm, out, prng, chl, ole));
            }
            else
            {
                MC_AWAIT(InsecurePerm::apply<u8>(mPi, op, in, soutperm, prng, chl, ole));
                MC_AWAIT(InsecurePerm::apply<u8>(soutperm, out, prng, chl, ole));
            }
        }

        MC_END();
    }
}
