#include "CorGenerator_Test.h"
#include "secure-join/CorGenerator/CorGenerator.h"
#include "secure-join/Prf/AltModPrfProto.h"
#include "secure-join/Prf/AltModSimd.h"
using namespace secJoin;

namespace
{
    using Noise = oc::SdNoiseDistribution;
    constexpr std::pair<bool, Noise> cases[] = {
        {false, Noise::Regular}, {true, Noise::Regular}, {false, Noise::Stationary}
    };
}

void CorGenerator_F4BitOt_Test(const oc::CLP& cmd)
{

	u64 n = (1ull << 16) + 3234;

	for (auto [mock, noise] : cases)
	{
		const u64 batch = noise == Noise::Stationary ? 8192 : 1 << 18;

		PRNG prng(oc::ZeroBlock);

		auto sock = coproto::LocalAsyncSocket::makePair();
		CorGenerator send;
		CorGenerator recv;
		send.init(std::move(sock[0]), prng, 0, 2, batch, mock, noise);
		recv.init(std::move(sock[1]), prng, 1, 2, batch, mock, noise);

		send.mGenState->mDebug = cmd.isSet("debug");
		recv.mGenState->mDebug = cmd.isSet("debug");

		auto sReq = send.request<F4BitOtSend>(n);
		auto rReq = recv.request<F4BitOtRecv>(n);
		auto p0 = send.start() | macoro::make_eager();
		auto p1 = recv.start() | macoro::make_eager();


		u64 s = 0;
		while (s < n)
		{
			F4BitOtSend sot;
			F4BitOtRecv rot;

			auto r = macoro::sync_wait(macoro::when_all_ready(
				sReq.get(sot),
				rReq.get(rot)
			));

			std::get<0>(r).result();
			std::get<1>(r).result();

			for (u64 i = 0; i < sot.size() / 128; ++i)
			{
				auto exp =
					(sot.mOts[0][i] & (~rot.mChoiceMsb[i]) & (~rot.mChoiceLsb[i])) ^
					(sot.mOts[1][i] & (~rot.mChoiceMsb[i]) & (rot.mChoiceLsb[i])) ^
					(sot.mOts[2][i] & (rot.mChoiceMsb[i]) & (~rot.mChoiceLsb[i])) ^
					(sot.mOts[3][i] & (rot.mChoiceMsb[i]) & (rot.mChoiceLsb[i]));

				if (exp != rot.mOts[i])
					throw RTE_LOC;
				//if (i < 100)
				//    std::cout << " "<< rot.mChoice[i] << std::endl;
			}

			s += sot.size();
		}


		auto r = macoro::sync_wait(macoro::when_all_ready(
			std::move(p0), std::move(p1)
		));

		std::get<0>(r).result();
		std::get<1>(r).result();

	}

	//auto r = macoro::sync_wait(macoro::when_all_ready(
	//    std::move(t0),
	//    std::move(t1)
	//));

	//std::get<0>(r).result();
	//std::get<1>(r).result();
}

