#include "F4BitOtBatch.h"
#include "CorGenerator.h"
#include "secure-join/Util/match.h"
#include "secure-join/Util/Simd.h"
#include <cstring>

namespace secJoin
{



	F4BitOtBatch::F4BitOtBatch(GenState* state, bool sender, oc::Socket&& s, PRNG&& p)
		: Batch(state, std::move(s), std::move(p))
		, mSendRecv(std::in_place_type_t<SendBatch>{})
	{
		if (sender)
			mSendRecv.emplace<0>();
		else
			mSendRecv.emplace<1>();
	}

	void F4BitOtBatch::getCor(Cor* c, u64 begin, u64 size)
	{
		if (c->mType != CorType::F4BitOt)
			std::terminate();


		//auto& d = *static_cast<BinOle*>(c);
		assert(begin % 128 == 0);
		assert(size % 128 == 0);
		//d.mAdd = mAdd.subspan(begin / 128, size / 128);
		//d.mMult = mMult.subspan(begin / 128, size / 128);

		mSendRecv | match{
			[&](SendBatch& send) {
				auto& d = *static_cast<F4BitOtSend*>(c);
				for (u64 i = 0; i < 4; ++i)
					d.mOts[i] = send.mOts[i].subspan(begin / 128, size / 128);
			},

			[&](RecvBatch& recv) {
				auto& d = *static_cast<F4BitOtRecv*>(c);
				d.mOts = recv.mOts.subspan(begin / 128, size / 128);
				d.mChoiceLsb = recv.mChoiceLsb.subspan(begin / 128, size / 128);
				d.mChoiceMsb = recv.mChoiceMsb.subspan(begin / 128, size / 128);
			}
		};
	}

	BaseRequest F4BitOtBatch::getBaseRequest(StationaryWorker* worker)
	{
		BaseRequest r;

		if (mGenState->mMock)
			return r;

		mSendRecv | match{
			[&](SendBatch& send) {
				if (worker)
				{
					auto [protocol, fresh] = worker->stream(worker->sendF4, CorType::F4BitOt, mSize, true);
					if (fresh)
						r.mSendSize = protocol.silentBaseOtCount();
					r.mSendVoleSize = protocol.baseVoleCount();
				}
				else
				{
					send.mSender.configure(mSize);
					r.mSendSize = send.mSender.silentBaseOtCount();
					r.mSendVoleSize = send.mSender.baseVoleCount();
				}
			},
			[&](RecvBatch& recv) {
				if (worker)
				{
					auto [protocol, fresh] = worker->stream(worker->recvF4, CorType::F4BitOt, mSize, false);
					if (fresh)
					{
						r.mChoice = protocol.sampleBaseChoiceBits(mPrng);
						protocol.recordPoints();
					}
					// Every round gets new uniform coefficients, including zero.
					r.mVoleChoice.resize(protocol.baseVoleCount());
					for (auto& c : r.mVoleChoice)
						c.mVal = mPrng.get<u8>() & 3;
				}
				else
				{
					recv.mReceiver.configure(mSize);
					r.mChoice = recv.mReceiver.sampleBaseChoiceBits(mPrng);
					r.mVoleChoice = recv.mReceiver.sampleBaseVoleVals(mPrng);
				}
			}
		};
		return r;
	}

	void F4BitOtBatch::setBase(BaseCor& base, StationaryWorker* worker)
	{

		if (mGenState->mMock)
			return;

		mSendRecv | match{
			[&](SendBatch& send) {
				send.mDelta = base.mVoleDelta;
				auto& protocol = worker ? worker->sendF4.at({CorType::F4BitOt, mSize}) : send.mSender;
				protocol.setSilentBaseOts(base.getSendOt(protocol.silentBaseOtCount()),
					base.getSendVole(protocol.baseVoleCount()));
			},
			[&](RecvBatch& recv) {
				auto& protocol = worker ? worker->recvF4.at({CorType::F4BitOt, mSize}) : recv.mReceiver;
				const auto count = protocol.baseVoleCount();
				const auto offset = base.mVoleRecvIndex;
				auto shares = base.getRecvVole(count);
				if (worker)
				{
					protocol.mBaseC.resize(count);
					std::copy_n(base.mVoleChoice.begin() + offset, count, protocol.mBaseC.begin());
				}
				protocol.setSilentBaseOts(base.getRecvOt(protocol.silentBaseOtCount()), shares);
			}
		};
	}

