#include "benchmark.h"
#include "secure-join/Prf/AltModPrfProto.h"
#include "secure-join/Sort/RadixSort.h"
#include "secure-join/Join/OmJoin.h"
#include "secure-join/Perm/PprfPermGen.h"

namespace secJoin
{
	static oc::SdNoiseDistribution benchmarkNoise(const oc::CLP& cmd, oc::SdNoiseDistribution fallback)
	{
		if (cmd.isSet("regular") && cmd.isSet("stationary"))
			throw std::invalid_argument("Choose one noise mode: -regular or -stationary");
		if (cmd.isSet("regular"))
			return oc::SdNoiseDistribution::Regular;
		if (cmd.isSet("stationary"))
			return oc::SdNoiseDistribution::Stationary;
		return fallback;
	}

	static std::pair<oc::SdNoiseDistribution, u64> correlationOptions(const oc::CLP& cmd)
	{
		const auto noise = benchmarkNoise(cmd, CorGenerator::defaultNoise);
		if (!cmd.isSet("b"))
			return { noise, CorGenerator::defaultBatchSize(noise) };
		const auto exponent = cmd.get<int>("b");
		if (exponent < 10 || exponent > 26)
			throw std::invalid_argument("Correlation batch exponent -b must be between 10 and 26");
		return { noise, 1ull << exponent };
	}

    void CorGen_benchmark(const oc::CLP& cmd)
	{
		// request size
		u64 n = cmd.getOr("n", 1ull << cmd.getOr("nn", 20));

		// number of ole requests
		u64 ole = cmd.getOr("ole", 32);

		// number of ot requests
		u64 ot = cmd.getOr("ot", 0);

		// batch size
		const auto [noise, batch] = correlationOptions(cmd);

		u64 trials = cmd.getOr("trials", 1);

		//bool gen = cmd.isSet("gen");

		u64 chunk = cmd.getOr("chunk", 5);


		u64 nt = cmd.getOr("nt", 1);

		macoro::thread_pool pool0;
		auto e0 = pool0.make_work();
		pool0.create_threads(nt);
		macoro::thread_pool pool1;
		auto e1 = pool1.make_work();
		pool1.create_threads(nt);

		oc::Timer timer;
		PRNG prng0(oc::ZeroBlock);
		PRNG prng1(oc::OneBlock);

		for (u64 trial = 0; trial < trials; ++trial)
		{
			auto sock = coproto::LocalAsyncSocket::makePair();
			sock[0].setExecutor(pool0);
			sock[1].setExecutor(pool1);

			CorGenerator g[2];
			auto begin = timer.setTimePoint("begin trial ------------- ");
			g[0].init(std::move(sock[0]), prng0, 0, nt, batch, false, noise);
			g[1].init(std::move(sock[1]), prng1, 1, nt, batch, false, noise);

			g[0].mGenState->mPool = &pool0;
			g[1].mGenState->mPool = &pool1;
			g[0].mGenState->setTimer(timer);
			g[1].mGenState->setTimer(timer);

			std::vector<std::array<BinOleRequest, 2>> oleReqs(ole);
			std::vector<OtRecvRequest> otRecvReqs(ot);
			std::vector<OtSendRequest> otSendReqs(ot);


			for (u64 i = 0; i < std::max<u64>(ole, ot); ++i)
			{
				if (i < ole)
				{
					oleReqs[i][0] = g[0].binOleRequest(n);
					oleReqs[i][1] = g[1].binOleRequest(n);
				}
				if (i < ot)
				{
					otRecvReqs[i] = g[0].recvOtRequest(n);
					otSendReqs[i] = g[1].sendOtRequest(n);
				}
			}

			auto t0 = g[0].start() | macoro::start_on(pool0);
			auto t1 = g[1].start() | macoro::start_on(pool1);

			//std::cout << "request done \n\n" << std::endl;
			for (u64 i = 0; i < std::max<u64>(ole, ot); )
			{
				//std::cout << "\ni = "<<i<<" \n\n" << std::endl;

				std::vector<macoro::eager_task<>> tasks;
				timer.setTimePoint("start batches " + std::to_string(i) + " ... " + std::to_string(i + chunk));

				for (u64 b = 0; b < chunk; ++b, ++i)
				{
					if (i < ole)
					{
						oleReqs[i][0].start();
						oleReqs[i][1].start();
					}
					if (i < ot)
					{
						otRecvReqs[i].start();
						otSendReqs[i].start();
					}
				}


				i -= chunk;
				//std::cout << "\nget i = " << i << " \n" << std::endl;
				for (u64 b = 0; b < chunk; ++b, ++i)
				{
					if (i < ole)
					{
						u64 m = 0;
						BinOle o[2];
						while (m < n)
						{
							//std::cout << "OLE get " << i << std::endl;
							macoro::sync_wait(macoro::when_all_ready(
								oleReqs[i][0].get(o[0]),
								oleReqs[i][1].get(o[1])));

							m += o[0].size();
						}
					}
					//std::cout << "OLE done " << i << std::endl;

					if (i < ot)
					{
						u64 m = 0;
						OtRecv r;
						OtSend s;
						while (m < n)
						{
							macoro::sync_wait(macoro::when_all_ready(
								otRecvReqs[i].get(r),
								otSendReqs[i].get(s)));
							m += r.size();
						}
					}
				}
				timer.setTimePoint("done batches " + std::to_string(i) + " ... " + std::to_string(i + chunk));

			}

			macoro::sync_wait(macoro::when_all_ready(std::move(t0), std::move(t1)));
			auto end = timer.setTimePoint("done trial ");
			auto time = std::chrono::duration_cast<std::chrono::milliseconds>(end - begin).count();
			if (cmd.isSet("quiet") == false)
			{
				std::cout << double(n * ole) / time << " OLE/ms " << std::endl;
				std::cout << double(n * ot) / time << " OT/ms " << std::endl;
			}
		}

		timer.setTimePoint("done");
		if (cmd.isSet("v"))
		{
			std::cout << timer << std::endl;
		}
	}


