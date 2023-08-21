#pragma once
#include "OtGenerator.h"
#include "BinOleGenerator.h"
#include "libOTe/Base/BaseOT.h"
namespace secJoin
{



    class CorGenerator
    {
    public:
        enum class Role
        {
            Sender,
            Receiver
        };

        Role mRole;
        bool mMock = false;

        void mock(Role role)
        {
            mRole = role;
            mMock = true;
            oc::PRNG prng(oc::ZeroBlock);
            mSBase.resize(128);
            mRBase.resize(128);
            mRBase.mChoice.randomize(prng);
            for (u64 i = 0; i < 128; ++i)
            {
                mSBase.mBase[i][0] = prng.get<oc::block>();
                mSBase.mBase[i][1] = prng.get<oc::block>();
                mRBase.mBase[i] = mSBase.mBase[i][mRBase.mChoice[i]].getSeed();
            }
        }

        //struct Session
        //{
        //    SendBase mSBase;
        //    RecvBase mRBase;

        //    enum Type
        //    {
        //        BinOle, SendOt, RecvOt
        //    };
        //    Type mType;
        //    struct Request
        //    {
        //        u64 mSize;
        //    };
        //    std::vector<Request> mRequests;
        //};


        SendBase mSBase;
        RecvBase mRBase;

        bool hasBase()
        {
            return mSBase.mBase.size() && mRBase.mBase.size();
        }

        macoro::task<> generateBase(Role role, oc::PRNG& prng, coproto::Socket& sock)
        {
            MC_BEGIN(macoro::task<>, this, role, &prng, &sock,
                base = oc::DefaultBaseOT{},
                msg2 = std::vector<std::array<oc::block, 2>>{},
                msg = std::vector<oc::block>{},
                choice = oc::BitVector{},
                t0 = macoro::task<>{},
                t1 = macoro::task<>{},
                sock2 = sock.fork(),
                prng2 = prng.fork());

            mRole = role;
            msg2.resize(128);
            msg.resize(128);
            choice.resize(128);
            if (mRole == Role::Sender)
            {
                t0 = base.send(msg2, prng, sock);
                t1 = base.receive(choice, msg, prng2, sock2);
            }
            else
            {
                t0 = base.receive(choice, msg, prng, sock);
                t1 = base.send(msg2, prng2, sock2);
            }

            MC_AWAIT(macoro::when_all_ready(std::move(t0), std::move(t1)));

            mSBase.resize(128);
            mRBase.resize(128);
            mRBase.mChoice = choice;
            for (u64 i = 0; i < 128; ++i)
            {
                mSBase.mBase[i][0] = msg2[i][0];
                mSBase.mBase[i][1] = msg2[i][1];
                mRBase.mBase[i] = msg[i];
            }

            MC_END();
        }



        //void binOleRequest(BinOleRequest& r, u64 numCorrelations)
        //{

        //    //if (hasBase() == false)
        //    //    throw RTE_LOC;
        //    //if (mRole == Role::Sender)
        //    //    r.init(numCorrelations, 1 << 16, sock, prng, mRBase, mMock);
        //    //else
        //    //    r.init(numCorrelations, 1 << 16, sock, prng, mSBase, mMock);
        //    //return r.task();
        //}
        macoro::task<> binOleRequest(BinOleRequest& r , u64 numCorrelations, coproto::Socket& sock, oc::PRNG& prng, u64 reservoirSize = 0)
        {
            if (hasBase() == false)
                throw RTE_LOC;
            BinOleGenerator gen;

            if (mRole == Role::Sender)
                gen.init(1ull << 16, sock, prng, mRBase, mMock);
            else
                gen.init(1 << 16, sock, prng, mSBase, mMock);

            r = gen.request(numCorrelations);
            MC_BEGIN(macoro::task<>, gen, &r);
            MC_AWAIT(r.start());
            MC_END();
        }

        macoro::task<> otRecvRequest(OtRecvGenerator & r, u64 numCorrelations, coproto::Socket& sock, oc::PRNG& prng, u64 reservoirSize = 0)
        {
            if (hasBase() == false)
                throw RTE_LOC;
            r.init(numCorrelations, 1 << 16, sock, prng, mSBase, mMock);
            return r.task();
        }

        macoro::task<> otSendRequest(OtSendGenerator& r, u64 numCorrelations, coproto::Socket& sock, oc::PRNG& prng, u64 reservoirSize = 0)
        {
            if (hasBase() == false)
                throw RTE_LOC;
            r.init(numCorrelations, 1 << 16, sock, prng, mRBase, mMock);
            return r.task();
        }

        CorGenerator fork()
        {
            CorGenerator r;
            r.mMock = mMock;
            r.mRole = mRole;

            r.mRBase = mRBase.fork();
            r.mSBase = mSBase.fork();
            return r;
        }
    };
}