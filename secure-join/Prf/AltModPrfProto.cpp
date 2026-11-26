

#include "AltModPrfProto.h"
#include "secure-join/AggTree/PerfectShuffle.h"
#include "mod3.h"
#include <ranges>
#include "macoro/async_scope.h"

namespace secJoin
{

	// initialize the protocol to perform inputSize prf evals.
	// correlations will be obtained from ole.
	// The caller can specify the key [share] and optionally
	// OTs associated with the key [share]. If not specify, they
	// will be generated internally.
	void AltModWPrfSender::init(
		u64 inputSize,
		CorGenerator& ole,
		AltModPrfKeyMode keyMode,
		AltModPrfInputMode inputMode,
		macoro::optional<AltModPrf::KeyType> key,
		span<const block> keyRecvOts,
		span<const std::array<block, 2>> keySendOts)
	{
		mInputSize = inputSize;
		mKeyMode = keyMode;
		mInputMode = inputMode;

		if (key.has_value() ^ (AltModPrf::KeySize == keyRecvOts.size()))
			throw RTE_LOC;

		mKeyMultRecver.init(ole, key, keyRecvOts);
		auto n128 = oc::roundUpTo(mInputSize, 128);

		if (mKeyMode == AltModPrfKeyMode::Shared &&
			mInputMode == AltModPrfInputMode::Shared)
		{
			mKeyMultSender.init(ole, key, keySendOts);
			mConvToF3.init(n128 * AltModPrf::KeySize, ole);
		}

		if (mUseMod2F4Ot)
		{
			auto num = n128 * AltModPrf::MidSize;
			mMod2F4Req = ole.request<F4BitOtSend>(num);
		}
		else
		{
			auto numOle = n128 * AltModPrf::MidSize * 2;
			mMod2OleReq = ole.binOleRequest(numOle);
		}
	}

	// clear the state. Removes any key that is set can cancels the prepro (if any).
	void AltModWPrfSender::clear()
	{
		mMod2F4Req.clear();
		mMod2OleReq.clear();
		mKeyMultRecver.clear();
		mKeyMultSender.clear();
		mInputSize = 0;
	}

	// perform the correlated randomness generation. 
	void AltModWPrfSender::preprocess()
	{

		mKeyMultRecver.preprocess();
		mKeyMultSender.preprocess();

		if (mUseMod2F4Ot)
			mMod2F4Req.start();
		else
			mMod2OleReq.start();
	}

	void AltModWPrfSender::setKeyOts(
		AltModPrf::KeyType k,
		span<const block> ots,
		span<const std::array<block, 2>> sendOts)
	{
		mKeyMultRecver.setKeyOts(k, ots);

		if (mKeyMode == AltModPrfKeyMode::Shared &&
			mInputMode == AltModPrfInputMode::Shared)
		{
			mKeyMultSender.setKeyOts(k, sendOts);
		}
		else
		{
			if (sendOts.size() != 0)
				throw RTE_LOC;
		}
	}

