#pragma once
// © 2024 Visa.
// Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the "Software"), to deal in the Software without restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions:
// 
// The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.
// 
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

#include "secure-join/Defines.h"

#include <cryptoTools/Common/Defines.h>
#include <cryptoTools/Common/Timer.h>
#include <libOTe/Tools/Tools.h>
#include "libOTe/Tools/Pprf/RegularPprf.h"
#include "libOTe/Tools/Pprf/StationaryPprf.h"
#include <libOTe/TwoChooseOne/TcoOtDefines.h>
#include <libOTe/TwoChooseOne/OTExtInterface.h>
#include <libOTe/TwoChooseOne/SoftSpokenOT/SoftSpokenMalOtExt.h>
#include <libOTe/Tools/Coproto.h>
#include <libOTe/Tools/ExConvCode/ExConvCode.h>
#include <libOTe/Base/BaseOT.h>
#include <libOTe/Vole/Noisy/NoisyVoleReceiver.h>
#include <libOTe/Vole/Noisy/NoisyVoleSender.h>
#include <numeric>
#include "libOTe/TwoChooseOne/Silent/SilentOtExtUtil.h"
#include <libOTe/Tools/TungstenCode/TungstenCode.h>
#include "libOTe/Tools/ExConvCode/ExConvCode.h"
#include "F4CoeffCtx.h"
#include "F4VoleCode.h"

namespace secJoin
{

    // A specialized VOLE protocol for GF(4) subfield.
	class SilentF4VoleReceiver : public oc::TimerAdapter
	{
	public:

		using F = block;
		using G = F4;
		using Ctx = CoeffCtxGF4;

		static constexpr bool MaliciousSupported = false;

		enum class State
		{
			Default,
			Configured,
			HasBase
		};

		using VecF = typename Ctx::template Vec<F>;
		using VecG = typename Ctx::template Vec<G>;

		// The current state of the protocol
		State mState = State::Default;

		// the context used to perform F, G operations
		Ctx mCtx;

		// The number of correlations the user requested.
		u64 mRequestSize = 0;

		// the LPN security parameter
		u64 mSecParam = 0;

		// The length of the noisy vectors (2 * mN for the most codes).
		u64 mNoiseVecSize = 0;

		// We perform regular LPN, so this is the
		// size of the each chunk. 
		u64 mSizePer = 0;

		// the number of noisy positions
		u64 mNumPartitions = 0;

		// The noisy coordinates.
		std::vector<u64> mS;

		// What type of Base OTs should be performed.
		oc::SilentBaseType mBaseType;

		// The matrix multiplication type which compresses 
		// the sparse vector.
		oc::MultType mMultType = oc::MultType::Spin;
        std::optional<oc::SpinCode> mSpin;

		// The multi-point punctured PRF for generating
		// the sparse vectors.
		oc::RegularPprfReceiver<F, Ctx> mGen;
        std::unique_ptr<oc::StationaryPprfReceiver<F, Ctx>> mStationary;
        block mCodeSeed = oc::ZeroBlock;

        auto& gen() const {
            using Base = oc::PprfReceiver<F, Ctx>;
            return mStationary ? static_cast<const Base&>(*mStationary) : static_cast<const Base&>(mGen);
        }
        auto& gen() {
            using Base = oc::PprfReceiver<F, Ctx>;
            return mStationary ? static_cast<Base&>(*mStationary) : static_cast<Base&>(mGen);
        }
        auto format() const {
            return mStationary ? oc::PprfOutputFormat::ByTreeIndex : oc::PprfOutputFormat::ByPhysicalIndex;
        }

		// The internal buffers for holding the expanded vectors.
		// mA  = mB + mC * delta
		VecF mA;

		u64 mNumThreads = 1;

		bool mDebug = false;

		oc::BitVector mIknpSendBaseChoice;

		oc::SilentSecType mMalType = oc::SilentSecType::SemiHonest;

		block mMalCheckSeed, mMalCheckX, mMalBaseA;

		VecF mEncodeTemp;