	void Radix_benchmark(const oc::CLP& cmd)
	{

		u64 n = cmd.getOr("n", 1ull << cmd.getOr("nn", 10));
		u64 m = cmd.getOr("m", 32);
		const auto [noise, batch] = correlationOptions(cmd);
		u64 trials = cmd.getOr("trials", 1);
		u64 nt = cmd.getOr("nt", 1);
		bool mock = cmd.getOr("mock", 0);

		macoro::thread_pool pool0, pool1;
		auto w0 = pool0.make_work();
		auto w1 = pool1.make_work();
		pool0.create_threads(nt);
		pool1.create_threads(nt);

		oc::Timer timer;

		RadixSort s0, s1;

		s0.setTimer(timer);
		s1.setTimer(timer);

		std::vector<oc::block> x(n);
		std::vector<oc::block> y0(n), y1(n);

		auto sock = coproto::LocalAsyncSocket::makePair();
		sock[0].setExecutor(pool0);
		sock[1].setExecutor(pool1);
		auto sock2 = coproto::LocalAsyncSocket::makePair();
		sock2[0].setExecutor(pool0);
		sock2[1].setExecutor(pool1);


		PRNG prng0(oc::ZeroBlock);
		PRNG prng1(oc::OneBlock);

		BinMatrix k[2];
		k[0].resize(n, m);
		k[1].resize(n, m);

		AdditivePerm d[2];

		CorGenerator g[2];


		auto begin = timer.setTimePoint("begin");
		for (u64 t = 0; t < trials; ++t)
		{

			g[0].init(sock2[0].fork(), prng0, 0, nt, batch, mock, noise);
			g[1].init(sock2[1].fork(), prng1, 1, nt, batch, mock, noise);
			g[0].mGenState->mPool = &pool0;
			g[1].mGenState->mPool = &pool1;

			s0.init(0, n, m, g[0]);
			s1.init(1, n, m, g[1]);

			auto r = coproto::sync_wait(coproto::when_all_ready(
				g[0].start(),
				g[1].start(),
				s0.genPerm(k[0], d[0], sock[0], prng0),
				s1.genPerm(k[1], d[1], sock[1], prng1)
			));

			std::get<0>(r).result();
			std::get<1>(r).result();
			std::get<2>(r).result();
			std::get<3>(r).result();
		}
		auto end = timer.setTimePoint("end");

		std::cout << "radix n:" << n << ", m:" << m << "  : " <<
			std::chrono::duration_cast<std::chrono::milliseconds>(end - begin).count() << "ms " <<
			sock[0].bytesSent() / double(n) << "+" << sock[0].bytesReceived() / double(n) << "=" <<
			(sock[0].bytesSent() + sock[0].bytesReceived()) / double(n) << " bytes/eval " << std::endl;
		//std::cout << ole0.mNumBinOle / double(n) << " " << ole1.mNumBinOle / double(n) << " binOle/per" << std::endl;;
		if (cmd.isSet("v"))
		{
			std::cout << timer << std::endl;
			std::cout << sock[0].bytesReceived() / 1000.0 << " " << sock[0].bytesSent() / 1000.0 << " kB " << std::endl;
		}
	}

