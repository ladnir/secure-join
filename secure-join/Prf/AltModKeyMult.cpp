#include "AltModKeyMult.h"
#include "AltModSimd.h"


namespace secJoin
{


	macoro::task<> AltModKeyMultReceiver::receiveKey()
	{
		OtRecv ots;
		co_await mRecvKeyReq.get(ots);
		if (ots.mChoice.size() != AltModPrf::KeySize)
			throw RTE_LOC;
		AltModPrf::BinaryKey key;
		osuCrypto::copyBytes(key, ots.mChoice.getSpan<u8>());
		setKeyOts(key, ots.mMsg);
		mRecvKeyReq.clear();
	}

	macoro::task<> AltModKeyMultReceiver::mult(
		u64 n,
		oc::Matrix<block>& xk0,
		oc::Matrix<block>& xk1,
		coproto::Socket& sock)
	{
		if (mRecvKeyReq.size())
			co_await receiveKey();

		// make sure we have a key.
		if (mKeyRecvOTs.size() != AltModPrf::KeySize)
			throw std::runtime_error("AltMod was called without a key and keyGen was not requested. " LOCATION);

		// debugging, make sure we have the correct key OTs.
		if (mDebug)
		{
			auto ots = oc::AlignedUnVector<std::array<block, 2>>{ mKeyRecvOTs.size() };
			co_await sock.recv(ots);
			for (u64 i = 0; i < ots.size(); ++i)
			{
				auto ki = *oc::BitIterator((u8*)&mKey, i);
				auto otik = mKeyRecvOTs[i].get<block>();
				if (ots[i][ki] != otik)
				{
					std::cout << "bad key ot " << i << "\nki=" << ki << " " << otik << " vs \n"
						<< ots[i][0] << " " << ots[i][1] << std::endl;
					throw RTE_LOC;
				}
			}
		}


		// for each bit of the key, perform an OT derandomization where we get a share
		// of the input x times the key mod 3. We store the LSB and MSB of the share separately.
		// Hence we need 2 * AltModWPrf::KeySize rows in xkShares
		xk0.resize(AltModPrf::KeySize, oc::divCeil(n, 128), oc::AllocType::Uninitialized);
		xk1.resize(AltModPrf::KeySize, oc::divCeil(n, 128), oc::AllocType::Uninitialized);
		oc::Matrix<block> msg(StepSize, xk0.cols() * 2, oc::AllocType::Uninitialized); // y.size() * 256 * 2 bits
		for (u64 i = 0; i < AltModPrf::KeySize;)
		{
			co_await sock.recv(msg);
			for (u64 k = 0; k < StepSize; ++i, ++k)
			{
				u8 ki = *oc::BitIterator((u8*)&mKey, i);

				auto lsbShare = xk0[i];
				auto msbShare = xk1[i];
				// TODO, make this branch free.
				if (ki)
				{
					auto msbMsg = msg[k].subspan(0, msbShare.size());
					auto lsbMsg = msg[k].subspan(msbShare.size(), lsbShare.size());

					// ui = (hi1,hi0)                                   ^ G(OT(i,1))
					//    = { [ G(OT(i,0))  + x  mod 3 ] ^ G(OT(i,1)) } ^ G(OT(i,1))
					//    =     G(OT(i,0))  + x  mod 3 
					xorVectorOne(msbShare, msbMsg, mKeyRecvOTs[i]);
					xorVectorOne(lsbShare, lsbMsg, mKeyRecvOTs[i]);
				}
				else
				{
					// ui = (hi1,hi0)         
					//    = G(OT(i,0))  mod 3 
					sampleMod3Lookup(mKeyRecvOTs[i], msbShare, lsbShare);
				}
			}
		}


		if (mDebug)
		{

			mDebugXk0 = xk0;
			mDebugXk1 = xk1;

			oc::Matrix<block>
				xk0b(xk0.rows(), xk0.cols()),
				xk1b(xk0.rows(), xk0.cols()),
				xk0_(xk0.rows(), xk0.cols()),
				xk1_(xk0.rows(), xk0.cols()),
				xLsb(xk0.rows(), xk0.cols()),
				xMsb(xk0.rows(), xk0.cols());
			AltModPrf::BinaryKey k2;

			co_await sock.recv(xk0b);
			co_await sock.recv(xk1b);
			co_await sock.recv(xLsb);
			co_await sock.recv(xMsb);
			co_await sock.recv(k2);

			mod3Add(xk1_, xk0_, xk1, xk0, xk1b, xk0b);

			auto k = mKey ^ k2;

			//std::cout << "k " << k << std::endl;
			//for (u64 j = 0; j < 10; ++j)
			//{
			//	AltModPrf::BinaryKey xx;
			//	for (u64 i = 0; i < AltModPrf::KeySize; ++i)
			//	{
			//		*oc::BitIterator((u8*)&xx, i) = bit(x.data(i), j);
			//	}
			//	std::cout << "in " << j << " " << xx << std::endl;
			//}

			for (u64 i = 0; i < AltModPrf::KeySize; ++i)
			{
				u8 ki = *oc::BitIterator((u8*)&k, i);
				for (u64 j = 0; j < xk0.cols(); ++j)
				{
					auto e = ki ? xLsb(i, j) : oc::ZeroBlock;
					if (xk0_(i, j) != e)
						throw RTE_LOC;

					auto eM = ki ? xMsb(i, j) : oc::ZeroBlock;
					if (xk1_(i, j) != eM)
						throw RTE_LOC;
				}
			}
		}

	}

