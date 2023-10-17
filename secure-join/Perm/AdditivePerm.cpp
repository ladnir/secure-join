#include "AdditivePerm.h"

namespace secJoin
{
    TODO("optimize AdditivePerm so that mRandPi o mRho are the shares of ComposedPerm");


    //AdditivePerm::AdditivePerm(span<u32> shares, PRNG& prng, u8 partyIdx) : mRandPi(shares.size(), partyIdx, prng)
    //{
    //    mShare.resize(shares.size());
    //    std::copy(shares.begin(), shares.end(), (u32*)mShare.data());
    //}

    void AdditivePerm::init2(u8 partyIdx, u64 size, u64 bytesPer, macoro::optional<bool> AltModKeyGen)
    {
        mIsSetup = false;
        mRandPi.init2(partyIdx, size, bytesPer ? bytesPer + 4 : 0, AltModKeyGen);
    }

    // generate the masking (replicated) permutation mRandPi
    // and then reveal mRhoPP = mRandPi(mShares).
    //
    // We can then apply our main permutation (mShares)
    // or an input vector x by computing
    //
    // t = mRho(x)
    // y =  mRandPi^-1(t) 
    //   = (mRandPi^-1 o mRho)(x) 
    //   = (mRandPi^-1 o mRandPi o mShares)(x)
    //   =                mShares (x)
    //
    // mRho is public and mRandPi is replicated so we
    // have protocols for both.
    //
    macoro::task<> AdditivePerm::setup(
        coproto::Socket& chl,
        PRNG& prng)
    {
        MC_BEGIN(macoro::task<>, this, &chl, &prng,
            rho1 = oc::Matrix<u32>{},
            rho2 = oc::Matrix<u32>{},
            ss = std::vector<u32>{},
            i = u64{});

        if (mInsecureMock)
        {
            rho1.resize(mShare.size(), 1);
            MC_AWAIT(chl.send(coproto::copy(mShare)));
            MC_AWAIT(chl.recv(rho1));

            mRho.mPi = mShare;
            for (u64 i = 0;i < mRho.size(); ++i)
                mRho.mPi[i] ^= rho1(i);

            mIsSetup = true;
            MC_RETURN_VOID();
        }

        if (mRandPi.hasPermutation() == false)
            mRandPi.samplePermutation(prng);


        TODO("change to apply reveal");
        // rho1 will resized() and initialed in the apply function
        rho1.resize(mShare.size(), 1);
        MC_AWAIT(mRandPi.apply<u32>(
            PermOp::Regular,
            oc::MatrixView<u32>(mShare.data(), mShare.size(), 1),
            rho1, chl, prng));

        // Exchanging the [Rho]
        if (mRandPi.mPartyIdx == 0)
        {
            // First party first sends the [rho] and then receives it
            MC_AWAIT(chl.send(coproto::copy(rho1)));

            rho2.resize(rho1.rows(), rho1.cols());
            MC_AWAIT(chl.recv(rho2));
        }
        else
        {
            // Second party first receives the [rho] and then sends it
            rho2.resize(rho1.rows(), rho1.cols());
            MC_AWAIT(chl.recv(rho2));

            MC_AWAIT(chl.send(coproto::copy(rho1)));
        }

        // Constructing Rho
        if (mShare.size() != rho2.rows())
            throw RTE_LOC;

        if (mShare.size() != rho1.rows())
            throw RTE_LOC;

        mRho.mPi.resize(rho1.rows());

        for (i = 0; i < rho1.rows(); ++i)
        {
            mRho.mPi[i] = *(u32*)rho1.data(i) ^ *(u32*)rho2.data(i);
            // #ifndef NDEBUG
            //                     if (mRho[i] >= size())
            //                     {
            //                         ss.resize(mShare.size());
            //                         MC_AWAIT(chl.send(coproto::copy(mShare)));
            //                         MC_AWAIT(chl.recv(ss));
            //
            //
            //                         for (u64 j = 0; j < size(); ++j)
            //                         {
            //                             if ((ss[j] ^ mShare[j]) > size())
            //                                 throw RTE_LOC;
            //                         }
            //                     }
            // #endif

            assert(mRho[i] < size());
        }

        mIsSetup = true;

        MC_END();
    }


    macoro::task<> AdditivePerm::apply(
        PermOp op, 
        BinMatrix& in, 
        BinMatrix& out, 
        PRNG& prng, 
        coproto::Socket& chl
        )
    {
        if (in.cols() != oc::divCeil(in.bitsPerEntry(), 8))
            throw RTE_LOC;
        if (out.cols() != oc::divCeil(out.bitsPerEntry(), 8))
            throw RTE_LOC;
        return apply<u8>(op, in.mData, out.mData, prng, chl);
    }

    macoro::task<> AdditivePerm::compose(
        AdditivePerm& pi,
        AdditivePerm& dst,
        PRNG& prng,
        coproto::Socket& chl)
    {
        if (pi.size() != size())
            throw RTE_LOC;
        
        dst.clearPermutation();

        if (dst.size() != size())
            dst.init2(mRandPi.mPartyIdx, size());

        if (dst.mRandPi.mSender.mRecver.hasKeyOts() == false)
        {
            auto send = mRandPi.mSender.mRecver.getKeyOts();
            auto recv = mRandPi.mReceiver.mSender.getKeyOts();
            auto k = mRandPi.mReceiver.mSender.getKey();

            dst.setKeyOts(k, recv, send);
        }

        dst.mShare.resize(dst.size());
        return apply<u32>(PermOp::Regular, pi.mShare, dst.mShare, prng, chl);
    }

    //macoro::task<> AdditivePerm::composeSwap(
    //    AdditivePerm& pi,
    //    AdditivePerm& dst,
    //    PRNG& prng,
    //    coproto::Socket& chl,
    //    CorGenerator& gen)
    //{
    //    if (pi.size() != size())
    //        throw RTE_LOC;

    //    dst.clearPermutation();
    //    if (dst.size() != size())
    //        dst.init(size(), 0, mRandPi.mPartyIdx);

    //    if (dst.mRandPi.mSender.mRecver.hasKeyOts() == false)
    //    {
    //        auto send = mRandPi.mSender.mRecver.getKeyOts();
    //        auto recv = mRandPi.mReceiver.mSender.getKeyOts();
    //        auto k = mRandPi.mReceiver.mSender.getKey();

    //        dst.setKeyOts(k, recv, send);
    //    }

    //    return pi.apply<u32>(PermOp::Regular, mShare, dst.mShare, prng, chl, gen);
    //}

    void AdditivePerm::request(CorGenerator& ole)
    {
        mRandPi.request(ole);
    }

    macoro::task<> AdditivePerm::preprocess()
    {
        return mRandPi.preprocess();
    }
}