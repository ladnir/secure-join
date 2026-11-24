
#include "F4Vole_Test.h"
#include "secure-join/CorGenerator/F4Vole/SilentF4VoleSender.h"
#include "secure-join/CorGenerator/F4Vole/SilentF4VoleReceiver.h"

#include "libOTe/Vole/Silent/SilentVoleReceiver.h"
#include "libOTe/Vole/Silent/SilentVoleSender.h"
#include "secure-join/Util/Simd.h"

using namespace secJoin;

namespace
{

	template<typename R, typename S, typename F, typename Ctx>
	void fakeBase(
		u64 n,
		PRNG& prng,
		F delta,
		R& recver,
		S& sender,
		Ctx ctx)
	{
		sender.configure(n, oc::SilentBaseType::Base, 128);
		recver.configure(n, oc::SilentBaseType::Base, 128);


		std::vector<std::array<block, 2>> msg2(sender.silentBaseOtCount());
		oc::BitVector choices = recver.sampleBaseChoiceBits(prng);
		std::vector<block> msg(choices.size());

		if (choices.size() != msg2.size())
			throw RTE_LOC;

		for (auto& m : msg2)
		{
			m[0] = prng.get();
			m[1] = prng.get();
		}

		for (auto i : oc::rng(msg.size()))
			msg[i] = msg2[i][choices[i]];

		// a = b + c * d
		// the sender gets b, d
		// the recver gets a, c
		auto c = recver.sampleBaseVoleVals(prng);
		typename Ctx::template Vec<F> a(c.size()), b(c.size());

		//prng.get(b.data(), b.size());
		for (auto i : oc::rng(c.size()))
		{	
			ctx.fromBlock(b[i], prng.get());
			ctx.mul(a[i], delta, c[i]);
			ctx.plus(a[i], b[i], a[i]);
		}
		span<block> aa = a;
		sender.setSilentBaseOts(msg2, b);
		recver.setSilentBaseOts(msg, a);
	}

}


