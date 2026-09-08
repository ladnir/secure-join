#include "secure-join/Sort/PiLogStar.h"
#include "secure-join/Sort/BatcherMerge.h"
#include "coproto/Socket/LocalAsyncSock.h"
#include "coproto/Socket/AsioSocket.h"
#include "macoro/sync_wait.h"
#include "macoro/when_all.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <iomanip>
#include <iostream>
#include <limits>
#include <numeric>
#include <random>
#include <stdexcept>
#include <string>
#include <tuple>
#include <vector>
#ifdef __linux__
#include <sys/resource.h>
#endif

namespace
{
    using namespace secJoin;
    using Clock = std::chrono::steady_clock;

    struct Options
    {
        u64 n = 1024, bits = 32, base = 16, block = 0;
        u64 batchSize = 262144, concurrency = 2;
        int party = -1;
        std::string address = "127.0.0.1:12123", pattern = "random", seed = "1";
        bool selfTest = false, help = false, packed = true, plan = false;
    };

    const std::array<std::string, 6> patterns = {
        "random", "equal", "disjoint", "interleaved", "max", "duplicates"
    };

    u64 number(const std::string& value)
    {
        if (value.empty() || value[0] == '-')
            throw std::invalid_argument("Expected a nonnegative integer: " + value);
        std::size_t consumed = 0;
        const auto result = std::stoull(value, &consumed, 10);
        if (consumed != value.size())
            throw std::invalid_argument("Invalid integer: " + value);
        return result;
    }

    Options parse(int argc, char** argv)
    {
        Options o;
        for (int i = 1; i < argc; ++i)
        {
            const std::string option = argv[i];
            if (option == "--help" || option == "-h") { o.help = true; continue; }
            if (option == "--self-test") { o.selfTest = true; continue; }
            if (option == "--plan") { o.plan = true; continue; }
            if (i + 1 == argc) throw std::invalid_argument("Missing value for " + option);
            const std::string value = argv[++i];
            if (option == "--n") o.n = number(value);
            else if (option == "--bits") o.bits = number(value);
            else if (option == "--base") o.base = number(value);
            else if (option == "--block") o.block = number(value);
            else if (option == "--packed") { if (value != "0" && value != "1") throw std::invalid_argument("--packed must be 0 or 1"); o.packed = value == "1"; }
            else if (option == "--batch-size") o.batchSize = number(value);
            else if (option == "--concurrency") o.concurrency = number(value);
            else if (option == "--party")
            {
                if (value != "0" && value != "1") throw std::invalid_argument("--party must be 0 or 1");
                o.party = value == "0" ? 0 : 1;
            }
            else if (option == "--address") o.address = value;
            else if (option == "--pattern") o.pattern = value;
            else if (option == "--seed") o.seed = value;
            else throw std::invalid_argument("Unknown option: " + option);
        }
        if (!o.n || o.n > (u64(1) << 28)) throw std::invalid_argument("--n must be between 1 and 2^28");
        if (!o.bits || o.bits > 64) throw std::invalid_argument("Synthetic benchmark --bits must be between 1 and 64");
        if (o.batchSize < (1ull << 10) || o.batchSize > (1ull << 26) || (o.batchSize & (o.batchSize - 1)))
            throw std::invalid_argument("--batch-size must be a power of two between 1024 and 67108864");
        if (!o.concurrency || o.concurrency > 1024)
            throw std::invalid_argument("--concurrency must be between 1 and 1024");
        if (std::find(patterns.begin(), patterns.end(), o.pattern) == patterns.end())
            throw std::invalid_argument("Unknown input pattern: " + o.pattern);
        if (o.selfTest && o.party != -1) throw std::invalid_argument("--self-test runs both parties locally");
        return o;
    }

    std::string quoted(const std::string& text)
    {
        std::string out = "\"";
        for (unsigned char c : text)
        {
            if (c == '"' || c == '\\') { out += '\\'; out += c; }
            else if (c >= 32 && c < 127) out += c;
            else
            {
                const char* hex = "0123456789abcdef";
                out += "\\u00"; out += hex[c >> 4]; out += hex[c & 15];
            }
        }
        return out + '"';
    }

