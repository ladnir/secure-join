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
                choice = oc::BitVector{});

            mRole = role;
            msg2.resize(128);
            msg.resize(128);
            choice.resize(128);
            if (mRole == Role::Sender)
            {
                TODO("parallel");
                MC_AWAIT(base.send(msg2, prng, sock));
                MC_AWAIT(base.receive(choice, msg, prng, sock));
            }
            else
            {
                MC_AWAIT(base.receive(choice, msg, prng, sock));
                MC_AWAIT(base.send(msg2, prng, sock));
            }

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


        macoro::task<> binOleRequest(BinOleGenerator& r , u64 numCorrelations, coproto::Socket& sock, oc::PRNG& prng, u64 reservoirSize = 0)
        {
            if (hasBase() == false)
                throw RTE_LOC;
            if (mRole == Role::Sender)
                r.init(numCorrelations, 1 << 16, sock, prng, mRBase, mMock);
            else
                r.init(numCorrelations, 1 << 16, sock, prng, mSBase, mMock);
            return r.task();
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
    };
}