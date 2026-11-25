#pragma once
#include "secure-join/Defines.h"
#include "cryptoTools/Common/Aligned.h"
#include "cryptoTools/Common/BitVector.h"

#include <vector>
#include <memory>
#include <numeric>

#include "macoro/task.h"
#include "macoro/channel.h"
#include "macoro/macros.h"
#include "macoro/manual_reset_event.h"

#include "libOTe/TwoChooseOne/Silent/SilentOtExtSender.h"
#include "libOTe/TwoChooseOne/Silent/SilentOtExtReceiver.h"
#include "secure-join/CorGenerator/F4Vole/F4CoeffCtx.h"

namespace secJoin
{
    template<typename SilentOt>
    void configureBatchOt(SilentOt& ot, u64 size)
    {
        ot.configure(size, 2, 1, oc::SilentSecType::SemiHonest,
            oc::SdNoiseDistribution::Regular, oc::MultType::Spin);
    }

    // A struct used to track base correlations requests.
    // These requests will be used as inputs to the batches
    // that will generate the end user correlations.
    struct BaseRequest
    {
        // the choice bits requested for recv base OTs
        oc::BitVector mChoice;

        // the number of send OTs requested
        u64 mSendSize = 0;

        // Fresh binary VOLEs requested for stationary noise.
        u64 mSendBinaryVoles = 0, mRecvBinaryVoles = 0;

        // the number of F4 voles.
        u64 mSendVoleSize = 0;

        // the base choice values for the F4 voles.
        oc::AlignedUnVector<F4> mVoleChoice;

        BaseRequest() = default;
        BaseRequest(const BaseRequest&) = default;
        BaseRequest(BaseRequest&&) = default;
        BaseRequest& operator=(BaseRequest&&) = default;

        // combine many base requests into one.
        BaseRequest(span<BaseRequest> reqs)
        {
            u64 s = 0;
            for (u64 i = 0; i < reqs.size(); ++i)
            {
                s += reqs[i].mChoice.size();
                mSendBinaryVoles += reqs[i].mSendBinaryVoles;
                mRecvBinaryVoles += reqs[i].mRecvBinaryVoles;
            }
            mChoice.reserve(s);
            for (u64 i = 0; i < reqs.size(); ++i)
                mChoice.append(reqs[i].mChoice);
            mSendSize = std::accumulate(reqs.begin(), reqs.end(), 0ull,
                [](auto c, auto& v) { return c + v.mSendSize; });

            s = 0;
            for (u64 i = 0; i < reqs.size(); ++i)
                s += reqs[i].mVoleChoice.size();
            mVoleChoice.resize(s);
            for (u64 i = 0, k = 0; i < reqs.size(); ++i)
            {
                for (u64 j = 0; j < reqs[i].mVoleChoice.size(); ++j, ++k)
                    mVoleChoice[k] = reqs[i].mVoleChoice[j];
            }
            mSendVoleSize = std::accumulate(reqs.begin(), reqs.end(), 0ull,
                [](auto c, auto& v) { return c + v.mSendVoleSize; });
        }
    };

    // A struct used to hold the base correlations used to generate
    // the batches.
    struct BaseCor
    {
        // the index of the next recv OT that can be consumed.
        u64 mOtRecvIndex = 0;

        // the recv OTs
        oc::BitVector mOtRecvChoice;
        oc::AlignedUnVector<block> mOtRecvMsg;

        // the index of the next send OT that can be consumed.
        u64 mOtSendIndex = 0;
        // the send OTs
        oc::AlignedUnVector<std::array<block, 2>> mOtSendMsg;

        // the index of the next send VOLE that can be consumed.
        u64 mVoleSendIndex = 0;

        // the index of the next recv VOLE that can be consumed.
        u64 mVoleRecvIndex = 0;
        
        u64 mBinarySendIndex = 0, mBinaryRecvIndex = 0;
        block mBinaryDelta;
        oc::AlignedUnVector<block> mBinaryA, mBinaryB;
        oc::BitVector mBinaryChoice;

        // the GF(4) sender delta.
        block mVoleDelta;

        // mVoleA = mVoleB + mVoleChoice * mVoleDelta
        oc::AlignedUnVector<F4> mVoleChoice;

        // mVoleA = mVoleB + mVoleChoice * mVoleDelta
        oc::AlignedUnVector<block> mVoleB, mVoleA;

        template<typename T>
        span<T> get(span<T> val, u64& index, u64 n)
        {
            if (index + n > val.size())
                throw RTE_LOC;

            span<T> v{ val.data() + index, n };
            index += n;

            return v;
        }

        span<std::array<block, 2>> getSendOt(u64 n)
        {
            return get<std::array<block, 2>>(mOtSendMsg, mOtSendIndex, n);
        }

        span<block> getRecvOt(u64 n)
        {
            return get<block>(mOtRecvMsg, mOtRecvIndex, n);
        }

        void setOtBase(oc::SilentOtExtReceiver& receiver)
        {
            const auto n = receiver.baseCount().mBaseOtCount;
            auto choices = mOtRecvChoice.subvec(mOtRecvIndex, n);
            receiver.setBaseCors(getRecvOt(n), choices, {}, {});
        }

        span<block> getRecvVole(u64 n)
        {
            return get<block>(mVoleA, mVoleRecvIndex, n);
        }

        span<block> getSendVole(u64 n)
        {
            return get<block>(mVoleB, mVoleSendIndex, n);
        }

    };
}