    u64 publicSeed(const std::string& value)
    {
        // Only synthetic input generation uses this reproducible public seed.
        // Every protocol PRNG below is seeded separately from the OS.
        u64 hash = 14695981039346656037ull;
        for (unsigned char c : value) { hash ^= c; hash *= 1099511628211ull; }
        return hash;
    }

    struct Data { std::vector<u64> x, y; std::vector<u32> expected; };

    Data dataset(const Options& o)
    {
        std::mt19937_64 rng(publicSeed(o.seed));
        const auto mask = o.bits == 64 ? ~u64(0) : (u64(1) << o.bits) - 1;
        Data data;
        data.x.resize(o.n); data.y.resize(o.n);
        for (u64 i = 0; i < o.n; ++i)
        {
            if (o.pattern == "random") { data.x[i] = rng() & mask; data.y[i] = rng() & mask; }
            else if (o.pattern == "equal") data.x[i] = data.y[i] = mask / 3;
            else if (o.pattern == "max") data.x[i] = data.y[i] = mask;
            else if (o.pattern == "interleaved") { data.x[i] = (2 * i) & mask; data.y[i] = (2 * i + 1) & mask; }
            else if (o.pattern == "duplicates")
            {
                const auto range = o.bits < 3 ? u64(1) << o.bits : 8;
                data.x[i] = rng() % range; data.y[i] = rng() % range;
            }
            else
            {
                const auto split = u64(1) << (o.bits - 1);
                data.x[i] = rng() & (split - 1);
                data.y[i] = split | (rng() & (split - 1));
            }
        }
        std::sort(data.x.begin(), data.x.end());
        std::sort(data.y.begin(), data.y.end());
        data.expected.reserve(2 * o.n);
        u64 x = 0, y = 0;
        while (x < o.n || y < o.n)
            if (y == o.n || (x < o.n && data.x[x] <= data.y[y]))
                data.expected.push_back(static_cast<u32>(x++));
            else data.expected.push_back(static_cast<u32>(o.n + y++));
        return data;
    }

    BinMatrix encode(const std::vector<u64>& values, u64 bits)
    {
        BinMatrix out(values.size(), bits);
        for (u64 row = 0; row < values.size(); ++row)
            for (u64 byte = 0; byte < out.bytesPerEntry(); ++byte)
                out(row, byte) = static_cast<u8>(values[row] >> (8 * byte));
        return out;
    }

    template<typename... Tasks>
    void complete(Tasks&&... tasks)
    {
        auto results = macoro::sync_wait(macoro::when_all_ready(std::forward<Tasks>(tasks)...));
        // when_all_ready itself does not propagate failures of its children.
        std::apply([](auto&... result) { (result.result(), ...); }, results);
    }

    double elapsed(Clock::time_point begin, Clock::time_point end)
    { return std::chrono::duration<double, std::milli>(end - begin).count(); }

    template<typename Action>
    void mustThrow(Action&& action, const char* name)
    {
        try { action(); }
        catch (const std::exception&) { return; }
        throw std::runtime_error(std::string("Rejection test failed: ") + name);
    }

    struct Counters { u64 sent = 0, received = 0; };
    Counters count(coproto::Socket& sock) { return { sock.bytesSent(), sock.bytesReceived() }; }
    Counters difference(Counters end, Counters begin) { return { end.sent - begin.sent, end.received - begin.received }; }

    struct Measurement
    {
        double setupMs = 0, offlineMs = 0, onlineMs = 0, wallMs = 0;
        Counters offline[2], online[2];
    };

    void verify(const AdditivePerm& a, const AdditivePerm& b, const Data& data)
    {
        if (a.size() != data.expected.size() || b.size() != data.expected.size())
            throw std::runtime_error("Verification failed: incorrect output size");
        for (u64 i = 0; i < a.size(); ++i)
            if ((a.mShare[i] ^ b.mShare[i]) != data.expected[i])
                throw std::runtime_error("Verification failed: stable gather index mismatch at output " + std::to_string(i) +
                    ", got " + std::to_string(a.mShare[i] ^ b.mShare[i]) + ", expected " + std::to_string(data.expected[i]));
    }

