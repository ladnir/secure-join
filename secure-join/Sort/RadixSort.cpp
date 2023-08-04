#include "RadixSort.h"
#include "secure-join/Sort/BitInjection.h"

namespace secJoin
{


    macoro::task<> RadixSort::checkHadamardSum(
        BinMatrix& f,
        Matrix32& s,
        span<u32> dst,
        coproto::Socket& comm,
        bool additive)
    {

        MC_BEGIN(macoro::task<>, &f, &s, dst, &comm, additive,
            ff = BinMatrix{},
            ss = Matrix32{},
            dd = std::vector<u32>{},
            exp = std::vector<u32>{},
            fIter = oc::BitIterator{}
        );
        MC_AWAIT(comm.send(coproto::copy(f)));
        MC_AWAIT(comm.send(coproto::copy(s)));
        MC_AWAIT(comm.send(coproto::copy(dst)));

        ff.resize(f.rows(), f.bitsPerEntry());
        ss.resize(s.rows(), s.cols());
        dd.resize(dst.size());

        MC_AWAIT(comm.recv(ff));
        MC_AWAIT(comm.recv(ss));
        MC_AWAIT(comm.recv(dd));

        for (u64 i = 0; i < ff.size(); ++i)
            ff(i) ^= f(i);

        for (u64 i = 0; i < ss.size(); ++i)
            ss(i) += s(i);

        for (u64 i = 0; i < dd.size(); ++i)
        {
            if (additive)
                dd[i] += dst[i];
            else
                dd[i] ^= dst[i];
        }

        exp.resize(dd.size());
        fIter = oc::BitIterator(ff.data());
        for (u64 i = 0; i < dd.size(); ++i)
        {
            exp[i] = 0;
            for (u64 j = 0; j < ss.cols(); ++j)
                exp[i] += *fIter++ * ss(i, j);
        }

        for (u64 i = 0; i < exp.size(); ++i)
            if (exp[i] != dd[i])
                throw RTE_LOC;
        MC_END();
    }

    macoro::task<> RadixSort::hadamardSumPreprocess(
        u64 size,
        CorGenerator& gen,
        coproto::Socket&sock, 
        OtRecvGenerator&recv, 
        OtSendGenerator&send,
        oc::PRNG&prng)
    {
        //std::cout << (int)gen.mRole << " had pre" << std::endl;
        MC_BEGIN(macoro::task<>, this, &gen, &sock, &recv, &send, size, &prng);

        //std::cout << (int)gen.mRole << " had start" << std::endl;
        if (gen.mRole == CorGenerator::Role::Sender)
        {
            TODO("parallel");
            MC_AWAIT(gen.otRecvRequest(recv, size, sock, prng));
            MC_AWAIT(gen.otSendRequest(send, size, sock.fork(), prng));
        }
        else
        {
            MC_AWAIT(gen.otSendRequest(send, size, sock, prng));
            MC_AWAIT(gen.otRecvRequest(recv, size, sock.fork(), prng));
        }

        //std::cout << (int)gen.mRole << " had finished" << std::endl;

        MC_END();
    }

