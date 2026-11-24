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
        assert(mReceiver.hasBaseCors());

        co_await mReceiver.silentReceiveInplace(mMsg.size(), prng, sock, oc::ChoiceBitPacking::True);
        mReceiver.hash(mChoice, mMsg, oc::ChoiceBitPacking::True);
    }

    void OtBatch::RecvOtBatch::mock(u64 batchIdx)
    {
        auto s = oc::mAesFixedKey.ecbEncBlock(oc::block(batchIdx, 0));
        osuCrypto::setBytes(mChoice.getSpan<u8>(), s.get<u8>(0));

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
        assert(mSender.hasBaseCors());

        co_await mSender.silentSendInplace(mSender.mDelta, mMsg2.size(), prng, sock);
        mSender.hash(mMsg2, oc::ChoiceBitPacking::True);
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
                configureBatchOt(send.mSender, mSize);
                r.mSendSize = send.mSender.baseCount().mBaseOtCount;
            },
            [&](RecvOtBatch& recv) {
                configureBatchOt(recv.mReceiver, mSize);
                r.mChoice = recv.mReceiver.sampleBaseChoiceBits(mPrng);
            }
        };

        return r;
    }

    void OtBatch::setBase(BaseCor& base)
    {
        if (mGenState->mMock)
            return;

        mSendRecv | match{
            [&](SendOtBatch& send) {
                send.mSender.setBaseCors(base.getSendOt(send.mSender.baseCount().mBaseOtCount), {}, mPrng.get<block>());
            },
            [&](RecvOtBatch& recv) {
                base.setOtBase(recv.mReceiver);
            }
        };
    }

    macoro::task<> OtBatch::getTask(BatchThreadState& threadState) {
            auto t = macoro::task<>{};
            auto msg = std::vector<std::array<block, 2>>{};

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
        else if (threadState.mStationary)
        {
            auto& worker = *threadState.mStationary;
            if (mSendRecv.index() == 0)
            {
                auto& send = std::get<0>(mSendRecv);
                auto& protocol = worker.begin(worker.sendOt, CorType::Ot, mSize, true);
                co_await protocol.silentSendInplace(protocol.mDelta, mSize, mPrng, mSock);
                send.mMsg2.resize(mSize);
                protocol.hash(send.mMsg2, oc::ChoiceBitPacking::True);
                worker.finish(protocol);
            }
            else
            {
                auto& recv = std::get<1>(mSendRecv);
                auto& protocol = worker.begin(worker.recvOt, CorType::Ot, mSize, false);
                co_await protocol.silentReceiveInplace(mSize, mPrng, mSock, oc::ChoiceBitPacking::True);
                recv.mChoice.resize(mSize);
                recv.mMsg.resize(mSize);
                protocol.hash(recv.mChoice, recv.mMsg, oc::ChoiceBitPacking::True);
                worker.finish(protocol);
            }
        }
        else
        {
            if (mGenState->mDebug)
            {
                if (mSendRecv.index() == 0)
                {
                    co_await mSock.send(coproto::copy(std::get<0>(std::get<0>(mSendRecv).mSender.mGenVar).mBaseOTs));

                }
                else
                {

                    msg.resize(std::get<1>(mSendRecv).mReceiver.baseCount().mBaseOtCount);
                    co_await mSock.recv(msg);

                    {
                        auto& recv = std::get<1>(mSendRecv);
                        for (u64 i = 0; i < msg.size(); ++i)
                        {
                            if (std::get<0>(recv.mReceiver.mGenVar).mBaseOTs(i) != msg[i][std::get<0>(recv.mReceiver.mGenVar).mBaseChoices(i)])
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

                    send.mSender.mB = std::move(threadState.mB);
                    send.mSender.mSpin = std::move(threadState.mSpin);
                    std::get<0>(send.mSender.mGenVar).mTempBuffer = std::move(threadState.mPprfTemp);
                    //send.mMsg2 = std::move(threadState.mMsg2);
                    return send.sendTask(mPrng, mSock);
                },
                [&](RecvOtBatch& recv) {
                    recv.mReceiver.mA = std::move(threadState.mA);
                    recv.mReceiver.mSpin = std::move(threadState.mSpin);
                    std::get<0>(recv.mReceiver.mGenVar).mTempBuffer = std::move(threadState.mPprfTemp);
                    //recv.mChoice = std::move(threadState.mChoice);
                    //recv.mMsg = std::move(threadState.mMsg);
                    return recv.recvTask(mPrng, mSock);
                }
            };

            co_await t;


            mSendRecv | match{
                [&](SendOtBatch& send) {
                    threadState.mB = std::move(send.mSender.mB);
                    threadState.mSpin = std::move(send.mSender.mSpin);
                    threadState.mPprfTemp = std::move(std::get<0>(send.mSender.mGenVar).mTempBuffer);
                    send.mSender.clear();
                    //threadState.mMsg2 = std::move(send.mMsg2);
                },
                [&](RecvOtBatch& recv) {
                    threadState.mA = std::move(recv.mReceiver.mA);
                    threadState.mSpin = std::move(recv.mReceiver.mSpin);
                    threadState.mPprfTemp = std::move(std::get<0>(recv.mReceiver.mGenVar).mTempBuffer);
                    recv.mReceiver.clear();
                    //threadState.mChoice = std::move(recv.mChoice);
                    //threadState.mMsg = std::move(recv.mMsg);
                }
            };
        }


        if (mGenState->mDebug)
        {
            if (mSendRecv.index() == 0)
            {
                //std::cout << " checking ot send " << mIndex << std::endl;
                co_await mSock.send(coproto::copy(std::get<0>(mSendRecv).mMsg2));

            }
            else
            {
                //std::cout << " checking ot recv " << mIndex << std::endl;

                msg.resize(std::get<1>(mSendRecv).mMsg.size());
                co_await mSock.recv(msg);

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