    void report(const Options& o, const PiLogStar& protocol, const Measurement& m)
    {
        std::cout << std::fixed << std::setprecision(3)
            << "{\"type\":\"benchmark\",\"protocol\":\"pi-logstar\",\"transport\":"
            << quoted(o.party < 0 ? "local_async" : "tcp") << ",\"party\":" << o.party
            << ",\"real_crypto\":true,\"public_synthetic_inputs\":true,\"verified\":true"
            << ",\"n\":" << o.n << ",\"key_bits\":" << o.bits << ",\"base_case\":" << o.base
            << ",\"block_override\":" << o.block << ",\"batch_size\":" << o.batchSize
            << ",\"packed_enabled\":" << (o.packed ? "true" : "false")
            << ",\"concurrency\":" << o.concurrency << ",\"pattern\":" << quoted(o.pattern)
            << ",\"public_seed\":" << quoted(o.seed) << ",\"padded_n\":" << protocol.paddedSize()
            << ",\"expanded_rows\":" << protocol.expandedSize()
            << ",\"online_gmw_rounds_partial\":" << protocol.gmwRounds()
            << ",\"online_round_bound\":" << protocol.onlineRoundBound()
            << ",\"online_gmw_ands_padded\":" << protocol.paddedAnds()
            << ",\"setup_ms\":" << m.setupMs << ",\"offline_ms\":" << m.offlineMs
            << ",\"online_ms\":" << m.onlineMs << ",\"wall_ms\":" << m.wallMs;
#ifdef __linux__
        rusage usage{};
        if (getrusage(RUSAGE_SELF, &usage) == 0)
            std::cout << ",\"peak_rss_kib\":" << usage.ru_maxrss;
#endif
        const auto beginParty = o.party < 0 ? 0 : o.party;
        const auto endParty = o.party < 0 ? 2 : o.party + 1;
        u64 offlineBytes = 0, onlineBytes = 0;
        for (int party = beginParty; party < endParty; ++party)
        {
            std::cout << ",\"offline_p" << party << "_sent_bytes\":" << m.offline[party].sent
                << ",\"offline_p" << party << "_received_bytes\":" << m.offline[party].received
                << ",\"online_p" << party << "_sent_bytes\":" << m.online[party].sent
                << ",\"online_p" << party << "_received_bytes\":" << m.online[party].received;
            offlineBytes += m.offline[party].sent;
            onlineBytes += m.online[party].sent;
        }
        if (o.party < 0)
            std::cout << ",\"offline_total_sent_bytes\":" << offlineBytes << ",\"online_total_sent_bytes\":" << onlineBytes;
        std::cout << ",\"stage_party\":" << (o.party < 0 ? 0 : o.party) << ",\"stages\":[";
        bool first = true;
        for (const auto& stage : protocol.stages())
        {
            if (!first) std::cout << ',';
            first = false;
            std::cout << "{\"name\":" << quoted(stage.name) << ",\"batches\":" << stage.batches
                << ",\"half_size\":" << stage.halfSize << ",\"block_size\":" << stage.blockSize
                << ",\"gmw_rounds_partial\":" << stage.gmwRounds << ",\"gmw_ands_padded\":" << stage.paddedAnds
                << ",\"sent_bytes\":" << stage.sentBytes << ",\"received_bytes\":" << stage.receivedBytes
                << ",\"milliseconds\":" << stage.milliseconds << '}';
        }
        std::cout << "]}" << std::endl;
    }

