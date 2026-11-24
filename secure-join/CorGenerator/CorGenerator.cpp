#include "CorGenerator.h"
#include "macoro/macros.h"
#include "BinOleBatch.h"
#include "OtBatch.h"
#include <map>

namespace secJoin
{


	void CorGenerator::init(
		coproto::Socket&& sock,
		PRNG& prng,
		u64 partyIdx,
		u64 numConcurrent,
		u64 batchSize,
		bool mock, oc::SdNoiseDistribution noise)
	{
		if (partyIdx > 1 || (noise != oc::SdNoiseDistribution::Regular && noise != oc::SdNoiseDistribution::Stationary))
            throw std::invalid_argument("Invalid correlation party or noise mode");
        mGenState = std::make_shared<GenState>(partyIdx, prng.fork(), std::move(sock), numConcurrent, batchSize, mock);
        mGenState->mNoise = noise;
        if (noise == oc::SdNoiseDistribution::Stationary)
        {
            if (!mBanks)
                mBanks = std::make_shared<SpinBankPool>();
            mGenState->mBanks = mBanks;
        }
	}


	bool CorGenerator::initialized()const
	{
		return mGenState.get();
	}

	u64 CorGenerator::partyIdx() const
	{
		if (!mGenState)
			throw RTE_LOC;
		return mGenState->mPartyIdx;
	}

	macoro::task<> CorGenerator::start()
	{
		mStarted.push_back(mGenState);
		auto gen = std::exchange(mGenState, nullptr);
		return gen->start(gen);
	}


	void CorGenerator::abort()
	{
		for (auto& s : mStarted)
			if (auto genState = s.lock())
				genState->abort();
	}



	std::shared_ptr<RequestState> CorGenerator::implRequest(CorType t, u64 role, u64  n)
	{
		if (mGenState == nullptr)
			throw RTE_LOC; // call init first.

		if (mGenState->mDebug)
		{
			mGenState->mReqs.push_back({ t, role, n });
		}
		auto r = std::make_shared<RequestState>(t, role, n, mGenState, mGenState->mReqIndex++);


		switch (r->mType)
		{
		case CorType::Ot:
			mGenState->mNumOt += n;
			break;
		case CorType::Ole:
			mGenState->mNumOle += n;
			break;
		case CorType::F4BitOt:
			mGenState->mNumF4BitOt += n;
			break;
		case CorType::TritOt:
			mGenState->mNumTritOt += n;
			break;
		default:
			std::terminate();
		}

		for (u64 j = 0;j < r->mSize;)
		{
			std::shared_ptr<Batch>& batch = [&]() -> std::shared_ptr<Batch>&
				{
					switch (r->mType)
					{
					case CorType::Ot:
						return mGenState->mOtBatch[r->mSender];
					case CorType::Ole:
						return mGenState->mOleBatch[r->mSender];
					case CorType::F4BitOt:
						return mGenState->mF4BitOtBatch[r->mSender];
					case CorType::TritOt:
						return mGenState->mTritOtBatch[r->mSender];
					default:
						std::terminate();
					}
				}();

			if (batch == nullptr)
			{
				auto ss = mGenState->mSock.fork();
				mGenState->mBatches.push_back(makeBatch(mGenState.get(), r->mSender, r->mType, std::move(ss), mGenState->mPrng.fork()));
				mGenState->mBatches.back()->mIndex = mGenState->mBatches.size() - 1;
				batch = mGenState->mBatches.back();
			}

			auto begin = batch->mSize;
			auto remReq = r->mSize - j;
			auto remAvb = mGenState->mBatchSize - begin;
			auto size = oc::roundUpTo(std::min<u64>(remReq, remAvb), 128);
			assert(size <= remAvb);

			batch->mSize += size;
			r->addBatch(BatchSegment{ batch, begin, size });
			j += size;

			if (remAvb == size)
				batch = nullptr;
		}

		return r;
	}

	struct Config
	{
		u32 size;
		u16 type;
		u16 role;
	};

