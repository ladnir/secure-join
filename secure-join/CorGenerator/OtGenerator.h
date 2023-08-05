#pragma once
#include "secure-join/Defines.h"
#include "secure-join/CorGenerator/Base.h"
#include "cryptoTools/Common/Aligned.h"
#include "cryptoTools/Common/BitVector.h"

#include <vector>
#include <memory>

#include "macoro/task.h"
#include "macoro/channel.h"
#include "macoro/macros.h"
#include "macoro/manual_reset_event.h"

#include "libOTe/TwoChooseOne/Silent/SilentOtExtSender.h"
#include "libOTe/TwoChooseOne/Silent/SilentOtExtReceiver.h"

namespace secJoin
{


    struct OtSend
    {
        OtSend() = default;
        OtSend(const OtSend&) = delete;
        OtSend& operator=(const OtSend&) = delete;
        OtSend(OtSend&&) = default;
        OtSend& operator=(OtSend&&) = default;

        oc::AlignedUnVector<std::array<oc::block, 2>> mMsg;
        u64 size() const
        {
            return mMsg.size();
        }
    };

    struct OtRecv
    {

        OtRecv() = default;
        OtRecv(const OtRecv&) = delete;
        OtRecv& operator=(const OtRecv&) = delete;
        OtRecv(OtRecv&&) = default;
        OtRecv& operator=(OtRecv&&) = default;

        oc::BitVector mChoice;
        oc::AlignedUnVector<oc::block> mMsg;

        u64 size() const
        {
            return mMsg.size();
        }

    };


    struct OtRecvGenerator
    {

        struct Impl
        {
            Impl() = default;
            Impl(Impl&&) { throw RTE_LOC; };
            oc::SilentOtExtReceiver mReceiver;
            oc::BitVector mChoice;
            oc::AlignedUnVector<oc::block> mMsg;
            macoro::eager_task<> mTask;
            macoro::async_manual_reset_event mDone;
            coproto::Socket mSock;
            oc::PRNG mPrng;

            void init(u64 size, coproto::Socket& sock, oc::PRNG& prng, SendBase& base);
            macoro::task<> task();
        };

        std::vector<Impl> mCorrelations;
        u64 mIdx = 0, mSize = 0;
        bool mMock = false;

        void init(
            u64 n,
            u64 batchSize,
            coproto::Socket& sock,
            oc::PRNG& prng,
            SendBase& base,
            bool mock);

        macoro::task<> get(OtRecv& d);

        macoro::task<> task();

    };


    struct OtSendGenerator
    {

        struct Impl
        {
            Impl() = default;
            Impl(Impl&&) { throw RTE_LOC; };
            oc::SilentOtExtSender mSender;
            oc::AlignedUnVector<std::array<oc::block, 2>> mMsg;
            macoro::eager_task<> mTask;
            macoro::async_manual_reset_event mDone;

            coproto::Socket mSock;
            oc::PRNG mPrng;

            void init(u64 size, coproto::Socket& sock, oc::PRNG& prng, RecvBase& base);

            macoro::task<> task();
        };

        std::vector<Impl> mCorrelations;
        u64 mIdx = 0, mSize = 0;
        bool mMock = false;

        void init(
            u64 n,
            u64 batchSize,
            coproto::Socket& sock,
            oc::PRNG& prng,
            RecvBase& base,
            bool mock);

        //macoro::task<> task();

        macoro::task<> task();

        macoro::task<> get(OtSend& d);


    };
}