    // compute dst = sum_i f.col(i) * s.col(i) where * 
    // is the hadamard (component-wise) product. 
    macoro::task<> RadixSort::hadamardSum(
        u64 round,
        BinMatrix& f,
        Matrix32& s,
        AdditivePerm& dst,
        coproto::Socket& comm)
    {
        MC_BEGIN(macoro::task<>, this, &f, &s, &dst, &comm, round,

            otRecvReq = OtRecvGenerator{},
            otSendReq = OtSendGenerator{},
            otRecv = OtRecv{},
            otSend = OtSend{},
            a = std::vector<oc::AlignedUnVector<u32>>{},
            shares = std::vector<u32>{},
            diff = oc::BitVector{},
            //A = Matrix32{},
            //B = Matrix32{},
            i = u64{},
            m = u64{},
            role = u64{},
            r = u64{},
            tt = std::vector<u32>{},
            dd = std::vector<u32>{},
            fIter = (block*)nullptr,
            bitCount = u64{}
        );

        // A = f + a
        // B = s + b
        //A.resize(f.rows(), f.cols());
        //B.resize(f.rows(), f.cols());

        shares.resize(s.rows());

        otRecvReq = std::move(mHadamardSumRecvOts[round]);
        otSendReq = std::move(mHadamardSumSendOts[round]);
        //if (gen.mRole == CorGenerator::Role::Sender)
        //{
        //    MC_AWAIT_SET(otRecvReq, gen.otRecvRequest(s.size()));
        //    MC_AWAIT_SET(otSendReq, gen.otSendRequest(s.size()));
        //}
        //else
        //{
        //    MC_AWAIT_SET(otSendReq, gen.otSendRequest(s.size()));
        //    MC_AWAIT_SET(otRecvReq, gen.otRecvRequest(s.size()));
        //}

        //initArith2BinCircuit(shares.size());
        //mArithToBinGmw[round].init(shares.size(), mArith2BinCir);

        for (role = 0; role < 2; ++role)
        {
            if (role ^ mRole)
            {
                fIter = (block*)f.data();
                // recv
                for (i = 0; i < s.size();)
                {
                    MC_AWAIT(otRecvReq.get(otRecv));
                    m = std::min<u64>(s.size() - i, otRecv.size());

                    for (u64 j = 0; j < m; ++j, ++i)
                    {
                        auto row = i / s.cols();
                        u8 fi = *oc::BitIterator((u8*)fIter, i);
                        otRecv.mChoice[j] = otRecv.mChoice[j] ^ fi;
                        shares[row] += otRecv.mMsg[j].get<u32>(0);
                    }

                    otRecv.mChoice.resize(m);
                    MC_AWAIT(comm.send(std::move(otRecv.mChoice)));
                }

                for (i = 0, r = 0; i < f.size();)
                {
                    MC_AWAIT(comm.recvResize(tt));
                    m = std::min<u64>(s.size() - i, tt.size());
                    for (u64 j = 0; j < m; ++j, ++i)
                    {
                        auto row = i / s.cols();
                        u8 fi = *oc::BitIterator((u8*)fIter, i);
                        shares[row] += tt[j] * fi;
                    }
                }
            }
            else
            {
                fIter = (block*)f.data();
                // send
                for (i = 0, r = 0; i < f.size();)
                {
                    MC_AWAIT(otSendReq.get(otSend));
                    m = std::min<u64>(s.size() - i, otSend.size());
                    diff.resize(m);
                    MC_AWAIT(comm.recv(diff));

                    tt.resize(m);
                    for (u64 j = 0; j < m; ++j, ++i)
                    {
                        auto row = i / s.cols();
                        u8 fi = *oc::BitIterator((u8*)fIter, i);
                        auto d = diff[j];

                        auto m0 = otSend.mMsg[j][0 ^ d].get<u32>(0);
                        auto m1 = otSend.mMsg[j][1 ^ d].get<u32>(0);

                        auto r = m0 - (fi * s(i));
                        //auto v0 = m0;
                        auto v1 = (1 ^ fi) * s(i) + r;
                        tt[j] = v1 - m1;

                        shares[row] -= r;
                    }

                    MC_AWAIT(comm.send(std::move(tt)));
                }
            }
        }

        if (mDebug)
            MC_AWAIT(checkHadamardSum(f, s, shares, comm, true));

        bitCount = std::max<u64>(1, oc::log2ceil(shares.size()));
        if(mArith2BinCir.mInputs.size() == 0 || mArith2BinCir.mInputs[0].size() != bitCount)
            throw RTE_LOC;
        if (mArithToBinGmw.size() <= round)
            throw RTE_LOC;
        if (mArithToBinGmw[round].mN != shares.size())
            throw RTE_LOC;

        mArithToBinGmw[round].setZeroInput(mRole);
        mArithToBinGmw[round].setInput(mRole ^ 1, oc::MatrixView<u32>(shares.data(), shares.size(), 1));

        MC_AWAIT(mArithToBinGmw[round].run(comm));

        dst.mShare.resize(shares.size());
        mArithToBinGmw[round].getOutput(0, oc::MatrixView<u32>(dst.mShare.data(), dst.mShare.size(), 1));

        if (mDebug)
        {

            tt.resize(shares.size());
            dd.resize(shares.size());
            MC_AWAIT(comm.send(coproto::copy(shares)));
            MC_AWAIT(comm.send(coproto::copy(dst.mShare)));
            MC_AWAIT(comm.recv(tt));
            MC_AWAIT(comm.recv(dd));

            for (u64 i = 0; i < shares.size(); ++i)
            {
                tt[i] += shares[i];
                dd[i] ^= dst.mShare[i];

            }

            if (tt != dd)
                throw RTE_LOC;

            MC_AWAIT(checkHadamardSum(f, s, dst.mShare, comm, false));
        }


        MC_END();
    }


    macoro::task<> RadixSort::checkGenValMasks(
        u64 bitCount,
        const BinMatrix& k,
        BinMatrix& f,
        coproto::Socket& comm,
        bool check)
    {

        MC_BEGIN(macoro::task<>, &k, &f, &comm, check,
            n = u64{},
            L = bitCount,
            kk = BinMatrix{},
            ff = BinMatrix{}
        );
        n = k.rows();
        kk.resize(k.rows(), k.cols());
        ff.resize(f.rows(), f.cols());
        MC_AWAIT(comm.send(coproto::copy(k)));
        MC_AWAIT(comm.send(coproto::copy(f)));
        MC_AWAIT(comm.recv(kk));
        MC_AWAIT(comm.recv(ff));

        for (u64 i = 0; i < kk.size(); ++i)
            kk(i) ^= k(i);
        for (u64 i = 0; i < ff.size(); ++i)
            ff(i) ^= f(i);

        if (!check)
        {
            ff.setZero();
        }

        for (u64 j = 0; j < n; ++j)
        {
            auto kj = (u64)kk(j);
            auto iter = oc::BitIterator((u8*)&(ff(j, 0)), 0);

            auto print = [&]() {
                std::lock_guard<std::mutex> ll(oc::gIoStreamMtx);
                std::cout << "exp " << j << " ~ ";
                for (u64 ii = 0; ii < (1ull << L); ++ii)
                    std::cout << ((kj == ii) ? 1 : 0) << " ";

                std::cout << "\nact " << j << " ~ ";
                for (u64 ii = 0; ii < (1ull << L); ++ii)
                    std::cout << *oc::BitIterator((u8*)&(ff(j, 0)), ii) << " ";
                std::cout << "\n";
            };
            print();

            for (u64 i = 0; i < (1ull << L); ++i, ++iter)
            {
                auto exp = (kj == i) ? 1 : 0;

                //auto iter = oc::BitIterator((u8*)&(ff(j, 0)), i);
                if (!check)
                {
                    *iter = exp;
                }
                else
                {




                    u8 fji = *iter;
                    if (fji != exp)
                    {
                        throw RTE_LOC;
                    }
                }
            }
        }

        //if (!check)
        //{
        //	Sh3Encryptor enc;
        //	enc.init(mPartyIdx, oc::block(0, mPartyIdx), oc::block(0, (mPartyIdx + 1) % 3));
        //	if (mPartyIdx == 0)
        //	{
        //		enc.localBinMatrix(comm, ff, f);
        //	}
        //	else
        //	{
        //		enc.remoteBinMatrix(comm, f);
        //	}
        //}
        MC_END();
    }