	coproto::task<> AltModWPrfSender::evaluate(
		span<const block> x,
		span<block> y,
		coproto::Socket& sock,
		PRNG& prng)
	{
		if (x.size() != mInputSize * bool(mInputMode == AltModPrfInputMode::Shared))
			throw std::runtime_error("input length does not match. " LOCATION);

		if (y.size() != mInputSize)
			throw std::runtime_error("output length does not match. " LOCATION);

		if (mUseMod2F4Ot)
		{
			if (mMod2F4Req.size() != oc::roundUpTo(y.size(), 128) * AltModPrf::MidSize)
				throw std::runtime_error("do not have enough preprocessing. Call request(...) first. " LOCATION);
		}
		else
		{
			if (mMod2OleReq.size() != oc::roundUpTo(y.size(), 128) * AltModPrf::MidSize * 2)
				throw std::runtime_error("do not have enough preprocessing. Call request(...) first. " LOCATION);
		}

		preprocess();
		setTimePoint("DarkMatter.sender.begin");

		mKeyMultRecver.mDebug = mDebug;
		mKeyMultSender.mDebug = mDebug;

		auto ek0 = oc::Matrix<block>{};
		auto ek1 = oc::Matrix<block>{};
		if ((mKeyMode == AltModPrfKeyMode::SenderOnly || mKeyMode == AltModPrfKeyMode::Shared)
			&& mInputMode == AltModPrfInputMode::ReceiverOnly)
		{
			// multiply our key with their inputs. The result is written
			// to ek in transposed and decomposed format.
			co_await mKeyMultRecver.mult(y.size(), ek0, ek1, sock);
		}
		else
		{
			// plaintext key and shared input is not implemented.
			if (mKeyMode != AltModPrfKeyMode::Shared &&
				mInputMode != AltModPrfInputMode::Shared)
				throw std::runtime_error("unsupported mode combination. " LOCATION);

			if (mKeyMultSender.mOptionalKeyShare.has_value() == false)
				throw RTE_LOC;
			if (mKeyMultRecver.mKey != *mKeyMultSender.mOptionalKeyShare)
				throw RTE_LOC;

			// we need x in a transformed format so that we can do SIMD operations.
			// we compute e = G * x.
			oc::Matrix<block> e(AltModPrf::KeySize, oc::divCeil(x.size(), 128));
			AltModPrf::expandInput(x, e);
			if (mDebug)
				mDebugInput = std::vector<block>(x.begin(), x.end());

			auto ekaLsb = oc::Matrix<block>{};
			auto ekaMsb = oc::Matrix<block>{};
			auto eMsb = oc::Matrix<block>{ e.rows(), e.cols(), oc::AllocType::Uninitialized };
			auto eLsb = oc::Matrix<block>{ e.rows(), e.cols(), oc::AllocType::Uninitialized };
			auto sb = sock.fork();

			// convert the binary sharing e into an F3 sharing (eMsb, eLsb)
			co_await mConvToF3.convert(e, sock, eMsb, eLsb);

			// concurrently, multiply these shares by the the individual key shares.
			co_await macoro::when_all_ready(
				mKeyMultRecver.mult(x.size(), ek0, ek1, sock),
				mKeyMultSender.mult(eLsb, eMsb, ekaLsb, ekaMsb, sb)
			);

			// add the partial multiplications to get (G * x) . k
			// ek += eka
			mod3Add(ek1, ek0, ekaMsb, ekaLsb);
		}

		if (mDebug)
		{
			mDebugEk0 = ek0;
			mDebugEk1 = ek1;
		}

		// Compute u = (A * ek) mod 3
		auto buffer = oc::AlignedUnVector<u8>{};
		auto u0 = oc::Matrix<block>{ AltModPrf::MidSize, oc::divCeil(y.size(), 128), oc::AllocType::Uninitialized };
		auto u1 = oc::Matrix<block>{ AltModPrf::MidSize, oc::divCeil(y.size(), 128), oc::AllocType::Uninitialized };
		AltModPrf::mACode.encode(ek1, ek0, u1, u0);
		ek0 = {};
		ek1 = {};

		if (mDebug)
		{
			mDebugU0 = u0;
			mDebugU1 = u1;
		}

		// Compute v = u mod 2
		auto v = oc::Matrix<block>{ AltModPrf::MidSize, u0.cols() };
		if (mUseMod2F4Ot)
			co_await mod2OtF4(u0, u1, v, sock);
		else
			co_await mod2Ole(u0, u1, v, sock);

		if (mDebug)
		{
			mDebugV = v;
		}

		// Compute y = B * v
		AltModPrf::compressB(v, y);
		mInputSize = 0;
	}

	void AltModWPrfReceiver::setKeyOts(
		span<const std::array<block, 2>> ots,
		std::optional<AltModPrf::KeyType> keyShare,
		span<const block> recvOts)
	{
		mKeyMultSender.setKeyOts(keyShare, ots);

		if (mKeyMode == AltModPrfKeyMode::Shared &&
			mInputMode == AltModPrfInputMode::Shared)
		{
			if (!keyShare.has_value())
				throw RTE_LOC;

			mKeyMultRecver.setKeyOts(*keyShare, recvOts);
		}
		else
		{
			if (recvOts.size() != 0)
				throw RTE_LOC;
		}
	}


	// clears any internal state.
	void AltModWPrfReceiver::clear()
	{
		mMod2F4Req.clear();
		mMod2OleReq.clear();
		mKeyMultRecver.clear();
		mKeyMultSender.clear();
		mInputSize = 0;
	}

	// initialize the protocol to perform inputSize prf evals.
	// set keyGen if you explicitly want to perform (or not) the 
	// key generation. default = perform if not already set.
	// keyShare is a share of the key. If not set, then the sender
	// will hold a plaintext key. keyOts is will base OTs for the
	// sender's key (share).
	void AltModWPrfReceiver::init(u64 size,
		CorGenerator& ole,
		AltModPrfKeyMode keyMode,
		AltModPrfInputMode inputMode,
		std::optional<AltModPrf::KeyType> keyShare,
		span<const std::array<block, 2>> keyOts,
		span<const block> keyRecvOts)
	{
		if (keyOts.size() != AltModPrf::KeySize && keyOts.size() != 0)
			throw RTE_LOC;
		if (!size)
			throw RTE_LOC;
		if (mInputSize)
			throw RTE_LOC;

		mInputSize = size;
		mKeyMode = keyMode;
		mInputMode = inputMode;
		auto n128 = oc::roundUpTo(mInputSize, 128);

		mKeyMultSender.init(ole, keyShare, keyOts);

		if (mKeyMode == AltModPrfKeyMode::Shared &&
			mInputMode == AltModPrfInputMode::Shared)
		{
			mKeyMultRecver.init(ole, keyShare, keyRecvOts);
			mConvToF3.init(n128 * AltModPrf::KeySize, ole);
		}

		if (mUseMod2F4Ot)
		{
			auto numOle = n128 * AltModPrf::MidSize;
			mMod2F4Req = ole.request<F4BitOtRecv>(numOle);
		}
		else
		{
			auto numOle = n128 * AltModPrf::MidSize * 2;
			mMod2OleReq = ole.binOleRequest(numOle);
		}
	}