	macoro::task<> F4BitOtBatch::getTask(BatchThreadState& threadState)
	{
		return mSendRecv | match{
			[&](SendBatch& send) {
				   return send.sendTask(mGenState, mIndex, mSize, mPrng, mSock, mCorReady, threadState);
			},
			[&](RecvBatch& recv) {
				  return  recv.recvTask(mGenState,mIndex, mSize, mPrng, mSock, mCorReady, threadState);
			}
		};
	}

	//void ThreeOleBatch::mock(u64 batchIdx)
	//{

	//    mCorReady.set();

	//}
	void F4BitOtBatch::SendBatch::mock(u64 batchIdx, u64 n)
	{
		//osuCrypto::setBytes(add, 0);
		//osuCrypto::setBytes(mult, 0);
		//return;
		assert(n % 128 == 0);
		auto  m = n / 128;
		mOts[0].resize(m);
		mOts[1].resize(m);
		mOts[2].resize(m);
		mOts[3].resize(m);

		if (1)
		{
			for (u64 j = 0; j < 4; ++j)
				osuCrypto::setBytes(mOts[j], 0);
			return;
		}

		//auto m = add.size();
		auto m8 = m / 8 * 8;
		oc::block mm8(4532453452, 43254534);
		oc::block mm = oc::mAesFixedKey.ecbEncBlock(oc::block(batchIdx, 0));
		block diff[4];
		for (u64 i = 0; i < 4; ++i)
			diff[i] = block(409890897878905234 * i + 45234523, 5234565646423452 * i + 409890897878905234);
		u64 i = 0;
		while (i < m8)
		{
			mOts[0].data()[i + 0] = mm;
			mOts[0].data()[i + 1] = mm >> 1 | mm << 1;
			mOts[0].data()[i + 2] = mm >> 2 | mm << 2;
			mOts[0].data()[i + 3] = mm >> 3 | mm << 3;
			mOts[0].data()[i + 4] = mm >> 4 | mm << 4;
			mOts[0].data()[i + 5] = mm >> 5 | mm << 5;
			mOts[0].data()[i + 6] = mm >> 6 | mm << 6;
			mOts[0].data()[i + 7] = mm >> 7 | mm << 7;

			for (u64 j = 1; j < 4; ++j)
			{
				mOts[j].data()[i + 0] = mOts[0].data()[i + 0] ^ diff[j];
				mOts[j].data()[i + 1] = mOts[0].data()[i + 1] ^ diff[j];
				mOts[j].data()[i + 2] = mOts[0].data()[i + 2] ^ diff[j];
				mOts[j].data()[i + 3] = mOts[0].data()[i + 3] ^ diff[j];
				mOts[j].data()[i + 4] = mOts[0].data()[i + 4] ^ diff[j];
				mOts[j].data()[i + 5] = mOts[0].data()[i + 5] ^ diff[j];
				mOts[j].data()[i + 6] = mOts[0].data()[i + 6] ^ diff[j];
				mOts[j].data()[i + 7] = mOts[0].data()[i + 7] ^ diff[j];
				diff[j] = (diff[j] + diff[j]) ^ block(342134123, 213412341);
			}

			//for(u64 ii = i; ii < i+8; ++ii)
			//{
			//	std::cout << "s ots " << ii << " ";
			//	for (u64 j = 0; j < 4; ++j)
			//		std::cout << mOts[j][ii] << " ";
			//	std::cout << std::endl;
			//}

			mm += mm8;
			i += 8;
		}
		for (; i < m; ++i)
		{
			mOts[0][i] = mm;
			for (u64 j = 1; j < 4; ++j)
			{
				mOts[j][i] = mOts[0][i] ^ diff[j];
			}

			//std::cout << "s diff " << i << " ";
			//for (u64 j = 0; j < 4; ++j)
			//	std::cout << diff[j] << " ";
			//std::cout << std::endl;
			//std::cout << "s ots " << i << " ";
			//for (u64 j = 0; j < 4; ++j)
			//	std::cout << mOts[j][i] << " ";
			//std::cout << std::endl;
		}
	}