    macoro::task<> RadixSort::checkGenValMasks(
        u64 L,
        const BinMatrix& k,
        Matrix32& f,
        coproto::Socket& comm)
    {
        MC_BEGIN(macoro::task<>, L, &k, &f, &comm,
            n = u64{},
            kk = BinMatrix{},
            ff = Matrix32{});
        n = k.rows();

        MC_AWAIT(comm.send(coproto::copy(f)));
        MC_AWAIT(comm.send(coproto::copy(k)));

        ff.resize(f.rows(), f.cols());
        kk.resize(k.rows(), k.cols());

        MC_AWAIT(comm.recv(ff));
        MC_AWAIT(comm.recv(kk));

        for (u64 i = 0; i < ff.size(); ++i)
            ff(i) += f(i);
        for (u64 i = 0; i < kk.size(); ++i)
            kk(i) ^= k(i);

        if ((u64)ff.rows() != n)
            throw RTE_LOC;
        if ((u64)ff.cols() != (1ull << L))
            throw RTE_LOC;

        for (u64 i = 0; i < (1ull << L); ++i)
        {
            for (u64 j = 0; j < n; ++j)
            {
                auto kj = (u64)kk(j);
                auto fji = ff(j, i);
                if (kj == i)
                {
                    if (fji != 1)
                        throw RTE_LOC;
                }
                else
                {
                    if (fji != 0)
                    {

                        throw RTE_LOC;
                    }
                }
            }
        }
        MC_END();
    }

    // from each row, we generate a series of sharing flag bits
    // f.col(0) ,..., f.col(n) where f.col(i) is one if k=i.
    // Computes the same function as genValMask but is more efficient
    // due to the use a binary secret sharing.
    macoro::task<> RadixSort::genValMasks2(
        u64 round,
        u64 bitCount,
        const BinMatrix& k,
        Matrix32& f,
        BinMatrix& fBin,
        coproto::Socket& comm)
    {
        MC_BEGIN(macoro::task<>, this, &k, &f, &comm, bitCount, &fBin, round
            //cir = oc::BetaCircuit{}
        );

        if (mRole > 1)
            throw RTE_LOC;
        // we oversized fBin to make sure we have trailing zeros.
        fBin.resize(k.rows() + sizeof(block), 1ull << bitCount, 1, oc::AllocType::Uninitialized);
        fBin.resize(k.rows(), 1ull << bitCount, 1, oc::AllocType::Uninitialized);

        if (bitCount == 1)
        {
            for (u64 i = 0; i < k.rows(); ++i)
            {
                assert(k(i) < 2);
                if (mRole)
                    fBin(i) = (k(i) << 1) | (~k(i) & 1);
                else
                {
                    fBin(i) = (k(i) << 1) | (k(i) & 1);
                }
            }
        }
        else
        {
            //cir = indexToOneHotCircuit(bitCount);
            //if(mOneHotCirBitCount != biTC)
            //initIndexToOneHotCircuit(bitCount);
            //eval.init(k.rows(), mIndexToOneHotCircuit);

            if (mIndexToOneHotGmw.size() <= round)
                throw RTE_LOC;

            mIndexToOneHotGmw[round].setInput(0, k);
            MC_AWAIT(mIndexToOneHotGmw[round].run(comm));
            mIndexToOneHotGmw[round].getOutput(0, fBin);
        }

        if (bitCount == 1)
        {
            auto src = fBin.data();
            auto dst = fBin.data();
            auto main = fBin.rows() / 4;
            for (u64 i = 0; i < main; ++i)
            {
                *dst =
                    ((src[0] & 3) << 0) |
                    ((src[1] & 3) << 2) |
                    ((src[2] & 3) << 4) |
                    ((src[3] & 3) << 6);

                ++dst;
                src += 4;
            }

            for (u64 j = 0; j < (fBin.rows() % 4); ++j)
            {
                *dst = ((src[j] & 3) << (2 * j)) | (bool(j) * (*dst));
            }

            fBin.resize(1, fBin.rows() * 2);
        }
        else if (bitCount == 2)
        {
            auto src = fBin.data();
            auto dst = fBin.data();
            auto main = fBin.rows() / 2;
            for (u64 i = 0; i < main; ++i)
            {
                *dst =
                    ((src[0] & 15) << 0) |
                    ((src[1] & 15) << 4);

                ++dst;
                src += 2;
            }

            for (u64 j = 0; j < (fBin.rows() % 2); ++j)
            {
                *dst = ((src[j] & 15) << (4 * j)) | (bool(j) * (*dst));
            }
            fBin.resize(1, fBin.rows() * 4);
        }
        else
        {
            fBin.resize(1, fBin.rows() * fBin.cols() * 8);
        }

        // we oversized fBin at the start of this fn to make sure we have trailing zeros.
        // here is where we set the zero value.
        memset(fBin.data() + fBin.size(), 0, sizeof(block));

        TODO("determine min bit count required. currently 32");
        if (round >= mBitInjects.size())
            throw RTE_LOC;
        MC_AWAIT(mBitInjects[round].bitInjection((1ull << bitCount) * k.rows(), fBin.mData, 32, f, comm));

        f.reshape(k.rows(), 1ull << bitCount);

        if (mDebug)
            MC_AWAIT(checkGenValMasks(bitCount, k, f, comm));

        MC_END();
    }