	// Perform the preprocessing for the correlated randomness and key gen (if requested).
	void AltModWPrfReceiver::preprocess()
	{
		if (mUseMod2F4Ot)
			mMod2F4Req.start();
		else
			mMod2OleReq.start();

		mKeyMultRecver.preprocess();
		mKeyMultSender.preprocess();
	}


	coproto::task<> AltModWPrfReceiver::evaluate(
		span<const block> x,
		span<block> y,
		coproto::Socket& sock,
		PRNG& prng)
	{

		if (mInputSize == 0)
			throw std::runtime_error("input lengths 0. " LOCATION);

		if (x.size() != mInputSize)
			throw std::runtime_error("input lengths do not match. " LOCATION);

		if (y.size() != mInputSize)
			throw std::runtime_error("output lengths do not match. " LOCATION);

		if (mUseMod2F4Ot)
		{
			if (mMod2F4Req.size() != oc::roundUpTo(y.size(), 128) * AltModPrf::MidSize)
				throw std::runtime_error("do not have enough preprocessing. Call request(...) first. " LOCATION);
		}
		else
		{
			if (mMod2OleReq.size() != oc::roundUpTo(y.size(), 128) * AltModPrf::MidSize * 2)
				throw std::runtime_error("do not have enough preprocessing. Call request(...) first. " LOCATION);
		}

		preprocess();
		setTimePoint("DarkMatter.recver.begin");
		if (mDebug)
			mDebugInput = std::vector<block>(x.begin(), x.end());

		// we need x in a transformed format so that we can do SIMD operations.
		// compute e = G * x
		oc::Matrix<block> e(AltModPrf::KeySize, oc::divCeil(x.size(), 128), oc::AllocType::Uninitialized);
		AltModPrf::expandInput(x, e);

		mKeyMultRecver.mDebug = mDebug;
		mKeyMultSender.mDebug = mDebug;

		auto ek0 = oc::Matrix<block>{};
		auto ek1 = oc::Matrix<block>{};
		if ((mKeyMode == AltModPrfKeyMode::SenderOnly || mKeyMode == AltModPrfKeyMode::Shared)
			&& mInputMode == AltModPrfInputMode::ReceiverOnly)
		{
			// multiply our input e with with key share
			co_await mKeyMultSender.mult(e, {}, ek0, ek1, sock);
		}
		else
		{
			// SenderOnly key and shared input is not implemented.
			if (mKeyMode != AltModPrfKeyMode::Shared &&
				mInputMode != AltModPrfInputMode::Shared)
				throw std::runtime_error("unsupported mode combination. SenderOnly key and secret shared input is not implemented. " LOCATION);

			if (mKeyMultSender.mOptionalKeyShare.has_value() == false)
				throw RTE_LOC;

			if (mKeyMultRecver.mKey != *mKeyMultSender.mOptionalKeyShare)
				throw RTE_LOC;

			auto eMsb = oc::Matrix<block>{ e.rows(), e.cols(), oc::AllocType::Uninitialized };
			auto eLsb = oc::Matrix<block>{ e.rows(), e.cols(), oc::AllocType::Uninitialized };
			auto ek0a = oc::Matrix<block>{  };
			auto ek1a = oc::Matrix<block>{  };
			auto sb = sock.fork();

			// convert the F2 sharing e into a F3 sharing (eMsb,eLsb).
			co_await mConvToF3.convert(e, sock, eMsb, eLsb);

			// multiply the F3 sharing of e by the key shares
			co_await macoro::when_all_ready(
				mKeyMultSender.mult(eLsb, eMsb, ek0a, ek1a, sock),
				mKeyMultRecver.mult(x.size(), ek0, ek1, sb)
			);

			ek1.resize(ek1a.rows(), ek1a.cols());
			ek0.resize(ek0a.rows(), ek0a.cols());
			assert(ek0.size() == ek0a.size());
			assert(ek1.size() == ek1a.size());

			// add the two partial results.
			// ek = ek + eka
			mod3Add(ek1, ek0, ek1a, ek0a);
		}

		if (mDebug)
		{
			mDebugEk0 = ek0;
			mDebugEk1 = ek1;
		}


		// Compute u = A * ek
		oc::Matrix<block>u0(AltModPrf::MidSize, oc::divCeil(x.size(), 128), oc::AllocType::Uninitialized);
		oc::Matrix<block>u1(AltModPrf::MidSize, oc::divCeil(x.size(), 128), oc::AllocType::Uninitialized);
		AltModPrf::mACode.encode(ek1, ek0, u1, u0);
		ek0 = {};
		ek1 = {};

		if (mDebug)
		{
			mDebugU0 = u0;
			mDebugU1 = u1;
		}

		// Compute v = u mod 2
		oc::Matrix<block>v(AltModPrf::MidSize, u0.cols());
		if (mUseMod2F4Ot)
			co_await mod2OtF4(u0, u1, v, sock);
		else
			co_await mod2Ole(u0, u1, v, sock);

		if (mDebug)
		{
			mDebugV = v;
		}

		// Compute y = B * v
		AltModPrf::compressB(v, y);
		mInputSize = 0;
	}




