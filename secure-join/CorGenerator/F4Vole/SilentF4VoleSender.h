#pragma once
// © 2024 Visa.
// Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the "Software"), to deal in the Software without restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions:
// 
// The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.
// 
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.


#include "secure-join/Defines.h"

#include <cryptoTools/Common/Defines.h>
#include <cryptoTools/Crypto/PRNG.h>
#include <cryptoTools/Common/Timer.h>
#include <cryptoTools/Common/Aligned.h>
#include "libOTe/Tools/Pprf/RegularPprf.h"
#include "libOTe/Tools/Pprf/StationaryPprf.h"
#include <libOTe/TwoChooseOne/TcoOtDefines.h>
#include <libOTe/TwoChooseOne/OTExtInterface.h>
#include <libOTe/TwoChooseOne/SoftSpokenOT/SoftSpokenMalOtExt.h>
#include <libOTe/Tools/ExConvCode/ExConvCode.h>
#include <libOTe/Base/BaseOT.h>
#include <libOTe/Vole/Noisy/NoisyVoleReceiver.h>
#include <libOTe/Vole/Noisy/NoisyVoleSender.h>
#include <libOTe/TwoChooseOne/Silent/SilentOtExtUtil.h>
#include <libOTe/Tools/QuasiCyclicCode.h>
#include <libOTe/Tools/TungstenCode/TungstenCode.h>
#include "F4CoeffCtx.h"
#include "F4VoleCode.h"

namespace secJoin
{
    // A specialized VOLE protocol for GF(4) subfield.
    class SilentF4VoleSender : public oc::TimerAdapter
    {
    public:

        using F = block;
        using G = F4;
        using Ctx = CoeffCtxGF4;

        static constexpr u64 mScaler = 2;

        static constexpr bool MaliciousSupported = false;

        enum class State
        {
            Default,
            Configured,
            HasBase
        };

        using VecF = typename Ctx::template Vec<F>;
        using VecG = typename Ctx::template Vec<G>;

        State mState = State::Default;

        // the context used to perform F, G operations
        Ctx mCtx;

        // the pprf used to generate the noise vector.
        oc::RegularPprfSender<F, Ctx> mGen;
        std::unique_ptr<oc::StationaryPprfSender<F, Ctx>> mStationary;
        block mCodeSeed = oc::ZeroBlock;

        auto& gen() const {
            using Base = oc::PprfSender<F, Ctx>;
            return mStationary ? static_cast<const Base&>(*mStationary) : static_cast<const Base&>(mGen);
        }
        auto& gen() {
            using Base = oc::PprfSender<F, Ctx>;
            return mStationary ? static_cast<Base&>(*mStationary) : static_cast<Base&>(mGen);
        }
        auto format() const {
            return mStationary ? oc::PprfOutputFormat::ByTreeIndex : oc::PprfOutputFormat::ByPhysicalIndex;
        }

        // the number of correlations requested.
        u64 mRequestSize = 0;

        // the length of the noisy vector.
        u64 mNoiseVecSize = 0;

        // the weight of the nosy vector
        u64 mNumPartitions = 0;

        // the size of each regular, weight 1, subvector
        // of the noisy vector. mNoiseVecSize = mNumPartions * mSizePer
        u64 mSizePer = 0;

        // the lpn security parameters
        u64 mSecParam = 0;

        // the type of base OT OT that should be performed.
        // Base requires more work but less communication.
        oc::SilentBaseType mBaseType = oc::SilentBaseType::BaseExtend;

        // the base Vole correlation. To generate the silent vole,
        // we must first create a small vole 
        //   mBaseA + mBaseB = mBaseC * mDelta.
        // These will be used to initialize the non-zeros of the noisy 
        // vector. mBaseB is the b in this corrlations.
        VecF mBaseB;

        // the full sized noisy vector. This will initalially be 
        // sparse with the corrlations
        //   mA = mB + mC * mDelta
        // before it is compressed. 
        VecF mB;

