#pragma once
#include "secure-join/Defines.h"
#include "cryptoTools/Common/Aligned.h"
#include "cryptoTools/Common/BitVector.h"

#include <vector>
#include <memory>

#include "macoro/task.h"
#include <utility>

namespace secJoin
{
    

    struct RequestState;
    enum class CorType
    {
        Ot,
        Ole
    };

    inline std::string toString(CorType t)
    {
        switch (t)
        {
        case secJoin::CorType::Ot:
            return "CorType::Ot";
        case secJoin::CorType::Ole:
            return "CorType::Ole";
        default:
            return "CorType::?????";
        }
    }

    struct Cor {
        Cor(CorType t)
            :mType(t)
        {}
        Cor(const Cor&) = default;
        Cor(Cor&&) = default;
        Cor& operator=(const Cor&) = default;
        Cor& operator=(Cor&&) = default;

        CorType mType;

        // The request associated with this correlation.
        std::shared_ptr<RequestState> mRequest;
    };

    // A receiver OT correlation.
    struct OtRecv : Cor
    {

        OtRecv() : Cor(CorType::Ot) {}
        OtRecv(const OtRecv&) = delete;
        OtRecv& operator=(const OtRecv&) = delete;
        OtRecv(OtRecv&&) = default;
        OtRecv& operator=(OtRecv&&) = default;


        // The choice bits 
        oc::BitVector mChoice;

        // the OT messages
        oc::span<oc::block> mMsg;

        // the number of correlations this chunk has.
        u64 size() const { return mMsg.size(); }

        // The choice bits 
        oc::BitVector& choice() { return mChoice; }

        // the OT messages
        oc::span<oc::block> msg() { return mMsg; }
    };



    // A sender OT correlation.
    struct OtSend : Cor
    {

        OtSend() : Cor(CorType::Ot) {}
        OtSend(const OtSend&) = delete;
        OtSend& operator=(const OtSend&) = delete;
        OtSend(OtSend&&) = default;
        OtSend& operator=(OtSend&&) = default;

        // the OT messages
        oc::span<std::array<oc::block, 2>> mMsg;

        u64 size() const
        {
            return mMsg.size();
        }

        oc::span<std::array<oc::block, 2>> msg() { return mMsg; }
    };


    // A sender OT correlation.
    struct BinOle : Cor
    {

        BinOle() : Cor(CorType::Ole) {}
        BinOle(const BinOle&) = delete;
        BinOle& operator=(const BinOle&) = delete;
        BinOle(BinOle&&) = default;
        BinOle& operator=(BinOle&&) = default;


        // the ole's
        oc::span<oc::block> mAdd, mMult;

        u64 size() const
        {
            return mMult.size() * 128;
        }

        //oc::span<std::array<oc::block, 2>> msg() { return mMsg; }
    };


}