	void OmJoin_benchmark(const oc::CLP& cmd)
	{

		u64 nL = cmd.getOr("Ln", 1ull << cmd.getOr("Lnn", cmd.getOr("nn", 10)));
		u64 nR = cmd.getOr("Rn", 1ull << cmd.getOr("Rnn", cmd.getOr("nn", 10)));

		const auto [noise, batch] = correlationOptions(cmd);
		u64 keySize = cmd.getOr("m", 32);
		bool mock = cmd.getOr("mock", 1);
		u64 nt = cmd.getOr("nt", 1);

		Table L, R;

		L.init(nL, { {
			{"L1", ColumnType::Int, keySize},
			{"L2", ColumnType::Int, 16}
		} });
		R.init(nR, { {
			{"R1", ColumnType::Int, keySize},
			{"R2", ColumnType::Int, 7}
		} });

		if (!keySize || (keySize < 64 && nL > (u64(1) << keySize)))
			throw std::invalid_argument("Join benchmark needs distinct left keys");
		for (u64 i = 0; i < nL; ++i)
		{
			oc::copyBytesMin(L.mColumns[0].mData.mData[i], i);
			oc::copyBytesMin(L.mColumns[1].mData.mData[i], i);
		}
		for (u64 i = 0; i < nR; ++i)
		{
			const u64 key = i / 2;
			oc::copyBytesMin(R.mColumns[0].mData.mData[i], key);
			R.mColumns[1].mData.mData(i, 0) = i & 127;
		}
		R.mColumns[0].mData.trim();

		PRNG prng(oc::ZeroBlock);
		std::array<Table, 2> Ls, Rs;
		L.share(Ls, prng);
		R.share(Rs, prng);


		auto sock = coproto::LocalAsyncSocket::makePair();
		auto sock2 = coproto::LocalAsyncSocket::makePair();
		macoro::thread_pool pool0;
		auto e0 = pool0.make_work();
		pool0.create_threads(nt);
		macoro::thread_pool pool1;
		auto e1 = pool1.make_work();
		pool1.create_threads(nt);
		sock[0].setExecutor(pool0);
		sock[1].setExecutor(pool1);
		sock2[0].setExecutor(pool0);
		sock2[1].setExecutor(pool1);

		OmJoin join0, join1;
		join0.mSort.mL = join1.mSort.mL = cmd.getOr("radixBits", join0.mSort.mL);

		CorGenerator ole0, ole1;
		ole0.init(sock2[0].fork(), prng, 0, nt, batch, mock, noise);
		ole1.init(sock2[1].fork(), prng, 1, nt, batch, mock, noise);
		ole0.mGenState->mPool = &pool0;
		ole1.mGenState->mPool = &pool1;

		PRNG prng0(oc::ZeroBlock);
		PRNG prng1(oc::OneBlock);

		Table out[2];

		auto exp = join(L[0], R[0], { L[0], R[1], L[1] });
		oc::Timer timer;
		join0.setTimer(timer);


		JoinQuery
			query0(Ls[0][0], Rs[0][0], { Ls[0][0], Rs[0][1], Ls[0][1] }),
			query1(Ls[1][0], Rs[1][0], { Ls[1][0], Rs[1][1], Ls[1][1] });

		join0.init(query0, ole0);
		join1.init(query1, ole1);

		auto begin = timer.setTimePoint("begin");
		auto r = macoro::sync_wait(macoro::when_all_ready(
			ole0.start(),
			ole1.start(),
			join0.join(query0, out[0], prng0, sock[0]),
			join1.join(query1, out[1], prng1, sock[1])
		));
		auto end = timer.setTimePoint("end");

		std::apply([](auto&... task) { (task.result(), ...); }, r);
		auto actual = reveal(out[0], out[1], true);
		exp.extractActive();
		exp.sort();
		actual.sort();
		if (actual != exp)
			throw std::runtime_error("Secure join disagrees with the plaintext result");

		const double rows = double(nL + nR);
		const u64 online = sock[0].bytesSent() + sock[0].bytesReceived();
		const u64 preprocessing = sock2[0].bytesSent() + sock2[0].bytesReceived();
		std::cout << "OmJoin Ln:" << nL << ", Rn:" << nR << " m:" << keySize
			<< " radix bits:" << join0.mSort.mL << "  ~ "
			<< std::chrono::duration_cast<std::chrono::milliseconds>(end - begin).count() << "ms "
			<< online / rows << " online + " << preprocessing / rows << " preprocessing = "
			<< (online + preprocessing) / rows << " bytes/elem" << std::endl;

		if (cmd.isSet("timing"))
			std::cout << timer << std::endl;
	}

