#include "StableSecretExtract.h"
#include "cryptoTools/Circuit/BetaLibrary.h"
#include "macoro/when_all.h"
#include <cstring>

namespace secJoin
{
    namespace
    {
        u8 getBit(const u8* p, u64 i) { return (p[i / 8] >> (i % 8)) & 1; }
        void putBit(u8* p, u64 i, u8 b) { p[i / 8] |= (b & 1) << (i % 8); }
        u64 word(const u8* p, u64 bytes) { u64 value = 0; std::memcpy(&value, p, bytes); return value; }
    }

    void StableSecretExtract::init(u64 rows_, u64 active_, u64 payloadBits_, CorGenerator& cor, bool suppliedRanks_)
    {
        if (initialized || !rows_ || rows_ >= (u64(1) << 32) || !active_ || active_ > rows_ ||
            active_ > (u64(1) << 31) || !payloadBits_ || payloadBits_ > 32 ||
            !cor.initialized() || cor.mGenState->mMock || cor.mGenState->mDebug)
            throw std::invalid_argument("StableSecretExtract: invalid initialization");
        rows = rows_; active = active_; payloadBits = payloadBits_; role = cor.partyIdx();
        suppliedRanks = suppliedRanks_;
        rankBits = 1;
        while ((u64(1) << rankBits) < active) ++rankBits;
        if (!suppliedRanks)
        {
            inject.init(rows, 1, cor);
            oc::BetaLibrary lib;
            auto circuit = *lib.uint_uint_add(rankBits, rankBits, rankBits, oc::BetaLibrary::Optimized::Depth);
            circuit.levelByAndDepth();
            ranks.init(rows, circuit, cor);
        }
        gen.init(role, rows, oc::divCeil(rankBits + payloadBits + 1, 8), cor);
        initialized = true;
    }

    void StableSecretExtract::preprocess()
    {
        if (!initialized || preprocessed) throw std::logic_error("StableSecretExtract: invalid preprocess state");
        if (!suppliedRanks) { inject.preprocess(); ranks.preprocess(); }
        gen.preprocess(); preprocessed = true;
    }

    macoro::task<> StableSecretExtract::prepare(coproto::Socket& sock, PRNG& prng)
    {
        if (!preprocessed || prepareStarted) throw std::logic_error("StableSecretExtract: invalid prepare state");
        prepareStarted = true;
        co_await gen.generate(sock, prng, rows, shuffle);
        prepared = true;
    }

    macoro::task<> StableSecretExtract::apply(const BinMatrix& flags, const oc::Matrix<u32>& payload,
        AdditivePerm& output, coproto::Socket& sock)
    {
        if (!prepared || used || suppliedRanks) throw std::logic_error("StableSecretExtract: single-use prepared instance required");
        if (flags.rows() != rows || flags.bitsPerEntry() != 1 || payload.rows() != rows || payload.cols() != 1)
            throw std::invalid_argument("StableSecretExtract: dimensions differ from public configuration");
        used = true;
        BinMatrix converted(rows, rankBits);
        {
            oc::Matrix<u32> arithmetic;
            // Use 32-bit OT messages; the prefix is reduced modulo 2^rankBits
            // when converted to binary. Even the final active rank fits.
            co_await inject.bitInjection(flags.mData, 32, arithmetic, sock);
            BinMatrix local(rows, rankBits);
            u32 sum = role ? 0 : u32(-1);
            for (u64 i = 0; i < rows; ++i)
            {
                sum += arithmetic(i, 0);
                for (u64 j = 0; j < rankBits; ++j) putBit(local.data(i), j, (sum >> j) & 1);
            }
            ranks.setInput(role, local); ranks.setZeroInput(role ^ 1);
            co_await ranks.run(sock); ranks.getOutput(0, converted); ranks.clear();
        }
        co_await finish(flags, converted, payload, output, sock);
    }

    macoro::task<> StableSecretExtract::applyRanked(const BinMatrix& flags, const BinMatrix& binaryRanks,
        const oc::Matrix<u32>& payload, AdditivePerm& output, coproto::Socket& sock)
    {
        if (!prepared || used || !suppliedRanks) throw std::logic_error("StableSecretExtract: invalid ranked invocation");
        if (flags.rows() != rows || flags.bitsPerEntry() != 1 || payload.rows() != rows || payload.cols() != 1 ||
            binaryRanks.rows() != rows || binaryRanks.bitsPerEntry() != rankBits)
            throw std::invalid_argument("StableSecretExtract: ranked dimensions differ from configuration");
        used = true;
        co_await finish(flags, binaryRanks, payload, output, sock);
    }

    macoro::task<> StableSecretExtract::finish(const BinMatrix& flags, const BinMatrix& binaryRanks,
        const oc::Matrix<u32>& payload, AdditivePerm& output, coproto::Socket& sock)
    {
        BinMatrix records(rows, rankBits + payloadBits + 1), shuffled(rows, rankBits + payloadBits + 1);
        for (u64 i = 0; i < rows; ++i)
        {
            const auto rank = word(binaryRanks.data(i), binaryRanks.bytesPerEntry()) & ((u64(1) << rankBits) - 1);
            const auto value = u64(payload(i, 0)) & ((u64(1) << payloadBits) - 1);
            const u64 packed = (flags(i, 0) & 1) | (rank << 1) | (value << (1 + rankBits));
            std::memcpy(records.data(i), &packed, records.bytesPerEntry());
        }
        co_await shuffle.apply<u8>(PermOp::Regular, records.mData, shuffled.mData, sock);
        records = {};
        // The composed shuffle includes one fresh private uniform permutation
        // from each party. Its output positions cannot be traced by either party.
        oc::BitVector mine(rows), peer(rows);
        for (u64 i = 0; i < rows; ++i) mine[i] = getBit(shuffled.data(i), 0);
        auto opened = co_await macoro::when_all_ready(sock.send(coproto::copy(mine)), sock.recv(peer));
        std::get<0>(opened).result(); std::get<1>(opened).result();
        mine ^= peer;
        std::vector<u32> positions, myRanks, peerRanks;
        positions.reserve(active); myRanks.reserve(active);
        for (u64 i = 0; i < rows; ++i)
            if (mine[i])
            {
                positions.push_back(static_cast<u32>(i));
                const u32 rank = (word(shuffled.data(i), shuffled.bytesPerEntry()) >> 1) & ((u64(1) << rankBits) - 1);
                myRanks.push_back(rank);
            }
        if (positions.size() != active) throw std::runtime_error("StableSecretExtract: wrong public active count");
        peerRanks.resize(active);
        auto openedRanks = co_await macoro::when_all_ready(sock.send(coproto::copy(myRanks)), sock.recv(peerRanks));
        std::get<0>(openedRanks).result(); std::get<1>(openedRanks).result();
        output.mShare.resize(active);
        oc::BitVector seen(active);
        for (u64 i = 0; i < active; ++i)
        {
            const auto rank = myRanks[i] ^ peerRanks[i];
            if (rank >= active || seen[rank]) throw std::runtime_error("StableSecretExtract: invalid shuffled rank");
            seen[rank] = 1;
            const u32 value = (word(shuffled.data(positions[i]), shuffled.bytesPerEntry()) >> (1 + rankBits))
                & ((u64(1) << payloadBits) - 1);
            output.mShare[rank] = value;
        }
    }
}
