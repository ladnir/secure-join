#include "OtGenerator.h"

namespace secJoin
{


    void fakeFill(u64 m, OtRecv& ole, coproto::SessionID sid)
    {
        //oc::PRNG prng(oc::block(mCurSize++));
        ole.mMsg.resize(m);
        ole.mChoice.resize(m);
        memset(ole.mChoice.data(), 0b10101100, ole.mChoice.sizeBytes());

        oc::block s;
        memcpy(s.data(), sid.mVal, sizeof(oc::block));
        for (u32 i = 0; i < ole.mMsg.size(); ++i)
        {
            oc::block m0 = std::array<u32, 4>{i, i, i, i};// prng.get();
            oc::block m1 = std::array<u32, 4>{~i, ~i, ~i, ~i};//prng.get();

            m0 = m0 ^ s;
            m1 = m1 ^ s;
            ole.mMsg[i] = ole.mChoice[i] ? m1 : m0;
        }
    }

    void fakeFill(u64 m, OtSend& ole, coproto::SessionID sid)
    {
        ole.mMsg.resize(m);
        oc::block s;
        memcpy(s.data(), sid.mVal, sizeof(oc::block));
        for (u32 i = 0; i < ole.mMsg.size(); ++i)
        {
            ole.mMsg[i][0] = std::array<u32, 4>{i, i, i, i};// prng.get();
            ole.mMsg[i][1] = std::array<u32, 4>{~i, ~i, ~i, ~i};//prng.get();


            ole.mMsg[i][0] = ole.mMsg[i][0] ^ s;
            ole.mMsg[i][1] = ole.mMsg[i][1] ^ s;
        }
    }

    void OtRecvGenerator::Batch::init(u64 size, coproto::Socket& sock, oc::PRNG& prng, SendBase& base)
    {
        mSock = sock.fork();
        mPrng = prng.get<oc::block>();
        mReceiver.configure(size);
        mReceiver.setBaseOts(base.get());
    }


    macoro::task<> OtRecvGenerator::Batch::task()
    {
        mMsg.resize(mReceiver.mRequestedNumOts);
        mChoice.resize(mReceiver.mRequestedNumOts);
        return mReceiver.silentReceive(mChoice, mMsg, mPrng, mSock);
        //MC_BEGIN(macoro::task<>, this);
        //mMsg.resize(mReceiver.mRequestedNumOts);
        //mChoice.resize(mReceiver.mRequestedNumOts);
        //MC_AWAIT(mReceiver.silentReceive(mChoice, mMsg, mPrng, mSock));
        ////std::cout << "mReceiver done " << mSock.mId << std::endl;
        //MC_END();
    }



    void OtRecvGenerator::init(
        u64 n,
        u64 batchSize,
        coproto::Socket& sock,
        oc::PRNG& prng,
        SendBase& base,
        bool mock)
    {
        mMock = mock;
        mSize = n;
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
        MC_BEGIN(macoro::task<>, this,
            i = u64{});

        if (mMock)
            MC_RETURN_VOID();

        TODO("Ex handle");
        for (i = 0; i < mCorrelations.size(); ++i)
        {
            //std::cout << "start ot recv " << mCorrelations[i].mSock.mId << std::endl;
            mCorrelations[i].mTask = mCorrelations[i].task() | macoro::make_eager();
        }

        for (i = 0; i < mCorrelations.size(); ++i)
        {
            MC_AWAIT(mCorrelations[i].mTask);
            mCorrelations[i].mDone.set();
        }

        MC_END();
    }


    macoro::task<> OtRecvGenerator::get(OtRecv& d)
    {
        MC_BEGIN(macoro::task<>, this, &d);

        if (mIdx >= mCorrelations.size())
            throw RTE_LOC;
        if (mMock)
        {
            auto s = mCorrelations[mIdx].mReceiver.mRequestedNumOts;
            fakeFill(s, d, mCorrelations[mIdx].mSock.mId);
        }
        else
        {
            MC_AWAIT(mCorrelations[mIdx].mDone);

            d.mMsg = std::move(mCorrelations[mIdx].mMsg);
            d.mChoice = std::move(mCorrelations[mIdx].mChoice);
        }
        ++mIdx;

        MC_END();
    }



    void OtSendGenerator::Batch::init(u64 size, coproto::Socket& sock, oc::PRNG& prng, RecvBase& base)
    {
        mSock = sock.fork();
        mPrng = prng.get<oc::block>();
        mSender.configure(size);
        mSender.setBaseOts(base.get(), base.mChoice);
    }

    macoro::task<> OtSendGenerator::Batch::task()
    {
        mMsg.resize(mSender.mRequestNumOts);
        return mSender.silentSend(mMsg, mPrng, mSock);
        //MC_BEGIN(macoro::task<>, this);
        //mMsg.resize(mSender.mRequestNumOts);
        //MC_AWAIT(mSender.silentSend(mMsg, mPrng, mSock));
        ////std::cout << "mSender done " << mSock.mId << std::endl;
        //MC_END();
    }


    void OtSendGenerator::init(
        u64 n,
        u64 batchSize,
        coproto::Socket& sock,
        oc::PRNG& prng,
        RecvBase& base,
        bool mock)
    {
        mMock = mock;
        mSize = n;
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
        if (mMock)
            MC_RETURN_VOID();

        TODO("Ex handle");
        for (i = 0; i < mCorrelations.size(); ++i)
        {
            //std::cout << "start ot send " << mCorrelations[i].mSock.mId << std::endl;
            mCorrelations[i].mTask = mCorrelations[i].task() | macoro::make_eager();
        }

        for (i = 0; i < mCorrelations.size(); ++i)
        {
            MC_AWAIT(mCorrelations[i].mTask);
            mCorrelations[i].mDone.set();
        }

        MC_END();
    }




    macoro::task<> OtSendGenerator::get(OtSend& d)
    {
        MC_BEGIN(macoro::task<>, this, &d);

        if (mMock)
        {
            auto s = mCorrelations[mIdx].mSender.mRequestNumOts;
            fakeFill(s, d, mCorrelations[mIdx].mSock.mId);
        }
        else
        {

            if (mIdx >= mCorrelations.size())
                throw RTE_LOC;

            MC_AWAIT(mCorrelations[mIdx].mDone);

            d.mMsg = std::move(mCorrelations[mIdx].mMsg);
        }
        ++mIdx;

        MC_END();
    }
}