	// The parties input a sharing of the u=(u0,u1) such that 
	// 
	//   u = u0 + u1 mod 3
	// 
	// When looking at the truth table of u mod 2 we have
	// 
	//           u1
	//          0 1 2
	//         ________
	//      0 | 0 1 0
	//   u0 1 | 1 0 0 
	//      2 | 0 0 1
	//   
	// Logically, what we are going to do is a 1-out-of-3
	// OT. The PrfSender with u0 will use select the row of
	// this table based on u0. For example, if u0=0 then the 
	// truth table reduces to
	// 
	//   0 1 0
	// 
	// where the PrfReceiver should use the OT to pick up
	// the element indexed bu u1. For example, they should pick up 
	// 1 iff u1 = 1.
	// 
	// To maintain security, we need to give the PrfReceiver a sharing
	// of this value, not the value itself. The PrfSender will pick a random mask
	// 
	//   r
	// 
	// and then the PrfReceiver should learn that table above XOR r.
	// 
	// We can build a 1-out-of-3 OT from OLE. Each mod2 / 1-out-of-3 OT consumes 2 
	// binary OLE's. A single OLE consists of PrfSender holding 
	// 
	//     x0, y0
	// 
	// the PrfReceiver holding
	// 
	//     x1, y1
	// 
	// such that (x0+x1) = (y0*y1). We will partially derandomize these
	// by allowing the PrfReceiver to change their y1 to be a chosen
	// value, c. That is, the parties want to hold correlation
	// 
	//    (x0',y0',x1', c)
	// 
	// where x0,x1,y0 are random and c is chosen. This is done by sending
	// 
	//   d = (y1+c)
	// 
	// and the PrfSender updates their share as
	// 
	//   x0' = x0 + y0 * d
	// 
	// The parties output (x0',y0,x1,c). Observe parties hold the correlation
	// 
	//   x1 + x0'                   = y0 * c
	//   x1 + x0 + y0 * d           = y0 * c
	//   x1 + x0 + y0 * (y1+c)      = y0 * c
	//   x1 + x0 + y0 * y1 + y0 * c = y0 * c
	//                       y0 * c = y0 * c
	//
	// Ok, now let us perform the mod 2 operations. As state, this 
	// will consume 2 OLEs. Let these be denoted as
	// 
	//   (x0,x1,y0,y1), (x0',x1',y0',y1')
	//
	// These have not been derandomized yet. To get an 1-out-of-3 OT, we
	// will derandomize them using the PrfReceiver's u1. This value is 
	// represented using two bits (u10,u11) such that u1 = u10 + 2 * u11
	// 
	// We will derandomize (x0,x1,y0,y1) using u10 and (x0',x1',y0',y1') 
	// using u11. Let us redefine these correlations as 
	// 
	//   (x0,x1,y0,u10), (x0',x1',y0',u11)
	// 
	// That is, we also redefined x0,x0' accordingly.
	// 
	// We will define the random OT strings (in tms case x1 single bit)
	// as 
	//    
	//    m0 = x0      + x0'
	//    m1 = x0 + y0 + x0'
	//    m2 = x0      + x0' + y0'
	// 
	// Note that 
	//  - when u10 = u11 = 0, then x1=x0, x1'=x0' and therefore
	//    the PrfReceiver knows m0. m1,m2 is uniform due to y0, y0' being 
	//    uniform, respectively. 
	// - When u10 = 1, u11 = 0, then x1 = x0 + y0 and x1' = x0 and therefore 
	//   PrfReceiver knows m1 = x1 + x1' = (x0 + y0) + x0'. m0 is uniform because 
	//   x0 = x1 + y0 and y0 is uniform given x1. m2 is uniform because y0' 
	//   is uniform given x1. 
	// - Finally, when u10 = 0, u11 = 1, the same basic case as above applies.
	// 
	// 
	// these will be used to mask the truth table T. That is, the PrfSender
	// will sample x1 mask r and send
	// 
	//   t0 = m0 + T[u0, 0] + r
	//   t1 = m1 + T[u0, 1] + r
	//   t2 = m2 + T[u0, 2] + r
	// 
	// The PrfReceiver can therefore compute
	// 
	//   z1 = t_u1 + m_u1
	//      = T[u1, u1] + r
	//      = (u mod 2) + r
	// 
	// and the PrfSender can compute
	//  
	//   z0 = r
	// 
	// and therefor we have z0+z1 = u mod 2.
	// 
	// As an optimization, we dont need to send t0. The idea is that if
	// the receiver want to learn T[u0, 0], then they can set their share
	// as m0. That is,
	// 
	//   z1 = (u1 == 0) ? m0 : t_u1 + m_u1
	//      = u10 * t_u1 + mu1
	// 
	// The sender now needs to define r appropriately. In particular, 
	// 
	//   r = m0 + T[u0, 0]
	// 
	// In the case of u1 = 0, z1 = m0, z0 = r + T[u0,0], and therefore we 
	// get the right result. The other cases are the same with the randomness
	// of the mast r coming from m0.
	// 
	//
	// u has 256 rows
	// each row holds 2bit values
	// 
	// out will have 256 rows.
	// each row will hold packed bits.
	// 
	macoro::task<> AltModWPrfSender::mod2Ole(
		oc::MatrixView<const block> u0,
		oc::MatrixView<const block> u1,
		oc::MatrixView<block> out,
		coproto::Socket& sock)
	{
		if (mUseMod2F4Ot)
		{
			std::cout << "mUseMod2F4Ot == true but call mod2Ole. " << LOCATION << std::endl;
			std::terminate();
		}

		if (out.rows() != u0.rows())
			throw RTE_LOC;
		if (out.rows() != u1.rows())
			throw RTE_LOC;
		if (out.cols() != u0.cols())
			throw RTE_LOC;
		if (out.cols() != u1.cols())
			throw RTE_LOC;

		auto tIdx = 0ull;
		auto tSize = 0ull;
		auto rows = u0.rows();
		auto cols = u0.cols();
		auto outIter = out.data();
		auto u0Iter = u0.data();
		auto u1Iter = u1.data();
		auto triple = BinOle{};
		auto buff = oc::AlignedUnVector<block>{};

		for (auto i = 0ull; i < rows; ++i)
		{
			for (auto j = 0ull; j < cols; )
			{
				if (tIdx == tSize)
				{
					co_await mMod2OleReq.get(triple);

					tSize = triple.mAdd.size();
					tIdx = 0;
					buff.resize(tSize);
					co_await sock.recv(buff);
				}

				// we have (cols - j) * 128 elems left in the row.
				// we have (tSize - tIdx) * 128 oles left
				// each elem requires 2 oles
				// 
				// so the amount we can do this iteration is 
				auto step = std::min<u64>(cols - j, (tSize - tIdx) / 2);
				auto end = step + j;
				for (; j < end; j += 1, tIdx += 2)
				{
					// we have x1, y1, s.t. x0 + x1 = y0 * y1
					auto x = &triple.mAdd.data()[tIdx];
					auto y = &triple.mMult.data()[tIdx];
					auto d = &buff.data()[tIdx];

					// x1[0] = x1[0] ^ (y1[0] * d[0])
					//       = x1[0] ^ (y1[0] * (u0[0] ^ y0[0]))
					//       = x1[0] ^ (y[0] * u0[0])
					for (u64 k = 0; k < 2; ++k)
					{
						x[k] = x[k] ^ (y[k] & d[k]);
					}

					block m0, m1, m2, t0, t1, t2;
					m0 = x[0] ^ x[1];
					m1 = m0 ^ y[0];
					m2 = m0 ^ y[1];

					//           u1
					//          0 1 2
					//         ________
					//      0 | 0 1 0
					//   u0 1 | 1 0 0 
					//      2 | 0 0 1
					//t0 = hi0 + T[u0,0] 
					//   = hi0 + u0(i,j)  

					assert(u0Iter == &u0(i, j));
					assert(u1Iter == &u1(i, j));
					assert((u0(i, j) & u1(i, j)) == oc::ZeroBlock);

					t0 = m0 ^ *u0Iter;
					t1 = t0 ^ m1 ^ *u0Iter ^ *u1Iter ^ oc::AllOneBlock;
					t2 = t0 ^ m2 ^ *u1Iter;

					++u0Iter;
					++u1Iter;

					//if (mDebug)// && i == mPrintI && (j == (mPrintJ / 128)))
					//{
					//    auto mPrintJ = 0;
					//    auto bitIdx = mPrintJ % 128;
					//    std::cout << j << " m  " << bit(m0, bitIdx) << " " << bit(m1, bitIdx) << " " << bit(m2, bitIdx) << std::endl;
					//    std::cout << j << " u " << bit(u1(i, j), bitIdx) << bit(u0(i, j), bitIdx) << " = " <<
					//        (bit(u1(i, j), bitIdx) * 2 + bit(u0(i, j), bitIdx)) << std::endl;
					//    std::cout << j << " r  " << bit(m0, bitIdx) << std::endl;
					//    std::cout << j << " t  " << bit(t0, bitIdx) << " " << bit(t1, bitIdx) << " " << bit(t2, bitIdx) << std::endl;
					//}

					// r
					assert(outIter == &out(i, j));
					*outIter++ = t0;
					d[0] = t1;
					d[1] = t2;
				}

				if (tIdx == tSize)
				{
					co_await sock.send(std::move(buff));
				}
			}
		}

		assert(buff.size() == 0);
	}




