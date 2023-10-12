#pragma once
#include "secure-join/Defines.h"
#include "cryptoTools/Common/Aligned.h"
#include "cryptoTools/Common/BitVector.h"

#include <vector>
#include <memory>

#include "macoro/task.h"
#include "macoro/macros.h"
#include "secure-join/CorGenerator/OtGenerator.h"
#include "secure-join/CorGenerator/BinOleGenerator.h"
#include <utility>

namespace secJoin
{
    

    struct ArithTriple {


        //ArithTriple() = default;
        //ArithTriple(const ArithTriple&) = delete;
        //ArithTriple& operator=(const ArithTriple&) = delete;
        //ArithTriple(ArithTriple&&) = default;
        //ArithTriple& operator=(ArithTriple&&) = default;

        u64 mBitCount = 0;
        oc::AlignedUnVector<u32> mA, mB, mC;

        u64 size() { return mA.size(); }
    };


    //template<typename T>
    //struct Request2
    //{
    //    Request2() = default;
    //    Request2(const Request2&) = delete;
    //    Request2(Request2&& o) noexcept
    //        , mSize(std::exchange(o.mSize, 0))
    //        , mNextBatchIdx(std::exchange(o.mNextBatchIdx, 0))
    //        , mCorrelations(std::move(o.mCorrelations))
    //    {}


    //    Request2& operator=(Request2&& o)
    //    {
    //        mSize = std::exchange(o.mSize, 0);
    //        mNextBatchIdx = std::exchange(o.mNextBatchIdx, 0);
    //        mCorrelations = std::move(o.mCorrelations);
    //        return *this;
    //    }

    //    using value_type = T;
    //    using TGen = typename value_type::gen;

    //    u64 mSize = 0, mNextBatchIdx = 0;
    //    std::vector<std::unique_ptr<TGen>> mCorrelations;
    //    //std::unique_ptr<macoro::mpsc::channel<std::pair<u64, T>>> mChl;

    //    auto get(T& d)
    //    {

    //        if (mNextBatchIdx >= mCorrelations.size())
    //            throw std::runtime_error("Out of correlations. " LOCATION);

    //        return mCorrelations[mNextBatchIdx].get(d);
    //        //MC_BEGIN(macoro::task<T>, this,
    //        //    t = std::pair<u64, T>{}
    //        //);

    //        //if (mNextBatchIdx >= mCorrelations.size())
    //        //    throw std::runtime_error("Out of correlations. " LOCATION);


    //        ////while (!mCorrelations[mNextBatchIdx])
    //        ////{
    //        ////    MC_AWAIT_SET(t, mChl->pop());
    //        ////    mCorrelations[t.first] = std::make_unique<T>(std::move(t.second));
    //        ////    //if (mRemReq.size())
    //        ////    //{
    //        ////    //    MC_AWAIT(mGen->mControlQueue->push(CorGenerator::Command{ std::move(mRemReq.back()) }));
    //        ////    //    mRemReq.pop_back();
    //        ////    //}
    //        ////}
    //        ////MC_RETURN(std::move(*mCorrelations[mNextBatchIdx++]));
    //        //MC_END();
    //    }
    //};
}