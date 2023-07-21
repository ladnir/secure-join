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
        mPi.mPerm.mPerm.resize(size);
        mRho.mPerm.resize(size);
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

            mRho.mPerm = mShare;
            for (u64 i = 0;i < mRho.size(); ++i)
                mRho.mPerm[i] ^= rho1(i);

            mIsSetup = true;
            MC_RETURN_VOID();
        }

        mPi.init(mShare.size(), (int)ole.mRole, prng);

        // rho1 will resized() and initialed in the apply function
        rho1.resize(mShare.size(), 1);
        MC_AWAIT(mPi.apply<u32>(
            oc::MatrixView<u32>(mShare.data(), mShare.size(), 1),
            rho1, chl, ole, false));

        // Exchanging the [Rho]
        if (mPi.mPartyIdx == 0)
        {
            // First party first sends the [rho] and then receives it
            MC_AWAIT(chl.send(rho1));

            rho2.resize(rho1.rows(), rho1.cols());
            MC_AWAIT(chl.recv(rho2));
        }
        else
        {
            // Second party first receives the [rho] and then sends it
            rho2.resize(rho1.rows(), rho1.cols());
            MC_AWAIT(chl.recv(rho2));

            MC_AWAIT(chl.send(rho1));
        }

        // Constructing Rho
        if (mShare.size() != rho2.rows())
            throw RTE_LOC;

        if (mShare.size() != rho1.rows())
            throw RTE_LOC;

        mRho.mPerm.resize(rho1.rows());

        // std::cout << "Rho1 Rows " << rho1.rows() << std::endl;
        // std::cout << "Rho1 Cols " << rho1.cols() << std::endl;

        // std::cout << "Size of one row is " << sizeof(*(u32*)rho1(0)) << std::endl;
        // std::cout << "Value of zero row is " << *(u32*)rho1.data(0) << std::endl;

        {
            for (i = 0; i < rho1.rows(); ++i)
            {
                mRho.mPerm[i] = *(u32*)rho1.data(i) ^ *(u32*)rho2.data(i);
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
        }

        mIsSetup = true;

        MC_END();
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
        return pi.apply<u32>(mShare, dst.mShare, prng, chl, gen);
    }
}