void F4Vole_Silent_test_impl(u64 n, oc::MultType type, bool debug, bool doFakeBase, bool mal)
{
	using F = block;
	using G = F4;
	using Ctx = CoeffCtxGF4;
	using VecF = typename Ctx::Vec<F>;
	using VecG = typename Ctx::Vec<G>;
	Ctx ctx;

	block seed = oc::CCBlock;
	PRNG prng(seed);

	for (u64 tt = 0; tt < 10; ++tt)
	{
		block x,y; 
		ctx.fromBlock(x, prng.get());
		ctx.fromBlock(y, prng.get());
		for (u64 i = 0; i < 4; ++i)
		{
			F4 r{ u8(i) };
			block z0, z1, t;
			ctx.mul(z0, x, r);
			ctx.mul(t, y, r);
			ctx.plus(z0, z0, t);

			ctx.plus(t, x, y);
			ctx.mul(z1, t, r);


			if (z1 != z0)
				throw RTE_LOC;
		}
	}


	auto chls = coproto::LocalAsyncSocket::makePair();

	SilentF4VoleReceiver  recv;
	SilentF4VoleSender  send;
	recv.mMultType = type;
	send.mMultType = type;
	recv.mDebug = debug;
	send.mDebug = debug;

	VecF a(n), b(n);
	VecG c(n);
	F d;
	ctx.fromBlock(d, prng.get<block>());

	if (doFakeBase)
		fakeBase(n, prng, d, recv, send, ctx);

	auto r = coproto::sync_wait(coproto::when_all_ready(
		recv.silentReceive(/*c,*/ a, prng, chls[0]),
		send.silentSend(d, b, prng, chls[1])
	));
	std::get<0>(r).result();
	std::get<1>(r).result();
	//eval(p0, p1);

	for (u64 i = 0; i < n; ++i)
	{
		// a = b + c * d
		F exp, ai, bi;
		G ci = { static_cast<u8>(a[i].get<u8>(0) & 3) };
		//G ci = c[i];

		ctx.fromBlock(ai, a[i]);
		ctx.fromBlock(bi, b[i]);

		d = d & block(~0ull, ~0ull << 2);
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
	{

		u32 lsb[8];
		u32 msb[8];
		auto nn = n / 8 * 8;
		for (u64 i = 0; i < nn; i += 8)
		{

			// extract the choice bit from the LSB of m
			storeu_si32(&lsb[0], a[i + 0] & oc::OneBlock);
			storeu_si32(&lsb[1], a[i + 1] & oc::OneBlock);
			storeu_si32(&lsb[2], a[i + 2] & oc::OneBlock);
			storeu_si32(&lsb[3], a[i + 3] & oc::OneBlock);
			storeu_si32(&lsb[4], a[i + 4] & oc::OneBlock);
			storeu_si32(&lsb[5], a[i + 5] & oc::OneBlock);
			storeu_si32(&lsb[6], a[i + 6] & oc::OneBlock);
			storeu_si32(&lsb[7], a[i + 7] & oc::OneBlock);

			storeu_si32(&msb[0], a[i + 0] & block(0,2));
			storeu_si32(&msb[1], a[i + 1] & block(0,2));
			storeu_si32(&msb[2], a[i + 2] & block(0,2));
			storeu_si32(&msb[3], a[i + 3] & block(0,2));
			storeu_si32(&msb[4], a[i + 4] & block(0,2));
			storeu_si32(&msb[5], a[i + 5] & block(0,2));
			storeu_si32(&msb[6], a[i + 6] & block(0,2));
			storeu_si32(&msb[7], a[i + 7] & block(0,2));


			// pack the choice bits.
			//auto lsb =
			//	 b_[0] ^
			//	(b_[1] << 1) ^
			//	(b_[2] << 2) ^
			//	(b_[3] << 3) ^
			//	(b_[4] << 4) ^
			//	(b_[5] << 5) ^
			//	(b_[6] << 6) ^
			//	(b_[7] << 7);

			for (u64 j = 0; j < 8; ++j)
			{
				//F ai, bi;
				G ci = { static_cast<u8>(a[i+j].get<u8>(0) & 3) };

				if ((ci.mVal & 1) != lsb[j])
					throw RTE_LOC;

				if ((ci.mVal & 2) != msb[j])
					throw RTE_LOC;

				//ctx.fromBlock(ai, a[i]);
				//ctx.fromBlock(bi, b[i]);

				//d = d & block(~0ull, ~0ull << 2);
				//ai = ai & block(~0ull, ~0ull << 2);
				//bi = bi & block(~0ull, ~0ull << 2);

				//ctx.mul(exp, d, ci);
				//ctx.plus(exp, exp, bi);
			}

		}
	}
}

void F4Vole_Silent_test_chosen(u64 n, oc::MultType type, bool debug, bool doFakeBase, bool mal)
{
	using F = block;
	using G = F4;
	using Ctx = CoeffCtxGF4;
	using VecF = typename Ctx::Vec<F>;
	using VecG = typename Ctx::Vec<G>;
	Ctx ctx;

	block seed = oc::CCBlock;
	PRNG prng(seed);

	auto chls = coproto::LocalAsyncSocket::makePair();

	SilentF4VoleReceiver  recv;
	SilentF4VoleSender  send;
	recv.mMultType = type;
	send.mMultType = type;
	recv.mDebug = debug;
	send.mDebug = debug;

	VecF a(n), b(n);
	VecG c(n);
	F d;
	ctx.fromBlock(d, prng.get<block>());

	if (doFakeBase)
		fakeBase(n, prng, d, recv, send, ctx);

	for (u64 i = 0; i < n; ++i)
		c[i].mVal = prng.get<u8>() & 3;

	auto C = c;
	auto r = coproto::sync_wait(coproto::when_all_ready(
		recv.receiveChosen(c, a, prng, chls[0]),
		send.sendChosen(d, b, prng, chls[1])
	));
	std::get<0>(r).result();
	std::get<1>(r).result();
	//eval(p0, p1);
	
	if (!std::equal(c.begin(), c.end(), C.begin()))
		throw RTE_LOC;

	for (u64 i = 0; i < n; ++i)
	{
		// a = b + c * d
		F exp, ai, bi;
		//G ci = { static_cast<u8>(a[i].get<u8>(0) & 3) };
		G ci = c[i];

		ctx.fromBlock(ai, a[i]);
		ctx.fromBlock(bi, b[i]);

		d = d & block(~0ull, ~0ull << 2);
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



void F4Vole_Silent_paramSweep_test(const oc::CLP& cmd)
{
	auto debug = cmd.isSet("debug");
	for (u64 n : {128, 4534, 8193, 65536})
	{
		//F4Vole_Silent_test_impl_base(n, oc::MultType::Spin, debug, true, false);
		F4Vole_Silent_test_impl(n, oc::MultType::Spin, debug, true, false);
		//F4Vole_Silent_test_chosen(n, oc::MultType::Spin, debug, true, false);
	}
}