		// we 
		VecF mBaseA;
		VecG mBaseC;


#ifdef ENABLE_SOFTSPOKEN_OT
		macoro::optional<oc::SoftSpokenMalOtSender> mOtExtSender;
		macoro::optional<oc::SoftSpokenMalOtReceiver> mOtExtRecver;
#endif

		//        // sets the Iknp base OTs that are then used to extend
		//        void setBaseOts(
		//            span<std::array<block, 2>> baseSendOts);
		//
		//        // return the number of base OTs IKNP needs
		//        u64 baseOtCount() const;

		u64 baseVoleCount() const
		{
			return mNumPartitions + 1 * (mMalType == oc::SilentSecType::Malicious);
		}

		//        // returns true if the IKNP base OTs are currently set.
		//        bool hasBaseOts() const;
		//
				// returns true if the silent base OTs are set.
		bool hasSilentBaseOts() const {
			return gen().hasBaseOts();
		};
		//
		//        // Generate the IKNP base OTs
		//        task<> genBaseOts(PRNG& prng, Socket& chl) ;

		// Generate the silent base OTs. If the Iknp 
		// base OTs are set then we do an IKNP extend,
		// otherwise we perform a base OT protocol to
		// generate the needed OTs.
		macoro::task<> genSilentBaseOts(PRNG& prng, coproto::Socket& chl)
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

			auto choice = oc::BitVector{};
			auto bb = oc::BitVector{};
			auto msg = oc::AlignedUnVector<block>{};
			auto baseVole = std::vector<block>{};
			auto baseOt = BaseOT{};
			auto chl2 = coproto::Socket{};
			auto prng2 = PRNG{};
			auto noiseVals = VecG{};
			auto baseAs = VecF{};
			auto nv = oc::NoisyVoleReceiver<F, G, Ctx>{};


			setTimePoint("SilentVoleReceiver.genSilent.begin");
			if (isConfigured() == false)
				throw std::runtime_error("configure must be called first");

			choice = sampleBaseChoiceBits(prng);
			msg.resize(choice.size());

			// sample the noise vector noiseVals such that we will compute
			//
			//  C = (000 noiseVals[0] 0000 ... 000 noiseVals[p] 000)
			//
			// and then we want secret shares of C * delta. As a first step
			// we will compute secret shares of
			//
			// delta * noiseVals
			//
			// and store our share in voleDeltaShares. This party will then
			// compute their share of delta * C as what comes out of the PPRF
			// plus voleDeltaShares[i] added to the appreciate spot. Similarly, the
			// other party will program the PPRF to output their share of delta * noiseVals.
			//
			noiseVals = sampleBaseVoleVals(prng);
			mCtx.resize(baseAs, noiseVals.size());

			if (mTimer)
				nv.setTimer(*mTimer);

			if (mBaseType == oc::SilentBaseType::BaseExtend)
			{
#ifdef ENABLE_SOFTSPOKEN_OT
				if (!mOtExtRecver)
					mOtExtRecver.emplace();

				if (!mOtExtSender)
					mOtExtSender.emplace();

				if (mOtExtSender->hasBaseOts() == false)
				{
					msg.resize(msg.size() + mOtExtSender->baseOtCount());
					bb.resize(mOtExtSender->baseOtCount());
					bb.randomize(prng);
					choice.append(bb);

					co_await mOtExtRecver->receive(choice, msg, prng, chl);

					mOtExtSender->setBaseOts(
						span<block>(msg).subspan(
							msg.size() - mOtExtSender->baseOtCount(),
							mOtExtSender->baseOtCount()),
						bb);

					msg.resize(msg.size() - mOtExtSender->baseOtCount());
					co_await nv.receive(noiseVals, baseAs, prng, *mOtExtSender, chl, mCtx);
				}
				else if (msg.empty())
                {
                    co_await nv.receive(noiseVals, baseAs, prng, *mOtExtSender, chl, mCtx);
                }
                else
				{
					chl2 = chl.fork();
					prng2.SetSeed(prng.get());


					co_await
						macoro::when_all_ready(
							nv.receive(noiseVals, baseAs, prng2, *mOtExtSender, chl2, mCtx),
							mOtExtRecver->receive(choice, msg, prng, chl)
						);
				}
#else
				throw std::runtime_error("soft spoken must be enabled");
#endif
			}
			else
			{
				chl2 = chl.fork();
				prng2.SetSeed(prng.get());

				co_await
					macoro::when_all_ready(
						baseOt.receive(choice, msg, prng, chl),
						nv.receive(noiseVals, baseAs, prng2, baseOt, chl2, mCtx));
			}

