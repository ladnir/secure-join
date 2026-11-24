#include "TritOtBatch.h"
#include "CorGenerator.h"
#include "secure-join/Util/match.h"
#include "secure-join/Prf/mod3.h"
#include "secure-join/Util/Simd.h"

namespace secJoin
{



	TritOtBatch::TritOtBatch(GenState* state, bool sender, oc::Socket&& s, PRNG&& p)
		: Batch(state, std::move(s), std::move(p))
	{
		if (sender)
			mSendRecv.emplace<0>();
		else
			mSendRecv.emplace<1>();
	}

	void TritOtBatch::getCor(Cor* c, u64 begin, u64 size)
	{
		if (c->mType != CorType::TritOt)
			std::terminate();


		assert(begin % 128 == 0);
		assert(size % 128 == 0);
		//d.mAdd = mAdd.subspan(begin / 128, size / 128);
		//d.mMult = mMult.subspan(begin / 128, size / 128);

		mSendRecv | match{
			[&](SendBatch& send) {
				auto& d = *static_cast<TritOtSend*>(c);
				for (u64 i = 0; i < 2; ++i)
				{
					d.mLsb[i] = send.mLsb[i].subspan(begin / 128, size / 128);
					d.mMsb[i] = send.mMsb[i].subspan(begin / 128, size / 128);

					//d.mOts[i] = send.mOts[i].subspan(begin / 128, size / 128);
				}
			},

			[&](RecvBatch& recv) {
				auto& d = *static_cast<TritOtRecv*>(c);
				d.mChoice = recv.mChoice.subspan(begin / 128, size / 128);
				d.mLsb = recv.mLsb.subspan(begin / 128, size / 128);
				d.mMsb = recv.mMsb.subspan(begin / 128, size / 128);
			}
		};
	}

	BaseRequest TritOtBatch::getBaseRequest()
	{
		BaseRequest r;

		if (mGenState->mMock)
			return r;

		mSendRecv | match{
			[&](SendBatch& send) {
				configureBatchOt(send.mSender, mSize);
				r.mSendSize = send.mSender.baseCount().mBaseOtCount;
			},

			[&](RecvBatch& recv) {
				configureBatchOt(recv.mReceiver, mSize);
				r.mChoice = recv.mReceiver.sampleBaseChoiceBits(mPrng);
			}
		};
		return r;
	}

	void TritOtBatch::setBase(BaseCor& base)
	{

		if (mGenState->mMock)
			return;

		mSendRecv | match{
			[&](SendBatch& send) {
				send.mSender.setBaseCors(base.getSendOt(send.mSender.baseCount().mBaseOtCount), {}, mPrng.get<block>());
			},
			[&](RecvBatch& recv) {
				base.setOtBase(recv.mReceiver);
			}
		};
	}

	macoro::task<> TritOtBatch::getTask(BatchThreadState& threadState)
	{
		return mSendRecv | match{
			[&](SendBatch& send) {
				   return send.sendTask(mGenState, mIndex, mSize, mPrng, mSock, mCorReady, threadState);
			},
			[&](RecvBatch& recv) {
				  return  recv.recvTask(mGenState, mIndex, mSize, mPrng, mSock, mCorReady, threadState);
			}
		};
	}

	void TritOtBatch::SendBatch::mock(u64 batchIdx, u64 n)
	{

		assert(n % 128 == 0);
		auto  m = n / 128;
		mLsb[0].resize(m);
		mLsb[1].resize(m);
		mMsb[0].resize(m);
		mMsb[1].resize(m);

		if (0)
		{
			for (u64 j = 0; j < 2; ++j)
			{
				osuCrypto::setBytes(mLsb[j], 0);
				osuCrypto::setBytes(mMsb[j], 0);
			}
			return;
		}
		else
		{
			std::array<block, 2> lsb, msb;
			lsb[0] = block(12342342342134231234ull, 2342341234123421341ull) & oc::CCBlock;;
			lsb[1] = block(13563456435643564356ull, 5734542341345236357ull) & oc::CCBlock;;
			msb[0] = ~lsb[0] & oc::CCBlock;
			msb[1] = ~lsb[1] & oc::CCBlock;

			assert((lsb[0] & msb[0]) == oc::ZeroBlock);
			assert((lsb[1] & msb[1]) == oc::ZeroBlock);

			for (u64 i = 0; i < m; ++i)
			{
				mLsb[0][i] = lsb[0];
				mLsb[1][i] = lsb[1];
				mMsb[0][i] = msb[0];
				mMsb[1][i] = msb[1];
			}
		}

	}