	macoro::task<> GenState::start(std::shared_ptr<GenState> This)
	{
		//auto i = u64{};
		//auto j = u64{};
		//auto r = u64{};
		//auto s = u64{};
		auto base = BaseCor{};
		auto protos = std::vector<macoro::task<>>{};
		auto tasks = std::vector<macoro::eager_task<>>{};
		auto prngs = std::vector<PRNG>{};
		auto rPrng = PRNG{};
		auto socks = std::vector<oc::Socket>{};
		auto req = BaseRequest{};
		auto reqs = std::vector<BaseRequest>{};
		auto temp = std::vector<u8>{};
		auto res = macoro::result<void>{};
		auto reqChecks = std::map<CorType, oc::RandomOracle>{};
		auto theirReq = std::vector<ReqInfo>{};
		std::vector<SpinBanks> banks;
		auto threadState = std::vector<BatchThreadState>{};

		std::exception_ptr failure;
		try
		{
		setTimePoint("GenState::start");
		mOtBatch = {};
		mOleBatch = {};

		// Bind public worker IDs and the private stream lifetime to this peer session.
        // The request schedule already fixes the batch index on both sides.
        if (!mMock && !mBatches.empty())
        {
            std::array<u64, 6> config{1, static_cast<u64>(mNoise), mNumConcurrent, mBatchSize,
                mPartyIdx, mPrng.get<u64>()};
            std::array<u64, 6> peer{};
            auto exchanged = co_await macoro::when_all_ready(mSock.send(config), mSock.recv(peer));
            std::get<0>(exchanged).result();
            std::get<1>(exchanged).result();
            if (peer[0] != config[0] || peer[1] != config[1] || peer[3] != mBatchSize ||
                peer[4] != 1 - mPartyIdx || (mNoise == oc::SdNoiseDistribution::Stationary && peer[2] != mNumConcurrent))
                throw std::runtime_error("Peer correlation mode, worker count, batch size or role mismatch");
            if (mNoise == oc::SdNoiseDistribution::Stationary)
            {
                const block session(mPartyIdx ? peer[5] : config[5], mPartyIdx ? config[5] : peer[5]);
                banks = mBanks->acquire(mNumConcurrent);
                threadState.resize(mNumConcurrent);
                for (u64 i = 0; i < mNumConcurrent; ++i)
                    threadState[i].mStationary = std::make_unique<StationaryWorker>(i, mPartyIdx, session, banks[i]);
            }
        }

        // make base OT requests
		reqs.reserve(mBatches.size());
		for (auto i = 0ull; i < mBatches.size();++i)
		{
			auto& batch = *mBatches[i];
			if (!batch.mSize)
				std::terminate();
			if (mNoise == oc::SdNoiseDistribution::Regular)
				reqs.push_back(batch.getBaseRequest());
		}

		if (mDebug)
		{

			for (auto i = 0ull; i < mReqs.size(); ++i)
			{
				co_await mSock.send(coproto::copy(mReqs));
				co_await mSock.recvResize(theirReq);
				for (i = 0; i < theirReq.size(); ++i)
					theirReq[i].mRole ^= 1;
			}
			if (mReqs != theirReq)
			{
				std::lock_guard<std::mutex> lock(oc::gIoStreamMtx);
				std::cout << "party " << mPartyIdx << std::endl;
				for (auto i = 0ull; i < mReqs.size();++i)
				{
					bool failed = false;
					ReqInfo exp = { CorType::Ole,0,0 };
					if (theirReq.size() > i)
					{
						exp = theirReq[i];
						if (exp != mReqs[i])
							failed = true;
					}
					else
						failed = true;

					if (failed)
					{
						std::cout << oc::Color::Red;
					}

					auto t = mReqs[i].mType;
					auto r = mReqs[i].mRole;
					auto s = mReqs[i].mSize;

					std::cout << "request " << i << ": " << t << "." << r << " " << s;

					if (failed)
					{
						if (exp.mSize)
						{
							std::cout << " ~  theirs " << theirReq[i].mType << "." << theirReq[i].mRole << " " << theirReq[i].mSize;
						}
						std::cout << oc::Color::Default;
					}

					std::cout << std::endl;
				}
				throw RTE_LOC;
			}
			setTimePoint("GenState::debug");

		}

		req = BaseRequest(reqs);

		//socks[0] = mSock;
		//socks[1] = mSock.fork();
		prngs.reserve(4);
		socks.reserve(4);

		for (u64 i = 0; i < 2; ++i)
		{
			if (i ^ mPartyIdx)
			{
				if (req.mSendSize)
				{
					socks.push_back(mSock.fork());
					prngs.push_back(mPrng.fork());
					base.mOtSendMsg.resize(req.mSendSize);
					protos.push_back(mSendOtBase.send(base.mOtSendMsg, prngs.back(), socks.back()));
				}

				if (req.mSendVoleSize)
				{
					socks.push_back(mSock.fork());
					prngs.push_back(mPrng.fork());
					base.mVoleDelta = mPrng.get();
					base.mVoleB.resize(req.mSendVoleSize);
					protos.push_back(mSendVoleBase.sendChosen(base.mVoleDelta, base.mVoleB, prngs.back(), socks.back()));
				}
			}
			else
			{
				// perform recv base OTs
				if (req.mChoice.size())
				{
					socks.push_back(mSock.fork());
					prngs.push_back(mPrng.fork());
					base.mOtRecvMsg.resize(req.mChoice.size());
					base.mOtRecvChoice = std::move(req.mChoice);
					protos.push_back(mRecvOtBase.receive(base.mOtRecvChoice, base.mOtRecvMsg, prngs.back(), socks.back()));
				}

				if (req.mVoleChoice.size())
				{
					socks.push_back(mSock.fork());
					prngs.push_back(mPrng.fork());
					base.mVoleA.resize(req.mVoleChoice.size());
					base.mVoleChoice = std::move(req.mVoleChoice);
					protos.push_back(mRecvVoleBase.receiveChosen(base.mVoleChoice, base.mVoleA, prngs.back(), socks.back()));
				}
			}
		}

		for (auto i = 0ull; i < protos.size(); ++i)
		{
			if (mPool)
				tasks.emplace_back(std::move(protos[i]) | macoro::start_on(*mPool));
			else
				tasks.emplace_back(std::move(protos[i]) | macoro::make_eager());

			//co_await tasks.back());
		}

		for (auto i = 0ull; i < tasks.size(); ++i)
			co_await tasks[i];

		setTimePoint("GenState::base");

		threadState.resize(mNumConcurrent);



		for (u64 i = 0ull, j = -mNumConcurrent + 1; j != mBatches.size(); ++i, ++j)
		{
			if (i < mBatches.size())
			{
				co_await mBatches[i]->mStart;

				if (mBatches[i]->mAbort == false)
				{
					auto& batch = *mBatches[i];
					if (mNoise == oc::SdNoiseDistribution::Regular)
                    {
                        batch.setBase(base);
                        if (!mMock)
                            prepareRegularSpin(threadState[i % mNumConcurrent].mSpin, batch.mSize);
                    }

					setTimePoint("GenState::batch.begin " + std::to_string(i));

					if (mPool)
					{
						// launch the next batch
						threadState[i % mNumConcurrent].mTask =
							mBatches[i]->run(threadState[i % mNumConcurrent]) |
							macoro::start_on(*mPool);
					}
					else
					{
						// launch the next batch
						threadState[i % mNumConcurrent].mTask =
							mBatches[i]->run(threadState[i % mNumConcurrent]) |
							macoro::make_eager();
					}
				}
			}


			// join the previous batch
			if (j < mBatches.size())
			{
				if (threadState[j % mNumConcurrent].mTask.handle())
					co_await threadState[j % mNumConcurrent].mTask;

				setTimePoint("GenState::batch.end " + std::to_string(j));
				mBatches[j] = {};
			}
		}

		mBatches = {};
        if (!banks.empty())
            mBanks->release(std::move(banks));
        setTimePoint("GenState::done ");
        co_return;
        }
        catch (...)
        {
            failure = std::current_exception();
        }

        // Keep worker and base storage alive until all suspended users unwind.
        abort();
        if (!mSock.closed())
            co_await mSock.close();
        for (auto& task : tasks)
            if (task.handle() && !task.is_ready())
                try { co_await task; } catch (...) {}
        for (auto& worker : threadState)
            if (worker.mTask.handle() && !worker.mTask.is_ready())
                try { co_await worker.mTask; } catch (...) {}
        for (auto& batch : mBatches)
        {
            if (batch && !batch->mCorReady.is_set())
            {
                batch->mError = failure;
                batch->mCorReady.set();
            }
        }
        std::rethrow_exception(failure);
	}

	//void GenState::set(SendBase& b) { auto v = b.get(); mRecvBase.setBaseOts(v); }
	//void GenState::set(RecvBase& b) { auto v = b.get(); mSendBase.setBaseOts(v, b.mChoice); }

}