void CorGenerator_Ot_Test(const oc::CLP& cmd)
{

	u64 n = (1ull << 16) + 3234;

	for (auto [mock, noise] : cases)
	{
		const u64 batch = noise == Noise::Stationary ? 8192 : 1 << 18;

		PRNG prng(oc::ZeroBlock);
		//SendBase sBase;sBase.resize(128);
		//RecvBase rBase;rBase.resize(128);
		//rBase.mChoice.randomize(prng);
		//for (u64 i = 0; i < 128; ++i)
		//{
		//    sBase.mBase[i][0] = prng.get<oc::block>();
		//    sBase.mBase[i][1] = prng.get<oc::block>();
		//    rBase.mBase[i] = sBase.mBase[i][rBase.mChoice[i]].getSeed();
		//}

		auto sock = coproto::LocalAsyncSocket::makePair();
		CorGenerator send;
		CorGenerator recv;
		send.init(std::move(sock[0]), prng, 0, 2, batch, mock, noise);
		recv.init(std::move(sock[1]), prng, 1, 2, batch, mock, noise);

		send.mGenState->mDebug = cmd.isSet("debug");
		recv.mGenState->mDebug = cmd.isSet("debug");

		auto sReq = send.sendOtRequest(n);
		auto rReq = recv.recvOtRequest(n);
		auto p0 = send.start() | macoro::make_eager();
		auto p1 = recv.start() | macoro::make_eager();


		u64 s = 0;
		while (s < n)
		{
			OtSend sot;
			OtRecv rot;

			auto r = macoro::sync_wait(macoro::when_all_ready(
				sReq.get(sot),
				rReq.get(rot)
			));

			std::get<0>(r).result();
			std::get<1>(r).result();

			for (u64 i = 0; i < sot.size(); ++i)
			{
				if (sot.mMsg[i][rot.mChoice[i]] != rot.mMsg[i])
					throw RTE_LOC;
				//if (i < 100)
				//    std::cout << " "<< rot.mChoice[i] << std::endl;
			}

			s += sot.size();
		}


		auto r = macoro::sync_wait(macoro::when_all_ready(
			std::move(p0), std::move(p1)
		));

		std::get<0>(r).result();
		std::get<1>(r).result();

	}

	//auto r = macoro::sync_wait(macoro::when_all_ready(
	//    std::move(t0),
	//    std::move(t1)
	//));

	//std::get<0>(r).result();
	//std::get<1>(r).result();
}

void CorGenerator_BinOle_Test(const oc::CLP& cmd)
{

	u64 n = cmd.getOr("n", (1ull << 16) + 3234);

	for (auto [mock, noise] : cases)
	{
		const u64 batch = noise == Noise::Stationary ? 8192 : 1 << 18;


		PRNG prng(oc::ZeroBlock);
		//SendBase sBase;sBase.resize(128);
		//RecvBase rBase;rBase.resize(128);
		//rBase.mChoice.randomize(prng);
		//for (u64 i = 0; i < 128; ++i)
		//{
		//    sBase.mBase[i][0] = prng.get<oc::block>();
		//    sBase.mBase[i][1] = prng.get<oc::block>();
		//    rBase.mBase[i] = sBase.mBase[i][rBase.mChoice[i]].getSeed();
		//}

		auto sock = coproto::LocalAsyncSocket::makePair();
		CorGenerator  send;
		CorGenerator  recv;

		send.init(std::move(sock[0]), prng, 0, 2, batch, mock, noise);
		recv.init(std::move(sock[1]), prng, 1, 2, batch, mock, noise);
		send.mGenState->mDebug = cmd.isSet("debug");
		recv.mGenState->mDebug = cmd.isSet("debug");



		auto sReq = send.binOleRequest(n);
		auto rReq = recv.binOleRequest(n);
		auto p0 = send.start() | macoro::make_eager();
		auto p1 = recv.start() | macoro::make_eager();


		u64 s = 0;
		while (s < n)
		{
			BinOle sot;
			BinOle rot;

			auto r = macoro::sync_wait(macoro::when_all_ready(
				sReq.get(sot),
				rReq.get(rot)
			));

			std::get<0>(r).result();
			std::get<1>(r).result();

			for (u64 i = 0; i < sot.mMult.size(); ++i)
			{
				if ((sot.mMult[i] & rot.mMult[i]) != (sot.mAdd[i] ^ rot.mAdd[i]))
					throw RTE_LOC;
			}

			s += sot.size();
		}

		auto r = macoro::sync_wait(macoro::when_all_ready(
			std::move(p0), std::move(p1)
		));

		std::get<0>(r).result();
		std::get<1>(r).result();
	}
}