    // compute a running sum. replace each element f(i,j) with the sum all previous 
    // columns f(*,1),...,f(*,j-1) plus the elements of f(0,j)+....+f(i-1,j).
    void RadixSort::aggregateSum(const Matrix32& f, Matrix32& s, u64 partyIdx)
    {
        assert(partyIdx < 2);

        auto L2 = f.cols();
        //auto main = L2 / 16 * 16;
        auto m = f.rows();

        std::vector<u32> partialSum;
        partialSum.resize(L2);

        // sum = -1
        partialSum[0] = -partyIdx;

        for (u64 i = 0; i < m; ++i)
        {
            u64 j = 0;
            //auto fi = (block * __restrict) & f(i, 0);
            //auto si = (block * __restrict) & s(i, 0);
            //auto p = (block * __restrict) & partialSum[0];
            //for (; j < main; j += 16)
            //{

            //    p[0] = p[0] + fi[0];
            //    p[1] = p[1] + fi[1];
            //    p[2] = p[2] + fi[2];
            //    p[3] = p[3] + fi[3];
            //    si[0] = p[0];
            //    si[1] = p[1];
            //    si[2] = p[2];
            //    si[3] = p[3];
            //    p += 4;
            //    si += 4;
            //    fi += 4;
            //}

            for (; j < L2; ++j)
            {
                partialSum[j] += f(i, j);
                s(i, j) = partialSum[j];
            }
        }

        u32 prev = 0;
        for (u64 j = 0; j < L2; ++j)
        {
            auto s0 = partialSum[j];
            partialSum[j] = prev;
            prev = prev + s0;
        }

        for (u64 i = 0; i < m; ++i)
        {
            //auto si = (block * __restrict) & s(i, 0);
            //auto p = (block * __restrict) & partialSum[0];
            u64 j = 0;
            //for (; j < main; j += 16)
            //{
            //    si[0] = si[0] + p[0];
            //    si[1] = si[1] + p[1];
            //    si[2] = si[2] + p[2];
            //    si[3] = si[3] + p[3];
            //    p += 4;
            //    si += 4;
            //}

            for (; j < L2; ++j)
            {
                s(i, j) += partialSum[j];
            }
        }

    }


    macoro::task<> RadixSort::checkAggregateSum(
        const Matrix32& f0,
        Matrix32& s0,
        coproto::Socket& comm
    )
    {
        MC_BEGIN(macoro::task<>, &f0, &s0, &comm,
            L2 = u64{},
            m = u64{},
            sum = u32{},
            ff = Matrix32{},
            ss = Matrix32{},
            s = Matrix32{}
        );

        ff.resize(f0.rows(), f0.cols());
        ss.resize(s0.rows(), s0.cols());
        s.resize(s0.rows(), s0.cols());

        MC_AWAIT(comm.send(coproto::copy(f0)));
        MC_AWAIT(comm.send(coproto::copy(s0)));

        MC_AWAIT(comm.recv(ff));
        MC_AWAIT(comm.recv(ss));

        for (u64 i = 0; i < ff.size(); ++i)
            ff(i) += f0(i);
        for (u64 i = 0; i < ss.size(); ++i)
            ss(i) += s0(i);

        L2 = ff.cols();
        m = ff.rows();
        // sum = -1
        sum = -1;

        for (u64 i = 0; i < m; ++i)
        {
            auto w = 0ull;
            for (u64 j = 0; j < L2; ++j)
            {
                w += ff(i, j);
            }
            if (w != 1)
                throw RTE_LOC;
        }

        // sum over column j.
        for (u64 j = 0; j < L2; ++j)
        {
            auto fff = ff.begin() + j;
            auto sss = s.begin() + j;
            for (u64 i = 0; i < m; ++i)
            {
                sum += *fff;
                *sss = sum;
                fff += L2;
                sss += L2;
            }
        }


        for (u64 i = 0; i < s.size(); ++i)
            if (ss(i) != s(i))
            {


                std::cout << "ff " << std::endl;
                for (u64 r = 0; r < ff.rows(); ++r) {
                    for (u64 c = 0; c < ff.cols(); ++c) {
                        std::cout << ff(r, c) << " ";
                    }
                    std::cout << std::endl;
                }
                std::cout << std::endl;
                std::cout << "ss " << std::endl;
                for (u64 r = 0; r < ss.rows(); ++r) {
                    for (u64 c = 0; c < ss.cols(); ++c) {
                        std::cout << (i32)ss(r, c) << " ";
                    }
                    std::cout << std::endl;
                }
                std::cout << std::endl;

                std::cout << "act s " << std::endl;
                for (u64 r = 0; r < ss.rows(); ++r) {
                    for (u64 c = 0; c < ss.cols(); ++c) {
                        std::cout << (i32)s(r, c) << " ";
                    }
                    std::cout << std::endl;
                }
                std::cout << std::endl;

                throw RTE_LOC;
            }

        MC_END();
    }