	void AltMod_benchmark(const oc::CLP& cmd)
	{



		u64 n = cmd.getOr("n", 1ull << cmd.getOr("nn", 10));
		u64 trials = cmd.getOr("trials", 1);
		u64 nt = cmd.getOr("nt", 1);
		const auto [noise, batch] = correlationOptions(cmd);
		auto useOle = cmd.isSet("ole");

		oc::Timer timer;

		AltModWPrfSender sender;
		AltModWPrfReceiver recver;

		sender.mUseMod2F4Ot = !useOle;
		recver.mUseMod2F4Ot = !useOle;

		sender.setTimer(timer);
		recver.setTimer(timer);

		std::vector<oc::block> x(n);
		std::vector<oc::block> y0(n), y1(n);

		auto sock = coproto::LocalAsyncSocket::makePair();
		auto sock2 = coproto::LocalAsyncSocket::makePair();
		macoro::thread_pool pool0;
		auto e0 = pool0.make_work();
		pool0.create_threads(nt);
		macoro::thread_pool pool1;
		auto e1 = pool1.make_work();
		pool1.create_threads(nt);
		sock[0].setExecutor(pool0);
		sock[1].setExecutor(pool1);
		sock2[0].setExecutor(pool0);
		sock2[1].setExecutor(pool1);


		PRNG prng0(oc::ZeroBlock);
		PRNG prng1(oc::OneBlock);

		CorGenerator ole0, ole1;
		ole0.init(sock2[0].fork(), prng0, 0, nt, batch, cmd.getOr("mock", 1), noise);
		ole1.init(sock2[1].fork(), prng1, 1, nt, batch, cmd.getOr("mock", 1), noise);


		prng0.get(x.data(), x.size());
		u64 numOle = 0;
		u64 numF4BitOt = 0;
		u64 numOt = 0;

		auto begin = timer.setTimePoint("begin");
		for (u64 t = 0; t < trials; ++t)
		{
			sender.init(n, ole0);
			recver.init(n, ole1);

			numOle += ole0.mGenState->mNumOle;
			numF4BitOt += ole0.mGenState->mNumF4BitOt;
			numOt += ole0.mGenState->mNumOt;

			auto r = coproto::sync_wait(coproto::when_all_ready(
				ole0.start() | macoro::start_on(pool0),
				ole1.start() | macoro::start_on(pool1),
				sender.evaluate({}, y0, sock[0], prng0) | macoro::start_on(pool0),
				recver.evaluate(x, y1, sock[1], prng1) | macoro::start_on(pool1)
			));
			std::get<0>(r).result();
			std::get<1>(r).result();
			std::get<2>(r).result();
			std::get<3>(r).result();
		}
		auto end = timer.setTimePoint("end");

		auto ntr = n * trials;

		std::cout << "AltModWPrf n:" << n << ", " <<
			std::chrono::duration_cast<std::chrono::nanoseconds>(end - begin).count() / double(ntr) << "ns/eval " <<
			sock[0].bytesSent() / double(ntr) << "+" << sock[0].bytesReceived() / double(ntr) << "=" <<
			(sock[0].bytesSent() + sock[0].bytesReceived()) / double(ntr) << " bytes/eval ";

		std::cout << numOle / double(ntr) << " ole/eval ";
		std::cout << numF4BitOt / double(ntr) << " f4/eval ";
		std::cout << numOt / double(ntr) << " ot/eval ";

		std::cout << std::endl;

		if (cmd.isSet("v"))
		{
			std::cout << timer << std::endl;
			std::cout << sock[0].bytesReceived() / 1000.0 << " " << sock[0].bytesSent() / 1000.0 << " kB " << std::endl;
		}
	}

