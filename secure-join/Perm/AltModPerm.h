#pragma once

#include "secure-join/Defines.h"
#include "secure-join/Prf/AltModPrf.h"
#include "secure-join/Perm/Permutation.h"
#include "secure-join/Perm/PermCorrelation.h"


namespace secJoin
{

    class AltModPermGenSender
    {
    public:
        bool mDebug = false;

        AltModPrfReceiver mPrfRecver;

        u64 mN = 0;

        u64 mBytesPerRow = 0;

        AltModPermGenSender() = default;
        AltModPermGenSender(const AltModPermGenSender&) = default;
        AltModPermGenSender(AltModPermGenSender&&) noexcept = default;
        AltModPermGenSender& operator=(const AltModPermGenSender&) = default;
        AltModPermGenSender& operator=(AltModPermGenSender&&) noexcept = default;

        // initialize this sender to have a permutation of size n, where 
        // bytesPerRow bytes can be permuted per position. keyGen can be 
        // set if the caller wants to explicitly ask to perform AltMod keygen or not.
        void init(
            u64 n, 
            u64 bytesPerRow,
            CorGenerator& cor,
            span<std::array<block, 2>> atlModKeys = {}
            )
        {
            mN = n;
            mBytesPerRow = bytesPerRow;
            u64 blocks = n * divCeil(bytesPerRow, sizeof(block));
            mPrfRecver.init(blocks, cor, atlModKeys);
        }

        // this will request CorGen to start our preprocessing
        void preprocess()
        {
            mPrfRecver.preprocess();
        }

        // Generate the correlated randomness for the permutation pi. pi will either
        // be mPi if it is already known or it will be mPrePerm.
        macoro::task<> generate(
            Perm perm,
            PRNG& prng,
            coproto::Socket& chl,
            PermCorSender& dst);

        void clear() {
            mPrfRecver.clear();
            mN = 0;
            mBytesPerRow = 0;
        }
    };


    class AltModPermGenReceiver
    {
    public:
        bool mDebug = false;

        // The AltMod prf sender protocol.
        AltModPrfSender mPrfSender;

        u64 mN = 0;

        u64 mBytesPerRow = 0;

        AltModPermGenReceiver() = default;
        AltModPermGenReceiver(const AltModPermGenReceiver&) = default;
        AltModPermGenReceiver(AltModPermGenReceiver&&) noexcept = default;
        AltModPermGenReceiver& operator=(const AltModPermGenReceiver&) = default;
        AltModPermGenReceiver& operator=(AltModPermGenReceiver&&) noexcept = default;

        void init(
            u64 n, 
            u64 bytesPerRow,
            CorGenerator& cor,
            macoro::optional<AltModPrf::KeyType> altModKey = {},
            span<block> altModKeyOts = {})
        {
            mN = n;
            mBytesPerRow = bytesPerRow;

            u64 blocks = n * divCeil(bytesPerRow, sizeof(block));
            mPrfSender.init(blocks, cor, altModKey, altModKeyOts);
        }

        // generete preprocessing for a rnadom permutation. This can be derandomized to a chosen perm later.
        void preprocess()
        {
            mPrfSender.preprocess();
        }

        macoro::task<> generate(
            PRNG& prng,
            coproto::Socket& chl,
            PermCorReceiver& dst);
    };
}