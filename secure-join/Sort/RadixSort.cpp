#include "RadixSort.h"
#include "secure-join/Sort/BitInjection.h"

namespace secJoin
{
    template<typename T>
    std::string whatError(macoro::result<T>& r)
    {
        try {
            std::rethrow_exception(r.error());
        }
        catch (std::exception& e)
        {
            return e.what();
        }
    }

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
        {

            if (exp[i] != dd[i])
            {
                std::cout << i << ": " << exp[i] << " " << dd[i] << std::endl;
                throw RTE_LOC;
            }
        }
        MC_END();
    }

    auto roundDownTo(u64 v, u64 d) { return v / d * d; }


    //void RadixSort::hadamardSumRequest(
    //    u64 size,
    //    CorGenerator& gen,
    //    OtRecvGenerator&recv, 
    //    OtSendGenerator&send)
    //{

    //}

    macoro::task<> RadixSort::hadamardSumSend(
        Matrix32& s,
        std::vector<u32>& shares,
        BinMatrix& f,
        OtRecvRequest& otRecvReq,
        coproto::Socket& comm
    )
    {
        MC_BEGIN(macoro::task<>, &s, &shares, &f, &otRecvReq, &comm,
            otRecv = OtRecv{},
            rows = u64{},
            cols = u64{},
            i = u64{},
            m = u64{},
            end = u64{},
            ec = macoro::result<void>{},
            fIter = (block*)nullptr,
            tt = std::vector<u32>{}
        );

        rows = s.rows();
        cols = s.cols();
        shares.resize(rows);


        fIter = (block*)f.data();
        // recv
        for (i = 0; i < rows;)
        {
            MC_AWAIT(otRecvReq.get(otRecv));
            if (otRecv.size() % cols)
                throw RTE_LOC;

            m = std::min<u64>(rows - i, otRecv.size() / cols);
            end = i + m;

            //for (u64 j = 0; j < m; ++j, ++i)
            //{
            //    auto row = i / s.cols();
            //    u8 fi = *oc::BitIterator((u8*)fIter, i);
            //    otRecv.mChoice[j] = otRecv.mChoice[j] ^ fi;
            //    assert(otRecv.mMsg.size() > j);
            //    shares[row] += otRecv.mMsg.data()[j].get<u32>(0);
            //}

            for (u64 k = i * cols, otIdx = 0; i < end; ++i)
            {
                auto& share = shares.data()[i];
                assert(share == 0);
                for (u64 j = 0; j < cols; ++j, ++k, ++otIdx)
                {
                    u8 fk = *oc::BitIterator((u8*)fIter, k);
                    otRecv.mChoice[otIdx] = otRecv.mChoice[otIdx] ^ fk;
                    assert(otRecv.mMsg.size() > otIdx);
                    share += otRecv.mMsg.data()[otIdx].get<u32>(0);
                }
            }


            otRecv.mChoice.resize(m * cols);
            MC_AWAIT(comm.send(std::move(otRecv.mChoice)));
        }

        for (i = 0; i < rows;)
        {
            MC_AWAIT_TRY(ec, comm.recvResize(tt));
            if (ec.has_error())
                std::cout << "ec: " << whatError(ec) << std::endl;
            ec.value();

            m = std::min<u64>(s.rows() - i, tt.size() / cols);
            end = i + m;
            for (u64 k = i * cols, otIdx = 0; i < end; ++i)
            {
                auto& share = shares.data()[i];
                for (u64 j = 0; j < cols; ++j, ++k, ++otIdx)
                {
                    u8 fk = *oc::BitIterator((u8*)fIter, k);
                    assert(otIdx < tt.size());
                    share += tt.data()[otIdx] * fk;
                }
            }
        }
        MC_END();
    }

    macoro::task<> RadixSort::hadamardSumRecv(
        Matrix32& s,
        std::vector<u32>& shares,
        BinMatrix& f,
        OtSendRequest& otSendReq,
        coproto::Socket& comm)
    {
        MC_BEGIN(macoro::task<>, &s, &shares, &f, &otSendReq, &comm,
            otSend = OtSend{},
            rows = u64{},
            cols = u64{},
            i = u64{},
            m = u64{},
            end = u64{},
            fIter = (block*)nullptr,
            tt = std::vector<u32>{},
            diff = oc::BitVector{});

        rows = s.rows();
        cols = s.cols();
        shares.resize(rows);

        fIter = (block*)f.data();
        for (i = 0; i < rows;)
        {
            MC_AWAIT(otSendReq.get(otSend));
            if (otSend.size() % cols)
                throw RTE_LOC;
            m = std::min<u64>(rows - i, otSend.size() / cols);
            end = i + m;

            diff.resize(m * cols);
            MC_AWAIT(comm.recv(diff));

            tt.resize(m * cols);

            for (u64 k = i * cols, otIdx = 0; i < end; ++i)
            {
                assert(i < shares.size());
                auto& share = shares.data()[i];

                for (u64 j = 0; j < cols; ++j, ++k, ++otIdx)
                {
                    u8 fk = *oc::BitIterator((u8*)fIter, k);
                    auto sk = s(k);
                    auto d = diff[otIdx];

                    assert(otSend.mMsg.size() > otIdx);
                    auto m0 = otSend.mMsg.data()[otIdx][0 ^ d].get<u32>(0);
                    auto m1 = otSend.mMsg.data()[otIdx][1 ^ d].get<u32>(0);

                    auto r = m0 - (fk * sk);
                    auto v1 = (1 ^ fk) * sk + r;
                    tt[otIdx] = v1 - m1;

                    share -= r;
                }
            }
            MC_AWAIT(comm.send(std::move(tt)));
        }
        MC_END();
    }

    // compute dst = sum_i f.col(i) * s.col(i) where * 
    // is the hadamard (component-wise) product. 
    macoro::task<> RadixSort::hadamardSum(
        Round& round,
        BinMatrix& f,
        Matrix32& s,
        AdditivePerm& dst,
        coproto::Socket& comm)
    {

        if (dst.size() != s.rows())
            throw RTE_LOC;
        MC_BEGIN(macoro::task<>, this, &f, &s, &dst, &comm, &round,
            sComm = coproto::Socket{},
            rComm = coproto::Socket{},
            otRecvReq = OtRecvRequest{},
            otSendReq = OtSendRequest{},
            shares = std::vector<u32>{},
            sendShares = std::vector<u32>{},
            ec = macoro::result<void>{},
            bitCount = u64{}
        );

        // A = f + a
        // B = s + b
        //A.resize(f.rows(), f.cols());
        //B.resize(f.rows(), f.cols());

        //rows = s.rows();
        //cols = s.cols();
        //shares.resize(rows);
        //sendShares.resize(rows);

        otRecvReq = std::move(round.mHadamardSumRecvOts);
        otSendReq = std::move(round.mHadamardSumSendOts);

        sComm = mRole ? comm : comm.fork();
        rComm = mRole ? comm.fork() : comm;

        MC_AWAIT(macoro::when_all_ready(
            hadamardSumSend(s, sendShares, f, otRecvReq, sComm),
            hadamardSumRecv(s, shares, f, otSendReq, rComm)
        ));

        for (u64 i = 0; i < shares.size(); ++i)
            shares[i] += sendShares[i];

        if (mDebug)
            MC_AWAIT(checkHadamardSum(f, s, shares, comm, true));

        bitCount = std::max<u64>(1, oc::log2ceil(shares.size()));
        if (mArith2BinCir.mInputs.size() == 0 || mArith2BinCir.mInputs[0].size() != bitCount)
            throw RTE_LOC;
        if (round.mArithToBinGmw.mN != shares.size())
            throw RTE_LOC;

        round.mArithToBinGmw.setZeroInput(mRole);
        round.mArithToBinGmw.setInput(mRole ^ 1, oc::MatrixView<u32>(shares.data(), shares.size(), 1));

        MC_AWAIT(round.mArithToBinGmw.run(comm));

        dst.mShare.resize(shares.size());
        round.mArithToBinGmw.getOutput(0, oc::MatrixView<u32>(dst.mShare.data(), dst.mShare.size(), 1));

        if (mDebug)
        {

            //tt.resize(shares.size());
            //dd.resize(shares.size());
            //MC_AWAIT(comm.send(coproto::copy(shares)));
            //MC_AWAIT(comm.send(coproto::copy(dst.mShare)));
            //MC_AWAIT(comm.recv(tt));
            //MC_AWAIT(comm.recv(dd));

            //for (u64 i = 0; i < shares.size(); ++i)
            //{
            //    tt[i] += shares[i];
            //    dd[i] ^= dst.mShare[i];

            //}

            //if (tt != dd)
            //    throw RTE_LOC;

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
        Round& round,
        u64 bitCount,
        const BinMatrix& k,
        Matrix32& f,
        BinMatrix& fBin,
        coproto::Socket& comm)
    {
        MC_BEGIN(macoro::task<>, this, &k, &f, &comm, bitCount, &fBin, &round
            //cir = oc::BetaCircuit{}
        );

        if (bitCount != mL)
            throw RTE_LOC;
        if (k.rows() != mSize)
            throw RTE_LOC;

        if (mRole > 1)
            throw RTE_LOC;
        // we oversized fBin to make sure we have trailing zeros.
        fBin.resize(mSize + sizeof(block), 1ull << bitCount, 1, oc::AllocType::Uninitialized);
        fBin.resize(mSize, 1ull << bitCount, 1, oc::AllocType::Uninitialized);


        if (bitCount == 1)
        {
            // For k = [ 1 1 0 1 0 ]
            //
            //  fBin = [ 0 0 1 0 1 ]
            //         [ 1 1 0 1 0 ]
            //
            // but we store them packed so we have
            //
            // fBin = [ 01 01 10 01 10 ]
            // 
            // Each of these elements will be a row. 
            // This is the code that does the packing
            for (u64 i = 0; i < mSize; ++i)
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
            // When we have more than one bit, we have to use MPC 
            //
            // for k = [ 0 2 1 3 0 1 ]
            // 
            //  fBin = [ 1 0 0 0 1 0 ]
            //         [ 0 0 1 0 0 1 ]
            //         [ 0 1 0 0 0 0 ]
            //         [ 0 0 0 1 0 0 ]
            // 
            // in packed format we have
            // 
            // fBin = [ 1000 0010 0100 0001 1000 0100]
            // 
            // Each of these elements will be a row. 
            round.mIndexToOneHotGmw.setInput(0, k);
            MC_AWAIT(round.mIndexToOneHotGmw.run(comm));
            round.mIndexToOneHotGmw.getOutput(0, fBin);
        }


        // we have a special case for 1 and 2 bits keys.
        // Overall we want to pack these fBin bits together 
        // because thats what BitIject is expecting.
        // However, for 1 and 2 bit keys each (byte aligned) 
        // row only holds 2 or 4 bits of data. Therefore
        // for 1 bit keys we will pack 4 rows into 1 row.
        if (bitCount == 1)
        {

            auto src = fBin.data();
            auto dst = fBin.data();
            auto main = mSize / 4;
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

            for (u64 j = 0; j < (mSize % 4); ++j)
            {
                *dst = ((src[j] & 3) << (2 * j)) | (bool(j) * (*dst));
            }

            // fBin will now how "one row" with mSize * 2 bits in it. 
            // Each pair of bits correspond to a key.
            fBin.resize(1, mSize * 2);
        }
        else if (bitCount == 2)
        {
            // for 2 bit keys we will pack 2 rows into 1 row.
            auto src = fBin.data();
            auto dst = fBin.data();
            auto main = mSize / 2;
            for (u64 i = 0; i < main; ++i)
            {
                *dst =
                    ((src[0] & 15) << 0) |
                    ((src[1] & 15) << 4);

                ++dst;
                src += 2;
            }

            for (u64 j = 0; j < (mSize % 2); ++j)
            {
                *dst = ((src[j] & 15) << (4 * j)) | (bool(j) * (*dst));
            }


            // fBin will now how "one row" with mSize * 4 bits in it. 
            // Each set of 4 bits correspond to a key.
            fBin.resize(1, mSize * 4);
        }
        else
        {
            // for 3 bits or more, we can just resize because the rows dont have any padding.
            fBin.resize(1, mSize * fBin.cols() * 8);
        }

        // we oversized fBin at the start of this fn to make sure we have trailing zeros.
        // here is where we set the zero value.
        memset(fBin.data() + fBin.size(), 0, sizeof(block));


        TODO("determine min bit count required. currently 32");
        //MC_AWAIT(round.mBitInjects.bitInjection((1ull << bitCount) * k.rows(), fBin.mData, 32, f, comm));
        MC_AWAIT(round.mBitInject.bitInjection(fBin.mData, 32, f, comm));

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
        Round& round,
        u64 keyBitCount,
        const BinMatrix& k,
        AdditivePerm& dst,
        coproto::Socket& comm)
    {
        MC_BEGIN(macoro::task<>, this, keyBitCount, &k, &comm, &round, &dst,
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
            }
            std::cout << std::endl;
            std::cout << std::endl;

            MC_RETURN(std::move(ret));
        }
        MC_END();
    }

    void RadixSort::init(
        u64 role,
        u64 n,
        u64 bitCount,
        u64 bytesPerElem)
    {
        mRole = role;
        mSize = n;
        mBitCount = bitCount;
        mBytesPerElem = bytesPerElem;
    }

    void RadixSort::request(CorGenerator& gen)
    {
        if (mSize == 0 || mBitCount == 0)
            throw std::runtime_error("init must be called before request(). " LOCATION);
        mHasRequest = true;

        initIndexToOneHotCircuit(mL);
        initArith2BinCircuit(mSize);

        // the number if radix sort rounds
        u64 ll = oc::divCeil(mBitCount, mL);
        mRounds.resize(ll);

        // 2^mL
        u64 pow2L = 1ull << mL;
        u64 expandedSize = mSize * pow2L;

        for (u64 i = 0; i < ll; ++i)
        {
            // the amount of correlated randomness for the permutations we will require.
            u64 permutationByteSize = oc::divCeil(mL, 8) + sizeof(u32);

            // for the last one, we use the requested number of bytes
            if (i == ll - 1)
                permutationByteSize = mBytesPerElem;

            bool keyGen = !i;
            mRounds[i].init(i, mRole, mSize,
                permutationByteSize, keyGen,
                expandedSize, mIndexToOneHotCircuit,
                mArith2BinCir, mDebug);
            mRounds[i].request(gen);
        }

        mHasRequest = true;
    }

    macoro::task<> RadixSort::preprocess(
        coproto::Socket& comm,
        PRNG& prng)
    {
        mHasPrepro = true;
        MC_BEGIN(macoro::task<>, this,
            g = prng.fork(),
            i = u64{},
            tasks = std::vector<macoro::eager_task<>>{});

        if (hasRequest() == false)
            throw std::runtime_error("request must be called first. " LOCATION);
        {

            auto task = [&](macoro::task<>&& t) -> void
                {
                    tasks.emplace_back(std::move(t) | macoro::make_eager());
                };
            if (mDebug)
                std::cout << "rounds: " << mRounds.size() << std::endl;
            for (i = 0; i < mRounds.size(); ++i)
            {
                if (i < mPreProLead)
                {
                    if (mDebug)
                        std::cout << "pre ready " << i << std::endl;
                    mRounds[i].mReady->set();
                }

                task(mRounds[i].preprocess());
            }
        }

        for (i = 0; i < tasks.size();++i)
        {
            MC_AWAIT(tasks[i]);
        }

        MC_END();
    }
    // generate the (inverse) permutation that sorts the keys k.
    macoro::task<> RadixSort::genPerm(
        const BinMatrix& k,
        AdditivePerm& dst,
        coproto::Socket& comm,
        PRNG& prng)
    {

        MC_BEGIN(macoro::task<>, this, &k, &dst, &comm, &prng,
            ll = u64{},
            kIdx = u64{},
            sk = BinMatrix{},
            ssk = BinMatrix{},
            rho = AdditivePerm{},
            i = u64{},
            lead = u64{},
            debugPerms = std::vector<Perm>{},
            debugPerm = Perm{},
            pre = macoro::eager_task<>{}
        );

        if (hasPreprocessing() == false)
        {
            pre = preprocess(comm, prng) | macoro::make_eager();
        }

        setTimePoint("genPerm begin");

        if (mInsecureMock)
        {
            MC_AWAIT(mockSort(k, dst, comm));
            MC_RETURN_VOID();
        }

        if (mDebug)
            MC_AWAIT_SET(debugPerms, debugGenPerm(k, comm));

        ll = oc::divCeil(k.bitsPerEntry(), mL);
        kIdx = 0;
        sk = extract(kIdx, mL, k); kIdx += mL;

        // generate the sorting permutation for the
        // first L bits of the key.

        MC_AWAIT(genBitPerm(mRounds[0], mL, sk, mRounds[0].mPerm, comm));
        setTimePoint("genBitPerm");

        lead = mPreProLead;

        // release the next batch of preprocessing
        if (lead < mRounds.size())
        {
            if (mDebug)
                std::cout << "main ready " << lead << std::endl;
            mRounds[lead++].mReady->set();
        }

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
                mRounds[i - 1].mPerm.setTimer(getTimer());

            // consumes 4 cor-rand.
            MC_AWAIT(mRounds[i - 1].mPerm.setup(comm, prng));

            // apply the partial sort that we have so far 
            // to the next L bits of the key.
            // consumes 1 cor-rand
            assert(mRounds[i - 1].mPerm.hasSetup(sk.bytesPerEntry()));
            MC_AWAIT(mRounds[i - 1].mPerm.apply<u8>(
                PermOp::Inverse, sk.mData, ssk.mData, prng, comm));
            setTimePoint("apply(sk)");

            // generate the sorting permutation for the
            // next L bits of the key.
            rho.init2(mRole, mSize, 0);
            if (!mRounds[i].mPreproDone)
                std::cout << i << " not ready " << std::endl;;
            MC_AWAIT(genBitPerm(mRounds[i], mL, ssk, rho, comm));
            setTimePoint("genBitPerm");

            // release the next batch of preprocessing
            if (lead < mRounds.size())
            {
                if (mDebug)
                    std::cout << "main ready " << lead << std::endl;
                mRounds[lead++].mReady->set();
            }

            // compose the current partial sort with
            // the permutation that sorts the next L bits
            // consumes 4 cor-rand
            assert(mRounds[i - 1].mPerm.hasSetup(4));
            MC_AWAIT(mRounds[i - 1].mPerm.compose(rho, mRounds[i].mPerm, prng, comm));
            setTimePoint("compose");

            assert(mRounds[i - 1].mPerm.hasSetup(1) == false);
            mRounds[i - 1].mPerm.clear();
            //std::swap(dst, sigma2);
            //dst.validate(comm);
        }
        setTimePoint("genPerm end");

        dst = std::move(mRounds.back().mPerm);
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
        coproto::Socket& comm)
    {
        MC_BEGIN(macoro::task<>, &k, &dst, &comm, this,
            data = BinMatrix{},
            perm = Perm{});

        if (mRole)
        {
            data.resize(k.rows(), k.bitsPerEntry());
            MC_AWAIT(comm.recv(data));

            for (u64 i = 0; i < k.size(); ++i)
            {
                data(i) ^= k(i);
            }

            perm = sort(data).inverse();

            dst.init2(mRole, perm.size());
            dst.mShare = perm.mPi;
        }
        else {
            MC_AWAIT(comm.send(coproto::copy(k)));
            dst.init2(mRole, k.numEntries());
            dst.mShare.resize(dst.size());
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

    macoro::task<> RadixSort::Round::preprocess()
    {
        MC_BEGIN(macoro::task<>, this);

        //TODO("fix");
        //if (mArithToBinGmw.mTriples.mReqState->mGenState->mMock == false)
        //    throw RTE_LOC;
        if (mDebug)
        {
            MC_AWAIT(*mReady);
            std::cout << "pre release " << mIdx << std::endl;
        }

        if (mPerm.hasRequest())
        {
            MC_AWAIT(macoro::when_all_ready(
                mPerm.preprocess(),
                mBitInject.preprocess(),
                mIndexToOneHotGmw.preprocess(),
                mArithToBinGmw.preprocess(),
                mHadamardSumRecvOts.start(),
                mHadamardSumSendOts.start()
            ));
        }
        else
        {
            MC_AWAIT(macoro::when_all_ready(
                mBitInject.preprocess(),
                mIndexToOneHotGmw.preprocess(),
                mArithToBinGmw.preprocess(),
                mHadamardSumRecvOts.start(),
                mHadamardSumSendOts.start()
            ));
        }
        mPreproDone = true;
        if (mDebug)
            std::cout << "pre done " << mIdx << std::endl;

        MC_END();
    }
} // namespace secJoin