	void F4BitOtBatch::RecvBatch::mock(u64 batchIdx, u64 n)
	{

		//osuCrypto::setBytes(add, 0);
		//osuCrypto::setBytes(mult, 0);
		//return;
		//throw RTE_LOC;
		assert(n % 128 == 0);
		auto  m = n / 128;
		mOts.resize(m);
		mChoiceLsb.resize(m);
		mChoiceMsb.resize(m);
		if (1)
		{
			osuCrypto::setBytes(mOts, 0);
			osuCrypto::setBytes(mChoiceLsb, 0);
			osuCrypto::setBytes(mChoiceMsb, 0);
			return;
		}

		//auto m = add.size();
		auto m8 = m / 8 * 8;
		oc::block mm8(4532453452, 43254534);
		oc::block mm = oc::mAesFixedKey.ecbEncBlock(oc::block(batchIdx, 0));
		block diff[4];
		for (u64 i = 0; i < 4; ++i)
			diff[i] = block(409890897878905234 * i + 45234523, 5234565646423452 * i + 409890897878905234);
		u64 i = 0;
		std::array< std::array<block, 8>, 4> ots;

		while (i < m8)
		{

			ots[0].data()[0] = mm;
			ots[0].data()[1] = mm >> 1 | mm << 1;
			ots[0].data()[2] = mm >> 2 | mm << 2;
			ots[0].data()[3] = mm >> 3 | mm << 3;
			ots[0].data()[4] = mm >> 4 | mm << 4;
			ots[0].data()[5] = mm >> 5 | mm << 5;
			ots[0].data()[6] = mm >> 6 | mm << 6;
			ots[0].data()[7] = mm >> 7 | mm << 7;

			mOts.data()[i + 0] = ots[0][0] & (~mChoiceMsb[i + 0] & ~mChoiceLsb[i + 0]);
			mOts.data()[i + 1] = ots[0][1] & (~mChoiceMsb[i + 1] & ~mChoiceLsb[i + 1]);
			mOts.data()[i + 2] = ots[0][2] & (~mChoiceMsb[i + 2] & ~mChoiceLsb[i + 2]);
			mOts.data()[i + 3] = ots[0][3] & (~mChoiceMsb[i + 3] & ~mChoiceLsb[i + 3]);
			mOts.data()[i + 4] = ots[0][4] & (~mChoiceMsb[i + 4] & ~mChoiceLsb[i + 4]);
			mOts.data()[i + 5] = ots[0][5] & (~mChoiceMsb[i + 5] & ~mChoiceLsb[i + 5]);
			mOts.data()[i + 6] = ots[0][6] & (~mChoiceMsb[i + 6] & ~mChoiceLsb[i + 6]);
			mOts.data()[i + 7] = ots[0][7] & (~mChoiceMsb[i + 7] & ~mChoiceLsb[i + 7]);


			for (u64 j = 1; j < 4; ++j)
			{
				ots[j].data()[0] = ots[0].data()[0] ^ diff[j];
				ots[j].data()[1] = ots[0].data()[1] ^ diff[j];
				ots[j].data()[2] = ots[0].data()[2] ^ diff[j];
				ots[j].data()[3] = ots[0].data()[3] ^ diff[j];
				ots[j].data()[4] = ots[0].data()[4] ^ diff[j];
				ots[j].data()[5] = ots[0].data()[5] ^ diff[j];
				ots[j].data()[6] = ots[0].data()[6] ^ diff[j];
				ots[j].data()[7] = ots[0].data()[7] ^ diff[j];
				diff[j] = (diff[j] + diff[j]) ^ block(342134123, 213412341);

				auto mLsb = block(~0ull * !bool(j & 1), ~0ull * !bool(j & 1));
				auto mMsb = block(~0ull * !bool(j & 2), ~0ull * !bool(j & 2));
				mOts.data()[i + 0] ^= ots[j][0] & ((mMsb ^ mChoiceMsb[i + 0]) & (mLsb ^ mChoiceLsb[i + 0]));
				mOts.data()[i + 1] ^= ots[j][1] & ((mMsb ^ mChoiceMsb[i + 1]) & (mLsb ^ mChoiceLsb[i + 1]));
				mOts.data()[i + 2] ^= ots[j][2] & ((mMsb ^ mChoiceMsb[i + 2]) & (mLsb ^ mChoiceLsb[i + 2]));
				mOts.data()[i + 3] ^= ots[j][3] & ((mMsb ^ mChoiceMsb[i + 3]) & (mLsb ^ mChoiceLsb[i + 3]));
				mOts.data()[i + 4] ^= ots[j][4] & ((mMsb ^ mChoiceMsb[i + 4]) & (mLsb ^ mChoiceLsb[i + 4]));
				mOts.data()[i + 5] ^= ots[j][5] & ((mMsb ^ mChoiceMsb[i + 5]) & (mLsb ^ mChoiceLsb[i + 5]));
				mOts.data()[i + 6] ^= ots[j][6] & ((mMsb ^ mChoiceMsb[i + 6]) & (mLsb ^ mChoiceLsb[i + 6]));
				mOts.data()[i + 7] ^= ots[j][7] & ((mMsb ^ mChoiceMsb[i + 7]) & (mLsb ^ mChoiceLsb[i + 7]));

			}
			//for (u64 ii = i; ii < i + 8; ++ii)
			//{
			//	std::cout << "r ots " << ii << " ";
			//	for (u64 j = 0; j < 4; ++j)
			//		std::cout << ots[j][ii] << " ";
			//	std::cout << std::endl;
			//}

			mm += mm8;
			i += 8;
		}
		for (; i < m; ++i)
		{
			ots[0][0] = mm;
			mOts[i] = ots[0][0] & (~mChoiceMsb[i + 0] & ~mChoiceLsb[i + 0]);


			for (u64 j = 1; j < 4; ++j)
			{
				ots[j][0] = ots[0][0] ^ diff[j];

				auto mLsb = block(~0ull * !bool(j & 1), ~0ull * !bool(j & 1));
				auto mMsb = block(~0ull * !bool(j & 2), ~0ull * !bool(j & 2));
				mOts[i] ^= ots[j][0] & ((mMsb ^ mChoiceMsb[i]) & (mLsb ^ mChoiceLsb[i]));
			}

			//std::cout << "r diff " << i << " ";
			//for (u64 j = 0; j < 4; ++j)
			//	std::cout << diff[j] << " ";
			//std::cout << std::endl;
			//std::cout << "r ots " << i << " ";
			//for (u64 j = 0; j < 4; ++j)
			//	std::cout << ots[j][0] << " ";
			//std::cout << std::endl;

		}
	}