        // determines if the malicious checks are performed.
        oc::SilentSecType mMalType = oc::SilentSecType::SemiHonest;

        // A flag to specify the linear code to use
        oc::MultType mMultType = oc::MultType::Spin;
        std::optional<oc::SpinCode> mSpin;

        VecF mEncodeTemp;

        block mDeltaShare;

        bool mDebug = false;

#ifdef ENABLE_SOFTSPOKEN_OT
        macoro::optional<oc::SoftSpokenMalOtSender> mOtExtSender;
        macoro::optional<oc::SoftSpokenMalOtReceiver> mOtExtRecver;
#endif

        bool hasSilentBaseOts()const
        {
            return gen().hasBaseOts();
        }


        u64 baseVoleCount() const
        {
            return mNumPartitions + 1 * (mMalType == oc::SilentSecType::Malicious);
        }

        // Generate the silent base OTs. If the Iknp 
        // base OTs are set then we do an IKNP extend,
        // otherwise we perform a base OT protocol to
        // generate the needed OTs.
        oc::task<> genSilentBaseOts(PRNG& prng, oc::Socket& chl, F delta)
        {
            MACORO_TRY{
#ifdef LIBOTE_HAS_BASE_OT

#if defined ENABLE_MRR_TWIST && defined ENABLE_SSE
            using BaseOT = oc::McRosRoyTwist;
#elif defined ENABLE_MR
            using BaseOT = oc::MasnyRindal;
#elif defined ENABLE_MRR
            using BaseOT = oc::McRosRoy;
#elif defined ENABLE_NP_KYBER
            using BaseOT = oc::MasnyRindalKyber;
#else
            using BaseOT = oc::DefaultBaseOT;
#endif
                auto msg = oc::AlignedUnVector<std::array<block, 2>>(silentBaseOtCount());
                auto baseOt = BaseOT{};
                auto prng2 = oc::PRNG{};
                auto xx = oc::BitVector{};
                auto chl2 = oc::Socket{};
                auto nv = oc::NoisyVoleSender<F, G, Ctx>{};
                auto b = VecF{};
            setTimePoint("SilentVoleSender.genSilent.begin");

            if (isConfigured() == false)
                throw std::runtime_error("configure must be called first");

            xx = mCtx.template binaryDecomposition<F>(delta);

            // compute the correlation for the noisy coordinates.
            b.resize(baseVoleCount());


            if (mBaseType == oc::SilentBaseType::BaseExtend)
            {
#ifdef ENABLE_SOFTSPOKEN_OT

                if (!mOtExtSender)
                    mOtExtSender = oc::SoftSpokenMalOtSender{};
                if (!mOtExtRecver)
                    mOtExtRecver = oc::SoftSpokenMalOtReceiver{};

                if (mOtExtRecver->hasBaseOts() == false)
                {
                    msg.resize(msg.size() + mOtExtRecver->baseOtCount());
                    co_await mOtExtSender->send(msg, prng, chl);

                    mOtExtRecver->setBaseOts(
                        span<std::array<block, 2>>(msg).subspan(
                            msg.size() - mOtExtRecver->baseOtCount(),
                            mOtExtRecver->baseOtCount()));
                    msg.resize(msg.size() - mOtExtRecver->baseOtCount());

                    co_await nv.send(delta, b, prng, *mOtExtRecver, chl, mCtx);
                }
                else if (msg.empty())
                {
                    co_await nv.send(delta, b, prng, *mOtExtRecver, chl, mCtx);
                }
                else
                {
                    chl2 = chl.fork();
                    prng2.SetSeed(prng.get());

                    co_await
                        macoro::when_all_ready(
                            nv.send(delta, b, prng2, *mOtExtRecver, chl2, mCtx),
                            mOtExtSender->send(msg, prng, chl));
                }
#else
                throw RTE_LOC;
#endif
            }
            else
            {
                chl2 = chl.fork();
                prng2.SetSeed(prng.get());
                co_await
                    macoro::when_all_ready(
                        nv.send(delta, b, prng2, baseOt, chl2, mCtx),
                        baseOt.send(msg, prng, chl));
            }


            setSilentBaseOts(msg, b);
            setTimePoint("SilentVoleSender.genSilent.done");
#else
            throw std::runtime_error("LIBOTE_HAS_BASE_OT = false, must enable relic, sodium or simplest ot asm." LOCATION);
            co_return
#endif
            } MACORO_CATCH(exPtr) {
                co_await chl.close();
                std::rethrow_exception(exPtr);
            }
        }

