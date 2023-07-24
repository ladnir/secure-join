#include "AdditivePerm.h"

namespace secJoin
{

    AdditivePerm::AdditivePerm(span<u32> shares, PRNG& prng, u8 partyIdx) : mPi(shares.size(), partyIdx, prng)
    {
        mShare.resize(shares.size());
        std::copy(shares.begin(), shares.end(), (u32*)mShare.data());
    }

    void AdditivePerm::init(u64 size)
    {
        mShare.resize(size);
        //mPi.mPi.mPi.resize(size);
        //mRho.mPi.resize(size);
        mIsSetup = false;
    }

    //void AdditivePerm::senderSetup(oc::block& key, std::vector<oc::block>& rk)
    //{
    //    mPi.senderSetup(key, rk);
    //}

    //void AdditivePerm::receiverSetup(std::vector<std::array<oc::block, 2>>& sk)
    //{
    //    mPi.receiverSetup(sk);
    //}

    //macoro::task<> AdditivePerm::senderSetup(OleGenerator& ole)
    //{
    //    return (mPi.senderSetup(ole));
    //}

    //macoro::task<> AdditivePerm::receiverSetup(OleGenerator& ole)
    //{
    //    return (mPi.receiverSetup(ole));
    //}

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
    macoro::task<> AdditivePerm::setup(
        coproto::Socket& chl,
        OleGenerator& ole,
        PRNG& prng)
    {
        MC_BEGIN(macoro::task<>, this, &chl, &ole, &prng,
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

        if (mPi.size() == 0)
            mPi.init(mShare.size(), (int)ole.mRole, prng);


        TODO("change to apply reveal");
        // rho1 will resized() and initialed in the apply function
        std::cout << (int)ole.mRole << " A::setup apply " << std::endl;
        rho1.resize(mShare.size(), 1);
        MC_AWAIT(mPi.apply<u32>(
            PermOp::Regular,
            oc::MatrixView<u32>(mShare.data(), mShare.size(), 1),
            rho1, chl, ole));

        std::cout << (int)ole.mRole << "A::setup exchange " << std::endl;

        // Exchanging the [Rho]
        if (mPi.mPartyIdx == 0)
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

        // std::cout << "Rho1 Rows " << rho1.rows() << std::endl;
        // std::cout << "Rho1 Cols " << rho1.cols() << std::endl;

        // std::cout << "Size of one row is " << sizeof(*(u32*)rho1(0)) << std::endl;
        // std::cout << "Value of zero row is " << *(u32*)rho1.data(0) << std::endl;

        {
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
#ifndef NDEBUG
            mRho.validate();
#endif
        }

        mIsSetup = true;
        std::cout << (int)ole.mRole << " A::setup done " << std::endl;

        MC_END();
    }


    macoro::task<> AdditivePerm::apply(PermOp op, BinMatrix& in, BinMatrix& out, oc::PRNG& prng, coproto::Socket& chl, OleGenerator& ole)
    {
        if (in.cols() != oc::divCeil(in.bitsPerEntry(), 8))
            throw RTE_LOC;
        if (out.cols() != oc::divCeil(out.bitsPerEntry(), 8))
            throw RTE_LOC;
        return apply<u8>(op, in.mData, out.mData, prng, chl, ole);
    }

    macoro::task<> AdditivePerm::composeSwap(
        AdditivePerm& pi,
        AdditivePerm& dst,
        oc::PRNG& prng,
        coproto::Socket& chl,
        OleGenerator& gen)
    {
        if (pi.size() != size())
            throw RTE_LOC;
        // dst.init(p2.size(), p2.mPi.mPartyIdx, gen);
        dst.init(size());
        return pi.apply<u32>(PermOp::Regular, mShare, dst.mShare, prng, chl, gen);
    }
    macoro::task<> AdditivePerm::preprocess(u64 n, u64 bytesPer, coproto::Socket& chl, OleGenerator& ole, oc::PRNG& prng)
    {
        TODO("redice the size of possible");
        mPi.init(n, (int)ole.mRole, prng);
        return mPi.preprocess(n, bytesPer + sizeof(u32), chl, ole, prng);
    }
}