    // Generate a permutation dst which will be the inverse of the
    // permutation that permutes the keys k into sorted order. 
    macoro::task<> RadixSort::genBitPerm(
        u64 round,
        u64 keyBitCount,
        const BinMatrix& k,
        AdditivePerm& dst,
        coproto::Socket& comm)
    {
        MC_BEGIN(macoro::task<>, this, keyBitCount, &k, &dst, &comm, round,
            m = u64{},
            L = u64{},
            L2 = u64{},
            f = Matrix32{},
            fBin = BinMatrix{},
            s = Matrix32{},
            sk = BinMatrix{},
            p = Perm{}
        );

        if (keyBitCount > k.cols() * 8)
            throw RTE_LOC;

        m = k.rows();
        L = keyBitCount;
        L2 = 1ull << L;
        //dst.init(k.rows(), mPartyIdx);

        f.resize(m, L2);
        s.resize(m, L2);

        MC_AWAIT(genValMasks2(round, keyBitCount, k, f, fBin, comm));


        aggregateSum(f, s, mRole);

        if (mDebug)
            MC_AWAIT(checkAggregateSum(f, s, comm));

        MC_AWAIT(hadamardSum(round, fBin, s, dst, comm));

        if (mDebug)
        {

            assert(k.cols() == 1);

            sk.resize(k.rows(), k.cols());
            MC_AWAIT(comm.send(coproto::copy(k)));
            MC_AWAIT(comm.recv(sk));

            p.mPi.resize(k.rows());
            MC_AWAIT(comm.send(coproto::copy(dst.mShare)));
            MC_AWAIT(comm.recv(p.mPi));

            {

                for (auto i = 0ull; i < k.size(); ++i)
                {
                    sk(i) ^= k(i);
                    p.mPi[i] ^= dst.mShare[i];
                }

                auto genBitPerm = [&](BinMatrix& k) {

                    Perm exp(k.size());
                    std::stable_sort(exp.begin(), exp.end(),
                        [&](const auto& a, const auto& b) {
                            return (k(a) < k(b));
                        });
                    return exp.inverse();
                };
                auto p2 = genBitPerm(sk);

                std::cout << "k ";
                for (auto i = 0ull; i < sk.size(); ++i)
                    std::cout << " " << (int)sk(i);
                std::cout << std::endl;

                if (p2 != p)
                    throw RTE_LOC;
                std::cout << "bitPerm " << p << std::endl;
                //sk = extract(kIdx, mL, k); kIdx += mL;

                //for (auto i = 0ull; i < sk.size(); ++i)
                //    std::cout << "k[" << i << "] " << (int)sk(i) << std::endl;


                //std::vector<Perm> ret;
                //// generate the sorting permutation for the
                //// first L bits of the key.
                //ret.emplace_back(genBitPerm(sk));
                //std::cout << ret.back() << std::endl;
            }
        }

        MC_END();
    }


    // get 'size' columns of k starting at column index 'begin'
    // Assumes 'size <= 8'. 
    BinMatrix RadixSort::extract(u64 begin, u64 size, const BinMatrix& k)
    {
        // we assume at most a byte size.
        if (size > 8)
            throw RTE_LOC;
        size = std::min<u64>(size, k.cols() * 8 - begin);


        auto byteIdx = begin / 8;
        auto shift = begin % 8;
        auto step = k.cols();
        u64 mask = (size % 64) ? (1ull << size) - 1 : ~0ull;
        BinMatrix sk(k.rows(), oc::divCeil(size, 8));

        auto n = k.rows() - 1;
        auto s0 = (k.data() + byteIdx);
        //auto main = (k.size() - byteIdx) / sizeof(u64);

        for (u64 i = 0; i < n; ++i)
        {
            u16 x = *(u16*)s0;
            sk(i) = (x >> shift) & mask;
            s0 += step;
        }

        u16 x = 0;
        auto s = std::min<u64>(2, k.size() - n * step - byteIdx);
        memcpy(&x, s0, s);
        sk(n) = (x >> shift) & mask;

        if (mDebug)
        {
            for (u64 i = 0; i < n; ++i)
            {
                for (u64 j = 0; j < size; ++j)
                {
                    if (*oc::BitIterator((u8*)k[i].data(), begin + j) !=
                        *oc::BitIterator(sk[i].data(), j))
                        throw RTE_LOC;
                }
            }
        }

        return sk;
    }


