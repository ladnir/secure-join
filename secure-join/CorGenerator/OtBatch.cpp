#include "OtBatch.h"
#include "CorGenerator.h"
#include "secure-join/Util/match.h"

namespace secJoin
{
    OtBatch::OtBatch(GenState* state, bool sender, oc::Socket&& s, PRNG&& p)
        : Batch(state, std::move(s), std::move(p))
    {
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
        mMsg.resize(mReceiver.mRequestNumOts);
        mChoice.resize(mReceiver.mRequestNumOts);
        assert(mReceiver.mGen.hasBaseOts());
        mReceiver.mMultType = oc::MultType::Tungsten;

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
        mSender.mMultType = oc::MultType::Tungsten;

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

        if (mGenState->mMock)
            return r;

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
        if (mGenState->mMock)
            return;

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
    }

    macoro::task<> OtBatch::getTask() {
        MC_BEGIN(macoro::task<>, this,
            t = macoro::task<>{},
            msg = std::vector<std::array<block, 2>>{}
        );

        if (mGenState->mMock)
        {
            mSendRecv | match{
                [&](SendOtBatch& send) {
                    send.mMsg2.resize(mSize);
                    send.mock(mIndex);
                },
                [&](RecvOtBatch& recv) {
                    recv.mChoice.resize(mSize);
                    recv.mMsg.resize(mSize);
                    recv.mock(mIndex);
                }
            };
        }
        else
        {
            if (mGenState->mDebug)
            {
                if (mSendRecv.index() == 0)
                {
                    MC_AWAIT(mSock.send(coproto::copy(std::get<0>(mSendRecv).mSender.mGen.mBaseOTs)));

                }
                else
                {

                    msg.resize(std::get<1>(mSendRecv).mReceiver.silentBaseOtCount());
                    MC_AWAIT(mSock.recv(msg));

                    {
                        auto& recv = std::get<1>(mSendRecv);
                        for (u64 i = 0; i < msg.size(); ++i)
                        {
                            if (recv.mReceiver.mGen.mBaseOTs(i) != msg[i][recv.mReceiver.mGen.mBaseChoices(i)])
                            {
                                std::cout << "OtBatch::getTask() base OTs do not match. " << LOCATION << std::endl;
                                std::terminate();
                            }
                        }
                    }
                }
            }


            t = mSendRecv | match{
                [&](SendOtBatch& send) {
                    return send.sendTask(mPrng, mSock);
                },
                [&](RecvOtBatch& recv) {
                    return recv.recvTask(mPrng, mSock);
                }
            };
            MC_AWAIT(t);
        }


        if (mGenState->mDebug)
        {
            if (mSendRecv.index() == 0)
            {
                //std::cout << " checking ot send " << mIndex << std::endl;
                MC_AWAIT(mSock.send(coproto::copy(std::get<0>(mSendRecv).mMsg2)));

            }
            else
            {
                //std::cout << " checking ot recv " << mIndex << std::endl;

                msg.resize(std::get<1>(mSendRecv).mMsg.size());
                MC_AWAIT(mSock.recv(msg));

                {
                    auto& recv = std::get<1>(mSendRecv);
                    for (u64 i = 0; i < msg.size(); ++i)
                    {
                        if (recv.mMsg[i] != msg[i][recv.mChoice[i]])
                        {
                            std::cout << "OtBatch::getTask() OTs do not match. " << LOCATION << std::endl;
                            std::terminate();
                        }
                    }
                }
            }
        }


        mCorReady.set();
        MC_END();
    }

    void OtBatch::clear()
    {
        mSendRecv | match{
            [&](SendOtBatch& send) {
                send.mMsg2 = {};
            },
            [&](RecvOtBatch& recv) {
                recv.mChoice = {};
                recv.mMsg = {};
            }
        };
    }

    //void OtBatch::mock_(u64 batchIdx)
    //{

    //    mCorReady.set();
    //}


}