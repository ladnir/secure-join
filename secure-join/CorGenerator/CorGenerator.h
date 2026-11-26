#pragma once
#include "secure-join/Defines.h"
#include "secure-join/CorGenerator/Base.h"

#include <vector>
#include <memory>
#include <numeric>

#include "macoro/task.h"

#include "Correlations.h"
#include "Batch.h"
#include "Request.h"

#include "secure-join/CorGenerator/F4Vole/SilentF4VoleSender.h"
#include "secure-join/CorGenerator/F4Vole/SilentF4VoleReceiver.h"


namespace secJoin
{
	struct GenState : oc::TimerAdapter
	{
		GenState() = delete;
		GenState(u64 partyIdx, PRNG&& prng, oc::Socket s, u64 numConcurrent, u64 batchSize, bool mock,
			oc::SdNoiseDistribution noise)
			: mPrng(std::move(prng))
			, mSock(std::move(s))
			, mNumConcurrent(numConcurrent)
			, mBatchSize(batchSize)
			, mMock(mock)
			, mNoise(noise)
			, mPartyIdx(partyIdx)
		{
			if (batchSize < (1ull << 10))
				throw std::runtime_error("too small of batch size." LOCATION);

			if (batchSize > (1ull << 26))
				throw std::runtime_error("too large of batch size." LOCATION);

			if (numConcurrent == 0)
				throw std::runtime_error("numConcurrent can not be zero." LOCATION);

			if (numConcurrent > (1ull << 10))
				throw std::runtime_error("numConcurrent too large." LOCATION);
		}

		GenState(const GenState&) = delete;
		GenState(GenState&&) = delete;


		oc::SoftSpokenShOtSender<> mSendOtBase;
		oc::SoftSpokenShOtReceiver<> mRecvOtBase;

		SilentF4VoleSender mSendVoleBase;
		SilentF4VoleReceiver mRecvVoleBase;

		macoro::thread_pool* mPool = nullptr;

		// next request index
		u64 mReqIndex = 0;

		u64 mNumOle = 0;
		u64 mNumOt = 0;
		u64 mNumF4BitOt = 0;
		u64 mNumTritOt = 0;

		struct ReqInfo
		{
			CorType mType;
			u64 mRole;
			u64 mSize;

			bool operator!=(const ReqInfo& r)const
			{
				return
					mType != r.mType ||
					mRole != r.mRole ||
					mSize != r.mSize;
			}
			bool operator==(const ReqInfo& r) const
			{
				return !(*this != r);
			}
		};
		std::vector<ReqInfo> mReqs;

		// randomness source
		PRNG mPrng;

		// the base socket that each subprotocol is forked from.
		coproto::Socket mSock;

		// the number of concurrent correlations that should be generated.
		u64 mNumConcurrent = 0;

		// the size that a batch of OT/OLEs should be generated in.
		u64 mBatchSize = 0;

		// enable additional debugging checks.
		bool mDebug = false;

		// true if we should just fake the correlation generation
		bool mMock = false;
        oc::SdNoiseDistribution mNoise;
        std::shared_ptr<SpinBankPool> mBanks;

		// used to determine which party should go first when its ambiguous.
		u64 mPartyIdx = -1;

		std::atomic<u64> mBatchStartIdx = 0;

		// returns a task that constructs the base OTs and assigns them to mBatches.
		macoro::task<> start(std::shared_ptr<GenState> This);

		std::array<std::shared_ptr<Batch>, 2> mOtBatch, mOleBatch, mF4BitOtBatch, mTritOtBatch;
		std::vector<std::shared_ptr<Batch>> mBatches;


		//void set(SendBase& b);
		//void set(RecvBase& b);


		void startBatch(Batch* b)
		{
			u64 idx = mBatchStartIdx;
			//assert(b->mIndex < mBatches.size());
			//assert(mBatches[b->mIndex].get() == b);

			while (b->mIndex >= idx)
			{
				bool s = mBatchStartIdx.compare_exchange_strong(idx, idx + 1);
				if (s)
				{
					mBatches[idx]->mStart.set();
					++idx;
				}
			}
		}