			setSilentBaseOts(msg, span<block>(baseAs.data(), baseAs.size()));
			setTimePoint("SilentVoleReceiver.genSilent.done");
#else
			throw std::runtime_error("LIBOTE_HAS_BASE_OT = false, must enable relic, sodium or simplest ot asm." LOCATION);
			co_return;
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
                mStationary = std::make_unique<oc::StationaryPprfReceiver<F, Ctx>>();
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

		// The silent base OTs must have specially set base OTs.
		// This returns the choice bits that should be used.
		// Call this is you want to use a specific base OT protocol
		// and then pass the OT messages back using setSilentBaseOts(...).
		oc::BitVector sampleBaseChoiceBits(PRNG& prng) {

			if (isConfigured() == false)
				throw std::runtime_error("configure(...) must be called first");

			auto choice = gen().sampleChoiceBits(prng);

			return choice;
		}

        void recordPoints()
        {
            mS.resize(mNumPartitions);
            gen().getPoints(mS, format());
            // Tree-index PPRF returns a local leaf for each contiguous tree.
            if (mStationary)
                for (u64 i = 0; i < mNumPartitions; ++i)
                    mS[i] += i * mSizePer;
        }

		VecG sampleBaseVoleVals(PRNG& prng)
		{
			if (isConfigured() == false)
				throw RTE_LOC;

			// sample the values of the noisy coordinate of c
			// and perform a noicy vole to get a = b + mD * c


			mCtx.resize(mBaseC, mNumPartitions + (mMalType == oc::SilentSecType::Malicious));

			// Regular noise is uniform on the three nonzero values. Stationary includes zero.
			for (u64 i = 0; i < mNumPartitions; ++i)
			{
				do { mBaseC[i].mVal = prng.get<u8>() & 3; } while (!mStationary && !mBaseC[i].mVal);
			}

			if (!mStationary || !mStationary->mExpanded)
                recordPoints();

			if (mMalType == oc::SilentSecType::Malicious)
			{
				//if constexpr (MaliciousSupported)
				//{
				//    mMalCheckSeed = prng.get();

				//    auto yIter = mBaseC.begin();
				//    mCtx.zero(mBaseC.end() - 1, mBaseC.end());
				//    for (u64 i = 0; i < mNumPartitions; ++i)
				//    {
				//        auto s = mS[i];
				//        auto xs = mMalCheckSeed.gf128Pow(s + 1);
				//        mBaseC[mNumPartitions] = mBaseC[mNumPartitions] ^ xs.gf128Mul(*yIter);
				//        ++yIter;
				//    }
				//}
				//else
				{
					throw std::runtime_error("malicious is currently only supported for GF128 block. " LOCATION);
				}
			}

			return mBaseC;
		}

		// Set the externally generated base OTs. This choice
		// bits must be the one return by sampleBaseChoiceBits(...).
		void setSilentBaseOts(
			span<block> recvBaseOts,
			span<block> baseA)
		{
			if (isConfigured() == false)
				throw std::runtime_error("configure(...) must be called first.");

			if (static_cast<u64>(recvBaseOts.size()) != silentBaseOtCount())
				throw std::runtime_error("wrong number of silent base OTs");

			if (baseA.size() != baseVoleCount())
				throw std::invalid_argument("Wrong number of base GF(4) VOLEs");
			gen().setBase(recvBaseOts);

			mBaseA.resize(baseA.size());
			for (u64 i = 0; i < baseA.size(); ++i)
				mBaseA[i] = baseA[i] & block(~0ull, ~3ull);

			mState = State::HasBase;
		}

