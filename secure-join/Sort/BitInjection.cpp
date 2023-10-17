#include "BitInjection.h"

namespace secJoin
{


    inline void unpack(span<const u8> in, u64 bitCount, span<u32> out)
    {
        auto n = oc::divCeil(bitCount, 8);
        if (out.size() * n != in.size())
            throw RTE_LOC;

        if (n == sizeof(u32))
            memcpy(out.data(), in.data(), in.size());
        else
        {
            for (u64 j = 0; j < out.size(); ++j)
                out[j] = *(u32*)&in[j * n];
        }

    }
    inline void pack(span<const u32> in, u64 bitCount, span<u8> out)
    {
        auto n = oc::divCeil(bitCount, 8);
        if (in.size() * n != out.size())
            throw RTE_LOC;


        if (n == sizeof(u32))
            memcpy(out.data(), in.data(), out.size());
        else
        {
            auto s = in.data();
            auto iter = out.begin();
            for (u64 j = 0; j < in.size(); ++j)
            {
                std::copy((u8 const*)s, (u8 const*)s + n, iter);
                iter += n;
                ++s;
            }
        }
    }


    void BitInject::request(CorGenerator& gen)
    {
        if (mRowCount == 0 || mInBitCount == 0)
            throw std::runtime_error("init has not been called. " LOCATION);

        mRole = (u64)gen.partyIdx();
        if (gen.partyIdx())
            mRecvReq = gen.recvOtRequest(mRowCount * mInBitCount);
        else
            mSendReq = gen.sendOtRequest(mRowCount * mInBitCount);
        mRequested = true;
    }

    macoro::task<> BitInject::preprocess()
    {
        mHasPreprocessing = true;
        if (mRecvReq.size())
            return mRecvReq.start();
        else if (mSendReq.size())
            return mSendReq.start();
        else
        {
            mHasPreprocessing = false;
            throw std::runtime_error("BitInject::request() must be called before preprocess() " LOCATION);
        }

    }

    //macoro::task<> BitInject::preprocess(
    //    u64 n,
    //    u64 mInBitCount,
    //    CorGenerator& gen_,
    //    PRNG& prng_,
    //    coproto::Socket& sock_)
    //{
    //    MC_BEGIN(macoro::task<>, this, n, mInBitCount, 
    //        gen = gen_.fork(), 
    //        sock = sock_.fork(), 
    //        prng = prng_.fork());
    //    mHasPreprocessing = true;
    //    mRole = (int)gen.mRole;
    //    //if (n == 0 || mInBitCount == 0)
    //    //    throw RTE_LOC;

    //    if (gen.mRole == CorGenerator::Role::Receiver)
    //    {
    //        MC_AWAIT(gen.recvOtRequest(mRecvReq, n * mInBitCount, sock, prng));
    //    }
    //    else
    //    {
    //        MC_AWAIT(gen.sendOtRequest(mSendReq, n * mInBitCount, sock, prng));
    //    }

    //    MC_END();
    //}


    // convert each bit of the binary secret sharing `in`
     // to integer Z_{2^outBitCount} arithmetic sharings.
     // Each row of `in` should have `mInBitCount` bits.
     // out will therefore have dimension `in.rows()` rows 
     // and `mInBitCount` columns.
    //macoro::task<> BitInject::bitInjection(
    //    const oc::Matrix<u8>& in,
    //    u64 outBitCount,
    //    oc::Matrix<u32>& out,
    //    PRNG& prng,
    //    coproto::Socket& sock)
    //{
    //    MC_BEGIN(macoro::task<>, this, &in, outBitCount, &out, &sock, &prng, 
    //        pre = macoro::eager_task<>{});


    //    MC_AWAIT(bitInjection(mInBitCount, in, outBitCount, out, sock));

    //    MC_END();
    //}

