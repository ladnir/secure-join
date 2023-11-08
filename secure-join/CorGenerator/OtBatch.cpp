#include "OtBatch.h"

namespace secJoin
{
    OtBatch::OtBatch(bool sender, oc::Socket&& s, PRNG&& p)
    {
        mSock = std::move(s);
        mPrng = std::move(p);
        if (sender)
        {
            mSendRecv.emplace<0>();
        }
        else
        {
            mSendRecv.emplace<1>();
        }
    }

    macoro::task<> OtBatch::RecvOtBatch::recvTask(PRNG& prng, oc::Socket& sock)
    {
        mMsg.resize(mReceiver.mRequestedNumOts);
        mChoice.resize(mReceiver.mRequestedNumOts);
        assert(mReceiver.mGen.hasBaseOts());

        return mReceiver.silentReceive(mChoice, mMsg, prng, sock);
    }

    void OtBatch::RecvOtBatch::mock(u64 batchIdx)
    {
        auto s = oc::mAesFixedKey.ecbEncBlock(oc::block(batchIdx, 0));
        memset(mChoice.data(), s.get<u8>(0), mChoice.sizeBytes());

        for (u32 i = 0; i < mMsg.size(); ++i)
        {
            oc::block m0 = block(i, i);// prng.get();
            oc::block m1 = block(~u64(i), ~u64(i));//prng.get();

            m0 = m0 ^ s;
            m1 = m1 ^ s;
            mMsg.data()[i] = mChoice[i] ? m1 : m0;
        }
    }

    macoro::task<> OtBatch::SendOtBatch::sendTask(PRNG& prng, oc::Socket& sock)
    {
        mMsg2.resize(mSender.mRequestNumOts);
        assert(mSender.hasSilentBaseOts());
        return mSender.silentSend(mMsg2, prng, sock);
    }

    void OtBatch::SendOtBatch::mock(u64 batchIdx)
    {
        auto s = oc::mAesFixedKey.ecbEncBlock(oc::block(batchIdx, 0));
        for (u32 i = 0; i < mMsg2.size(); ++i)
        {
            mMsg2.data()[i][0] = block(i, i);// prng.get();
            mMsg2.data()[i][1] = block(~u64(i), ~u64(i));//prng.get();


            mMsg2.data()[i][0] = mMsg2.data()[i][0] ^ s;
            mMsg2.data()[i][1] = mMsg2.data()[i][1] ^ s;
        }
    }


    void OtBatch::getCor(Cor* c, u64 begin, u64 size)
    {

        mSendRecv | match{
            [&](SendOtBatch& send) {
                if (c->mType != CorType::Ot)
                    std::terminate();

                auto& d = *static_cast<OtSend*>(c);
                d.mMsg = send.mMsg2.subspan(begin, size);
            },
            [&](RecvOtBatch& recv) {
                if (c->mType != CorType::Ot)
                    std::terminate();

                auto& d = *static_cast<OtRecv*>(c);
                auto& msg = recv.mMsg;
                auto& choice = recv.mChoice;

                d.mMsg = msg.subspan(begin, size);

                if (size == choice.size())
                    d.mChoice = std::move(choice);
                else
                {
                    d.mChoice.resize(0);
                    d.mChoice.append(choice, size, begin);
                }
            }
        };
    }

    BaseRequest OtBatch::getBaseRequest()
    {
        BaseRequest r;
        mSendRecv | match{
            [&](SendOtBatch& send) {
                send.mSender.configure(mSize);
                r.mSendSize = send.mSender.silentBaseOtCount();
            },
            [&](RecvOtBatch& recv) {
                recv.mReceiver.configure(mSize);
                r.mChoice = recv.mReceiver.sampleBaseChoiceBits(mPrng);
            }
        };

        return r;
    }

    void OtBatch::setBase(span<oc::block> rMsg, span<std::array<oc::block, 2>> sMsg)
    {
        BaseRequest r;
        mSendRecv | match{
            [&](SendOtBatch& send) {
                if (rMsg.size())
                    std::terminate();
                send.mSender.setSilentBaseOts(sMsg);
            },
            [&](RecvOtBatch& recv) {
                if (sMsg.size())
                    std::terminate();
                recv.mReceiver.setSilentBaseOts(rMsg);
            }
        };
        mHaveBase.set();
    }

    macoro::task<> OtBatch::getTask() {
        MC_BEGIN(macoro::task<>, this, 
            t = macoro::task<>{});

        MC_AWAIT(mHaveBase);

        t = mSendRecv | match{
            [&](SendOtBatch& send) {
                return send.sendTask(mPrng, mSock);
            },
            [&](RecvOtBatch& recv) {
                return recv.recvTask(mPrng, mSock);
            }
        };
        MC_AWAIT(t);

        mCorReady.set();
        MC_END();
    }

    void OtBatch::mock(u64 batchIdx)
    {
        mSendRecv | match{
            [&](SendOtBatch& send) {
                send.mMsg2.resize(mSize);
                send.mock(batchIdx);
            },
            [&](RecvOtBatch& recv) {
                recv.mChoice.resize(mSize);
                recv.mMsg.resize(mSize);
                recv.mock(batchIdx);
            }
        };
    }


}