    void localRun(const Options& o, bool randomShares = false)
    {
        const auto data = dataset(o);
        auto plainX = encode(data.x, o.bits), plainY = encode(data.y, o.bits);
        auto socks = coproto::LocalAsyncSocket::makePair();
        PRNG prng0(oc::sysRandomSeed()), prng1(oc::sysRandomSeed());
        BinMatrix x[2], y[2];
        x[0] = plainX; x[1].resize(o.n, o.bits);
        y[0].resize(o.n, o.bits); y[1] = plainY;
        if (randomShares)
        {
            prng0.get(x[1].data(), x[1].size());
            prng0.get(y[0].data(), y[0].size());
            x[1].trim(); y[0].trim();
            for (u64 i = 0; i < plainX.size(); ++i)
            { x[0](i) = plainX(i) ^ x[1](i); y[1](i) = plainY(i) ^ y[0](i); }
        }
        CorGenerator cor[2];
        PiLogStar protocol[2];
        AdditivePerm output[2];
        PiLogStarOptions params{ o.base, o.block, o.packed };
        Measurement m;
        auto wallStart = Clock::now();
        cor[0].init(socks[0].fork(), prng0, 0, o.concurrency, o.batchSize, false);
        cor[1].init(socks[1].fork(), prng1, 1, o.concurrency, o.batchSize, false);
        for (u64 party = 0; party < 2; ++party)
        { protocol[party].init(o.n, o.bits, cor[party], params); protocol[party].preprocess(); }
        auto offlineStart = Clock::now();
        const Counters before[2] = { count(socks[0]), count(socks[1]) };
        complete(cor[0].start(), cor[1].start(),
            protocol[0].prepare(socks[0], prng0), protocol[1].prepare(socks[1], prng1));
        complete(socks[0].flush(), socks[1].flush());
        auto onlineStart = Clock::now();
        const Counters prepared[2] = { count(socks[0]), count(socks[1]) };
        complete(protocol[0].merge(x[0], y[0], output[0], socks[0], prng0),
            protocol[1].merge(x[1], y[1], output[1], socks[1], prng1));
        complete(socks[0].flush(), socks[1].flush());
        auto onlineEnd = Clock::now();
        for (u64 party = 0; party < 2; ++party)
        {
            m.offline[party] = difference(prepared[party], before[party]);
            m.online[party] = difference(count(socks[party]), prepared[party]);
        }
        m.setupMs = elapsed(wallStart, offlineStart); m.offlineMs = elapsed(offlineStart, onlineStart);
        m.onlineMs = elapsed(onlineStart, onlineEnd); m.wallMs = elapsed(wallStart, onlineEnd);
        // Deliberate output reconstruction for the test oracle, after timing.
        verify(output[0], output[1], data);
        if (randomShares)
        {
            mustThrow([&] { complete(protocol[0].merge(x[0], y[0], output[0], socks[0], prng0)); }, "reused merge");
            mustThrow([&] { complete(protocol[0].prepare(socks[0], prng0)); }, "reused prepare");
            mustThrow([&] { protocol[0].preprocess(); }, "reused preprocess");
            mustThrow([&] { protocol[0].init(o.n, o.bits, cor[0], params); }, "reused init");
        }
        report(o, protocol[0], m);
    }

