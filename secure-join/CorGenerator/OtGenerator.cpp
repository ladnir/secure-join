#include "OtGenerator.h"

namespace secJoin
{

    void OtRecvGenerator::Impl::init(u64 size, coproto::Socket& sock, oc::PRNG& prng, SendBase& base)
    {
        mSock = sock.fork();
        mPrng = prng.get<oc::block>();
        mMsg.resize(size);
        mChoice.resize(size);
        mReceiver.configure(size);
        mReceiver.setBaseOts(base.get());
    }


    macoro::task<> OtRecvGenerator::Impl::task()
    {
        return mReceiver.silentReceive(mChoice, mMsg, mPrng, mSock);
    }



    void OtRecvGenerator::init(
        u64 n,
        u64 batchSize,
        coproto::Socket& sock,
        oc::PRNG& prng,
        SendBase& base)
    {

        mCorrelations.reserve(oc::divCeil(n, batchSize));
        for (u64 i = 0; i < n; i += batchSize)
        {
            u64 size = std::min(n - i * batchSize, batchSize);
            mCorrelations.emplace_back();
            mCorrelations.back().init(size, sock, prng, base);
        }
    }

    macoro::task<> OtRecvGenerator::task()
    {
        MC_BEGIN(macoro::task<>, this, i = u64{});

        TODO("ex handling");

        for (i = 0; i < mCorrelations.size(); ++i)
            mCorrelations[i].mTask = mCorrelations[i].task() | macoro::make_eager();
        for (i = 0; i < mCorrelations.size(); ++i)
            MC_AWAIT(mCorrelations[i].mTask);

        MC_END();
    }

    macoro::task<> OtRecvGenerator::get(OtRecv& d)
    {
        MC_BEGIN(macoro::task<>, this, &d);

        if (mIdx >= mCorrelations.size())
            throw RTE_LOC;

        MC_AWAIT(mCorrelations[mIdx].mTask);

        d.mMsg = std::move(mCorrelations[mIdx].mMsg);
        d.mChoice = std::move(mCorrelations[mIdx].mChoice);
        ++mIdx;

        MC_END();
    }



    void OtSendGenerator::Impl::init(u64 size, coproto::Socket& sock, oc::PRNG& prng, RecvBase& base)
    {
        mSock = sock.fork();
        mPrng = prng.get<oc::block>();
        mMsg.resize(size);
        mSender.configure(size);
        mSender.setBaseOts(base.get(), base.mChoice);
    }

    macoro::task<> OtSendGenerator::Impl::task()
    {
        return mSender.silentSend(mMsg, mPrng, mSock);
    }


    void OtSendGenerator::init(
        u64 n,
        u64 batchSize,
        coproto::Socket& sock,
        oc::PRNG& prng,
        RecvBase& base)
    {
        mCorrelations.reserve(oc::divCeil(n, batchSize));
        for (u64 i = 0; i < n; i += batchSize)
        {
            u64 size = std::min(n - i * batchSize, batchSize);
            mCorrelations.emplace_back();
            mCorrelations.back().init(size, sock, prng, base);
        }
    }

    macoro::task<> OtSendGenerator::task()
    {
        MC_BEGIN(macoro::task<>, this, i = u64{});

        TODO("ex handling");
        for (i = 0; i < mCorrelations.size(); ++i)
            mCorrelations[i].mTask = mCorrelations[i].task() | macoro::make_eager();
        for (i = 0; i < mCorrelations.size(); ++i)
            MC_AWAIT(mCorrelations[i].mTask);

        MC_END();
    }


    macoro::task<> OtSendGenerator::get(OtSend& d)
    {
        MC_BEGIN(macoro::task<>, this, &d);

        if (mIdx >= mCorrelations.size())
            throw RTE_LOC;

        MC_AWAIT(mCorrelations[mIdx].mTask);

        d.mMsg = std::move(mCorrelations[mIdx].mMsg);
        ++mIdx;

        MC_END();
    }
}