    macoro::task<std::vector<Perm>> RadixSort::debugGenPerm(
        const BinMatrix& k,
        coproto::Socket& comm)
    {
        MC_BEGIN(macoro::task<std::vector<Perm>>, this, &k, &comm,
            kk = BinMatrix{}
        );

        kk.resize(k.numEntries(), k.bitsPerEntry());
        MC_AWAIT(comm.send(coproto::copy(k)));
        MC_AWAIT(comm.recv(kk));

        {

            for (auto i = 0ull; i < k.size(); ++i)
            {
                kk(i) ^= k(i);
            }

            auto genBitPerm = [&](BinMatrix& k) {

                Perm exp(k.size());
                std::stable_sort(exp.begin(), exp.end(),
                    [&](const auto& a, const auto& b) {
                        return (k(a) < k(b));
                    });
                return exp.inverse();
            };

            auto ll = oc::divCeil(k.bitsPerEntry(), mL);
            auto kIdx = 0;
            auto sk = extract(kIdx, mL, kk); kIdx += mL;

            std::cout << "k 0 { ";
            for (auto i = 0ull; i < sk.size(); ++i)
                std::cout << " " << (int)sk(i);
            std::cout << "}" << std::endl;

            std::vector<Perm> ret;
            // generate the sorting permutation for the
            // first L bits of the key.
            ret.emplace_back(genBitPerm(sk));
            std::cout << ret.back() << std::endl;

            Perm dst = ret.back();
            {
                auto kk2 = kk;
                sk.resize(kk.rows(), kk.cols());
                dst.apply<u8>(kk2, sk, PermOp::Inverse);

                for (u64 j = 1; j < k.rows(); ++j)
                {
                    auto k0 = oc::BitVector((u8*)sk[j - 1].data(),
                        std::min<u64>(kIdx, k.bitsPerEntry()));
                    auto k1 = oc::BitVector((u8*)sk[j].data(),
                        std::min<u64>(kIdx, k.bitsPerEntry()));

                    if (k0 > k1)
                    {
                        std::cout << k0 << std::endl;
                        std::cout << k1 << std::endl;
                        throw RTE_LOC;
                    }
                }
            }
            for (auto i = 1ull; i < ll; ++i)
            {
                // get the next L bits of the key.
                sk = extract(kIdx, mL, kk); kIdx += mL;
                auto ssk = sk;

                std::cout << "k " << i << " { ";
                for (auto i = 0ull; i < sk.size(); ++i)
                    std::cout << " " << (int)sk(i);
                std::cout << "}" << std::endl;

                // apply the partial sort that we have so far 
                // to the next L bits of the key.
                dst.apply<u8>(sk, ssk, PermOp::Inverse);

                // generate the sorting permutation for the
                // next L bits of the key.
                ret.emplace_back(genBitPerm(ssk));
                std::cout << ret.back() << std::endl;

                // composeSwap the current partial sort with
                // the permutation that sorts the next L bits
                dst = ret.back().composeSwap(dst);

                auto kk2 = kk;
                sk.resize(kk2.rows(), kk2.cols());
                dst.apply<u8>(kk2, sk, PermOp::Inverse);

                for (u64 j = 1; j < k.rows(); ++j)
                {
                    auto k0 = oc::BitVector((u8*)sk[j - 1].data(),
                        std::min<u64>(kIdx, k.bitsPerEntry()));
                    auto k1 = oc::BitVector((u8*)sk[j].data(),
                        std::min<u64>(kIdx, k.bitsPerEntry()));

                    if (k0 > k1)
                        throw RTE_LOC;
                }

                //dst.validate(comm);
            }
            std::cout << std::endl;
            std::cout << std::endl;

            MC_RETURN(std::move(ret));
        }
        MC_END();
    }



    macoro::task<> RadixSort::preprocess(
        u64 n,
        u64 bitCount,
        CorGenerator& gen,
        coproto::Socket& comm,
        oc::PRNG& prng)
    {
        MC_BEGIN(macoro::task<>, this, n, bitCount, &gen, &comm, &prng,
            ll = u64{},
            pow2L = u64{},
            i = u64{},
            byteCount = u64{},
            tasks = std::vector<macoro::eager_task<>>{},
            socks = std::vector<coproto::Socket>{},
            parallel = false);



        mRole = (int)gen.mRole;
        ll = oc::divCeil(bitCount, mL);
        mPerms.resize(ll);
        byteCount = oc::divCeil(std::min<u64>(mL, bitCount - i * mL), 8) + 2 * sizeof(u32);
        socks.reserve(ll * 5);

        pow2L = 1ull << mL;
        mArithToBinGmw.reserve(ll);
        mIndexToOneHotGmw.reserve(ll);
        mBitInjects.reserve(ll);
        initIndexToOneHotCircuit(mL);
        initArith2BinCircuit(n);
        assert(mIndexToOneHotCircuit.mOutputs[0].size() == pow2L);
        for (i = 0; i < ll; ++i)
        {
            if (i != ll-1)
            {
                assert(socks.capacity() > socks.size());
                socks.emplace_back(comm.fork());
                tasks.emplace_back(mPerms[i].preprocess(
                    n, byteCount,
                    socks.back(), gen, prng) | macoro::make_eager());

                if (!parallel)
                    MC_AWAIT(tasks.back());
            }

            mBitInjects.emplace_back();
            assert(socks.capacity() > socks.size());
            socks.emplace_back(comm.fork());
            tasks.emplace_back(
                mBitInjects[i].preprocess(n, pow2L, gen, prng, socks.back()) | macoro::make_eager()
            );
            if (!parallel)
                MC_AWAIT(tasks.back());

            mIndexToOneHotGmw.emplace_back();
            mIndexToOneHotGmw.back().init(n, mIndexToOneHotCircuit);
            assert(socks.capacity() > socks.size());
            socks.emplace_back(comm.fork());
            tasks.emplace_back(
                mIndexToOneHotGmw.back().preprocess(gen, socks.back(),prng) | macoro::make_eager()
            );
            if (!parallel)
                MC_AWAIT(tasks.back());

            mArithToBinGmw.emplace_back();
            mArithToBinGmw.back().init(n, mArith2BinCir);
            assert(socks.capacity() > socks.size());
            socks.emplace_back(comm.fork());
            tasks.emplace_back(
                mArithToBinGmw.back().preprocess(gen, socks.back(), prng) | macoro::make_eager()
            );
            if (!parallel)
                MC_AWAIT(tasks.back());


            mHadamardSumRecvOts.emplace_back();
            mHadamardSumSendOts.emplace_back();
            assert(socks.capacity() > socks.size());
            socks.emplace_back(comm.fork());
            tasks.emplace_back(
                hadamardSumPreprocess(n * pow2L ,gen, socks.back(), mHadamardSumRecvOts.back(), mHadamardSumSendOts.back(), prng) | macoro::make_eager()
            );
            if (!parallel)
                MC_AWAIT(tasks.back());

            //mHadamardSumRecvOts.back()
        }

        if (parallel)
        {

            for (i = 0; i < tasks.size(); ++i)
                MC_AWAIT(tasks[i]);
        }

        mHasPreprocessing = true;
        MC_END();
    }

