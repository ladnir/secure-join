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


    struct BinOle
    {

        BinOle() = default;
        BinOle(const BinOle&) = delete;
        BinOle& operator=(const BinOle&) = delete;
        BinOle(BinOle&&) = default;
        BinOle& operator=(BinOle&&) = default;

        oc::AlignedUnVector<oc::block> mMult, mAdd;
        u64 size() const
        {
            return mMult.size() * 128;
        }


    };

    struct BinOleGenerator
    {
        BinOleGenerator() = default;
        BinOleGenerator(const BinOleGenerator&) = delete;
        BinOleGenerator(BinOleGenerator&&) = default;
        BinOleGenerator&operator=(BinOleGenerator&&) = default;

        struct Impl
        {
            Impl() = default;
            Impl(Impl&&) { throw RTE_LOC; };
            oc::SilentOtExtReceiver mReceiver;
            oc::SilentOtExtSender mSender;
            oc::AlignedUnVector<oc::block> mMult, mAdd;

            u64 mSize = 0;
            oc::BitVector mChoice;
            oc::AlignedUnVector<oc::block> mMsg;
            oc::AlignedUnVector<std::array<oc::block, 2>> mMsg2;

            //macoro::async_manual_reset_event mDone;
            macoro::eager_task<> mTask;
            macoro::task<> mT;
            macoro::async_manual_reset_event mDone;

            coproto::Socket mSock;
            oc::PRNG mPrng;

            void init(u64 size, coproto::Socket& sock, oc::PRNG& prng);
            void init(u64 size, coproto::Socket& sock, oc::PRNG& prng, SendBase& base);
            void init(u64 size, coproto::Socket& sock, oc::PRNG& prng, RecvBase& base);
            macoro::task<> recvTask();
            macoro::task<> sendTask();
            macoro::task<> task();
            void compressRecver(oc::BitVector& bv, span<oc::block> recvMsg, span<oc::block> add, span<oc::block> mult);
            void compressSender(span<std::array<oc::block, 2>> sendMsg, span<oc::block> add, span<oc::block> mult);
        };
        std::vector<Impl> mCorrelations;
        u64 mIdx = 0, mSize = 0, mRole = 0;
        bool mMock = false;
        template<typename Base>
        void init(
            u64 n,
            u64 batchSize,
            coproto::Socket& sock,
            oc::PRNG& prng,
            Base& base, 
            bool mock);


        macoro::task<> task();
        macoro::task<> get(BinOle& d);

        macoro::task<> close() {
            MC_BEGIN(macoro::task<>, this);

            if (mMock)
                MC_RETURN_VOID();

            while (mIdx < mCorrelations.size())
            {
                MC_AWAIT(mCorrelations[mIdx++].mTask);
            }
            MC_END();
        }
    };

}