        // configure the silent OT extension. This sets
        // the parameters and figures out how many base OT
        // will be needed. These can then be ganerated for
        // a different OT extension or using a base OT protocol.
        void configure(
            u64 requestSize,
            oc::SilentBaseType type = oc::SilentBaseType::BaseExtend,
            u64 secParam = 128,
            Ctx ctx = {}, oc::SdNoiseDistribution noise = oc::SdNoiseDistribution::Regular)
        {
            mCtx = std::move(ctx);
            mSecParam = secParam;
            mRequestSize = requestSize;
            mState = State::Configured;
            mBaseType = type;

            const auto config = configureF4Code(mSecParam, mRequestSize, mMultType, noise);
            mNumPartitions = config.mNumPartitions;
            mSizePer = config.mSizePer;
            mNoiseVecSize = config.mNoiseVectorSize;

            if (noise == oc::SdNoiseDistribution::Stationary)
            {
                mCodeSeed = block(12528943721987127, 98743297823479812);
                mSpin.reset();
                mStationary = std::make_unique<oc::StationaryPprfSender<F, Ctx>>();
            }
            else
                mStationary.reset();
            gen().configure(mSizePer, mNumPartitions);
        }

        // return true if this instance has been configured.
        bool isConfigured() const { return mState != State::Default; }

        // Returns how many base OTs the silent OT extension
        // protocol will needs.
        u64 silentBaseOtCount() const
        {
            if (isConfigured() == false)
                throw std::runtime_error("configure must be called first");

            return gen().baseOtCount();
        }

        // Set the externally generated base OTs. This choice
        // bits must be the one return by sampleBaseChoiceBits(...).
        void setSilentBaseOts(
            span<std::array<block, 2>> sendBaseOts,
            span<block> b)
        {
            if ((u64)sendBaseOts.size() != silentBaseOtCount())
                throw RTE_LOC;

            if (b.size() != baseVoleCount())
                throw std::invalid_argument("Wrong number of base GF(4) VOLEs");
            gen().setBase(sendBaseOts);


            // Negation in characteristic two is the identity. Reserve the
            // bottom two bits in each share for the receiver's coefficient.
            mBaseB.resize(b.size());
            for (u64 i = 0; i < b.size(); ++i)
                mBaseB[i] = b[i] & block(~0ull, ~3ull);
        }

        // The native OT extension interface of silent
        // OT. The receiver does not get to specify 
        // which OT message they receiver. Instead
        // the protocol picks them at random. Use the 
        // send(...) interface for the normal behavior.
        oc::task<> silentSend(
            F delta,
            VecF& b,
            PRNG& prng,
            oc::Socket& chl)
        {
            MACORO_TRY{

            co_await silentSendInplace(delta, b.size(), prng, chl);

            mCtx.copy(mB.begin(), mB.begin() + b.size(), b.begin());
            clear();

            setTimePoint("SilentVoleSender.expand.ldpc.msgCpy");

            } MACORO_CATCH(exPtr) {
                co_await chl.close();
                std::rethrow_exception(exPtr);
            }
        }