void CorGenerator_TritOt_Test(const oc::CLP& cmd)
{
	u64 n = (1ull << 16) + 3234;

	PRNG prng(oc::ZeroBlock);

	for (u64 i = 0; i < n; ++i)
	{
		std::array<block, 8> b;
		b = prng.get();
		u8 msb, lsb;
		sample8Mod3(b.data(), msb, lsb);

		if (msb & lsb)
			throw RTE_LOC;
	}

	for (auto [mock, noise] : cases)
	{
		const u64 batch = noise == Noise::Stationary ? 8192 : 1 << 18;


		auto sock = coproto::LocalAsyncSocket::makePair();
		CorGenerator send;
		CorGenerator recv;
		send.init(std::move(sock[0]), prng, 0, 2, batch, mock, noise);
		recv.init(std::move(sock[1]), prng, 1, 2, batch, mock, noise);

		send.mGenState->mDebug = cmd.isSet("debug");
		recv.mGenState->mDebug = cmd.isSet("debug");

		auto sReq = send.request<TritOtSend>(n);
		auto rReq = recv.request<TritOtRecv>(n);
		auto p0 = send.start() | macoro::make_eager();
		auto p1 = recv.start() | macoro::make_eager();


		u64 s = 0;
		while (s < n)
		{
			TritOtSend sot;
			TritOtRecv rot;

			auto r = macoro::sync_wait(macoro::when_all_ready(
				sReq.get(sot),
				rReq.get(rot)
			));

			std::get<0>(r).result();
			std::get<1>(r).result();

			for (u64 i = 0; i < sot.size(); ++i)
			{
				std::array<int, 2> m =
				{
					bit(sot.lsb()[0].data(), i) + 2 * bit(sot.msb()[0].data(), i),
					bit(sot.lsb()[1].data(), i) + 2 * bit(sot.msb()[1].data(), i)
				};

				int mc = bit(rot.lsb().data(), i) + 2 * bit(rot.msb().data(), i);
				int c = bit(rot.choice().data(), i);
				if (m[c] != mc)
					throw RTE_LOC;

				if (m[0] > 2 || m[1] > 2)
					throw RTE_LOC;
			}

			s += sot.size();
		}


		auto r = macoro::sync_wait(macoro::when_all_ready(
			std::move(p0), std::move(p1)
		));

		std::get<0>(r).result();
		std::get<1>(r).result();

	}

	//auto r = macoro::sync_wait(macoro::when_all_ready(
	//    std::move(t0),
	//    std::move(t1)
	//));

	//std::get<0>(r).result();
	//std::get<1>(r).result();
}