	macoro::task<>  F4BitOtBatch::RecvBatch::recvTask(
		GenState* state,
		u64 batchIdx,
		u64 size,
		PRNG& prng,
		oc::Socket& sock,
		macoro::async_manual_reset_event& corReady,
		BatchThreadState& threadState)
	{
		//M_C_BEGIN(macoro::task<>, this, state, batchIdx, size, &prng,
			//&sock, &corReady, &threadState,
			auto baseSend = std::vector<std::array<block, 2>>{};
			auto baseB = std::vector<block>{};
			auto baseDelta = block{};
		//);

		if (state->mMock)
		{
			mock(batchIdx, size);
		}
		else if (threadState.mStationary)
        {
            auto& worker = *threadState.mStationary;
            auto& protocol = worker.begin(worker.recvF4, CorType::F4BitOt, size, false);
            co_await protocol.silentReceiveInplace(size, prng, sock);
            compressRecver(protocol.mA);
            worker.finish(protocol);
        }
        else
		{

			if (state->mDebug)
			{
				baseSend.resize(mReceiver.silentBaseOtCount());
				baseB.resize(mReceiver.baseVoleCount());
				co_await sock.recv(baseSend);
				co_await sock.recv(baseB);
				co_await sock.recv(baseDelta);

				{
					for (u64 i = 0; i < baseSend.size(); ++i)
					{
						if (mReceiver.mGen.mBaseOTs(i) != baseSend[i][mReceiver.mGen.mBaseChoices(i)])
						{
							std::cout << "F4RecvOleBatch::getTask() base OTs do not match. " << LOCATION << std::endl;
							std::terminate();
						}
					}


					for (u64 i = 0; i < baseB.size(); ++i)
					{
						auto ci = mReceiver.mBaseC[i];
						auto ai = mReceiver.mBaseA[i] & block(~0ull, ~3ull);
						auto bi = baseB[i] & block(~0ull, ~3ull);

						block exp;
						CoeffCtxGF4{}.mul(exp, baseDelta & block(~0ull, ~3ull), ci);
						exp = exp ^ bi;

						if (ai != exp)
						{
							std::cout << "F4RecvOleBatch::getTask() base VOLE do not match. " << LOCATION << std::endl;
							std::cout << "ai  " << ai << std::endl;
							std::cout << "exp " << exp << std::endl;
							std::terminate();
						}
					}

				}
			}

			assert(mReceiver.mGen.hasBaseOts());
			mReceiver.mA = std::move(threadState.mA);
			mReceiver.mSpin = std::move(threadState.mSpin);
			mReceiver.mGen.mTempBuffer = std::move(threadState.mPprfTemp);
			mReceiver.mGen.mEagerSend = false;

			mReceiver.mDebug = state->mDebug;

			 co_await mReceiver.silentReceiveInplace(mReceiver.mRequestSize, prng, sock);


			compressRecver(mReceiver.mA);

			threadState.mA = std::move(mReceiver.mA);
			threadState.mSpin = std::move(mReceiver.mSpin);
			threadState.mPprfTemp = std::move(mReceiver.mGen.mTempBuffer);

		}

		corReady.set();
	}