		// Perform the actual OT extension. If silent
		// base OTs have been generated or set, then
		// this function is non-interactive. Otherwise
		// the silent base OTs will automatically be performed.
		macoro::task<> silentReceive(
			VecF& a,
			PRNG& prng,
			coproto::Socket& chl)
		{
			MACORO_TRY{

			co_await silentReceiveInplace(a.size(), prng, chl);

			mCtx.copy(mA.begin(), mA.begin() + a.size(), a.begin());

			if (!mStationary)
			    clear();

			} MACORO_CATCH(exPtr) {
				if(!chl.closed()) co_await chl.close();
				std::rethrow_exception(exPtr);
			}
		}

		// Perform the actual OT extension. If silent
		// base OTs have been generated or set, then
		// this function is non-interactive. Otherwise
		// the silent base OTs will automatically be performed.
		macoro::task<> receiveChosen(
			VecG& c,
			VecF& a,
			PRNG& prng,
			coproto::Socket& chl)
		{
			MACORO_TRY{

			auto diff = std::vector<u8>{};

			co_await silentReceiveInplace(a.size(), prng, chl);

			mCtx.copy(mA.begin(), mA.begin() + a.size(), a.begin());

			diff.resize(oc::divCeil(a.size(), 4));
			for (u64 i = 0, ii = 0; i < diff.size(); ++i)
			{
				auto m = std::min<u64>(a.size() - i * 4, 4);
				for (u64 j = 0; j < m; ++j, ++ii)
				{
					assert(c[ii].mVal < 4);
					auto d = (mA[ii].get<u8>(0) ^ c[ii].mVal) & 3;
					diff[i] |= d << (j * 2);
				}
			}

			co_await chl.send(std::move(diff));

			if (!mStationary)
			    clear();

			} MACORO_CATCH(exPtr) {
				if (!chl.closed()) co_await chl.close();
				std::rethrow_exception(exPtr);
			}
		}
		// Perform the actual OT extension. If silent
		// base OTs have been generated or set, then
		// this function is non-interactive. Otherwise
		// the silent base OTs will automatically be performed.
		macoro::task<> silentReceiveInplace(
			u64 n,
			PRNG& prng,
			coproto::Socket& chl)
		{
			MACORO_TRY{
			auto myHash = std::array<u8, 32>{};
			auto theirHash = std::array<u8, 32>{};

			//gTimer.setTimePoint("SilentVoleReceiver.ot.enter");

			if (isConfigured() == false)
			{
				// first generate 128 normal base OTs
				configure(n, oc::SilentBaseType::BaseExtend);
			}

			if (mRequestSize < n)
				throw std::invalid_argument("n does not match the requested number of OTs via configure(...). " LOCATION);

			if (!hasSilentBaseOts() || mBaseA.empty())
			{
				co_await genSilentBaseOts(prng, chl);
			}

			setTimePoint("SilentVoleReceiver.alloc");

			// allocate mA
			mA.reserve(mNoiseVecSize);


			setTimePoint("SilentVoleReceiver.alloc.zero");

			if (mTimer)
				gen().setTimer(*mTimer);

			// As part of the setup, we have generated 
			//  
			//  mBaseA + mBaseB = mBaseC * mDelta
			// 
			// We have   mBaseA, mBaseC, 
			// they have mBaseB, mDelta
			// This was done with a small (noisy) vole.
			// 
			// We use the Pprf to expand as
			//   
			//    mA' = mB + mS(mBaseB)
			//        = mB + mS(mBaseC * mDelta - mBaseA)
			//        = mB + mS(mBaseC * mDelta) - mS(mBaseA) 
			// 
			// Therefore if we add mS(mBaseA) to mA' we will get
			// 
			//    mA = mB + mS(mBaseC * mDelta)
			//
			mA.resize(mNumPartitions * mSizePer);
			co_await gen().expand(chl, mA, format(), true, mNumThreads, mCtx);
			clearF4ChoiceBits(mA);
			mA.resize(mNoiseVecSize);
			std::fill(mA.begin() + mNumPartitions * mSizePer, mA.end(), oc::ZeroBlock);

			setTimePoint("SilentVoleReceiver.expand.pprf_transpose");

			// populate the noisy coordinates of mC and
			// update mA to be a secret share of mC * delta
			for (u64 i = 0; i < mNumPartitions; ++i)
			{
				auto pnt = mS[i];
				//mCtx.copy(mC[pnt], mBaseC[i]);
				mCtx.plus(mA[pnt], mA[pnt], mBaseA[i]);
			}

#ifndef NDEBUG
			for (u64 i = 0; i < mA.size(); ++i)
			{
				assert((mA[i].get<u64>(0) & 3) == 0);
			}
#endif

			for (u64 i = 0; i < mNumPartitions; ++i)
				mA[mS[i]] = mA[mS[i]] | block(0, mBaseC[i].mVal);

			if (mDebug)
			{
				co_await checkRT(chl);
				setTimePoint("SilentVoleReceiver.expand.checkRT");
			}


			if (mMalType == oc::SilentSecType::Malicious)
			{
				co_await chl.send(std::move(mMalCheckSeed));

				if constexpr (MaliciousSupported)
					myHash = ferretMalCheck();
				else
					throw std::runtime_error("malicious is currently only supported for GF128 block. " LOCATION);

				co_await chl.recv(theirHash);

				if (theirHash != myHash)
					throw RTE_LOC;
			}

			switch (mMultType)
			{
			case oc::MultType::Spin:
			    compressF4Spin(mSpin, mRequestSize, mA, mStationary ?
                    oc::SdNoiseDistribution::Stationary : oc::SdNoiseDistribution::Regular, mCodeSeed);
			    break;
			case osuCrypto::MultType::ExConv7x24:
			case osuCrypto::MultType::ExConv21x24:
			{
				u64 expanderWeight, accumulatorWeight, scaler;
				double minDist;
				ExConvConfigure(mMultType, scaler, expanderWeight, accumulatorWeight, minDist);
				oc::ExConvCode encoder;
				assert(scaler == 2 && minDist < 1 && minDist > 0);
				encoder.config(mRequestSize, mNoiseVecSize, expanderWeight, accumulatorWeight);

				if (mTimer)
					encoder.setTimer(getTimer());

				encoder.dualEncode<F, Ctx>(
					mA.begin(),
					mCtx
				);
				break;
			}
			case osuCrypto::MultType::Tungsten:
			{
				oc::experimental::TungstenCode encoder;
				encoder.config(oc::roundUpTo(mRequestSize, 8), mNoiseVecSize);
				encoder.dualEncode<F, Ctx>(mA.begin(), mCtx, mEncodeTemp);
				break;
			}
			default:
				throw std::runtime_error("Code is not supported. " LOCATION);
				break;
			}

			mCtx.resize(mA, mRequestSize);

			mBaseC = {};
			mBaseA = {};

			// make the protocol as done and that
			// mA,mC are ready to be consumed.
			mState = mStationary ? State::Configured : State::Default;

			if (mDebug)
			{
				co_await chl.send(mA);
			}

			} MACORO_CATCH(exPtr) {
				if (!chl.closed()) co_await chl.close();
				std::rethrow_exception(exPtr);
			}
		}