void CorGenerator_mixed_Test(const oc::CLP& cmd)
{

	u64 n = (1ull << 11) + 3234;
	u64 reqSize = 1 << 10;
	for (auto mock : { false, true })
	{
		PRNG prng(oc::ZeroBlock);
		auto sock = coproto::LocalAsyncSocket::makePair();
		CorGenerator  ole[2];

		ole[0].init(std::move(sock[0]), prng, 0, 1, 1 << 18, mock);
		ole[1].init(std::move(sock[1]), prng, 1, 1, 1 << 18, mock);
		ole[0].mGenState->mDebug = cmd.isSet("debug");
		ole[1].mGenState->mDebug = cmd.isSet("debug");

		std::array<std::vector<OtSendRequest>, 2> otSends;
		std::array<std::vector<OtRecvRequest>, 2> otRecvs;
		std::array<std::vector<BinOleRequest>, 2> oles;

		for (u64 i = 0; i < n; i += reqSize)
		{
			oles[0].push_back(ole[0].binOleRequest(reqSize));
			oles[1].push_back(ole[1].binOleRequest(reqSize));

			otRecvs[0].push_back(ole[0].recvOtRequest(reqSize));
			otSends[1].push_back(ole[1].sendOtRequest(reqSize));

			otSends[0].push_back(ole[0].sendOtRequest(reqSize));
			otRecvs[1].push_back(ole[1].recvOtRequest(reqSize));
		}
		auto p0 = ole[0].start() | macoro::make_eager();
		auto p1 = ole[1].start() | macoro::make_eager();


		u64 s = 0;
		u64 rIdx = 0;
		while (s < n)
		{

			{
				BinOle sot;
				BinOle rot;

				//auto p0 = oles[0][rIdx].start() | macoro::make_eager();
				//auto p1 = oles[1][rIdx].start() | macoro::make_eager();
				for (u64 j = 0; j < reqSize;)
				{
					auto r = macoro::sync_wait(macoro::when_all_ready(
						oles[0][rIdx].get(rot),
						oles[1][rIdx].get(sot)
					));

					std::get<0>(r).result();
					std::get<1>(r).result();

					for (u64 i = 0; i < sot.mMult.size(); ++i)
					{
						if ((sot.mMult[i] & rot.mMult[i]) != (sot.mAdd[i] ^ rot.mAdd[i]))
							throw RTE_LOC;
					}

					j += sot.size();
				}

				//auto r = macoro::sync_wait(macoro::when_all_ready(
				//    std::move(p0), std::move(p1)
				//));

				//std::get<0>(r).result();
				//std::get<1>(r).result();
			}

			for (u64 p = 0; p < 2; ++p)
			{

				OtSend sot;
				OtRecv rot;

				//auto p0 = otRecvs[p ^ 0][rIdx].start() | macoro::make_eager();
				//auto p1 = otSends[p ^ 1][rIdx].start() | macoro::make_eager();

				for (u64 j = 0; j < reqSize; )
				{
					rot.mChoice = {};
					auto r = macoro::sync_wait(macoro::when_all_ready(
						otRecvs[p ^ 0][rIdx].get(rot),
						otSends[p ^ 1][rIdx].get(sot)
					));

					std::get<0>(r).result();
					std::get<1>(r).result();
					//std::cout << "--------------" << std::endl;
					u64 failed = 0;
					for (u64 i = 0; i < sot.mMsg.size(); ++i)
					{
						if (sot.mMsg[i][rot.mChoice[i]] != rot.mMsg[i])
						{
							std::cout << i << " " << sot.mMsg[i][0] << " " << sot.mMsg[i][1] << " != " << rot.mMsg[i] << " @ " << rot.mChoice[i] << std::endl;

							++failed;
							if (failed > 40)
								break;
						}
					}
					if (failed)
						throw RTE_LOC;

					j += sot.size();
				}

			}
			++rIdx;
			s += reqSize;
		}

		auto r = macoro::sync_wait(macoro::when_all_ready(
			std::move(p0), std::move(p1)
		));

		std::get<0>(r).result();
		std::get<1>(r).result();
		//auto r = macoro::sync_wait(macoro::when_all_ready(
		//    std::move(p0), std::move(p1)
		//));

		//std::get<0>(r).result();
		//std::get<1>(r).result();
	}
}


