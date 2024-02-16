//#include "AltModPerm.h"
//#include "secure-join/Util/Matrix.h"
//namespace secJoin
//{
//
//    void AltModPermReceiver::setKeyOts(AltModPrf::KeyType& key, std::vector<oc::block>& rk)
//    {
//        mSender.setKeyOts(key, rk);
//    }
//
//    void AltModPermSender::setKeyOts(std::vector<std::array<oc::block, 2>>& sk)
//    {
//        mRecver.setKeyOts(sk);
//    }
//
//    namespace {
//
//        void xorShare(
//            oc::MatrixView<const oc::block> v1_,
//            u64 byteOffset,
//            oc::MatrixView<const oc::u8> v2,
//            oc::MatrixView<oc::u8>& s)
//        {
//            auto v1 = matrixCast<const u8>(v1_);
//            auto r = s.rows();
//            auto c = s.cols();
//            auto d = s.data();
//            auto d2 = v2.data();
//            for (oc::u64 i = 0; i < r; ++i)
//            {
//                auto d1 = v1.data(i) + byteOffset;
//                for (oc::u64 j = 0; j < c; ++j)
//                    *d++ = *d1++ ^ *d2++;
//            }
//        }
//
//        void xorShare(oc::MatrixView<const u8> v1,
//            oc::MatrixView<const oc::u8> v2,
//            oc::MatrixView<oc::u8>& s)
//        {
//            // Checking the dimensions
//            if (v1.rows() != v2.rows())
//                throw RTE_LOC;
//            if (v1.cols() != v2.cols())
//                throw RTE_LOC;
//            for (oc::u64 i = 0; i < v1.size(); ++i)
//                s(i) = v1(i) ^ v2(i);
//        }
//    }
//
//    // generate the preprocessing when all inputs are unknown.
//    void AltModPermSender::request(CorGenerator& ole)
//    {
//        if (mNumElems == 0)
//            throw std::runtime_error("AltModPermSender::init must be called before request. " LOCATION);
//        if (mByteOffset == 0)
//            throw std::runtime_error("the number of bytes per element is not set. call setBytePerRow(...). " LOCATION);
//        if (hasRequest())
//            throw std::runtime_error("the correlated randomness has already been requested. " LOCATION);
//
//        mRecver.init(oc::divCeil(mBytesPerRow, sizeof(oc::block)) * mNumElems, mKeyGen);
//        mRecver.request(ole);
//    }
//
//    void AltModPermReceiver::request(CorGenerator& ole)
//    {
//        if (mNumElems == 0)
//            throw std::runtime_error("AltModPermReceiver::init must be called before request. " LOCATION);
//        if (mByteOffset == 0)
//            throw std::runtime_error("the number of bytes per element is not set. call setBytePerRow(...). " LOCATION);
//        if (hasRequest())
//            throw std::runtime_error("the correlated randomness has already been requested. " LOCATION);
//
//        mSender.init(oc::divCeil(mBytesPerRow, sizeof(oc::block)) * mNumElems, mKeyGen);
//        mSender.request(ole);
//    }
//
//    macoro::task<> AltModPermReceiver::preprocess()
//    {
//        if (hasRequest() == false)
//            throw std::runtime_error("AltModPermReceiver::request() must be called before AltModPermReceiver::preprocess() " LOCATION);
//
//        return mSender.preprocess();
//    }
//
//    macoro::task<> AltModPermSender::preprocess()
//    {
//        if (hasRequest() == false)
//            throw std::runtime_error("AltModPermReceiver::request() must be called before AltModPermSender::preprocess() " LOCATION);
//
//        return mRecver.preprocess();
//    }
//
//
//
//    // initialize this sender to have a permutation of size n, where 
//    // bytesPerRow bytes can be permuted per position. keyGen can be 
//    // set if the caller wants to explicitly ask to perform AltMod keygen or not.
//    void AltModPermSender::init(u64 n, u64 bytesPerRow, macoro::optional<bool> keyGen)
//    {
//        //clear();
//        mNumElems = n;
//        mBytesPerRow = bytesPerRow;
//        mByteOffset = bytesPerRow;
//        mKeyGen = keyGen;
//        clearCorrelatedRandomness();
//    }
//
//    bool AltModPermSender::hasPreprocessing()const
//    {
//        return mRecver.hasPreprocessing();
//    }
//
//    // clears the internal state
//    void AltModPermSender::clear()
//    {
//        mRecver.clear();
//        mHasRandomSetup = false;
//        mPrePerm.clear();
//        mPi = nullptr;
//        mPermStorage.clear();
//        mDelta.resize(0, 0);
//        mNumElems = 0;
//        mBytesPerRow = 0;
//        mByteOffset = 0;
//    }
//
//    // sets the permutation of the sender
//    void AltModPermSender::setPermutation(Perm&& p)
//    {
//        mPermStorage = std::move(p);
//        setPermutation(mPermStorage);
//    }
//
//    // sets the permutation of the sender
//    void AltModPermSender::setPermutation(const Perm& p)
//    {
//        if (p.size() != mNumElems)
//            throw std::runtime_error("setPermutation called with the wrong size permutation. " LOCATION);
//
//        if (mPi)
//            throw std::runtime_error("setPermutation was called when there is already a permutation set. " LOCATION);
//
//        mPi = &p;
//    }
//
//    struct SetupMeta
//    {
//        oc::block mKey;
//        u8 mPrepro;
//    };
//    // AltMod Receiver calls this setup
//    // generate random mDelta such that
//    // mDelta ^ mB = pi(mA)
//    macoro::task<> AltModPermSender::setup(
//        PRNG& prng,
//        coproto::Socket& chl)
//    {
//        MC_BEGIN(macoro::task<>, &chl, &prng, this,
//            aesCipher = oc::Matrix<oc::block>(),
//            blocksPerRow = u64{},
//            aes = oc::AES(),
//            meta = SetupMeta(),
//            pi = (const Perm*)nullptr,
//            debugB = oc::Matrix<oc::block>{},
//            debugInput = oc::Matrix<oc::block>{},
//            debugKey = AltModPrf::KeyType{});
//
//        if (mNumElems == 0)
//            throw std::runtime_error("AltModPermSender::init() must be called before setup. " LOCATION);
//
//        if (hasRequest() == false)
//            throw std::runtime_error("AltModPermSender::request() must be called before setup. " LOCATION);
//
//        if (hasPreprocessing() == false)
//        {
//            MC_AWAIT(preprocess());
//        }
//
//        //if(hasPreprocessing())
//
//        if (mPi)
//        {
//            mHasRandomSetup = false;
//            pi = mPi;
//        }
//        else
//        {
//            mHasRandomSetup = true;
//            mPrePerm.randomize(mNumElems, prng);
//            pi = &mPrePerm;
//        }
//
//        if (pi->size() != mNumElems)
//            throw std::runtime_error("the setup perm has not been initialized or is the wrong size. " LOCATION);
//
//
//        blocksPerRow = oc::divCeil(mBytesPerRow, sizeof(oc::block));
//        mByteOffset = 0;
//
//        meta.mPrepro = mHasRandomSetup;
//        meta.mKey = prng.get();
//        aes.setKey(meta.mKey);
//
//        MC_AWAIT(chl.send(std::move(meta)));
//
//        mDelta.resize(mNumElems, blocksPerRow);
//        for (u64 i = 0; i < mNumElems; i++)
//        {
//            for (u64 j = 0; j < blocksPerRow; j++)
//            {
//                auto srcIdx = pi->mPi[i] * blocksPerRow + j;
//                mDelta(i, j) = oc::block(0, srcIdx);
//            }
//        }
//        aes.ecbEncBlocks(mDelta, mDelta);
//
//        if (mDebug)
//            debugInput = mDelta;
//
//        MC_AWAIT(mRecver.evaluate(mDelta, mDelta, chl, prng));
//
//        if (mDebug)
//        {
//            debugB.resize(mDelta.rows(), mDelta.cols());
//            MC_AWAIT(chl.recv(debugKey));
//            MC_AWAIT(chl.recv(debugB));
//
//            {
//                AltModPrf prf;
//                prf.setKey(debugKey);
//                for (u64 i = 0; i < debugB.size();++i)
//                {
//                    if ((mDelta(i) ^ debugB(i)) != prf.eval(debugInput(i)))
//                        throw RTE_LOC;
//                }
//            }
//        }
//
//        MC_END();
//    }
//
//    // AltMod Receiver calls this setup
//    // generate random mA, mB such that
//    // mDelta ^ mB = pi(mA)
//    macoro::task<> AltModPermReceiver::setup(
//        PRNG& prng,
//        coproto::Socket& chl)
//    {
//        MC_BEGIN(macoro::task<>, &chl, &prng, this,
//            aesPlaintext = oc::Matrix<oc::block>(),
//            aesCipher = oc::Matrix<oc::block>(),
//            preProsAltModCipher = oc::Matrix<oc::block>(),
//            AltModCipher = oc::Matrix<oc::block>(),
//            blocksPerRow = u64(),
//            aes = oc::AES(),
//            meta = SetupMeta());
//
//        if (mNumElems == 0)
//            throw std::runtime_error("AltModPermReceiver::init() must be called before setup. " LOCATION);
//
//        if (hasRequest() == false)
//            throw std::runtime_error("AltModPermReceiver::request() must be called before setup. " LOCATION);
//
//        if (hasPreprocessing() == false)
//        {
//            MC_AWAIT(preprocess());
//        }
//
//        blocksPerRow = oc::divCeil(mBytesPerRow, sizeof(oc::block));
//        mByteOffset = 0;
//
//        MC_AWAIT(chl.recv(meta));
//
//        mHasRandomSetup = meta.mPrepro;
//        //if (mSender.hasKeyOts())
//        //    mSender.tweakKeyOts(meta.mKey);
//
//        // B = (AltMod(k,AES(k', pi(0))), ..., (AltMod(k,AES(k', pi(n-1)))))
//        mB.resize(mNumElems, blocksPerRow);
//        MC_AWAIT(mSender.evaluate(mB, chl, prng));
//
//        if (mDebug)
//        {
//            MC_AWAIT(chl.send(std::move(mSender.getKey())));
//            MC_AWAIT(chl.send(coproto::copy(mB)));
//        }
//
//        // A = (AltMod(k,AES(k', 0)), ..., (AltMod(k,AES(k', n-1))))
//        aes.setKey(meta.mKey);
//
//        mA.resize(mNumElems, blocksPerRow);
//        for (u64 i = 0; i < mA.size(); i++)
//            mA(i) = oc::block(0, i);
//        aes.ecbEncBlocks(mA, mA);
//        mSender.mPrf.eval(mA, mA);
//
//        MC_END();
//    }
//
//    macoro::task<> AltModPermReceiver::validateShares(coproto::Socket& sock)
//    {
//        //assert(hasSetup());
//        MC_BEGIN(macoro::task<>, this, &sock);
//
//        MC_AWAIT(sock.send(mA));
//        MC_AWAIT(sock.send(mB));
//
//        MC_END();
//    }
//
//    macoro::task<> AltModPermSender::validateShares(coproto::Socket& sock, Perm p)
//    {
//        //assert(hasSetup());
//        MC_BEGIN(macoro::task<>, this, &sock, p,
//            A = oc::Matrix<oc::block>{},
//            B = oc::Matrix<oc::block>{}
//        );
//
//        A.resize(mDelta.rows(), mDelta.cols());
//        B.resize(mDelta.rows(), mDelta.cols());
//        MC_AWAIT(sock.recv(A));
//        MC_AWAIT(sock.recv(B));
//
//        for (u64 i = 0; i < p.size(); ++i)
//        {
//            for (u64 j = 0; j < A.cols(); ++j)
//            {
//                if ((B(i, j) ^ mDelta(i, j)) != A(p[i], j))
//                    throw RTE_LOC;
//            }
//        }
//
//        MC_END();
//    }
//
//    template <>
//    macoro::task<> AltModPermSender::apply<u8>(
//        PermOp op,
//        oc::MatrixView<u8> sout,
//        PRNG& prng,
//        coproto::Socket& chl)
//    {
//        MC_BEGIN(macoro::task<>, &chl, &prng, this, sout, op,
//            xEncrypted = oc::Matrix<u8>(),
//            xPermuted = oc::Matrix<u8>(),
//            delta = Perm{}
//        );
//
//        if (mNumElems == 0)
//            throw std::runtime_error("init() has not been called." LOCATION);
//
//        if (mNumElems != sout.rows())
//            throw std::runtime_error("output rows does not match init(). " LOCATION);
//
//        if (mPi == nullptr)
//            throw std::runtime_error("permutation has not been set." LOCATION);
//
//        if (mNumElems != mPi->mPi.size())
//            throw RTE_LOC;
//
//        if (hasSetup(sout.cols()) == false)
//            MC_AWAIT(setup(prng, chl));
//
//        if (mDelta.rows() != mNumElems)
//            throw RTE_LOC;
//
//        if (mHasRandomSetup)
//        {
//            // delta = pi o pre^-1
//            // they are going to update their correlation using delta
//            // to translate it to a correlation of pi.
//            delta = //op == PermOp::Inverse ?
//                //mPi->compose(mPrePerm) :
//                mPi->inverse().compose(mPrePerm)
//                ;
//            MC_AWAIT(chl.send(std::move(delta.mPi)));
//            //MC_AWAIT(validateShares(chl, /*mInverse  ? mPi->inverse() : */*mPi));
//
//            mHasRandomSetup = false;
//        }
//
//        xPermuted.resize(mNumElems, sout.cols());
//        xEncrypted.resize(mNumElems, sout.cols());
//
//        MC_AWAIT(chl.recv(xEncrypted));
//
//        if (op == PermOp::Regular)
//        {
//            mPi->apply<u8>(xEncrypted, xPermuted, op);
//
//            xorShare(mDelta, mByteOffset, xPermuted, sout);
//            mByteOffset += sout.cols();
//        }
//        else
//        {
//            xorShare(mDelta, mByteOffset, xEncrypted, xEncrypted);
//            mPi->apply<u8>(xEncrypted, sout, op);
//            mByteOffset += sout.cols();
//        }
//        //mDelta.resize(0, 0);
//
//        if (remainingSetup() == false)
//            clearCorrelatedRandomness();
//
//        MC_END();
//    }
//
//
//
//    // If AltMod receiver only wants to call apply
//    // when it also has inputs
//    // this will internally call setup for it
//    template <>
//    macoro::task<> AltModPermSender::apply<u8>(
//        PermOp op,
//        oc::MatrixView<const u8> in,
//        oc::MatrixView<u8> sout,
//        PRNG& prng,
//        coproto::Socket& chl)
//    {
//        MC_BEGIN(macoro::task<>, &chl, &prng, this, sout, in, op,
//            xPermuted = oc::Matrix<u8>());
//        if (!mPi)
//            throw std::runtime_error("permutation has not been set. " LOCATION);
//        xPermuted.resize(in.rows(), in.cols());
//
//        MC_AWAIT(apply(op, sout, prng, chl));
//
//        mPi->apply<u8>(in, xPermuted, op);
//        xorShare(xPermuted, sout, sout);
//
//        MC_END();
//    }
//
//    // If AltMod sender only wants to call apply
//    // this will internally call setup for it
//    template <>
//    macoro::task<> AltModPermReceiver::apply<u8>(
//        PermOp op,
//        oc::MatrixView<const u8> input,
//        oc::MatrixView<u8> sout,
//        PRNG& prng,
//        coproto::Socket& chl)
//    {
//        MC_BEGIN(macoro::task<>, &chl, &prng, this, input, sout, op,
//            xEncrypted = oc::Matrix<u8>(),
//            delta = Perm{});
//
//        if (input.rows() != sout.rows() || input.cols() != sout.cols())
//            throw std::runtime_error("input output size mismatch. " LOCATION);
//
//        if (mNumElems == 0)
//            throw std::runtime_error("init has not been called. " LOCATION);
//
//        if (mNumElems != input.rows())
//            throw std::runtime_error("input rows do not match init(n). " LOCATION);
//
//        if (hasSetup(input.cols()) == false)
//            MC_AWAIT(setup(prng, chl));
//
//        if (mA.rows() != mNumElems)
//            throw RTE_LOC;
//        if (mA.cols() * sizeof(oc::block) < input.cols())
//            throw RTE_LOC;
//
//        if (mHasRandomSetup)
//        {
//            // we current have the correlation 
//            // 
//            //          mDelta ^ mB  = pre(mA)
//            //   pre^-1(mDelta ^ mB) = mA
//            // 
//            // if we multiply both sides by (pi^-1 o pre) we get
//            // 
//            //   (pi^-1 o pre)( pre^-1(mDelta ^ mB)) = (pi^-1 o pre) (mA)
//            //   (pi^-1 o pre o pre^-1)(mDelta ^ mB)) = (pi^-1 o pre) (mA)
//            //   (pi^-1)(mDelta ^ mB)) = (pi^-1 o pre)(mA)
//            //   mDelta ^ mB = pi((pi^-1 o pre)(mA))
//            //   mDelta ^ mB = pi(mA')
//            // 
//            // where mA' = (pi^-1 o pre)(mA)
//            //           = delta(mA)
//            // 
//            delta.mPi.resize(mNumElems);
//            MC_AWAIT(chl.recv(delta.mPi));
//
//            {
//                oc::Matrix<oc::block> AA(mA.rows(), mA.cols());
//                delta.apply<oc::block>(mA, AA);
//                std::swap(mA, AA);
//            }
//            //MC_AWAIT(validateShares(chl));
//            mHasRandomSetup = false;
//        }
//
//        // MC_AWAIT(apply(input, sout, chl));
//        xEncrypted.resize(input.rows(), input.cols());
//
//        if (op == PermOp::Regular)
//        {
//            xorShare(mA, mByteOffset, input, xEncrypted);
//            MC_AWAIT(chl.send(std::move(xEncrypted)));
//            for (u64 i = 0; i < mNumElems; ++i)
//                memcpy(sout.data(i), (u8*)mB.data(i) + mByteOffset, sout.cols());
//        }
//        else
//        {
//            xorShare(mB, mByteOffset, input, xEncrypted);
//            MC_AWAIT(chl.send(std::move(xEncrypted)));
//            for (u64 i = 0; i < mNumElems; ++i)
//                memcpy(sout.data(i), (u8*)mA.data(i) + mByteOffset, sout.cols());
//        }
//
//        mByteOffset += sout.cols();
//
//        if (remainingSetup() == false)
//            clearCorrelatedRandomness();
//
//        MC_END();
//    }
//}