	void OT_benchmark(const oc::CLP& cmd)
	{
#ifdef ENABLE_SILENTOT
		using namespace oc;
		SilentOtExtSender sender;
		SilentOtExtReceiver recver;

		u64 trials = cmd.getOr("t", 10);

		u64 n = cmd.getOr("n", 1ull << cmd.getOr("nn", 20));
		MultType multType = (MultType)cmd.getOr("m", (int)MultType::Spin);
		std::cout << multType << std::endl;


		PRNG prng0(ZeroBlock), prng1(ZeroBlock);
		block delta = prng0.get();

		auto sock = coproto::LocalAsyncSocket::makePair();

		Timer sTimer;
		Timer rTimer;
		const auto noise = benchmarkNoise(cmd, multType == MultType::Spin ?
			CorGenerator::defaultNoise : SdNoiseDistribution::Regular);
        auto s = rTimer.setTimePoint("start");
		sender.setTimer(rTimer);
		recver.setTimer(rTimer);
		for (u64 t = 0; t < trials; ++t)
		{
            if (t == 0 || (noise == SdNoiseDistribution::Regular && multType != MultType::Spin))
            {
                sender.configure(n, 2, 1, SilentSecType::SemiHonest, noise, multType);
                recver.configure(n, 2, 1, SilentSecType::SemiHonest, noise, multType);
                if (noise == SdNoiseDistribution::Regular && multType == MultType::Spin)
                {
                    prepareRegularSpin(sender.mSpin, n);
                    prepareRegularSpin(recver.mSpin, n);
                }
            }

			auto choice = recver.sampleBaseChoiceBits(prng0);
			std::vector<std::array<block, 2>> sendBase(sender.baseCount().mBaseOtCount);
			std::vector<block> recvBase(recver.baseCount().mBaseOtCount);
			oc::BitVector c(sender.baseCount().mBaseVoleCount);
            c.randomize(prng0);
            std::vector<block> b(c.size(), ZeroBlock), a(c.size());
            for (u64 i = 0; i < c.size(); ++i)
                a[i] = c[i] ? delta : ZeroBlock;
            sender.setBaseCors(sendBase, b, delta);
            recver.setBaseCors(recvBase, choice, a, c);

			auto p0 = sender.silentSendInplace(delta, n, prng0, sock[0]);
			auto p1 = recver.silentReceiveInplace(n, prng1, sock[1], oc::ChoiceBitPacking::True);

			rTimer.setTimePoint("r start");
			auto results = coproto::sync_wait(macoro::when_all_ready(
				std::move(p0), std::move(p1)));
			std::get<0>(results).result();
			std::get<1>(results).result();
			rTimer.setTimePoint("r done");

		}
		auto e = rTimer.setTimePoint("end");

		auto time = std::chrono::duration_cast<std::chrono::milliseconds>(e - s).count();
		auto avgTime = time / double(trials);
		auto timePer512 = avgTime / n * 512;
		std::cout << "OT n:" << n << ", " <<
			avgTime << "ms/batch, " << timePer512 << "ms/512ot" << std::endl;

		std::cout << rTimer << std::endl;

		std::cout << sock[0].bytesReceived() / trials << " " << sock[1].bytesReceived() / trials << " bytes per " << std::endl;
#else
		std::cout << "ENABLE_SILENTOT = false" << std::endl;
#endif
	}


	void F4_benchmark(const oc::CLP& cmd)
	{
#ifdef ENABLE_SILENTOT
		using namespace oc;
		SilentF4VoleReceiver recver;
		SilentF4VoleSender sender;

		u64 trials = cmd.getOr("t", 10);

		u64 n = cmd.getOr("n", 1ull << cmd.getOr("nn", 20));
		MultType multType = (MultType)cmd.getOr("m", (int)MultType::Spin);
		std::cout << multType << std::endl;

		recver.mMultType = multType;
		sender.mMultType = multType;

		PRNG prng0(ZeroBlock), prng1(ZeroBlock);
		block delta = prng0.get<block>() & block(~0ull, ~0ull << 2);

		auto sock = coproto::LocalAsyncSocket::makePair();

		Timer sTimer;
		Timer rTimer;
		const auto noise = benchmarkNoise(cmd, multType == MultType::Spin ?
			CorGenerator::defaultNoise : SdNoiseDistribution::Regular);
        auto s = rTimer.setTimePoint("start");
		sender.setTimer(rTimer);
		recver.setTimer(rTimer);
		for (u64 t = 0; t < trials; ++t)
		{
			if (t == 0 || noise == SdNoiseDistribution::Regular)
            {
                sender.configure(n, SilentBaseType::BaseExtend, 128, {}, noise);
                recver.configure(n, SilentBaseType::BaseExtend, 128, {}, noise);
            }

			auto choice = recver.sampleBaseChoiceBits(prng0);
			auto cBase = recver.sampleBaseVoleVals(prng0);

			std::vector<block> bBase(cBase.size()), aBase(cBase.size());

			prng0.get(aBase.data(), aBase.size());
			CoeffCtxGF4 ctx;

			for (u64 i = 0; i < aBase.size(); ++i)
			{
				aBase[i] &= block(~0ull, ~0ull << 2);
				ctx.mul(bBase[i], delta, cBase[i]);
				ctx.plus(bBase[i], bBase[i], aBase[i]);
			}

			std::vector<std::array<block, 2>> sendBase(sender.silentBaseOtCount());
			std::vector<block> recvBase(recver.silentBaseOtCount());
			sender.setSilentBaseOts(sendBase, bBase);
			recver.setSilentBaseOts(recvBase, aBase);

			auto p0 = sender.silentSendInplace(delta, n, prng0, sock[0]);
			auto p1 = recver.silentReceiveInplace(n, prng1, sock[1]);

			rTimer.setTimePoint("r start");
			auto results = coproto::sync_wait(macoro::when_all_ready(
				std::move(p0), std::move(p1)));
			std::get<0>(results).result();
			std::get<1>(results).result();
			rTimer.setTimePoint("r done");

		}
		auto e = rTimer.setTimePoint("end");

		auto time = std::chrono::duration_cast<std::chrono::milliseconds>(e - s).count();
		auto avgTime = time / double(trials);
		auto timePer512 = avgTime / n * 512;
		std::cout << "F4 n:" << n << ", " <<
			avgTime << "ms/batch, " << timePer512 << "ms/512ot" << std::endl;

		if (cmd.isSet("verbose"))
		{
			std::cout << rTimer << std::endl;
			std::cout << sock[0].bytesReceived() / trials << " " << sock[1].bytesReceived() / trials << " bytes per " << std::endl;
		}
#else
		std::cout << "ENABLE_SILENTOT = false" << std::endl;
#endif
	}