void CorGenerator_SpinWorkers_Test(const oc::CLP&)
{
    constexpr u64 k = 8192;
    std::optional<oc::SpinCode> first, second;
    prepareRegularSpin(first, k - 128);
    prepareRegularSpin(second, k);
    if (&first->encoder() != &second->encoder())
        throw std::runtime_error("Regular workers rebuilt an identical padded code");

    SpinBanks bank[2];
    StationaryWorker sender(0, 0, oc::OneBlock, bank[0]), receiver(0, 1, oc::OneBlock, bank[1]);
    PRNG random[2]{PRNG(oc::ZeroBlock), PRNG(oc::OneBlock)};
    auto channel = coproto::LocalAsyncSocket::makePair();
    auto nextSeed = sender.initialSeed(CorType::Ot, k, 0);
    for (unsigned round = 0; round < 2; ++round)
    {
        auto& s = sender.begin(sender.sendOt, CorType::Ot, k, true);
        auto& r = receiver.begin(receiver.recvOt, CorType::Ot, k, false);
        if (s.mCodeSeed != nextSeed || r.mCodeSeed != nextSeed)
            throw std::runtime_error("Stationary stream restarted");
        auto done = macoro::sync_wait(macoro::when_all_ready(
            s.silentSendInplace(s.mDelta, k, random[0], channel[0]),
            r.silentReceiveInplace(k, random[1], channel[1], oc::ChoiceBitPacking::True)));
        std::get<0>(done).result();
        std::get<1>(done).result();
        const auto spec = s.mSpin->encoder().specification();
        if (s.mSpin->encoder().descriptor() != r.mSpin->encoder().descriptor() ||
            spec.route_seed != nextSeed.get<u64>(0) || spec.inner_seed != nextSeed.get<u64>(1))
            throw std::runtime_error("Peers used the wrong stationary round map");
        nextSeed = oc::mAesFixedKey.hashBlock(nextSeed);
        sender.finish(s);
        receiver.finish(r);
    }

    // Four full batches exercise two rounds on each of two workers. Keep the
    // public storage across init() and check private streams start afresh.
    PRNG prng(oc::ZeroBlock);
    std::array<CorGenerator, 2> peers;
    std::array<std::optional<spin::PreparedEncoder::Workspace>, 2> identity;
    std::array<std::array<std::byte, 56>, 2> previous{};
    for (u64 join = 0; join < 2; ++join)
    {
        auto sockets = coproto::LocalAsyncSocket::makePair();
        for (u64 party = 0; party < 2; ++party)
            peers[party].init(std::move(sockets[party]), prng, party, 2, k, false, Noise::Stationary);
        auto send = peers[0].sendOtRequest(4 * k);
        auto recv = peers[1].recvOtRequest(4 * k);
        send.start();
        recv.start();
        auto tasks = macoro::sync_wait(macoro::when_all_ready(peers[0].start(), peers[1].start()));
        std::get<0>(tasks).result();
        std::get<1>(tasks).result();

        u64 seeds[2];
        for (u64 w = 0; w < 2; ++w)
        {
            const auto& left = peers[0].mBanks->idle.at(0).at(w).codes.at(k)->encoder();
            const auto& right = peers[1].mBanks->idle.at(0).at(w).codes.at(k)->encoder();
            if (left.descriptor() != right.descriptor())
                throw std::runtime_error("Stationary peer maps disagree");
            if (join)
            {
                left.prepare_workspace(*identity[w]); // Checks the identity of the underlying bank.
                if (left.descriptor() == previous[w])
                    throw std::runtime_error("New peer session restarted the previous seed schedule");
            }
            else
                identity[w].emplace(left.make_workspace());
            previous[w] = left.descriptor();
            seeds[w] = left.setup_options().bank_seed;
        }
        if (seeds[0] == seeds[1])
            throw std::runtime_error("Concurrent workers sampled the same bank");
    }

    auto sockets = coproto::LocalAsyncSocket::makePair();
    peers[0].init(std::move(sockets[0]), prng, 0, 2, k, false, Noise::Stationary);
    peers[1].init(std::move(sockets[1]), prng, 1, 1, k, false, Noise::Stationary);
    auto send = peers[0].sendOtRequest(k);
    auto recv = peers[1].recvOtRequest(k);
    auto tasks = macoro::sync_wait(macoro::when_all_ready(peers[0].start(), peers[1].start()));
    auto expectMismatch = [](auto& t) {
        try { t.result(); }
        catch (const std::runtime_error& e) {
            if (std::string(e.what()).find("Peer correlation") != std::string::npos)
                return;
            throw;
        }
        throw std::runtime_error("Mismatched worker counts accepted");
    };
    expectMismatch(std::get<0>(tasks));
    expectMismatch(std::get<1>(tasks));
    OtSend out;
    OtRecv in;
    auto requests = macoro::sync_wait(macoro::when_all_ready(send.get(out), recv.get(in)));
    expectMismatch(std::get<0>(requests));
    expectMismatch(std::get<1>(requests));
}