    // generate the (inverse) permutation that sorts the keys k.
    macoro::task<> RadixSort::genPerm(
        const BinMatrix& k,
        AdditivePerm& dst,
        CorGenerator& gen,
        coproto::Socket& comm,
        oc::PRNG& prng)
    {

        MC_BEGIN(macoro::task<>, this, &k, &dst, &gen, &comm, &prng,
            ll = u64{},
            kIdx = u64{},
            sk = BinMatrix{},
            ssk = BinMatrix{},
            rho = AdditivePerm{},
            i = u64{},
            debugPerms = std::vector<Perm>{},
            debugPerm = Perm{}
        );

        if (hasPreprocessing() == false)
        {
            MC_AWAIT(preprocess(
                k.rows(), k.bitsPerEntry(), gen, comm, prng));
        }

        setTimePoint("genPerm begin");

        if (mInsecureMock)
        {
            MC_AWAIT(mockSort(k, dst, gen, comm));
            MC_RETURN_VOID();
        }

        if (mDebug)
            MC_AWAIT_SET(debugPerms, debugGenPerm(k, comm));

        ll = oc::divCeil(k.bitsPerEntry(), mL);
        kIdx = 0;
        sk = extract(kIdx, mL, k); kIdx += mL;

        // generate the sorting permutation for the
        // first L bits of the key.
        MC_AWAIT(genBitPerm(0, mL, sk, mPerms[0], comm));
        setTimePoint("genBitPerm");
        //dst.validate(comm);

        if (mDebug)
        {
            MC_AWAIT(comm.send(coproto::copy(dst.mShare)));
            debugPerm.mPi.resize(dst.size());
            MC_AWAIT(comm.recv(debugPerm.mPi));

            for (u64 j = 0; j < debugPerm.size(); ++j)
            {
                debugPerm.mPi[j] ^= dst.mShare[j];
            }

            if (debugPerm != debugPerms[0])
            {
                std::cout << "exp " << debugPerms[0] << std::endl;
                std::cout << "act " << debugPerm << std::endl;
                throw RTE_LOC;
            }
        }

        for (i = 1; i < ll; ++i)
        {
            //std::cout << "genPerm i=" << i << std::endl;

            // get the next L bits of the key.
            sk = extract(kIdx, mL, k); kIdx += mL;
            ssk.resize(sk.rows(), sk.cols());


            if (mTimer)
                mPerms[i - 1].setTimer(getTimer());

            // apply the partial sort that we have so far 
            // to the next L bits of the key.
            // 4 + 1 bytes
            assert(mPerms[i - 1].hasSetup(sk.bytesPerEntry()));
            MC_AWAIT(mPerms[i - 1].apply<u8>(
                PermOp::Inverse, sk.mData, ssk.mData, prng, comm, gen));
            setTimePoint("apply(sk)");

            // generate the sorting permutation for the
            // next L bits of the key.
            MC_AWAIT(genBitPerm(i, mL, ssk, rho, comm));
            setTimePoint("genBitPerm");

            // compose the current partial sort with
            // the permutation that sorts the next L bits
            // 4 bytes
            assert(mPerms[i - 1].hasSetup(4));
            MC_AWAIT(mPerms[i - 1].compose(rho, mPerms[i], prng, comm, gen));
            setTimePoint("compose");
            mPerms[i - 1].clear();
            //std::swap(dst, sigma2);
            //dst.validate(comm);
        }
        setTimePoint("genPerm end");

        dst = std::move(mPerms.back());
        MC_END();
    }



    //// sort `src` based on the key `k`. The sorted values are written to `dst`
    //// and the sorting (inverse) permutation is written to `dstPerm`.
    //BinMatrix sort(
    //	u64 keyBitCount,
    //	const BinMatrix& k,
    //	const BinMatrix& src,
    //	CorGenerator& gen,
    //	coproto::Socket& comm)
    //{

    //	if (k.rows() != src.rows())
    //		throw RTE_LOC;

    //	BinMatrix dst;
    //	ComposedPerm dstPerm;

    //	// generate the sorting permutation.
    //	genPerm(k, dstPerm, gen, comm);

    //	// apply the permutation.
    //	dstPerm.apply(src, dst, gen, comm, , true);

    //	return dst;
    //}