    void networkRun(const Options& o)
    {
#ifdef COPROTO_ENABLE_BOOST
        const auto data = dataset(o);
        auto sock = coproto::asioConnect(o.address, o.party == 0);
        // Match public configuration before consuming any protocol correlations.
        const auto pattern = std::find(patterns.begin(), patterns.end(), o.pattern) - patterns.begin();
        const std::array<u64, 11> config = { 2, o.n, o.bits, o.base, o.block,
            o.batchSize, o.concurrency, static_cast<u64>(pattern), publicSeed(o.seed), static_cast<u64>(o.packed), static_cast<u64>(o.party) };
        std::array<u64, 11> peer{};
        complete(sock.send(coproto::copy(config)), sock.recv(peer));
        peer.back() ^= 1;
        if (peer != config) throw std::runtime_error("The parties supplied different public configurations");

        BinMatrix x(o.n, o.bits), y(o.n, o.bits);
        if (o.party == 0) x = encode(data.x, o.bits);
        else y = encode(data.y, o.bits);
        PRNG prng(oc::sysRandomSeed());
        CorGenerator cor;
        PiLogStar protocol;
        AdditivePerm output, peerOutput;
        Measurement m;
        auto wallStart = Clock::now();
        cor.init(sock.fork(), prng, o.party, o.concurrency, o.batchSize, false);
        protocol.init(o.n, o.bits, cor, { o.base, o.block, o.packed });
        protocol.preprocess();
        auto offlineStart = Clock::now();
        auto before = count(sock);
        complete(cor.start(), protocol.prepare(sock, prng));
        complete(sock.flush());
        const auto offlineEnd = Clock::now();
        const auto offlineDone = count(sock);
        // Finish both parties' offline measurements before either sends online
        // traffic. Synchronization traffic itself is outside phase counters.
        std::array<u64, 1> barrier = { 1 }, peerBarrier{};
        complete(sock.send(coproto::copy(barrier)), sock.recv(peerBarrier));
        complete(sock.flush());
        if (peerBarrier != barrier) throw std::runtime_error("Offline phase synchronization failed");
        auto onlineStart = Clock::now();
        auto prepared = count(sock);
        complete(protocol.merge(x, y, output, sock, prng));
        complete(sock.flush());
        auto onlineEnd = Clock::now();
        m.offline[o.party] = difference(offlineDone, before);
        m.online[o.party] = difference(count(sock), prepared);
        m.setupMs = elapsed(wallStart, offlineStart); m.offlineMs = elapsed(offlineStart, offlineEnd);
        m.onlineMs = elapsed(onlineStart, onlineEnd); m.wallMs = elapsed(wallStart, onlineEnd);
        // This executable is a public-input benchmark. Opening its output is
        // intentional and separate from the secure API and measured traffic.
        barrier[0] = 2;
        complete(sock.send(coproto::copy(barrier)), sock.recv(peerBarrier));
        if (peerBarrier != barrier) throw std::runtime_error("Online phase synchronization failed");
        peerOutput.mShare.resize(output.size());
        complete(sock.send(coproto::copy(output.mShare)), sock.recv(peerOutput.mShare));
        complete(sock.flush());
        verify(output, peerOutput, data);
        report(o, protocol, m);
#else
        (void)o;
        throw std::runtime_error("TCP support requires SECUREJOIN_ENABLE_BOOST=ON");
#endif
    }

    void comparatorTest()
    {
        std::mt19937_64 rng(192847);
        u64 cases = 0;
        for (u64 width : { 1, 2, 3, 4, 5, 6, 7, 8, 9, 15, 16, 31, 32, 33, 63, 64, 65, 127 })
        {
            BetaCircuit cir;
            BetaBundle a(width), b(width), z(1);
            cir.addInputBundle(a); cir.addInputBundle(b); cir.addOutputBundle(z);
            cir.addCopy(logstarLessThan(cir, a, b), z[0]);
            cir.levelByAndDepth();
            std::array<oc::BitVector, 2> inputs = { oc::BitVector(width), oc::BitVector(width) };
            std::array<oc::BitVector, 1> outputs = { oc::BitVector(1) };
            const u64 trials = width <= 6 ? u64(1) << (2 * width) : 256;
            for (u64 trial = 0; trial < trials; ++trial)
            {
                for (u64 bit = 0; bit < width; ++bit)
                {
                    inputs[0][bit] = width <= 6 ? (trial >> bit) & 1 : rng() & 1;
                    inputs[1][bit] = width <= 6 ? (trial >> (width + bit)) & 1 : rng() & 1;
                }
                if (width > 6 && trial % 17 == 0) inputs[1] = inputs[0];
                bool expected = false;
                for (u64 bit = width; bit-- > 0;)
                    if (inputs[0][bit] != inputs[1][bit])
                    { expected = inputs[0][bit] < inputs[1][bit]; break; }
                cir.evaluate(inputs, outputs, false);
                if (bool(outputs[0][0]) != expected) throw std::runtime_error("Comparison circuit failed");
                ++cases;
            }
        }
        std::cout << "{\"type\":\"self_test\",\"test\":\"comparison_circuit\",\"cases\":" << cases << ",\"passed\":true}" << std::endl;
    }