    // convert each bit of the binary secret sharing `in`
     // to integer Z_{2^outBitCount} arithmetic sharings.
     // Each row of `in` should have `mInBitCount` bits.
     // out will therefore have dimension `in.rows()` rows 
     // and `mInBitCount` columns.
    macoro::task<> BitInject::bitInjection(
        const oc::Matrix<u8>& in,
        u64 outBitCount,
        oc::Matrix<u32>& out,
        coproto::Socket& sock)
    {
        MC_BEGIN(macoro::task<>, this, &in, outBitCount, &out, &sock,
            in2 = oc::Matrix<u8>{},
            ec = macoro::result<void>{},
            recvs = std::vector<OtRecv>{},
            send = OtSend{},
            i = u64{ 0 },
            k = u64{ 0 },
            m = u64{ 0 },
            diff = oc::BitVector{},
            buff = oc::AlignedUnVector<u8>{},
            updates = oc::AlignedUnVector<u32>{},
            mask = u32{},
            pre = macoro::eager_task<>{}
        );

        if (mInBitCount > in.cols() * 8)
            throw std::runtime_error("mInBitCount longer than the row size. " LOCATION);

        if (in.rows() != mRowCount)
            throw std::runtime_error("row count does not match init(). " LOCATION);

        if (hasRequest() == false)
            throw std::runtime_error("request must be called first. " LOCATION);

        if (hasPreprocessing() == false)
            pre = preprocess() | macoro::make_eager();


        out.resize(in.rows(), mInBitCount);
        mask = outBitCount == 32 ? -1 : ((1 << outBitCount) - 1);

        if (mRole)
        {
            if (hasPreprocessing() == false)
                throw RTE_LOC;
            if (mRecvReq.size() < in.rows() * mInBitCount)
                throw RTE_LOC;

            while (i < out.size())
            {
                recvs.emplace_back();
                MC_AWAIT(mRecvReq.get(recvs.back()));

                m = std::min<u64>(recvs.back().size(), out.size() - i);
                recvs.back().mChoice.resize(m);
                //recvs.back().mMsg.resize(m);

                diff.reserve(m);
                for (u64 j = 0; j < m; )
                {
                    auto row = i / mInBitCount;
                    auto off = i % mInBitCount;
                    auto rem = std::min<u64>(m - j, mInBitCount - off);

                    diff.append((u8*)&in(row, 0), rem, off);

                    i += rem;
                    j += rem;
                }

                diff ^= recvs.back().mChoice;
                recvs.back().mChoice ^= diff;
                MC_AWAIT(sock.send(std::move(diff)));
            }

            i = 0; k = 0;
            while (i < out.size())
            {
                m = recvs[k].mChoice.size();
                buff.resize(m * oc::divCeil(outBitCount, 8));
                MC_AWAIT_TRY(ec, sock.recv(buff));
                if (ec.has_error()) {
                    try { std::rethrow_exception(ec.error()); }
                    catch (std::exception& e) {
                        std::cout << e.what() << std::endl;
                        throw;
                    }
                }
                updates.resize(m);
                unpack(buff, outBitCount, updates);

                for (u64 j = 0; j < m; ++j, ++i)
                {
                    //recvs[k].mMsg[j].set<u32>(0, 0);

                    if (recvs[k].mChoice[j])
                        out(i) = (recvs[k].mMsg[j].get<u32>(0) + updates[j]) & mask;
                    else
                        out(i) = recvs[k].mMsg[j].get<u32>(0) & mask;
                }

                ++k;
            }
        }
        else
        {

            if (hasPreprocessing() == false)
                throw RTE_LOC;
            if (mSendReq.size() < in.rows() * mInBitCount)
                throw RTE_LOC;

            while (i < out.size())
            {
                MC_AWAIT(mSendReq.get(send));

                m = std::min<u64>(send.size(), out.size() - i);
                diff.resize(m);
                MC_AWAIT(sock.recv(diff));

                updates.resize(m);
                for (u64 j = 0; j < m; ++j, ++i)
                {
                    auto row = i / mInBitCount;
                    auto off = i % mInBitCount;

                    auto y = (u8)*oc::BitIterator((u8*)&in(row, 0), off);
                    auto b = (u8)diff[j];
                    auto m0 = send.mMsg[j][b];
                    auto m1 = send.mMsg[j][b ^ 1];

                    auto v0 = m0.get<u32>(0);
                    auto v1 = v0 + (-2 * y + 1);
                    out(i) = (-v0 + y) & mask;
                    updates[j] = (v1 - m1.get<u32>(0)) & mask;
                }

                buff.resize(m * oc::divCeil(outBitCount, 8));
                pack(updates, outBitCount, buff);

                MC_AWAIT(sock.send(std::move(buff)));
            }
        }


        if (pre.handle())
            MC_AWAIT(pre);

        MC_END();
    }

} // namespace secJoin