	void TritOtBatch::RecvBatch::mock(u64 batchIdx, u64 n)
	{
		assert(n % 128 == 0);
		auto  m = n / 128;
		mChoice.resize(m);
		mLsb.resize(m);
		mMsb.resize(m);
		if (0)
		{
			osuCrypto::setBytes(mChoice, 0);
			osuCrypto::setBytes(mLsb, 0);
			osuCrypto::setBytes(mMsb, 0);
			return;
		}
		else
		{
			std::array<block, 2> lsb, msb;
			block choice = block(343524129893458929ull, 2453289232749293483ull);
			lsb[0] = block(12342342342134231234ull, 2342341234123421341ull) & oc::CCBlock;;
			lsb[1] = block(13563456435643564356ull, 5734542341345236357ull) & oc::CCBlock;;
			msb[0] = ~lsb[0] & oc::CCBlock;
			msb[1] = ~lsb[1] & oc::CCBlock;

			assert((lsb[0] & msb[0]) == oc::ZeroBlock);
			assert((lsb[1] & msb[1]) == oc::ZeroBlock);

			for (u64 i = 0; i < m; ++i)
			{
				mChoice[i] = choice ^ block((i << 32) + i, (i << 32) + i);
				mLsb[i] = (lsb[0] & ~mChoice[i]) ^ (lsb[1] & mChoice[i]);
				mMsb[i] = (msb[0] & ~mChoice[i]) ^ (msb[1] & mChoice[i]);
			}
		}

		////auto m = add.size();
		//auto m8 = m / 8 * 8;
		//oc::block mm8(4532453452, 43254534);
		//oc::block mm = oc::mAesFixedKey.ecbEncBlock(oc::block(batchIdx, 0));
		//block diff[4];
		//for (u64 i = 0; i < 4; ++i)
		//	diff[i] = block(409890897878905234 * i + 45234523, 5234565646423452 * i + 409890897878905234);
		//u64 i = 0;
		//std::array< std::array<block, 8>, 4> ots;

		//while (i < m8)
		//{

		//	ots[0].data()[0] = mm;
		//	ots[0].data()[1] = mm >> 1 | mm << 1;
		//	ots[0].data()[2] = mm >> 2 | mm << 2;
		//	ots[0].data()[3] = mm >> 3 | mm << 3;
		//	ots[0].data()[4] = mm >> 4 | mm << 4;
		//	ots[0].data()[5] = mm >> 5 | mm << 5;
		//	ots[0].data()[6] = mm >> 6 | mm << 6;
		//	ots[0].data()[7] = mm >> 7 | mm << 7;

		//	mOts.data()[i + 0] = ots[0][0] & (~mChoiceMsb[i + 0] & ~mChoiceLsb[i + 0]);
		//	mOts.data()[i + 1] = ots[0][1] & (~mChoiceMsb[i + 1] & ~mChoiceLsb[i + 1]);
		//	mOts.data()[i + 2] = ots[0][2] & (~mChoiceMsb[i + 2] & ~mChoiceLsb[i + 2]);
		//	mOts.data()[i + 3] = ots[0][3] & (~mChoiceMsb[i + 3] & ~mChoiceLsb[i + 3]);
		//	mOts.data()[i + 4] = ots[0][4] & (~mChoiceMsb[i + 4] & ~mChoiceLsb[i + 4]);
		//	mOts.data()[i + 5] = ots[0][5] & (~mChoiceMsb[i + 5] & ~mChoiceLsb[i + 5]);
		//	mOts.data()[i + 6] = ots[0][6] & (~mChoiceMsb[i + 6] & ~mChoiceLsb[i + 6]);
		//	mOts.data()[i + 7] = ots[0][7] & (~mChoiceMsb[i + 7] & ~mChoiceLsb[i + 7]);


		//	for (u64 j = 1; j < 4; ++j)
		//	{
		//		ots[j].data()[0] = ots[0].data()[0] ^ diff[j];
		//		ots[j].data()[1] = ots[0].data()[1] ^ diff[j];
		//		ots[j].data()[2] = ots[0].data()[2] ^ diff[j];
		//		ots[j].data()[3] = ots[0].data()[3] ^ diff[j];
		//		ots[j].data()[4] = ots[0].data()[4] ^ diff[j];
		//		ots[j].data()[5] = ots[0].data()[5] ^ diff[j];
		//		ots[j].data()[6] = ots[0].data()[6] ^ diff[j];
		//		ots[j].data()[7] = ots[0].data()[7] ^ diff[j];
		//		diff[j] = diff[j] + diff[j] ^ block(342134123, 213412341);

		//		auto mLsb = block(~0ull * !bool(j & 1), ~0ull * !bool(j & 1));
		//		auto mMsb = block(~0ull * !bool(j & 2), ~0ull * !bool(j & 2));
		//		mOts.data()[i + 0] ^= ots[j][0] & ((mMsb ^ mChoiceMsb[i + 0]) & (mLsb ^ mChoiceLsb[i + 0]));
		//		mOts.data()[i + 1] ^= ots[j][1] & ((mMsb ^ mChoiceMsb[i + 1]) & (mLsb ^ mChoiceLsb[i + 1]));
		//		mOts.data()[i + 2] ^= ots[j][2] & ((mMsb ^ mChoiceMsb[i + 2]) & (mLsb ^ mChoiceLsb[i + 2]));
		//		mOts.data()[i + 3] ^= ots[j][3] & ((mMsb ^ mChoiceMsb[i + 3]) & (mLsb ^ mChoiceLsb[i + 3]));
		//		mOts.data()[i + 4] ^= ots[j][4] & ((mMsb ^ mChoiceMsb[i + 4]) & (mLsb ^ mChoiceLsb[i + 4]));
		//		mOts.data()[i + 5] ^= ots[j][5] & ((mMsb ^ mChoiceMsb[i + 5]) & (mLsb ^ mChoiceLsb[i + 5]));
		//		mOts.data()[i + 6] ^= ots[j][6] & ((mMsb ^ mChoiceMsb[i + 6]) & (mLsb ^ mChoiceLsb[i + 6]));
		//		mOts.data()[i + 7] ^= ots[j][7] & ((mMsb ^ mChoiceMsb[i + 7]) & (mLsb ^ mChoiceLsb[i + 7]));

		//	}
		//	//for (u64 ii = i; ii < i + 8; ++ii)
		//	//{
		//	//	std::cout << "r ots " << ii << " ";
		//	//	for (u64 j = 0; j < 4; ++j)
		//	//		std::cout << ots[j][ii] << " ";
		//	//	std::cout << std::endl;
		//	//}

		//	mm += mm8;
		//	i += 8;
		//}
		//for (; i < m; ++i)
		//{
		//	ots[0][0] = mm;
		//	mOts[i] = ots[0][0] & (~mChoiceMsb[i + 0] & ~mChoiceLsb[i + 0]);


		//	for (u64 j = 1; j < 4; ++j)
		//	{
		//		ots[j][0] = ots[0][0] ^ diff[j];

		//		auto mLsb = block(~0ull * !bool(j & 1), ~0ull * !bool(j & 1));
		//		auto mMsb = block(~0ull * !bool(j & 2), ~0ull * !bool(j & 2));
		//		mOts[i] ^= ots[j][0] & ((mMsb ^ mChoiceMsb[i]) & (mLsb ^ mChoiceLsb[i]));
		//	}

		//	//std::cout << "r diff " << i << " ";
		//	//for (u64 j = 0; j < 4; ++j)
		//	//	std::cout << diff[j] << " ";
		//	//std::cout << std::endl;
		//	//std::cout << "r ots " << i << " ";
		//	//for (u64 j = 0; j < 4; ++j)
		//	//	std::cout << ots[j][0] << " ";
		//	//std::cout << std::endl;

		//}
	}