	void AltModPerm_benchmark(const oc::CLP& cmd)
	{

		u64 n = cmd.getOr("n", 1ull << cmd.getOr("nn", 10));
		u64 rowSize = cmd.getOr("m", 16);
		const auto [noise, batch] = correlationOptions(cmd);
		//bool debug = cmd.isSet("debug");
		u64 nt = cmd.getOr("nt", 1);
		// bool invPerm = false;

		macoro::thread_pool pool0;
		auto e0 = pool0.make_work();
		pool0.create_threads(nt);

#if 0
		auto& pool1 = pool0;
#else
		macoro::thread_pool pool1;
		auto e1 = pool1.make_work();
		pool1.create_threads(nt);
#endif
		auto sock = coproto::LocalAsyncSocket::makePair();
		sock[0].setExecutor(pool0);
		sock[1].setExecutor(pool1);
		auto sock2 = coproto::LocalAsyncSocket::makePair();
		sock2[0].setExecutor(pool0);
		sock2[1].setExecutor(pool1);

		PRNG prng0(oc::ZeroBlock);
		PRNG prng1(oc::OneBlock);

		AltModPermGenSender AltModPerm0;
		AltModPermGenReceiver AltModPerm1;

		oc::Matrix<oc::block>
			aExp(n, oc::divCeil(rowSize, 16));

		Perm pi(n, prng0);
		CorGenerator ole0, ole1;


		ole0.init(std::move(sock2[0]), prng0, 0, nt, batch, cmd.getOr("mock", 1), noise);
		ole1.init(std::move(sock2[1]), prng1, 1, nt, batch, cmd.getOr("mock", 1), noise);
		ole0.mGenState->mPool = &pool0;
		ole1.mGenState->mPool = &pool1;

		AltModPerm0.init(n, rowSize, ole0);
		AltModPerm1.init(n, rowSize, ole1);

		PermCorSender perm0;
		PermCorReceiver perm1;
		oc::Timer timer;
		auto begin = timer.setTimePoint("b");
		auto r = macoro::sync_wait(macoro::when_all_ready(
			ole0.start() | macoro::start_on(pool0),
			ole1.start() | macoro::start_on(pool1),
			AltModPerm0.generate(pi, prng0, sock[0], perm0) | macoro::start_on(pool0),
			AltModPerm1.generate(prng1, sock[1], perm1) | macoro::start_on(pool1)
		));
		auto end = timer.setTimePoint("b");

		std::get<0>(r).result();
		std::get<1>(r).result();


		std::cout << "AltModPerm: n " << n << ", nt " << nt << ", " <<
			std::chrono::duration_cast<std::chrono::milliseconds>(end - begin).count() << "ms " <<
			sock[0].bytesSent() / double(n) << "+" << sock[0].bytesReceived() / double(n) << "=" <<
			(sock[0].bytesSent() + sock[0].bytesReceived()) / double(n) << " bytes/eval " << std::endl;
	}

	void AltMod_compressB_benchmark(const oc::CLP& cmd)
	{
		u64 n = cmd.getOr("n", 1ull << cmd.getOr("nn", 20));

		if (cmd.isSet("single"))
		{
			oc::AlignedUnVector<oc::block> y(n);
			oc::AlignedUnVector<oc::block> v(n);
			PRNG prng(oc::ZeroBlock);
			prng.get(v.data(), v.size());
			oc::Timer timer;
			auto b = timer.setTimePoint("begin");
			for (u64 i = 0; i < n; ++i)
			{
				AltModPrf::mBCode.encode(v[i], y[i]);
			}
			//compressB(v, y);
			auto e = timer.setTimePoint("end");
			oc::block bb;
			for (u64 i = 0; i < n; ++i)
				bb = bb ^ y.data()[i];

			std::cout << "single compressB n:" << n << ", " <<
				std::chrono::duration_cast<std::chrono::milliseconds>(e - b).count() << "ms " << std::endl;;

			std::cout << bb << std::endl;
		}
		{
			oc::AlignedUnVector<oc::block> y(n);
			oc::Matrix<oc::block> v(256, n / 128);
			oc::Timer timer;
			auto b = timer.setTimePoint("begin");
			AltModPrf::compressB(v, y);
			auto e = timer.setTimePoint("end");
			oc::block bb;
			for (u64 i = 0; i < n; ++i)
				bb = bb ^ y.data()[i];

			std::cout << "batched compressB n:" << n << ", " <<
				std::chrono::duration_cast<std::chrono::milliseconds>(e - b).count() << "ms " << std::endl;;

			std::cout << bb << std::endl;
		}
	}