	macoro::task<> AltModWPrfReceiver::mod2Ole(
		oc::MatrixView<const block> u0,
		oc::MatrixView<const block> u1,
		oc::MatrixView<block> out,
		coproto::Socket& sock)
	{

		if (mUseMod2F4Ot)
		{
			std::cout << "mUseMod2F4Ot == true but call mod2Ole. " << LOCATION << std::endl;
			std::terminate();
		}

		auto triple = std::vector<BinOle>{};
		triple.reserve(mMod2OleReq.batchCount());
		auto tIdx = 0ull;
		auto tSize = 0ull;

		// the format of u is that it should have AltModWPrf::MidSize rows.
		auto rows = u0.rows();

		// cols should be the number of inputs.
		auto cols = u0.cols();

		// we are performing mod 2. u0 is the lsb, u1 is the msb. these are packed into 128 bit blocks. 
		// we then have a matrix of these with `rows` rows and `cols` columns. We mod requires
		// 2 OLEs. So in total we need rows * cols * 128 * 2 OLEs.
		assert(mMod2OleReq.size() == rows * cols * 128 * 2);

		auto u0Iter = u0.data();
		auto u1Iter = u1.data();
		auto buff = oc::AlignedUnVector<block>{};
		for (auto i = 0ull; i < rows; ++i)
		{
			for (auto j = 0ull; j < cols;)
			{

				if (tSize == tIdx)
				{

					triple.emplace_back();
					co_await(mMod2OleReq.get(triple.back()));

					tSize = triple.back().mAdd.size();
					tIdx = 0;
					buff.resize(tSize);
				}

				auto step = std::min<u64>(cols - j, (tSize - tIdx) / 2);
				auto end = step + j;

				for (; j < end; ++j, tIdx += 2)
				{
					auto y = &triple.back().mMult.data()[tIdx];
					auto b = &buff.data()[tIdx];
					assert(u0Iter == &u0(i, j));
					assert(u1Iter == &u1(i, j));
					b[0] = *u0Iter ^ y[0];
					b[1] = *u1Iter ^ y[1];

					++u0Iter;
					++u1Iter;
				}

				if (tSize == tIdx)
				{
					co_await(sock.send(std::move(buff)));
					TODO("clear mult");
					//triple.back().clear();
				}
			}
		}

		if (buff.size())
			co_await(sock.send(std::move(buff)));

		tIdx = 0;
		tSize = 0;
		auto tIter = triple.begin();
		auto outIter = out.data();
		auto add = span<block>{};
		u0Iter = u0.data();
		u1Iter = u1.data();
		for (auto i = 0ull; i < rows; ++i)
		{
			for (auto j = 0ull; j < cols; )
			{

				if (tIdx == tSize)
				{

					tIdx = 0;
					tSize = tIter->mAdd.size();
					add = tIter->mAdd;
					++tIter;
					buff.resize(tSize);
					co_await(sock.recv(buff));
				}

				auto step = std::min<u64>(cols - j, (tSize - tIdx) / 2);
				auto end = step + j;

				for (; j < end; ++j, tIdx += 2)
				{

					assert((u0(i, j) & u1(i, j)) == oc::ZeroBlock);
					assert(u0Iter == &u0(i, j));
					assert(u1Iter == &u1(i, j));

					// if u = 0, w = hi0
					// if u = 1, w = hi1 + t1
					// if u = 2, w = m2 + t2
					block w =
						(*u0Iter++ & buff.data()[tIdx + 0]) ^ // t1
						(*u1Iter++ & buff.data()[tIdx + 1]) ^ // t2
						add.data()[tIdx + 0] ^ add.data()[tIdx + 1];// m_u

					//if (mDebug)// && i == mPrintI && (j == (mPrintJ / 128)))
					//{
					//    auto mPrintJ = 0;
					//    auto bitIdx = mPrintJ % 128;
					//    std::cout << j << " u " << bit(u1(i, j), bitIdx) << bit(u0(i, j), bitIdx) << " = " <<
					//        (bit(u1(i, j), bitIdx) * 2 + bit(u0(i, j), bitIdx)) << std::endl;
					//    std::cout << j << " t  _ " << bit(buff[tIdx + 0], bitIdx) << " " << bit(buff[tIdx + 1], bitIdx) << std::endl;
					//    std::cout << j << " w  " << bit(w, bitIdx) << std::endl;
					//}

					assert(outIter == &out(i, j));
					*outIter++ = w;
				}
			}
		}
	}