		// internal.
		macoro::task<> checkRT(coproto::Socket& chl)
		{
			MACORO_TRY{

			auto B = VecF{};
			auto sparseNoiseDelta = VecF{};
			auto baseB = VecF{};
			auto delta = VecF{};
			auto tempF = VecF{};
			auto tempG = VecG{};
			auto buffer = std::vector<u8>{};

			// recv delta
			buffer.resize(mCtx.template byteSize<F>());
			mCtx.resize(delta, 1);
			co_await chl.recv(buffer);
			mCtx.deserialize(buffer.begin(), buffer.end(), delta.begin());

			// recv B
			buffer.resize(mCtx.template byteSize<F>() * mA.size());
			mCtx.resize(B, mA.size());
			co_await chl.recv(buffer);
			mCtx.deserialize(buffer.begin(), buffer.end(), B.begin());

			// recv the noisy values.
			buffer.resize(mCtx.template byteSize<F>() * mBaseA.size());
			mCtx.resize(baseB, mBaseA.size());
			co_await chl.recvResize(buffer);
			mCtx.deserialize(buffer.begin(), buffer.end(), baseB.begin());

			// it shoudl hold that 
			// 
			// mBaseA = baseB + mBaseC * mDelta
			//
			// and
			// 
			//  mA = mB + mC * mDelta
			//
			{
				bool verbose = false;
				bool failed = false;
				std::vector<std::size_t> index(mS.size());
				std::iota(index.begin(), index.end(), 0);
				std::sort(index.begin(), index.end(),
					[&](std::size_t i, std::size_t j) { return mS[i] < mS[j]; });

				mCtx.resize(tempF, 2);
				mCtx.resize(tempG, 1);
				mCtx.zero(tempG.begin(), tempG.end());


				block mask(~0ull, ~3ull);
				auto dd = delta[0] & mask;
				// check the correlation that
				//
				//  mBaseA + mBaseB = mBaseC * mDelta
				for (auto i : oc::rng(mBaseA.size()))
				{
					// temp[0] = baseB[i] + mBaseA[i]
					mCtx.plus(tempF[0], baseB[i] & mask, mBaseA[i] & mask);

					// temp[1] =  mBaseC[i] * delta[0]
					mCtx.mul(tempF[1], delta[0] & mask, mBaseC[i]);

					if (!mCtx.eq(tempF[0], tempF[1]))
						throw RTE_LOC;

					if (i < mNumPartitions)
					{
						//auto idx = index[i];
						auto point = mS[i];
						G ci = { static_cast<u8>(mA[point].get<u8>(0) & 3) };
						if (!mCtx.eq(mBaseC[i], ci))
							throw RTE_LOC;

						if (i && mS[index[i - 1]] >= mS[index[i]])
							throw RTE_LOC;
					}
				}


				auto iIter = index.begin();
				auto leafIdx = mS[*iIter];
				F act = tempF[0];
				G zero = tempG[0];
				mCtx.zero(tempG.begin(), tempG.end());


				for (u64 j = 0; j < mA.size(); ++j)
				{
					G ci = { static_cast<u8>(mA[j].get<u8>(0) & 3) };
					mCtx.mul(act, dd, ci);
					mCtx.plus(act, act, B[j] & mask);

					bool active = false;
					if (j == leafIdx)
					{
						active = true;
					}
					else if (!mCtx.eq(zero, ci))
						throw RTE_LOC;

					if ((mA[j] & mask) != act)
					{
						failed = true;
						if (verbose)
							std::cout << oc::Color::Red;
					}

					if (verbose || failed)
					{
						std::cout << j << " act " << mCtx.str(act)
							<< " a " << mCtx.str(mA[j] & mask) << " b " << mCtx.str(B[j] & mask);

						if (active)
							std::cout << " < " << mCtx.str(dd);

						std::cout << std::endl << oc::Color::Default;
					}

					if (j == leafIdx)
					{
						++iIter;
						if (iIter != index.end())
						{
							leafIdx = mS[*iIter];
						}
					}
				}

				if (failed)
					throw RTE_LOC;
			}

			} MACORO_CATCH(exPtr) {
				if (!chl.closed()) co_await chl.close();
				std::rethrow_exception(exPtr);
			}
		}

		std::array<u8, 32> ferretMalCheck()
		{

			block xx = mMalCheckSeed;
			block sum0 = oc::ZeroBlock;
			block sum1 = oc::ZeroBlock;


			for (u64 i = 0; i < (u64)mA.size(); ++i)
			{
				block low, high;
				xx.gf128Mul(mA[i], low, high);
				sum0 = sum0 ^ low;
				sum1 = sum1 ^ high;
				//mySum = mySum ^ xx.gf128Mul(mA[i]);

				// xx = mMalCheckSeed^{i+1}
				xx = xx.gf128Mul(mMalCheckSeed);
			}

			// <A,X> = <
			block mySum = sum0.gf128Reduce(sum1);

			std::array<u8, 32> myHash;
			oc::RandomOracle ro(32);
			ro.Update(mySum ^ mBaseA.back());
			ro.Final(myHash);
			return myHash;
		}

		void clear()
		{
			mS = {};
			mA = {};
			//mC = {};
			gen().clear();
            mState = State::Default;
		}
	};
}