	macoro::task<> AltModKeyMultSender::mult(
		oc::MatrixView<const block> xLsb,
		oc::MatrixView<const block> xMsb,
		oc::Matrix<block>& xk0,
		oc::Matrix<block>& xk1,
		coproto::Socket& sock)
	{
		if (xLsb.rows() != AltModPrf::KeySize)
			throw RTE_LOC;

		if (xMsb.size())
		{
			if (xMsb.rows() != AltModPrf::KeySize)
				throw RTE_LOC;
			if (xMsb.cols() != xLsb.cols())
				throw RTE_LOC;
		}

		// if we are doing the key gen, get the results.
		if (mSendKeyReq.size())
		{
			auto otSend = OtSend{ };
			co_await(mSendKeyReq.get(otSend));
			setKeyOts(mOptionalKeyShare, otSend.mMsg);
			mSendKeyReq.clear();
		}

		// make sure we have a key.
		if (!mKeySendOTs.size())
			throw std::runtime_error("AltMod was called without a key and keyGen was not requested. " LOCATION);

		// debugging, make sure we have the correct key OTs.
		if (mDebug)
		{
			std::vector<std::array<block, 2>> baseOts(mKeySendOTs.size());
			for (u64 i = 0; i < baseOts.size(); ++i)
			{
				baseOts[i][0] = mKeySendOTs[i][0].get();
				baseOts[i][1] = mKeySendOTs[i][1].get();
			}
			co_await(sock.send(std::move(baseOts)));
		}

		static_assert(AltModPrf::KeySize % StepSize == 0, "we dont handle remainders. Should be true.");

		// for each bit of the key, perform an OT derandomization where we get a share
		// of the input x times the key mod 3. We store the LSB and MSB of the share separately.
		// Hence we need 2 * AltModWPrf::KeySize rows in xkShares
		xk0.resize(AltModPrf::KeySize, xLsb.cols(), oc::AllocType::Uninitialized);
		xk1.resize(AltModPrf::KeySize, xLsb.cols(), oc::AllocType::Uninitialized);

		// For each bit of the key k, we run the following mini-protocol
		// 
		// OT:
		//  m0 = G(OT0)
		//  m1 = (1 - 2*k0) x - m0
		// 
		// s0 = k0 x - m0
		// 
		// ----------------
		// s1 = m_{k1} 
		//	  = k1 (1-2*k0) x + m0
		// 
		// s0+s1 = x * (k0 xor k1)  mod 3
		//
		// If the key isn't shared, then k0=0 and k1 is the full key.
		//
		for (u64 i = 0; i < AltModPrf::KeySize;)
		{
			assert(AltModPrf::KeySize % StepSize == 0);
			oc::Matrix<block> msg(StepSize, xk0.cols() * 2, oc::AllocType::Uninitialized);

			for (u64 k = 0; k < StepSize; ++i, ++k)
			{
				auto keyShare = mOptionalKeyShare ? *oc::BitIterator(&*mOptionalKeyShare, i) : 0;

				// we store them in swapped order to negate the value.
				auto msbShare = xk0[i];
				auto lsbShare = xk1[i];
				auto msbMsg = msg[k].subspan(0, msbShare.size());
				auto lsbMsg = msg[k].subspan(msbShare.size(), lsbShare.size());

				sampleMod3Lookup(mKeySendOTs[i][0], msbShare, lsbShare);

				if (xMsb.size())
				{
					// if we have key shares and our share is 1, then we sub subtract
					// x since (1-2*k0) = -1 mod 3
					if (keyShare)
					{
						// msg = G(OT(i,0)) - x mod 3
						mod3Sub(
							msbMsg, lsbMsg,
							msbShare, lsbShare,
							xMsb[i], xLsb[i]);

						// xk = -G(OT(i,0)) + xt mod 3
						mod3Add(xk1[i], xk0[i], xMsb[i], xLsb[i]);
					}
					else
					{
						// msg = G(OT(i,0)) + x mod 3
						mod3Add(
							msbMsg, lsbMsg,
							msbShare, lsbShare,
							xMsb[i], xLsb[i]);

						// xk = -G(OT(i,0))

					}
				}
				else
				{

					// if we have key shares and our share is 1, then we sub subtract
					// x since (1-2*k0) = -1 mod 3
					if (keyShare)
					{
						// msg = G(OT(i,0)) - x mod 3
						mod3Sub(
							msbMsg, lsbMsg,
							msbShare, lsbShare,
							xLsb[i]);

						// xk = -G(OT(i,0)) + xt mod 3
						mod3Add(xk1[i], xk0[i], xLsb[i]);
					}
					else
					{
						// msg = G(OT(i,0)) + x mod 3
						mod3Add(
							msbMsg, lsbMsg,
							msbShare, lsbShare,
							xLsb[i]);

						// xk = -G(OT(i,0))

					}
				}
				// ## msg = m ^ G(OT(i,1))
				xorVector(msbMsg, mKeySendOTs[i][1]);
				xorVector(lsbMsg, mKeySendOTs[i][1]);
			}
			co_await sock.send(std::move(msg));
		}


		if (mDebug)
		{

			mDebugXk0 = xk0;
			mDebugXk1 = xk1;

			co_await sock.send(xk0);
			co_await sock.send(xk1);
			co_await sock.send(xLsb);
			if (xMsb.size())
				co_await sock.send(xMsb);
			else
			{
				oc::Matrix<block> m(xLsb.rows(), xLsb.cols());
				co_await sock.send(m);
			}

			if (mOptionalKeyShare)
				co_await sock.send(*mOptionalKeyShare);
			else
			{
				AltModPrf::BinaryKey k;
				osuCrypto::setBytes(k, 0);
				co_await sock.send(k);
			}

		}

	}
}