	// The parties input a sharing (u,v) such that 
	// 
	//   u + v mod 3
	// 
	// is the true value. When looking at the truth table of u mod 2 we have
	// 
	//            u
	//          0 1 2
	//         ________
	//      0 | 0 1 0
	//   v  1 | 1 0 0 
	//      2 | 0 0 1
	//   
	// Logically, what we are going to do is a 1-out-of-3
	// OT. The PrfSender with v will use select the row of
	// this table based on v. For example, if v=0 then the 
	// truth table reduces to
	// 
	//   0 1 0
	// 
	// where the PrfReceiver should use the OT to pick up
	// the element indexed by u. For example, they should pick up 
	// 1 iff u = 1.
	// 
	// To maintain security, we need to give the PrfReceiver a sharing
	// of this value, not the value itself. The PrfSender will pick a random
	// share
	// 
	//   r
	// 
	// and then the PrfReceiver should learn that table above XOR r. Let v0 and v1 
	// be the lsb and msb of v. The truth table can be computed as 
	// 
	//  T[v,0] = v0
	//  T[v,1] = ~(v0 ^ v1)
	//  T[v,2] = v1
	// 
	// and so we will sent via a 1-oo-3 OT the messages  
	// 
	//  t0 = r + T[v,0], 
	//  t1 = r + T[v,1], 
	//  t2 = r + T[v,2]
	// 
	// As an optimization, we dont need to send t0. The idea is that if
	// the receiver want to learn T[v, 0], then they can set their share
	// as OT string m0. The sender will set their output share as
	// 
	// z1 = m0 + T[v,0]
	// 
	// The sender now needs to define r appropriately. 
	macoro::task<> AltModWPrfSender::mod2OtF4(
		oc::MatrixView<const block> v0,
		oc::MatrixView<const block> v1,
		oc::MatrixView<block> out,
		coproto::Socket& sock)
	{
		if (!mUseMod2F4Ot)
		{
			std::cout << "mUseMod2F4Ot == false but call mod2OtF4. " << LOCATION << std::endl;
			std::terminate();
		}
		if (out.rows() != v0.rows())
			throw RTE_LOC;
		if (out.rows() != v1.rows())
			throw RTE_LOC;
		if (out.cols() != v0.cols())
			throw RTE_LOC;
		if (out.cols() != v1.cols())
			throw RTE_LOC;

		auto rows = v0.rows();
		auto cols = v0.cols();
		auto remaining = rows * cols;
		auto outIter = out.data();
		auto v0Iter = v0.data();
		auto v1Iter = v1.data();
		auto otEnd = (block*)nullptr;
		auto otIter = std::array<block*, 4>{nullptr, nullptr, nullptr, nullptr};
		auto ots = F4BitOtSend{};
		auto buff = oc::AlignedUnVector<block>{};
		auto derand = (block*)nullptr;
		auto derandEnd = (block*)nullptr;
		for (auto i = 0ull; i < rows; ++i)
		{
			for (auto j = 0ull; j < cols; )
			{

				if (otIter[0] == otEnd)
				{
					co_await(mMod2F4Req.get(ots));

					otEnd = ots.mOts[0].data() + ots.mOts[0].size();
					for (u64 k = 0; k < 4; ++k)
						otIter[k] = ots.mOts[k].data();

					buff.resize(std::min<u64>(ots.mOts[0].size(), remaining) * 2);
					remaining -= buff.size() / 2;

					co_await(sock.recv(buff));
					derand = buff.data();
					derandEnd = derand + buff.size();
				}


				// we have (cols - j) * 128 elems left in the row.
				// we have (tSize - tIdx) * 128 oles left
				// each elem requires 2 oles
				// 
				// so the amount we can do this iteration is 
				auto step = std::min<u64>(cols - j, (derandEnd - derand) / 2);
				auto end = step + j;
				for (; j < end; ++j)
				{

					assert(v0Iter == &v0(i, j));
					assert(v1Iter == &v1(i, j));
					assert((v0(i, j) & v1(i, j)) == oc::ZeroBlock);


					auto swap = [](block* l0, block* l1, block c)
						{
							auto diff = *l0 ^ *l1;
							*l0 = *l0 ^ (c & diff);
							*l1 = *l0 ^ diff;
						};

					// derandomize the OT strings based on their choice values derand
					swap(otIter[0], otIter[1], derand[0]);
					swap(otIter[2], otIter[3], derand[0]);
					swap(otIter[0], otIter[2], derand[1]);
					swap(otIter[1], otIter[3], derand[1]);

					//            u
					//          0 1 2
					//         ________
					//      0 | 0 1 0
					//   v  1 | 1 0 0 
					//      2 | 0 0 1

					// the shared value if u=0
					auto Tv0 = *v0Iter;

					// the shared value if u=1
					auto Tv1 = ~(*v0Iter ^ *v1Iter);

					// the shared value if u=2
					auto Tv2 = *v1Iter;

					assert(outIter == &out(i, j));

					// outShare = T[v,0] ^ ot_0. They will have ot_1 which xors with this to T[v,0]
					*outIter = Tv0 ^ *otIter[0];

					// t1 = Enc( T[v, 1] ^ outShare )
					auto t1 = Tv1 ^ *outIter ^ *otIter[1];

					// t2 = Enc( T[u, 2] ^ outShare )
					auto t2 = Tv2 ^ *outIter ^ *otIter[2];

					//if (mDebug)// && i == mPrintI && (j == (mPrintJ / 128)))
					//{
					//    auto mPrintJ = 0;
					//    auto bitIdx = mPrintJ % 128;
					//	std::cout << j << " v  " << bit(v1, bitIdx) << bit(v0, bitIdx) << std::endl;
					//	std::cout << j << " m  " << bit(Tv0, bitIdx) << " " << bit(Tv1, bitIdx) << " " << bit(Tv2, bitIdx) << std::endl;
					//	std::cout << j << " ot " << bit(*otIter[0], bitIdx) << " " << bit(*otIter[0], bitIdx) << " " << bit(*otIter[0], bitIdx) << std::endl;
					//	std::cout << j << " t  " << bit(t1, bitIdx) << " " << bit(t2, bitIdx) << std::endl;
					//    std::cout << j << " r  " << bit(*outIter, bitIdx) << std::endl;
					//}

					// r
					derand[0] = t1;
					derand[1] = t2;

					++v0Iter;
					++v1Iter;
					++outIter;
					derand += 2;
					for (u64 k = 0; k < 4; ++k)
						++otIter[k];
				}

				if (derandEnd == derand)
				{
					co_await(sock.send(std::move(buff)));
				}
			}
		}

		assert(buff.size() == 0);

	}