	macoro::task<>  TritOtBatch::RecvBatch::recvTask(
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
		std::vector<std::array<block, 2>> baseSend;

		if (state->mMock)
		{
			mock(batchIdx, size);
		}
		else if (threadState.mStationary)
        {
            auto& worker = *threadState.mStationary;
            auto& protocol = worker.begin(worker.recvOt, CorType::TritOt, size, false);
            co_await protocol.silentReceiveInplace(size, prng, sock, oc::ChoiceBitPacking::True);
            compressRecver(protocol.mA);
            worker.finish(protocol);
        }
        else
		{

			if (state->mDebug)
			{
				baseSend.resize(mReceiver.baseCount().mBaseOtCount);
				co_await sock.recv(baseSend);

				{
					for (u64 i = 0; i < baseSend.size(); ++i)
					{
						if (std::get<0>(mReceiver.mGenVar).mBaseOTs(i) != baseSend[i][std::get<0>(mReceiver.mGenVar).mBaseChoices(i)])
						{
							std::cout << "F4RecvOleBatch::getTask() base OTs do not match. " << LOCATION << std::endl;
							std::terminate();
						}
					}
				}
			}

			assert(mReceiver.hasBaseCors());
			mReceiver.mA = std::move(threadState.mA);
			mReceiver.mSpin = std::move(threadState.mSpin);
			std::get<0>(mReceiver.mGenVar).mTempBuffer = std::move(threadState.mPprfTemp);
			std::get<0>(mReceiver.mGenVar).mEagerSend = false;

			mReceiver.mDebug = state->mDebug;

			co_await mReceiver.silentReceiveInplace(mReceiver.mRequestNumOts, prng, sock, oc::ChoiceBitPacking::True);


			compressRecver(mReceiver.mA);

			threadState.mA = std::move(mReceiver.mA);
			threadState.mSpin = std::move(mReceiver.mSpin);
			threadState.mPprfTemp = std::move(std::get<0>(mReceiver.mGenVar).mTempBuffer);

		}

		corReady.set();
	}