	void AltMod_encodeX_benchmark(const oc::CLP& cmd)
	{

		auto n = cmd.getOr("n", 1 << cmd.getOr("nn", 18));
		auto t = cmd.getOr("t", 4);

		if (cmd.isSet("e1"))
		{

			for (u64 tt : stdv::iota(0, t))
			{
				(void)tt;

				if (n % 128)
					throw RTE_LOC;
				oc::AlignedUnVector<block>x(n);
				oc::Matrix<block>y(4 * 128, n / 128);

				oc::Timer timer;
				AltModPrf prf;

				auto b = timer.setTimePoint("begin");

				prf.expandInputAes(x, y);

				auto e = timer.setTimePoint("end");

				std::cout << "expand  n:" << n << ", " <<
					std::chrono::duration_cast<std::chrono::milliseconds>(e - b).count() << "ms " << std::endl;;
			}
		}

		if (cmd.isSet("e2"))
		{

			for (u64 tt : stdv::iota(0, t))
			{
				(void)tt;
				oc::AlignedUnVector<block>x(n);
				oc::Matrix<block>y(4 * 128, n / 128);


				oc::Timer timer;
				AltModPrf prf;

				auto b = timer.setTimePoint("begin");

				prf.expandInputLinear(x, y);

				auto e = timer.setTimePoint("end");

				std::cout << "expand2 n:" << n << ", " <<
					std::chrono::duration_cast<std::chrono::milliseconds>(e - b).count() << "ms " << std::endl;;
			}
		}

		// if (cmd.isSet("e3"))
		// {

		// 	for (u64 tt : stdv::iota(0, t))
		// 	{
		// 		(void)tt;
		// 		oc::AlignedUnVector<block>x(n);
		// 		oc::Matrix<block>y(4 * 128, n / 128);


		// 		oc::Timer timer;
		// 		AltModPrf prf;
		// 		prf.initExpandInputPermuteLinear();

		// 		auto b = timer.setTimePoint("begin");

		// 		prf.expandInputPermuteLinear(x, y);

		// 		auto e = timer.setTimePoint("end");

		// 		std::cout << "expand3 n:" << n << ", " <<
		// 			std::chrono::duration_cast<std::chrono::milliseconds>(e - b).count() << "ms " << std::endl;;
		// 	}
		// }
	}

	void AltMod_expandA_benchmark(const oc::CLP& cmd)
	{

		F3AccPermCode c;
		auto l = cmd.getOr("n", 1 << cmd.getOr("nn", 18));
		auto k = cmd.getOr("k", 128 * 4);
		auto p = cmd.getOr("p", 0);
		auto batch = 1ull << cmd.getOr("b", 10);
		auto n = k / 2;
		c.init(k, n, p);

		{
			oc::Matrix<block>msb(k, l / 128), lsb(k, l / 128);
			oc::Matrix<block>msbOut(n, l / 128), lsbOut(n, l / 128);
			PRNG prng(oc::ZeroBlock);
			sampleMod3Lookup(prng, msb, lsb);


			auto msb2 = msb;
			auto lsb2 = lsb;


			oc::Timer timer;
			auto b = timer.setTimePoint("begin");
			c.encode(msb2, lsb2, msbOut, lsbOut, batch);

			auto e = timer.setTimePoint("end");

			std::cout << "multA n:" << l << ", " <<
				std::chrono::duration_cast<std::chrono::milliseconds>(e - b).count() << "ms " << std::endl;;
		}

	}