        oc::task<> sendChosen(
            F delta,
            VecF& b,
            PRNG& prng,
            oc::Socket& chl)
        {
            MACORO_TRY{
            auto diff = oc::AlignedUnVector<u8>{};

            co_await silentSendInplace(delta, b.size(), prng, chl);

            mCtx.copy(mB.begin(), mB.begin() + b.size(), b.begin());
            setTimePoint("SilentVoleSender.expand.ldpc.msgCpy");

            diff.resize(oc::divCeil(b.size(), 4));
            co_await chl.recv(diff);

            for (u64 i = 0, ii = 0; i < diff.size(); ++i)
            {
                auto m = std::min<u64>(b.size() - i * 4, 4);
                for (u64 j = 0; j < m; ++j, ++ii)
                {
                    auto d = G{ static_cast<u8>((diff[i] >> (2 * j)) & 3) };
                    block u;
                    mCtx.mul(u, delta, d);
                    b[ii] ^= u;
                }
            }
            setTimePoint("SilentVoleSender.expand.derandomize");

            clear();

            } MACORO_CATCH(exPtr) {
                co_await chl.close();
                std::rethrow_exception(exPtr);
            }
        }


        // The native OT extension interface of silent
        // OT. The receiver does not get to specify 
        // which OT message they receiver. Instead
        // the protocol picks them at random. Use the 
        // send(...) interface for the normal behavior.
        oc::task<> silentSendInplace(
            F delta,
            u64 n,
            PRNG& prng,
            oc::Socket& chl)
        {
            MACORO_TRY{
            auto     X = block{};
            auto     hash = std::array<u8, 32>{};
            auto     baseB = VecF{};
            auto     A = oc::AlignedUnVector<block>{};
            setTimePoint("SilentVoleSender.ot.enter");


            if (isConfigured() == false)
            {
                // first generate 128 normal base OTs
                configure(n, oc::SilentBaseType::BaseExtend);
            }

            if (mRequestSize < n)
                throw std::invalid_argument("n does not match the requested number of OTs via configure(...). " LOCATION);

            if (!gen().hasBaseOts() || mBaseB.empty())
            {
                // recvs data
                co_await genSilentBaseOts(prng, chl, delta);
            }

            setTimePoint("SilentVoleSender.start");
            //gTimer.setTimePoint("SilentVoleSender.iknp.base2");

            // allocate B
            mB.reserve(mNoiseVecSize);

            if (mTimer)
                gen().setTimer(*mTimer);

            // extract just the first mNumPartitions value of mBaseB. 
            // the last is for the malicious check (if present).
            mCtx.resize(baseB, mNumPartitions);
            mCtx.copy(mBaseB.begin(), mBaseB.begin() + mNumPartitions, baseB.begin());

            // program the output the PPRF to be secret shares of
            // our secret share of delta * noiseVals. The receiver
            // can then manually add their shares of this to the
            // output of the PPRF at the correct locations.
            mB.resize(mNumPartitions * mSizePer);
            co_await gen().expand(chl, baseB, prng.get(), mB,
                format(), true, 1, mCtx);
            for (auto& leaf : mB)
                leaf &= block(~0ull, ~3ull);
            mB.resize(mNoiseVecSize);
            std::fill(mB.begin() + mNumPartitions * mSizePer, mB.end(), oc::ZeroBlock);
            setTimePoint("SilentVoleSender.expand.pprf");

            if (mDebug)
            {
                co_await checkRT(chl, delta);
                setTimePoint("SilentVoleSender.expand.checkRT");
            }

            if (mMalType == oc::SilentSecType::Malicious)
            {
                co_await chl.recv(X);

                if constexpr (MaliciousSupported)
                    hash = ferretMalCheck(X);
                else
                    throw std::runtime_error("malicious is currently only supported for GF128 block. " LOCATION);

                co_await chl.send(std::move(hash));
            }

            switch (mMultType)
            {
            case oc::MultType::Spin:
                compressF4Spin(mSpin, mRequestSize, mB, mStationary ?
                    oc::SdNoiseDistribution::Stationary : oc::SdNoiseDistribution::Regular, mCodeSeed);
                break;
            case osuCrypto::MultType::ExConv7x24:
            case osuCrypto::MultType::ExConv21x24:
            {
                oc::ExConvCode encoder;
                u64 expanderWeight, accumulatorWeight, scaler;
                double minDist;
                oc::ExConvConfigure(mMultType, scaler, expanderWeight, accumulatorWeight, minDist);
                assert(scaler == 2 && minDist < 1 && minDist > 0);

                encoder.config(mRequestSize, mNoiseVecSize, expanderWeight, accumulatorWeight);
                if (mTimer)
                    encoder.setTimer(getTimer());
                encoder.dualEncode<F, Ctx>(mB.begin(), mCtx);
                break;
            }
            case osuCrypto::MultType::Tungsten:
            {
                oc::experimental::TungstenCode encoder;
                encoder.config(oc::roundUpTo(mRequestSize, 8), mNoiseVecSize);
                encoder.dualEncode<F, Ctx>(mB.begin(), mCtx, mEncodeTemp);
                break;
            }
            default:
                throw std::runtime_error("Code is not supported. " LOCATION);
                break;
            }

            mCtx.resize(mB, mRequestSize);


            mState = mStationary ? State::Configured : State::Default;
            mBaseB.clear();

            if (mDebug)
            {
                A.resize(mB.size());
                co_await chl.recv(A);

                {
                    u64 n = mB.size();
                    using F = block;
                    using G = F4;
                    auto& a = A;
                    auto& b = mB;
                    CoeffCtxGF4 ctx;
                    for (u64 i = 0; i < n; ++i)
                    {
                        // a = b + c * d
                        F exp, ai, bi;
                        G ci = { static_cast<u8>(a[i].get<u8>(0) & 3) };
                        //G ci = c[i];

                        ctx.fromBlock(ai, a[i]);
                        ctx.fromBlock(bi, b[i]);

                        auto d = delta & block(~0ull, ~0ull << 2);
                        ai = ai & block(~0ull, ~0ull << 2);
                        bi = bi & block(~0ull, ~0ull << 2);

                        ctx.mul(exp, d, ci);
                        ctx.plus(exp, exp, bi);

                        if (ai != exp)
                        {
                            std::cout << i << std::endl;
                            F options[4];
                            for (u8 j = 0; j < 4; ++j)
                            {
                                ctx.mul(options[j], d, G{ j });
                                ctx.plus(options[j], options[j], bi);
                                std::cout << "op " << j << " " << options[j] << std::endl;
                            }

                            std::cout << "ai  " << ai << std::endl;
                            std::cout << "exp " << exp << " " << int(ci.mVal) << std::endl;

                            throw RTE_LOC;
                        }
                    }
                }
            }

            } MACORO_CATCH(exPtr) {
                co_await chl.close();
                std::rethrow_exception(exPtr);
            }
        }

        oc::task<> checkRT(oc::Socket& chl, F delta) const
        {
            MACORO_TRY{
            co_await chl.send(delta);
            co_await chl.send(mB);
            co_await chl.send(mBaseB);

            } MACORO_CATCH(exPtr) {
                co_await chl.close();
                std::rethrow_exception(exPtr);
            }
        }

        std::array<u8, 32> ferretMalCheck(block X)
        {

            auto xx = X;
            block sum0 = oc::ZeroBlock;
            block sum1 = oc::ZeroBlock;
            for (u64 i = 0; i < (u64)mB.size(); ++i)
            {
                block low, high;
                xx.gf128Mul(mB[i], low, high);
                sum0 = sum0 ^ low;
                sum1 = sum1 ^ high;

                xx = xx.gf128Mul(X);
            }

            block mySum = sum0.gf128Reduce(sum1);

            std::array<u8, 32> myHash;
            oc::RandomOracle ro(32);
            ro.Update(mySum ^ mBaseB.back());
            ro.Final(myHash);

            return myHash;
            //chl.send(myHash);
        }

        void clear()
        {
            mB = {};
            gen().clear();
            mState = State::Default;
        }
    };

}