	macoro::task<>  F4BitOtBatch::SendBatch::sendTask(
		GenState* state,
		u64 batchIdx,
		u64 size,
		PRNG& prng,
		oc::Socket& sock,
		macoro::async_manual_reset_event& corReady,
		BatchThreadState& threadState)
	{

		if (state->mMock)
		{
			mock(batchIdx, size);
		}
		else if (threadState.mStationary)
        {
            auto& worker = *threadState.mStationary;
            auto& protocol = worker.begin(worker.sendF4, CorType::F4BitOt, size, true);
            co_await protocol.silentSendInplace(mDelta, size, prng, sock);
            compressSender(protocol.mB);
            worker.finish(protocol);
        }
        else
		{

			if (state->mDebug)
			{
				co_await sock.send(coproto::copy(mSender.mGen.mBaseOTs));
				co_await sock.send(mSender.mBaseB);
				co_await sock.send(mDelta);

			}

			assert(mSender.mGen.hasBaseOts());


			mSender.mB = std::move(threadState.mB);
			mSender.mSpin = std::move(threadState.mSpin);
			mSender.mGen.mTempBuffer = std::move(threadState.mPprfTemp);
			mSender.mGen.mEagerSend = false;

			mSender.mDebug = state->mDebug;

			co_await mSender.silentSendInplace(mDelta, mSender.mRequestSize, prng, sock);


			compressSender(mSender.mB);

			threadState.mB = std::move(mSender.mB);
			threadState.mSpin = std::move(mSender.mSpin);
			threadState.mPprfTemp = std::move(mSender.mGen.mTempBuffer);
		}


		corReady.set();
	}

	// the first two bits of A is the choice bit of the VOLE.

