#include "DLpnPerm.h"

namespace secJoin
{

    void DLpnPermReceiver::setKeyOts(oc::block& key, std::vector<oc::block>& rk)
    {
        mSender.setKey(key);
        mSender.setKeyOts(rk);
    }

    void DLpnPermSender::setKeyOts(std::vector<std::array<oc::block, 2>>& sk)
    {
        mRecver.setKeyOts(sk);
    }

    macoro::task<> DLpnPermReceiver::genKeyOts(OleGenerator& ole, coproto::Socket&chl)
    {
        return mSender.genKeyOts(ole,chl);
    }

    macoro::task<> DLpnPermSender::genKeyOts(OleGenerator& ole, coproto::Socket& chl)
    {
        return mRecver.genKeyOts(ole, chl);
    }


    void xorShare(oc::MatrixView<const oc::block> v1_,
        oc::MatrixView<const oc::u8> v2,
        oc::MatrixView<oc::u8>& s)
    {
        // Checking the dimensions
        //if (v1.rows() != v2.rows())
        //    throw RTE_LOC;
        //if (v1.cols() != v2.cols())
        //    throw RTE_LOC;
        oc::MatrixView<const u8> v1((u8*)v1_.data(), v1_.rows(), v1_.cols() * sizeof(oc::block));
        for (oc::u64 i = 0; i < s.rows(); ++i)
            for (oc::u64 j = 0; j < s.cols(); ++j)
                s(i, j) = v1(i, j) ^ v2(i, j);
    }

    void xorShare(oc::MatrixView<const u8> v1,
        oc::MatrixView<const oc::u8> v2,
        oc::MatrixView<oc::u8>& s)
    {
        // Checking the dimensions
        if (v1.rows() != v2.rows())
            throw RTE_LOC;
        if (v1.cols() != v2.cols())
            throw RTE_LOC;
        for (oc::u64 i = 0; i < v1.size(); ++i)
            s(i) = v1(i) ^ v2(i);
    }

    // generate the preprocessing when all inputs are unknown.
    macoro::task<> DLpnPermSender::preprocess(
        u64 totElements,
        u64 bytesPerRow,
        oc::PRNG& prng,
        coproto::Socket& chl,
        OleGenerator& ole)
    {
        mHasPreprocess = true;
        mPrePerm.randomize(totElements, prng);
        return setup(mPrePerm, bytesPerRow, prng, chl, false, ole);
    }

    // generate the preprocessing when all inputs are unknown.
    macoro::task<> DLpnPermReceiver::preprocess(
        u64 totElements,
        u64 bytesPerRow,
        oc::PRNG& prng,
        coproto::Socket& chl,
        OleGenerator& ole)
    {
        mHasPreprocess = true;
        return setup(totElements, bytesPerRow, prng, chl, ole);
    }

    // DLpn Receiver calls this setup
    // generate random mDelta such that
    // mDelta ^ mB = pi(mA)
    macoro::task<> DLpnPermSender::setup(
        const Perm& pi,
        u64 bytesPerRow,
        oc::PRNG& prng,
        coproto::Socket& chl,
        bool invPerm,
        OleGenerator& ole)
    {
        MC_BEGIN(macoro::task<>, &pi, &chl, &prng, &ole, bytesPerRow, this, invPerm,
            //aesPlaintext = oc::Matrix<oc::block>(),
            aesCipher = oc::Matrix<oc::block>(),
            //dlpnCipher = oc::Matrix<oc::block>(),
            blocksPerRow = u64(),
            totElements = u64(),
            aes = oc::AES(),
            key = oc::block());


        totElements = pi.mPerm.size();
        blocksPerRow = oc::divCeil(bytesPerRow, sizeof(oc::block));

        key = prng.get();
        aes.setKey(key);

        if(mRecver.hasKeyOts())
            mRecver.tweakKeyOts(key);
        MC_AWAIT(chl.send(std::move(key)));
        // if(mRecver.hasKeyOts() == false)
        //     MC_AWAIT(mRecver.genKeyOts(ole));

        // Encryption starts here
        //aesPlaintext.resize(totElements, blocksPerRow);
        aesCipher.resize(totElements, blocksPerRow);
        //dlpnCipher.resize(totElements, blocksPerRow);

        mDelta.resize(totElements, blocksPerRow);
        for (u64 i = 0; i < totElements; i++)
        {
            for (u64 j = 0; j < blocksPerRow; j++)
            {
                if (!invPerm)
                {
                    auto srcIdx = pi[i] * blocksPerRow + j;
                    aesCipher(i, j) = oc::block(0, srcIdx);
                }
                else
                {
                    auto srcIdx = i * blocksPerRow + j;
                    aesCipher(pi[i], j) = oc::block(0, srcIdx);
                }
            }
        }
        aes.ecbEncBlocks(aesCipher, aesCipher);

        MC_AWAIT(mRecver.evaluate(aesCipher, mDelta, chl, prng, ole));

        //for (u64 i = 0; i < totElements; i++)
        //    memcpyMin(mDelta[i], dlpnCipher[i]);

        MC_END();
    }