	macoro::task<>  TritOtBatch::SendBatch::sendTask(
		GenState* state,
		u64 batchIdx,
		u64 size,
		PRNG& prng,
		oc::Socket& sock,
		macoro::async_manual_reset_event& corReady,
		BatchThreadState& threadState)
	{
		//M_C_BEGIN(macoro::task<>, this, state, batchIdx, size, &prng,
		//	&sock, &corReady, &threadState);

		if (state->mMock)
		{
			mock(batchIdx, size);
		}
		else if (threadState.mStationary)
        {
            auto& worker = *threadState.mStationary;
            auto& protocol = worker.begin(worker.sendOt, CorType::TritOt, size, true);
            co_await protocol.silentSendInplace(protocol.mDelta, size, prng, sock);
            compressSender(*protocol.mDelta, protocol.mB);
            worker.finish(protocol);
        }
        else
		{

			if (state->mDebug)
			{
				co_await sock.send(coproto::copy(std::get<0>(mSender.mGenVar).mBaseOTs));
			}

			assert(mSender.hasBaseCors());


			mSender.mB = std::move(threadState.mB);
			mSender.mSpin = std::move(threadState.mSpin);
			std::get<0>(mSender.mGenVar).mTempBuffer = std::move(threadState.mPprfTemp);
			std::get<0>(mSender.mGenVar).mEagerSend = false;

			mSender.mDebug = state->mDebug;


			co_await mSender.silentSendInplace(mSender.mDelta, mSender.mRequestNumOts, prng, sock);


			compressSender(*mSender.mDelta, mSender.mB);

			threadState.mB = std::move(mSender.mB);
			threadState.mSpin = std::move(mSender.mSpin);
			threadState.mPprfTemp = std::move(std::get<0>(mSender.mGenVar).mTempBuffer);
		}


		corReady.set();
	}