	void F4BitOtBatch::RecvBatch::compressRecver(
		span<oc::block> A)
	{
		if (A.size() % 128)
			throw RTE_LOC;
		mOts.resize(A.size() / 128);
		mChoiceLsb.resize(A.size() / 128);
		mChoiceMsb.resize(A.size() / 128);
		auto aIter = reinterpret_cast<u8*>(mOts.data());
		auto lsbIter8 = (u8*)mChoiceLsb.data();
		auto msbIter8 = (u8*)mChoiceMsb.data();

		block mask = block(~0ull, ~3ull);

		auto m = A.data();

		for (u64 i = 0; i < A.size(); i += 16)
		{
			const block bytes = gatherLowBytes(m);
			const u16 lsb = packByteBits<0>(bytes);
			const u16 msb = packByteBits<1>(bytes);
			std::memcpy(lsbIter8, &lsb, sizeof(lsb));
			std::memcpy(msbIter8, &msb, sizeof(msb));
			lsbIter8 += sizeof(lsb);
			msbIter8 += sizeof(msb);
			for (u64 j = 0; j < 2; ++j)
			{
				// mask of the choice bits which is stored in the LSB
				m[0] = m[0] & mask;
				m[1] = m[1] & mask;
				m[2] = m[2] & mask;
				m[3] = m[3] & mask;
				m[4] = m[4] & mask;
				m[5] = m[5] & mask;
				m[6] = m[6] & mask;
				m[7] = m[7] & mask;

				oc::mAesFixedKey.hashBlocks<8>(m, m);

				m += 8;


			}
			m -= 16;

			const u16 ap = packByteBits<0>(gatherLowBytes(m));

			std::memcpy(aIter, &ap, sizeof(ap));
			aIter += sizeof(ap);
			m += 16;

		}
	}


	void F4BitOtBatch::SendBatch::compressSender(
		span<oc::block> B)
	{
		if (B.size() % 128)
			throw RTE_LOC;
		for (auto& ots : mOts)
			ots.resize(B.size() / 128);

		std::array<u8*, 4> iters
		{
			reinterpret_cast<u8*>(mOts[0].data()),
			reinterpret_cast<u8*>(mOts[1].data()),
			reinterpret_cast<u8*>(mOts[2].data()),
			reinterpret_cast<u8*>(mOts[3].data())
		};
		//auto bIter16 = (u16*)add.data();
		//auto aIter16 = (u16*)mult.data();
		//if (add.size() * 128 != B.size())
		//    throw RTE_LOC;
		//if (mult.size() * 128 != B.size())
		//    throw RTE_LOC;
		using block = oc::block;

		std::array<block, 16> sendMsg;
		//auto m = B.data();

		block mask = block(~0ull, ~3ull);
		std::array<block, 4> deltas;
		deltas[0] = block(0, 0);
		deltas[1] = mDelta & mask;
		CoeffCtxGF4{}.mul(deltas[2], deltas[1], F4{ 2 });
		CoeffCtxGF4{}.mul(deltas[3], deltas[1], F4{ 3 });

		for (u64 i = 0; i < B.size(); i += 16)
		{
			for (u64 b = 0; b < 4; ++b)
			{
				auto s = sendMsg.data();
				auto m = B.data() + i;

				switch (b)
				{
				case 0:
				{
					for (u64 j = 0; j < 2; ++j)
					{
						assert((m[0].get<u64>(0) & 3) == 0);

						oc::mAesFixedKey.hashBlocks<8>(m, s);

						s += 8;
						m += 8;
					}

					break;
				}
				case 1:
				case 2:
				case 3:
					for (u64 j = 0; j < 2; ++j)
					{
						s[0] = m[0] ^ deltas[b];
						s[1] = m[1] ^ deltas[b];
						s[2] = m[2] ^ deltas[b];
						s[3] = m[3] ^ deltas[b];
						s[4] = m[4] ^ deltas[b];
						s[5] = m[5] ^ deltas[b];
						s[6] = m[6] ^ deltas[b];
						s[7] = m[7] ^ deltas[b];

						oc::mAesFixedKey.hashBlocks<8>(s, s);

						s += 8;
						m += 8;
					}
					break;
				default:
					assert(0);
					//__assume(0);
					break;
				}

				const u16 ap = packByteBits<0>(gatherLowBytes(sendMsg.data()));

				assert(iters[b] + sizeof(ap) <= reinterpret_cast<u8*>(mOts[b].data() + mOts[b].size()));
				std::memcpy(iters[b], &ap, sizeof(ap));
				iters[b] += sizeof(ap);
			}
		}
	}


}