    //// sort `src` based on the key `k`. The sorted values are written to `dst`
    //// and the sorting (inverse) permutation is written to `dstPerm`.
    //void sort(
    //	const BinMatrix& k,
    //	const BinMatrix& src,
    //	BinMatrix& dst,
    //	ComposedPerm& dstPerm,
    //	CorGenerator& gen,
    //	coproto::Socket& comm)
    //{
    //	if (k.rows() != src.rows())
    //		throw RTE_LOC;

    //	// generate the sorting permutation.
    //	genPerm(k, dstPerm, gen, comm);

    //	// apply the permutation.
    //	dstPerm.apply(src, dst, gen, comm, true);
    //}

    // this circuit takes as input a index i\in {0,1}^L and outputs
    // a binary vector o\in {0,1}^{2^L} where is one at index i.
    void RadixSort::initIndexToOneHotCircuit(u64 L)
    {
        if (mIndexToOneHotCircuitBitCount == L)
            return;

        oc::BetaCircuit& indexToOneHot = mIndexToOneHotCircuit;
        indexToOneHot = {};
        //bool debug = false;
        //auto str = [](auto x) -> std::string {return std::to_string(x); };

        u64 numLeaves = 1ull << L;
        u64 nodesPerTree = numLeaves - 1;

        // input comparison bits, the bit is the lsb of each inputAlignment bits.
        oc::BetaBundle idx(L);

        // Flag bit for each node. The bit is set to 1 if that node is active.
        // Therefore each level of the tree is like a one-hot vector.
        oc::BetaBundle nodes(nodesPerTree);
        oc::BetaBundle leafNodes(numLeaves);

        indexToOneHot.addInputBundle(idx);

        // We output a bit for each leaf which is one iff its the active leaf.
        indexToOneHot.addOutputBundle(leafNodes);

        indexToOneHot.addTempWireBundle(nodes);

        // the root node is always active.
        indexToOneHot.addConst(nodes[0], 1);

        // the combined nodes.
        nodes.mWires.insert(nodes.mWires.end(), leafNodes.mWires.begin(), leafNodes.mWires.end());

        for (u64 i = 0; i < nodesPerTree; ++i)
        {
            // the active wire for the parent (current) node.
            auto prntWire = nodes[i];

            // child indexes.
            auto child0 = (i + 1) * 2 - 1;
            auto child1 = (i + 1) * 2;

            // Get the active wire for each child.
            auto chld0Wire = nodes[child0];
            auto chld1Wire = nodes[child1];


            // get the comparison bit for the current node. (each bit is the lsb of an inputAlignment sequence).
            auto cmpWire = idx[idx.size() - 1 - oc::log2floor(i + 1)];

            // the right child is active if the cmp bit is 1 and the parent is active.
            indexToOneHot.addGate(prntWire, cmpWire, oc::GateType::And, chld1Wire);

            // the left child is active if the cmp bit is 0 and the parent is active. This
            // can be implemented with XOR'ing the parent and the right child.
            indexToOneHot.addGate(prntWire, chld1Wire, oc::GateType::Xor, chld0Wire);
        }

        indexToOneHot.levelByAndDepth();
    }

    void RadixSort::initArith2BinCircuit(u64 n)
    {
        auto bitCount = std::max<u64>(1, oc::log2ceil(n));
        if (mArith2BinCir.mGates.size() == 0 ||
            mArith2BinCir.mInputs[0].size() != bitCount)
        {
            oc::BetaLibrary lib;
            mArith2BinCir = *lib.uint_uint_add(bitCount, bitCount, bitCount, oc::BetaLibrary::Optimized::Depth);
            mArith2BinCir.levelByAndDepth();
        }
    }


    macoro::task<> RadixSort::mockSort(
        const BinMatrix& k,
        AdditivePerm& dst,
        CorGenerator& ole,
        coproto::Socket& comm)
    {
        MC_BEGIN(macoro::task<>, &k, &dst, &ole, &comm,
            data = BinMatrix{},
            perm = Perm{});

        if (ole.mRole == CorGenerator::Role::Receiver)
        {
            data.resize(k.rows(), k.bitsPerEntry());
            MC_AWAIT(comm.recv(data));

            for (u64 i = 0; i < k.size(); ++i)
            {
                data(i) ^= k(i);
            }

            perm = sort(data).inverse();

            dst.init(perm.size());
            dst.mShare = perm.mPi;
        }
        else {
            MC_AWAIT(comm.send(coproto::copy(k)));
            dst.init(k.numEntries());
        }

        MC_END();
    }


    bool lessThan(span<const u8> l, span<const u8> r)
    {
        assert(l.size() == r.size());
        for (u64 i = l.size() - 1; i < l.size(); --i)
        {
            if (l[i] < r[i])
                return true;
            if (l[i] > r[i])
                return false;
        }
        return false;
    }

    Perm sort(const BinMatrix& x)
    {
        Perm res(x.rows());

        std::stable_sort(res.begin(), res.end(),
            [&](const auto& a, const auto& b) {
                return lessThan(x[a], x[b]);

            });

        // std::cout << "in" << std::endl;
        // for (u64 i = 0; i < x.rows(); ++i)
        //     std::cout << i << ": " << hex(x[i]) << std::endl;
        // std::cout << "out" << std::endl;
        // for (u64 i = 0; i < x.rows(); ++i)
        //     std::cout << i << ": " << hex(x[res[i]]) << std::endl;
        return res;
    }
} // namespace secJoin