	// the first bit of A is the choice bit of the VOLE.
	void TritOtBatch::RecvBatch::compressRecver(
		span<oc::block> A)
	{

		if (A.size() % 128)
			throw RTE_LOC;

		mChoice.resize(A.size() / 128);
		mLsb.resize(A.size() / 128);
		mMsb.resize(A.size() / 128);
		auto choiceIter8 = (u8*)mChoice.data();
		auto lsbIter = (u8*)mLsb.data();
		auto msbIter = (u8*)mMsb.data();

		block OneBlock = block(0, 1);
		block mask = block(~0ull, ~1ull);
		std::array<block, 8> aa;

		for (u64 i = 0; i < A.size(); i += 8)
		{
			u32 choice[8];
			auto m = &A[i];

			// extract the choice bit from the LSB of m
			storeu_si32(&choice[0], m[0] & OneBlock);
			storeu_si32(&choice[1], m[1] & OneBlock);
			storeu_si32(&choice[2], m[2] & OneBlock);
			storeu_si32(&choice[3], m[3] & OneBlock);
			storeu_si32(&choice[4], m[4] & OneBlock);
			storeu_si32(&choice[5], m[5] & OneBlock);
			storeu_si32(&choice[6], m[6] & OneBlock);
			storeu_si32(&choice[7], m[7] & OneBlock);

			assert(choice[0] == (m[0].get<u64>(0) & 1));

			// pack the choice bits.
			*choiceIter8++ =
				choice[0] ^
				(choice[1] << 1) ^
				(choice[2] << 2) ^
				(choice[3] << 3) ^
				(choice[4] << 4) ^
				(choice[5] << 5) ^
				(choice[6] << 6) ^
				(choice[7] << 7);

			// mask of the choice bits which is stored in the LSB
			aa[0] = m[0] & mask;
			aa[1] = m[1] & mask;
			aa[2] = m[2] & mask;
			aa[3] = m[3] & mask;
			aa[4] = m[4] & mask;
			aa[5] = m[5] & mask;
			aa[6] = m[6] & mask;
			aa[7] = m[7] & mask;

			oc::mAesFixedKey.hashBlocks<8>(aa.data(), aa.data());
			sample8Mod3(aa.data(), *msbIter++, *lsbIter++);
		}
	}


	void TritOtBatch::SendBatch::compressSender(
		block delta,
		span<oc::block> B)
	{

		if (B.size() % 128)
			throw RTE_LOC;

		for (u64 i = 0; i < 2; ++i)
		{
			mLsb[i].resize(B.size() / 128);
			mMsb[i].resize(B.size() / 128);
		}

		std::array<u8*, 2> lsbIter
		{
			(u8*)mLsb[0].data(),
			(u8*)mLsb[1].data()
		};
		std::array<u8*, 2> msbIter
		{
			(u8*)mMsb[0].data(),
			(u8*)mMsb[1].data()
		};

		std::array<block, 8> sendMsg,aa;
		auto mask = block(~0ull, ~1ull);
		delta = delta & mask;

		for (u64 i = 0; i < B.size(); i += 8)
		{
			auto s = sendMsg.data();
			auto m = B.data() + i;

			aa[0] = m[0] & mask;
			aa[1] = m[1] & mask;
			aa[2] = m[2] & mask;
			aa[3] = m[3] & mask;
			aa[4] = m[4] & mask;
			aa[5] = m[5] & mask;
			aa[6] = m[6] & mask;
			aa[7] = m[7] & mask;

			oc::mAesFixedKey.hashBlocks<8>(aa.data(), s);
			sample8Mod3(s, *msbIter[0], *lsbIter[0]);

			assert((*msbIter[0] & *lsbIter[0]) == 0);

			msbIter[0]++;
			lsbIter[0]++;

			aa[0] = aa[0] ^ delta;
			aa[1] = aa[1] ^ delta;
			aa[2] = aa[2] ^ delta;
			aa[3] = aa[3] ^ delta;
			aa[4] = aa[4] ^ delta;
			aa[5] = aa[5] ^ delta;
			aa[6] = aa[6] ^ delta;
			aa[7] = aa[7] ^ delta;
			oc::mAesFixedKey.hashBlocks<8>(aa.data(), aa.data());
			sample8Mod3(aa.data(), *msbIter[1], *lsbIter[1]);
			assert((*msbIter[1] & *lsbIter[1]) == 0);
			msbIter[1]++;
			lsbIter[1]++;
		}
	}

}