    void rejectionTest()
    {
        auto sock = coproto::LocalAsyncSocket::makePair();
        PRNG prng(oc::sysRandomSeed());
        CorGenerator real, mock;
        real.init(sock[0].fork(), prng, 0, 1, 1 << 14, false);
        mock.init(sock[1].fork(), prng, 1, 1, 1 << 14, true);
        mustThrow([&] { PiLogStar p; p.init(17, 32, mock); }, "mock correlations");
        mustThrow([&] { PiLogStar p; p.init(0, 32, real); }, "zero input length");
        mustThrow([&] { PiLogStar p; p.init(17, 0, real); }, "zero key width");
        mustThrow([&] { PiLogStar p; p.init(17, 257, real); }, "oversized key width");
        mustThrow([&] { PiLogStar p; p.init(17, 32, real, { 3, 0 }); }, "invalid base size");
        mustThrow([&] { PiLogStar p; p.init(17, 32, real, { 8, 3 }); }, "invalid block size");
        mustThrow([&] { BatcherMerge b; b.init(1, 3, 8, 8, real); }, "non-power-of-two merge size");
        mustThrow([&] { BatcherMerge b; b.init(1, 4, 9, 8, real); }, "oversized comparison key");
        mustThrow([&] { BatcherMerge b; b.init(1, 4, 4, 8, real, { 1, 1 }); }, "repeated output bit");
        mustThrow([&] { BatcherMerge b; b.init(1, 4, 4, 8, real, { 8 }); }, "out-of-range output bit");
        std::cout << "{\"type\":\"self_test\",\"test\":\"invalid_parameters_and_mock_rejection\",\"cases\":10,\"passed\":true}" << std::endl;
    }

    void batcherTest(bool project = false)
    {
        constexpr u64 batches = 3, half = 4, keyBits = 5, rowBits = 18;
        constexpr u64 rows = batches * half * 2;
        std::mt19937_64 rng(84);
        std::vector<u64> values(rows), originalKeys(rows);
        for (u64 i = 0; i < rows; ++i)
        {
            originalKeys[i] = rng() % 8;
            values[i] = (i << keyBits) | originalKeys[i];
        }
        for (u64 i = 0; i < rows; i += half)
            std::stable_sort(values.begin() + i, values.begin() + i + half,
                [](u64 a, u64 b) { return (a & 31) < (b & 31); });
        auto plain = encode(values, rowBits);
        PRNG prng0(oc::sysRandomSeed()), prng1(oc::sysRandomSeed());
        BinMatrix input[2] = { plain, plain }, output[2];
        prng0.get(input[0].data(), input[0].size()); input[0].trim();
        for (u64 i = 0; i < plain.size(); ++i) input[1](i) = plain(i) ^ input[0](i);
        auto sock = coproto::LocalAsyncSocket::makePair();
        CorGenerator cor[2]; BatcherMerge merge[2];
        cor[0].init(sock[0].fork(), prng0, 0, 2, 1 << 14, false);
        cor[1].init(sock[1].fork(), prng1, 1, 2, 1 << 14, false);
        // Deliberately out of order to exercise packed-output remapping.
        const std::vector<u64> outputBits = project ?
            std::vector<u64>{ 17, 5, 16, 6, 15, 7, 14, 8, 13, 9, 12, 10, 11 } : std::vector<u64>{};
        for (u64 party = 0; party < 2; ++party)
        { merge[party].init(batches, half, keyBits, rowBits, cor[party], outputBits); merge[party].preprocess(); }
        complete(cor[0].start(), cor[1].start(), merge[0].apply(input[0], output[0], sock[0]),
            merge[1].apply(input[1], output[1], sock[1]));
        complete(sock[0].flush(), sock[1].flush());
        for (u64 batch = 0; batch < batches; ++batch)
        {
            std::vector<u64> got;
            u64 previous = 0;
            for (u64 i = batch * 2 * half; i < (batch + 1) * 2 * half; ++i)
            {
                u64 value = 0;
                for (u64 byte = 0; byte < output[0].bytesPerEntry(); ++byte)
                    value |= u64(output[0](i, byte) ^ output[1](i, byte)) << (8 * byte);
                const auto identity = value >> keyBits;
                if (identity >= rows) throw std::runtime_error("Batcher merge payload outside domain");
                const auto key = project ? originalKeys[identity] : value & 31;
                if (project && ((output[0](i, 0) & 31) || (output[1](i, 0) & 31)))
                    throw std::runtime_error("Batcher omitted output bits are not public zero");
                if (i % (2 * half) && key < previous)
                    throw std::runtime_error("Batcher merge ordering failed");
                previous = key; got.push_back(value);
            }
            std::vector<u64> expected(values.begin() + batch * 2 * half, values.begin() + (batch + 1) * 2 * half);
            if (project) for (auto& value : expected) value &= ~u64(31);
            std::sort(got.begin(), got.end()); std::sort(expected.begin(), expected.end());
            if (got != expected) throw std::runtime_error("Batcher merge payload preservation failed");
        }
        std::cout << "{\"type\":\"self_test\",\"test\":\"batched_merge_real_crypto\",\"projected_output\":"
            << (project ? "true" : "false") << ",\"passed\":true}" << std::endl;
    }