		void abort()
		{
			u64 idx = mBatchStartIdx;
			while (idx < mBatches.size())
			{

				bool s = mBatchStartIdx.compare_exchange_strong(idx, idx + 1);
				if (s)
				{
					mBatches[idx]->mAbort = true;
					mBatches[idx]->mStart.set();
					++idx;
				}
			}
		}
	};


	struct RequestState;

	struct CorGenerator
	{
		std::shared_ptr<GenState> mGenState;

		std::vector<std::weak_ptr<GenState>> mStarted;
        std::shared_ptr<SpinBankPool> mBanks;

		static constexpr auto defaultNoise = oc::SdNoiseDistribution::Stationary;

		static constexpr u64 defaultBatchSize(oc::SdNoiseDistribution noise)
		{
			return noise == oc::SdNoiseDistribution::Stationary ? 1ull << 15 : 1ull << 20;
		}

		void init(coproto::Socket&& sock, PRNG& prng, u64 partyIdx, u64 numConcurrent,
			oc::SdNoiseDistribution noise = defaultNoise)
		{
			init(std::move(sock), prng, partyIdx, numConcurrent, defaultBatchSize(noise), false, noise);
		}

		void init(
			coproto::Socket&& sock,
			PRNG& prng,
			u64 partyIdx,
			u64 numConcurrent,
			u64 batchSize,
			bool mock,
			oc::SdNoiseDistribution noise = defaultNoise);


		template<typename T>
		auto request(u64 n)
		{
			if (initialized() == false)
				throw std::runtime_error(LOCATION);

			if constexpr (std::is_same_v<T, OtRecv>)
			{
				return Request<OtRecv>{implRequest(CorType::Ot, 0, oc::roundUpTo(n, 128))};
			}
			else if constexpr (std::is_same_v<T, OtSend>)
			{
				return Request<OtSend>{implRequest(CorType::Ot, 1, oc::roundUpTo(n, 128))};
			}
			else if constexpr (std::is_same_v<T, F4BitOtRecv>)
			{
				return Request<F4BitOtRecv>{implRequest(CorType::F4BitOt, 0, oc::roundUpTo(n, 128))};
			}
			else if constexpr (std::is_same_v<T, F4BitOtSend>)
			{
				return Request<F4BitOtSend>{implRequest(CorType::F4BitOt, 1, oc::roundUpTo(n, 128))};
			}
			else if constexpr (std::is_same_v<T, BinOle>)
			{
				return Request<BinOle>{implRequest(CorType::Ole, partyIdx(), oc::roundUpTo(n, 128))};
			}
			else if constexpr (std::is_same_v<T, TritOtRecv>)
			{
				return Request<TritOtRecv>{implRequest(CorType::TritOt, 0, oc::roundUpTo(n, 128))};
			}
			else if constexpr (std::is_same_v<T, TritOtSend>)
			{
				return Request<TritOtSend>{implRequest(CorType::TritOt, 1, oc::roundUpTo(n, 128))};
			}
			else 
			{
				std::cout << "request type not supported" << LOCATION << std::endl;
				std::terminate();
				//static_assert(0, "request type not supported");
			}
		}

		Request<OtRecv> recvOtRequest(u64 n) {
			return request<OtRecv>(n);
		}
		Request<OtSend> sendOtRequest(u64 n) {
			return request<OtSend>(n);
		}
		Request<BinOle> binOleRequest(u64 n) {
			return request<BinOle>(n);
		}

		bool initialized()const;

		u64 partyIdx() const;

		macoro::task<> start();

		void abort();

	private:

		std::shared_ptr<RequestState> implRequest(CorType, u64 role, u64 size);

	};

	using OtRecvRequest = Request<OtRecv>;
	using OtSendRequest = Request<OtSend>;
	using BinOleRequest = Request<BinOle>;

}