	macoro::task<> AltModWPrfReceiver::mod2OtF4(
		oc::MatrixView<const block> u0,
		oc::MatrixView<const block> u1,
		oc::MatrixView<block> out,
		coproto::Socket& sock)
	{
		if (!mUseMod2F4Ot)
		{
			std::cout << "mUseMod2F4Ot == false but call mod2OtF4. " << LOCATION << std::endl;
			std::terminate();
		}

		if (u0.rows() != u1.rows() || u0.cols() != u1.cols() ||
			out.rows() != u0.rows() || out.cols() != u0.cols())
			throw RTE_LOC;

		F4BitOtRecv ots;
		oc::AlignedUnVector<block> correction;
		for (u64 offset = 0; offset < out.size();)
		{
			co_await mMod2F4Req.get(ots);
			const u64 count = std::min<u64>(ots.mOts.size(), out.size() - offset);
			correction.resize(2 * count);
			for (u64 j = 0; j < count; ++j)
			{
				correction[2 * j] = u0.data()[offset + j] ^ ots.mChoiceLsb[j];
				correction[2 * j + 1] = u1.data()[offset + j] ^ ots.mChoiceMsb[j];
			}
			co_await sock.send(correction);
			co_await sock.recv(correction);
			for (u64 j = 0; j < count; ++j)
			{
				out.data()[offset + j] = ots.mOts[j] ^
					(u0.data()[offset + j] & correction[2 * j]) ^
					(u1.data()[offset + j] & correction[2 * j + 1]);
			}
			offset += count;
		}
	}
}