    void wideKeyTest(u64 n = 17)
    {
        std::mt19937_64 rng(582074);
        for (u64 bits : { 127, 256 })
        {
            BinMatrix plaintext[2] = { BinMatrix(n, bits), BinMatrix(n, bits) };
            const auto bytes = plaintext[0].bytesPerEntry();
            auto less = [bytes](const u8* a, const u8* b)
            {
                for (u64 i = bytes; i-- > 0;)
                    if (a[i] != b[i]) return a[i] < b[i];
                return false;
            };
            for (auto& p : plaintext)
            {
                for (auto& byte : p) byte = static_cast<u8>(rng());
                // Test maximum values and carry significance above bit 64.
                std::fill(p[0].begin(), p[0].end(), 0xff);
                p.trim();
                std::vector<u64> order(n);
                std::iota(order.begin(), order.end(), 0);
                std::stable_sort(order.begin(), order.end(), [&](u64 a, u64 b) { return less(p.data(a), p.data(b)); });
                BinMatrix sorted(n, bits);
                for (u64 i = 0; i < n; ++i) std::copy(p[order[i]].begin(), p[order[i]].end(), sorted[i].begin());
                p = std::move(sorted);
            }
            Data data;
            u64 xPos = 0, yPos = 0;
            while (xPos < n || yPos < n)
                if (yPos == n || (xPos < n && !less(plaintext[1].data(yPos), plaintext[0].data(xPos))))
                    data.expected.push_back(static_cast<u32>(xPos++));
                else data.expected.push_back(static_cast<u32>(n + yPos++));
            PRNG prng0(oc::sysRandomSeed()), prng1(oc::sysRandomSeed());
            BinMatrix x[2] = { BinMatrix(n, bits), BinMatrix(n, bits) };
            BinMatrix y[2] = { BinMatrix(n, bits), BinMatrix(n, bits) };
            prng0.get(x[0].data(), x[0].size()); prng0.get(y[0].data(), y[0].size());
            x[0].trim(); y[0].trim();
            for (u64 i = 0; i < plaintext[0].size(); ++i)
            { x[1](i) = plaintext[0](i) ^ x[0](i); y[1](i) = plaintext[1](i) ^ y[0](i); }
            auto sock = coproto::LocalAsyncSocket::makePair();
            CorGenerator cor[2]; PiLogStar protocol[2]; AdditivePerm output[2];
            cor[0].init(sock[0].fork(), prng0, 0, 2, 1 << 18, false);
            cor[1].init(sock[1].fork(), prng1, 1, 2, 1 << 18, false);
            for (u64 role = 0; role < 2; ++role)
            { protocol[role].init(n, bits, cor[role]); protocol[role].preprocess(); }
            complete(cor[0].start(), cor[1].start(), protocol[0].prepare(sock[0], prng0), protocol[1].prepare(sock[1], prng1));
            complete(protocol[0].merge(x[0], y[0], output[0], sock[0], prng0), protocol[1].merge(x[1], y[1], output[1], sock[1], prng1));
            complete(sock[0].flush(), sock[1].flush());
            verify(output[0], output[1], data);
            std::cout << "{\"type\":\"self_test\",\"test\":\"wide_keys_real_crypto\",\"key_bits\":"
                << bits << ",\"n\":" << n << ",\"passed\":true}" << std::endl;
        }
    }

