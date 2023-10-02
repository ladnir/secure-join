#include "ComposedPerm.h"

namespace secJoin
{

    void ComposedPerm::setKeyOts(oc::block& key, std::vector<oc::block>& rk, std::vector<std::array<oc::block, 2>>& sk)
    {
        mSender.setKeyOts(sk);
        mReceiver.setKeyOts(key, rk);
    }

    void ComposedPerm::init2(u8 partyIdx, u64 n, u64 bytesPer, macoro::optional<bool> dlpnKeyGen)
    {
        mPartyIdx = partyIdx;
        mSender.init(n, bytesPer, dlpnKeyGen);
        mReceiver.init(n, bytesPer, dlpnKeyGen);
    }

    void ComposedPerm::request(CorGenerator& ole)
    {
        mSender.request(ole);
    }

    void ComposedPerm::setBytePerRow(u64 bytesPer)
    {
        mSender.setBytePerRow(bytesPer);
        mReceiver.setBytePerRow(bytesPer);
    }

    macoro::task<> ComposedPerm::preprocess(
        coproto::Socket& chl, PRNG& prng_)
    {
        MC_BEGIN(macoro::task<>, this, &chl,
            prng = oc::PRNG(prng_.get<oc::block>()),
            //ole2 = CorGenerator{},
            chl2 = coproto::Socket{ },
            prng2 = prng_.fork(),
            t0 = macoro::task<>{},
            t1 = macoro::task<>{}
        );

        if (hasPermutation())
            throw std::runtime_error("ComposedPerm permutation share has not been set. " LOCATION);
            
        chl2 = chl.fork();

        if (mPartyIdx)
        {
            t0 = mSender.setup(prng, chl);
            t1 = mReceiver.setup(prng2, chl2);
        }
        else
        {
            t0 = mReceiver.setup(prng, chl );
            t1 = mSender.setup(prng2, chl2);
        }

        MC_AWAIT(macoro::when_all_ready(std::move(t0), std::move(t1)));

        MC_END();
    }


    template<>
    macoro::task<> ComposedPerm::apply<u8>(
        PermOp op,
        oc::MatrixView<const u8> in,
        oc::MatrixView<u8> out,
        coproto::Socket& chl,
        oc::PRNG& prng
        )
    {
        MC_BEGIN(macoro::task<>, in, out, &chl, op, &prng,
            this,
            soutperm = oc::Matrix<u8>{}
        );

        if (out.rows() != in.rows() ||
            out.cols() != in.cols())
            throw RTE_LOC;

        if (out.rows() != size())
            throw RTE_LOC;

        if (mPartyIdx > 1)
            throw RTE_LOC;

        if(hasPermutation() == false)
            throw std::runtime_error("permutation has not been set. " LOCATION);

        if (mSender.hasSetup(in.cols()) == false && mSender.hasRequest() == false)
            throw std::runtime_error("preprocessing has not been requested. Call request() before. " LOCATION);

        soutperm.resize(in.rows(), in.cols());
        if (((op == PermOp::Inverse) ^ bool(mPartyIdx)) == true)
        {
            if (mIsSecure)
            {
                MC_AWAIT(mReceiver.apply<u8>(op, in, soutperm, prng, chl));
                MC_AWAIT(mSender.apply<u8>(op, soutperm, out, prng, chl));
            }
            else
            {
                MC_AWAIT(InsecurePerm::apply<u8>(in, soutperm, prng, chl));
                MC_AWAIT(InsecurePerm::apply<u8>(*mSender.mPi, op, soutperm, out, prng, chl));
            }
        }
        else
        {
            if (mIsSecure)
            {
                MC_AWAIT(mSender.apply<u8>(op, in, soutperm, prng, chl));
                MC_AWAIT(mReceiver.apply<u8>(op, soutperm, out, prng, chl));
            }
            else
            {
                MC_AWAIT(InsecurePerm::apply<u8>(*mSender.mPi, op, in, soutperm, prng, chl));
                MC_AWAIT(InsecurePerm::apply<u8>(soutperm, out, prng, chl));
            }
        }

        MC_END();
    }
}