    // DLpn Receiver calls this setup
    // generate random mA, mB such that
    // mDelta ^ mB = pi(mA)
    macoro::task<> DLpnPermReceiver::setup(
        u64 totElements,
        u64 bytesPerRow,
        oc::PRNG& prng,
        coproto::Socket& chl,
        OleGenerator& ole)
    {

        MC_BEGIN(macoro::task<>, &chl, &prng, &ole, totElements, bytesPerRow, this,
            aesPlaintext = oc::Matrix<oc::block>(),
            aesCipher = oc::Matrix<oc::block>(),
            preProsdlpnCipher = oc::Matrix<oc::block>(),
            dlpnCipher = oc::Matrix<oc::block>(),
            blocksPerRow = u64(),
            aes = oc::AES(),
            key = oc::block());

        blocksPerRow = oc::divCeil(bytesPerRow, sizeof(oc::block));

        MC_AWAIT(chl.recv(key));

        if (mSender.hasKeyOts())
            mSender.tweakKeyOts(key);

        // B = (DLPN(k,AES(k', pi(0))), ..., (DLPN(k,AES(k', pi(n-1)))))
        mB.resize(totElements, blocksPerRow);
        MC_AWAIT(mSender.evaluate(mB, chl, prng, ole));

        // A = (DLPN(k,AES(k', 0)), ..., (DLPN(k,AES(k', n-1))))
        aes.setKey(key);

        mA.resize(totElements, blocksPerRow);
        for (u64 i = 0; i < mA.size(); i++)
            mA(i) = oc::block(0, i);
        aes.ecbEncBlocks(mA, mA);
        mSender.mPrf.eval(mA, mA);

        ////////////////////////////////////////

        //blocksPerRow = oc::divCeil(bytesPerRow, sizeof(oc::block));

        //// Calculating a from the ppt
        //aesPlaintext.resize(totElements, blocksPerRow);
        //aesCipher.resize(totElements, blocksPerRow);
        //dlpnCipher.resize(totElements, blocksPerRow);
        //preProsdlpnCipher.resize(totElements, blocksPerRow);

        //for (u64 i = 0; i < aesPlaintext.size(); i++)
        //    aesPlaintext(i) = oc::block(0, i);


        //MC_AWAIT(chl.recv(key));

        //if (mSender.hasKeyOts())
        //    mSender.tweakKeyOts(key);
        //aes.setKey(key);
        //aes.ecbEncBlocks(aesPlaintext, aesCipher);

        //MC_AWAIT(mSender.evaluate(dlpnCipher, chl, prng, ole));

        //for (u64 i = 0; i < aesCipher.rows(); i++)
        //{
        //    for (u64 j = 0; j < aesCipher.cols(); j++)
        //    {
        //        preProsdlpnCipher(i, j) = mSender.mPrf.eval(aesCipher(i, j));
        //    }
        //}

        //// Placing a in sout[0]
        //mA.resize(totElements, bytesPerRow);
        //for (u64 i = 0; i < totElements; i++)
        //    memcpyMin(mA[i], preProsdlpnCipher[i]);

        //// Placing [y] in sout[1]
        //mB.resize(totElements, bytesPerRow);

        //for (u64 i = 0; i < totElements; i++)
        //    memcpyMin(mB[i], dlpnCipher[i]);
        MC_END();
    }

    macoro::task<> DLpnPermReceiver::validateShares(coproto::Socket& sock)
    {
        assert(hasSetup());
        MC_BEGIN(macoro::task<>, this, &sock);
            
        MC_AWAIT(sock.send(mA));
        MC_AWAIT(sock.send(mB));

        MC_END();
    }

    macoro::task<> DLpnPermSender::validateShares(coproto::Socket& sock, const Perm& p)
    {
        assert(hasSetup());
        MC_BEGIN(macoro::task<>, this, &sock, &p,
            A = oc::Matrix<oc::block>{},
            B = oc::Matrix<oc::block>{}
            );

        A.resize(mDelta.rows(), mDelta.cols());
        B.resize(mDelta.rows(), mDelta.cols());
        MC_AWAIT(sock.recv(A));
        MC_AWAIT(sock.recv(B));

        for (u64 i = 0; i < p.size(); ++i)
        {
            for (u64 j = 0; j < A.cols(); ++j)
            {
                if ((B(i, j) ^ mDelta(i, j)) != A(p[i], j))
                    throw RTE_LOC;
            }
        }

        MC_END();
    }