	void AltMod_sampleMod3_benchmark(const oc::CLP& cmd)
	{
		u64 n = cmd.getOr("n", 1ull << cmd.getOr("nn", 16));
		u64 t = cmd.getOr("t", 1);

		oc::AlignedUnVector<block> msb(n), lsb(n);
		PRNG prng(oc::ZeroBlock);

		// if (cmd.isSet("old"))
		// {

		// 	oc::Timer timer;
		// 	auto b = timer.setTimePoint("begin");
		// 	for (u64 k = 0; k < t; ++k)
		// 		sampleMod3Lookup(prng, msb, lsb);
		// 	auto e = timer.setTimePoint("end");

		// 	std::cout << "mod3lookup n:" << n << ", " <<
		// 		std::chrono::duration_cast<std::chrono::milliseconds>(e - b).count() << "ms " << std::endl;;
		// }
		//if(0)
		//{

		//    oc::Timer timer;
		//    auto b = timer.setTimePoint("begin");

		//    for (u64 k = 0; k < t; ++k)
		//        sampleMod3Lookup2(prng, msb, lsb);
		//    auto e = timer.setTimePoint("end");

		//    std::cout << "mod3lookup2 n:" << n << ", " <<
		//        std::chrono::duration_cast<std::chrono::milliseconds>(e - b).count() << "ms " << std::endl;;
		//}
		{

			oc::Timer timer;
			auto b = timer.setTimePoint("begin");

			for (u64 k = 0; k < t; ++k)
				sampleMod3Lookup(prng, msb, lsb);
			auto e = timer.setTimePoint("end");

			std::cout << "mod3lookup3 n:" << n << ", " <<
				std::chrono::duration_cast<std::chrono::milliseconds>(e - b).count() << "ms " << std::endl;;
		}

		//if (0)
		//{

		//    oc::Timer timer;
		//    auto b = timer.setTimePoint("begin");

		//    for (u64 k = 0; k < t; ++k)
		//        sampleMod3Lookup4(prng, msb, lsb);
		//    auto e = timer.setTimePoint("end");

		//    std::cout << "mod3lookup4 n:" << n << ", " <<
		//        std::chrono::duration_cast<std::chrono::milliseconds>(e - b).count() << "ms " << std::endl;;
		//}
	}

	void PprfPerm_benchmark(const oc::CLP& cmd)
	{


		// we want N items permuted.
		u64 N = cmd.getOr("n", 1ull << cmd.getOr("nn", 10));

		// we permute in batches of t items.
		u64 T = cmd.getOr("t", 256);

		// 
		u64 batches = N / T;
		u64 depth = oc::log2ceil(batches);
		//u64 n_ = N * depth;
		u64 trials = cmd.getOr("trials", 1);

		oc::Timer timer;

		PprfPermGenSender sender;
		PprfPermGenReceiver recver;

		auto sock = coproto::LocalAsyncSocket::makePair();

		PRNG prng0(oc::ZeroBlock);
		PRNG prng1(oc::OneBlock);

		auto begin = timer.setTimePoint("begin");
		std::thread thrd([&]() {
			for (u64 t = 0; t < trials; ++t)
			{
				for (u64 d = 0; d < depth; ++d)
				{
					sender.init(N, T);
					macoro::sync_wait(sender.gen(sock[0], prng0));
				}
			}
			});
		for (u64 t = 0; t < trials; ++t)
		{
			for (u64 d = 0; d < depth; ++d)
			{
				recver.init(N, T);
				macoro::sync_wait(recver.gen(sock[1], prng1));
			}
		}
		thrd.join();
		auto end = timer.setTimePoint("end");

		auto totalComm =
			sock[0].bytesSent() + sock[0].bytesReceived() +
			N * 16 * depth * trials;

		std::cout << "PprfPerm n:" << N << ", #ots/per " << (recver.mBaseCount / N) << " depth " << depth << " " <<
			std::chrono::duration_cast<std::chrono::milliseconds>(end - begin).count() / double(trials) << "ms " <<
			(totalComm) / double(1 << 20) << " MB/N " << std::endl;

		//std::cout << ole0.mNumBinOle / double(n) << " " << ole1.mNumBinOle / double(n) << " binOle/per" << std::endl;;
		if (cmd.isSet("v"))
		{
			std::cout << timer << std::endl;
			std::cout << sock[0].bytesReceived() / 1000.0 << " " << sock[0].bytesSent() / 1000.0 << " kB " << std::endl;
		}
	}

	void transpose_benchmark(const oc::CLP& cmd)
	{
		u64 n = oc::roundUpTo(cmd.getOr("n", 1ull << cmd.getOr("nn", 20)), 128);
		oc::AlignedUnVector<oc::block> y(n);

		oc::Timer timer;
		auto b = timer.setTimePoint("begin");
		for (u64 i = 0; i < n; i += 128)
		{
			oc::transpose128(&y[i]);
		}
		auto e = timer.setTimePoint("end");

		std::cout << "transpose n:" << n << ", " <<
			std::chrono::duration_cast<std::chrono::milliseconds>(e - b).count() << "ms " << std::endl;;

	}

}