    void selfTest(Options o)
    {
        rejectionTest(); comparatorTest(); batcherTest(); batcherTest(true); wideKeyTest(); wideKeyTest(32);
        struct Test { u64 n, bits, base, block; const char* pattern; };
        const std::array<Test, 11> cases = {{
            { 1, 1, 8, 0, "equal" }, { 2, 64, 8, 0, "max" },
            { 7, 7, 8, 0, "duplicates" }, { 8, 32, 8, 0, "random" },
            { 17, 32, 8, 0, "equal" }, { 17, 1, 8, 0, "disjoint" },
            { 32, 64, 8, 0, "interleaved" }, { 65, 32, 8, 0, "random" },
            { 65, 64, 8, 0, "max" }, { 65, 9, 4, 16, "duplicates" },
            { 65, 32, 128, 0, "duplicates" }
        }};
        for (const auto& c : cases)
        {
            o.n = c.n; o.bits = c.bits; o.base = c.base; o.block = c.block; o.pattern = c.pattern;
            localRun(o, true);
        }
        u64 packedCases = 0;
        for (u64 block : { 2, 4, 8, 16 })
            for (const auto& pattern : patterns)
                for (u64 bits : { 1, 32, 64 })
                {
                    o.n = 64; o.bits = bits; o.base = block; o.block = block;
                    o.pattern = pattern; o.packed = true;
                    localRun(o, true); ++packedCases;
                }
        std::cout << "{\"type\":\"self_test\",\"test\":\"packed_logstar_random_shares\",\"cases\":"
            << packedCases << ",\"passed\":true}" << std::endl;
        std::cout << "{\"type\":\"self_test\",\"test\":\"pi_logstar_real_crypto\",\"cases\":"
            << cases.size() << ",\"passed\":true}" << std::endl;
    }

    void plan(const Options& o)
    {
        auto socks = coproto::LocalAsyncSocket::makePair();
        PRNG prng(oc::sysRandomSeed());
        CorGenerator cor;
        cor.init(socks[0].fork(), prng, 0, o.concurrency, o.batchSize, false);
        PiLogStar protocol;
        protocol.init(o.n, o.bits, cor, {o.base, o.block, o.packed});
        std::cout << "{\"type\":\"public_schedule\",\"n\":" << o.n << ",\"key_bits\":" << o.bits
            << ",\"base_case\":" << o.base << ",\"block_override\":" << o.block
            << ",\"padded_ands\":" << protocol.paddedAnds() << ",\"round_bound\":" << protocol.onlineRoundBound()
            << ",\"expanded_rows\":" << protocol.expandedSize() << ",\"path\":" << quoted(protocol.stages()[0].name) << "}" << std::endl;
    }
}

int main(int argc, char** argv)
{
    try
    {
        const auto o = parse(argc, argv);
        if (o.help)
        {
            std::cout << "Usage: logstar [--n 1024] [--bits 32] [--base 16] [--block 0]\n"
                "  [--batch-size 262144] [--concurrency 2] [--seed public-seed]\n"
                "  [--pattern random|equal|disjoint|interleaved|max|duplicates]\n"
                "  [--party 0|1 --address 127.0.0.1:12123] [--packed 0|1] [--plan] [--self-test]\n\n"
                "Default: both parties in one process using LocalAsyncSocket.\n"
                "Synthetic CLI keys support 1..64 bits; the library API supports 1..256 bits.\n"
                "TCP: run matching commands on two hosts; party 0 listens, party 1 connects.\n"
                "All benchmark data are public synthetic inputs. Crypto uses fresh private OS randomness.\n"
                "Verification deliberately reconstructs outputs after all timing/byte measurements.\n"
                "JSONL GMW round counts exclude permutation, bit-injection and offline rounds.\n"
                "Communication counts are coproto bytes, including protocol framing but excluding TCP/IP headers.\n";
            return 0;
        }
        if (o.plan) plan(o);
        else if (o.selfTest) selfTest(o);
        else if (o.party < 0) localRun(o);
        else networkRun(o);
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "logstar: " << error.what() << std::endl;
        return 1;
    }
}