    template <>
    macoro::task<> DLpnPermSender::apply<u8>(
        const Perm& pi,
        oc::MatrixView<u8> sout,
        oc::PRNG& prng,
        coproto::Socket& chl,
        bool invPerm,
        OleGenerator& ole)
    {
        MC_BEGIN(macoro::task<>, &pi, &chl, &prng, &ole, this, sout, invPerm,
            xEncrypted = oc::Matrix<u8>(),
            xPermuted = oc::Matrix<u8>(),
            totElements = u64(),
            bytesPerRow = u64(),
            delta = Perm{}
        );
        totElements = pi.mPerm.size();
        bytesPerRow = sout.cols();

        if (hasSetup() == false)
            MC_AWAIT(setup(pi, sout.cols(), prng, chl, invPerm, ole));

        if (mDelta.rows() != totElements)
            throw RTE_LOC;
        if (mDelta.cols() * sizeof(oc::block) < bytesPerRow)
            throw RTE_LOC;

        if (mHasPreprocess)
        {
            // delta = pi o pre^-1
            // they are going to update their correlation using delta
            // to translate it to a correlation of pi.
            delta = invPerm ?
                pi.compose(mPrePerm) :
                pi.inverse().compose(mPrePerm)
                ;
            MC_AWAIT(chl.send(std::move(delta.mPerm)));

            mHasPreprocess = false;
        }

        xPermuted.resize(totElements, sout.cols());
        xEncrypted.resize(totElements, sout.cols());

        MC_AWAIT(chl.recv(xEncrypted));

        for (u64 i = 0; i < totElements; ++i)
        {
            if (!invPerm)
            {
                memcpy(xPermuted[i], xEncrypted[pi[i]]);
            }
            else
            {
                memcpy(xPermuted[pi[i]], xEncrypted[i]);
            }
        }

        xorShare(mDelta, xPermuted, sout);
        mDelta.resize(0, 0);

        MC_END();
    }

    // If DLPN receiver only wants to call apply
    // when it also has inputs
    // this will internally call setup for it
    template <>
    macoro::task<> DLpnPermSender::apply<u8>(
        const Perm& pi,
        oc::MatrixView<const u8> in,
        oc::MatrixView<u8> sout,
        oc::PRNG& prng,
        coproto::Socket& chl,
        bool invPerm,
        OleGenerator& ole)
    {
        MC_BEGIN(macoro::task<>, &pi, &chl, &prng, &ole, this, sout, invPerm, in,
            xPermuted = oc::Matrix<u8>(),
            soutPerm = oc::Matrix<u8>());

        xPermuted.resize(in.rows(), in.cols());
        soutPerm.resize(sout.rows(), sout.cols());

        MC_AWAIT(apply(pi, soutPerm, prng, chl, invPerm, ole));
        // MC_AWAIT(setup(pi, bytesPerRow, prng, chl, ole, invPerm));
        // MC_AWAIT(apply(pi, soutPerm, bytesPerRow, chl, invPerm));

        pi.apply<u8>(in, xPermuted, invPerm);
        xorShare(xPermuted, soutPerm, sout);

        MC_END();
    }

    // If DLPN sender only wants to call apply
    // this will internally call setup for it
    template <>
    macoro::task<> DLpnPermReceiver::apply<u8>(
        oc::MatrixView<const u8> input,
        oc::MatrixView<u8> sout,
        oc::PRNG& prng,
        coproto::Socket& chl,
        OleGenerator& ole)
    {
        MC_BEGIN(macoro::task<>, &chl, &prng, &ole, this, input, sout,
            totElements = u64(),
            bytesPerRow = u64(),
            xEncrypted = oc::Matrix<u8>(),
            delta = Perm{});

        totElements = input.rows();
        bytesPerRow = input.cols();
        if (hasSetup() == false)
            MC_AWAIT(setup(totElements, bytesPerRow, prng, chl, ole));

        if (mA.rows() != totElements)
            throw RTE_LOC;
        if (mA.cols() * sizeof(oc::block) < bytesPerRow)
            throw RTE_LOC;

        if (mHasPreprocess)
        {
            // we current have the correlation 
            // 
            //          mDelta ^ mB  = pre(mA)
            //   pre^-1(mDelta ^ mB) = mA
            // 
            // if we multiply both sides by (pi^-1 o pre) we get
            // 
            //   (pi^-1 o pre)( pre^-1(mDelta ^ mB)) = (pi^-1 o pre) (mA)
            //   (pi^-1 o pre o pre^-1)(mDelta ^ mB)) = (pi^-1 o pre) (mA)
            //   (pi^-1)(mDelta ^ mB)) = (pi^-1 o pre)(mA)
            //   mDelta ^ mB = pi((pi^-1 o pre)(mA))
            //   mDelta ^ mB = pi(mA')
            // 
            // where mA' = (pi^-1 o pre)(mA)
            //           = delta(mA)
            // 
            delta.mPerm.resize(totElements);
            MC_AWAIT(chl.recv(delta.mPerm));

            {
                oc::Matrix<oc::block> AA(mA.rows(), mA.cols());
                delta.apply<oc::block>(mA, AA);
                std::swap(mA, AA);
            }

            mHasPreprocess = false;
        }

        // MC_AWAIT(apply(input, sout, chl));
        //totElements = input.rows();
        xEncrypted.resize(input.rows(), input.cols());

        xorShare(mA, input, xEncrypted);

        MC_AWAIT(chl.send(std::move(xEncrypted)));

        for (u64 i = 0; i < totElements; ++i)
            memcpyMin(sout[i], mB[i]);

        mA.resize(0, 0);
        mB.resize(0, 0);
        MC_END();
    }
}