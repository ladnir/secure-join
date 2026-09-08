#include "PackedLogStar.h"
#include <chrono>
#include <cstring>

namespace secJoin
{
    namespace
    {
        u64 lg(u64 x) { u64 r = 0; while (x >>= 1) ++r; return r; }
        u8 get(const u8* p, u64 i) { return (p[i / 8] >> (i % 8)) & 1; }
        void set(u8* p, u64 i, u8 b) { p[i / 8] |= (b & 1) << (i % 8); }
        void copy(u8* d, u64 off, const u8* s, u64 start, u64 count)
        {
            // Copy partial bytes without reading beyond either record. Most
            // keys are 32/64 bits; this avoids a separate loop body per bit.
            while (count)
            {
                const auto take = std::min<u64>(8 - off % 8, count);
                u16 value = s[start / 8] >> (start % 8);
                if (start % 8 + take > 8) value |= u16(s[start / 8 + 1]) << (8 - start % 8);
                d[off / 8] |= (value & ((1u << take) - 1)) << (off % 8);
                off += take; start += take; count -= take;
            }
        }
        u32 integer(const u8* p, u64 off, u64 count)
        {
            u64 v = 0;
            std::memcpy(&v, p + off / 8, oc::divCeil(off % 8 + count, 8));
            return static_cast<u32>((v >> (off % 8)) & ((u64(1) << count) - 1));
        }
        u64 rounds(const Gmw& g)
        { u64 r = 0; for (auto c : g.mCir.mLevelAndCounts) r += c != 0; return r; }
        u64 ands(const Gmw& g) { return g.mCir.mNonlinearGateCount * oc::roundUpTo(g.mN, 128); }

        BetaCircuit interval(u64 width)
        {
            BetaCircuit c;
            BetaBundle b(width), s(width), hi(width), valid(1), out(2);
            c.addInputBundle(b); c.addInputBundle(s);
            c.addInputBundle(hi); c.addInputBundle(valid); c.addOutputBundle(out);
            // Compare (key, original source). Equal same-source upper bounds
            // are safe: those rows belong to an earlier block of that source.
            auto hiB = logstarLessThan(c, hi, b);
            auto hiS = logstarLessThan(c, hi, s);
            // Export hiB and invert its sharing locally. GMW output views do
            // not interpret BetaCircuit's lazy InvWire output annotation.
            c.addCopy(hiB, out[0]);
            c.addGate(hiS, valid[0], oc::GateType::na_And, out[1]);
            c.levelByAndDepth();
            return c;
        }

        BetaCircuit allPairs(u64 m, u64 width)
        {
            // Sorted runs make every row/column of cross comparisons monotone.
            // Adjacent XORs therefore give one-hot insertion positions, without
            // a popcount, rank adder, or key/payload swap network.
            const auto offsets = lg(m), payloadWidth = offsets + 2;
            BetaCircuit c;
            BetaBundle input(2 * m * (width + 1) + 2), output(2 * m * payloadWidth);
            c.addInputBundle(input); c.addOutputBundle(output);
            const auto valid = input[input.size() - 2];
            const auto zero = input[input.size() - 1]; // Public zero supplied by both parties.
            auto temp = [&]() { BetaBundle b(1); c.addTempWireBundle(b); return b[0]; };
            std::vector<BetaBundle> key(2 * m);
            for (u64 i = 0; i < 2 * m; ++i)
                key[i].mWires.assign(input.mWires.begin() + i * (width + 1),
                    input.mWires.begin() + i * (width + 1) + width);
            std::vector<std::vector<u32>> less(m, std::vector<u32>(m));
            for (u64 j = 0; j < m; ++j)
                for (u64 k = 0; k < m; ++k)
                {
                    auto comparison = logstarLessThan(c, key[m + k], key[j]);
                    less[j][k] = temp();
                    // An absent opposite predecessor contributes zero keys;
                    // this also makes the direct global-rank formula valid.
                    c.addGate(comparison, valid, oc::GateType::And, less[j][k]);
                }
            std::vector<std::vector<u32>> terms(output.size());
            for (u64 side = 0; side < 2; ++side)
                for (u64 j = 0; j < m; ++j)
                    for (u64 k = 0; k <= m; ++k)
                    {
                        u32 position;
                        if (!k || k == m)
                        {
                            position = side ? less[k ? m - 1 : 0][j] : less[j][k ? m - 1 : 0];
                            if ((!side && !k) || (side && k == m))
                            { auto inverted = temp(); c.addInvert(position, inverted); position = inverted; }
                        }
                        else
                        {
                            position = temp();
                            c.addGate(side ? less[k - 1][j] : less[j][k - 1],
                                side ? less[k][j] : less[j][k], oc::GateType::Xor, position);
                        }
                        const auto r = (j + k) * payloadWidth;
                        for (u64 bit = 0; bit < offsets; ++bit)
                            if ((j >> bit) & 1) terms[r + bit].push_back(position);
                        if (side) terms[r + offsets].push_back(position);
                        // A copied row with no B predecessor lies below B's
                        // minimum. Excluding k=0 implements the lower mask for free.
                        if (!side || k)
                        {
                            auto real = temp();
                            c.addGate(position, input[(side * m + j) * (width + 1) + width],
                                oc::GateType::And, real);
                            terms[r + offsets + 1].push_back(real);
                        }
                    }
            for (u64 bit = 0; bit < output.size(); ++bit)
            {
                auto sum = zero;
                for (auto term : terms[bit])
                { auto next = temp(); c.addGate(sum, term, oc::GateType::Xor, next); sum = next; }
                // Materialize any inversion rather than exporting InvWire.
                c.addCopy(sum, output[bit]);
            }
            c.levelByAndDepth();
            return c;
        }

        BetaCircuit sumBlockIds(u64 width)
        {
            BetaCircuit c;
            BetaBundle a(width), b(width), invertedA(width), invertedB(width), sum(width), complementedSumPlusOne(width);
            c.addInputBundle(a); c.addInputBundle(b);
            c.addOutputBundle(sum); c.addOutputBundle(complementedSumPlusOne);
            c.addTempWireBundle(invertedA); c.addTempWireBundle(invertedB);
            for (u64 i = 0; i < width; ++i)
            { c.addInvert(a[i], invertedA[i]); c.addInvert(b[i], invertedB[i]); }
            using L = oc::BetaLibrary;
            L::parallelPrefix_build(c, a, b, sum, L::IntType::Unsigned, L::AdderType::Addition);
            // ~(~a + ~b) = a + b + 1 modulo 2^width. Two additions execute
            // in parallel; the final complement is applied locally.
            L::parallelPrefix_build(c, invertedA, invertedB, complementedSumPlusOne,
                L::IntType::Unsigned, L::AdderType::Addition);
            c.levelByAndDepth();
            return c;
        }

        BetaCircuit selectId(u64 width)
        {
            BetaCircuit c;
            BetaBundle a(width), b(width), choice(1), out(width), diff(width), prod(width);
            c.addInputBundle(a); c.addInputBundle(b); c.addInputBundle(choice);
            c.addOutputBundle(out); c.addTempWireBundle(diff); c.addTempWireBundle(prod);
            for (u64 i = 0; i < width; ++i)
            {
                c.addGate(a[i], b[i], oc::GateType::Xor, diff[i]);
                c.addGate(diff[i], choice[0], oc::GateType::And, prod[i]);
                c.addGate(a[i], prod[i], oc::GateType::Xor, out[i]);
            }
            c.levelByAndDepth();
            return c;
        }
    }

    void PackedLogStar::init(u64 n_, u64 keyBits, u64 block_, CorGenerator& cor)
    {
        n = n_; bits = keyBits; block = block_; blocks = 2 * n / block;
        idBits = lg(blocks); offsetBits = lg(block); role = cor.partyIdx();
        blockBits = idBits + 1 + block * bits;
        orderBits = bits + 1 + offsetBits;
        std::vector<u64> ids;
        for (u64 i = 0; i < idBits; ++i) ids.push_back(i);
        medians.init(1, n / block, bits + idBits, bits + idBits, cor, ids);
        blockGen.init(role, blocks, oc::divCeil(blockBits, 8) + 4, cor);
        prefix.init(1, blocks, blockBits, cor, 8);
        mask.init(2 * n, interval(bits + 1), cor);
        tinyMerge.init(blocks, allPairs(block, bits + 1), cor);
        recover.init(4 * n, selectId(idBits), cor);
        blockRanks.init(blocks, sumBlockIds(idBits), cor);
        compact.init(4 * n, 2 * n, lg(2 * n), cor, true);
        stats = {
            {"packed_partition", 1, n, block, medians.numRounds() + prefix.numRounds() + rounds(mask),
                medians.numAnds() + prefix.numAnds() + ands(mask)},
            {"all_pairs_base_merge", blocks, block, 0, rounds(tinyMerge) + rounds(recover),
                ands(tinyMerge) + ands(recover)},
            {"shuffle_extraction", 1, 4 * n, 0, rounds(blockRanks), ands(blockRanks)}
        };
    }

    void PackedLogStar::preprocess()
    {
        medians.preprocess(); blockGen.preprocess(); prefix.preprocess(); mask.preprocess();
        tinyMerge.preprocess(); recover.preprocess(); blockRanks.preprocess(); compact.preprocess();
    }

    macoro::task<> PackedLogStar::prepare(coproto::Socket& sock, PRNG& prng)
    {
        co_await blockGen.generate(sock, prng, blocks, blockPerm);
        co_await compact.prepare(sock, prng);
    }

    macoro::task<> PackedLogStar::merge(const BinMatrix& x, const BinMatrix& y,
        AdditivePerm& output, coproto::Socket& sock, PRNG& prng)
    {
        auto start = std::chrono::steady_clock::now();
        u64 sent = sock.bytesSent(), received = sock.bytesReceived();
        auto record = [&](u64 stage)
        {
            auto now = std::chrono::steady_clock::now();
            stats[stage].milliseconds = std::chrono::duration<double, std::milli>(now - start).count();
            stats[stage].sentBytes = sock.bytesSent() - sent;
            stats[stage].receivedBytes = sock.bytesReceived() - received;
            start = now; sent = sock.bytesSent(); received = sock.bytesReceived();
        };
        BinMatrix ordered, strays, active(2 * n, 2);
        {
            BinMatrix med(blocks, idBits + bits), sortedMed, input(blocks, blockBits);
            for (u64 i = 0; i < blocks; ++i)
            {
                if (!role)
                {
                    for (u64 k = 0; k < idBits; ++k)
                    { set(med.data(i), k, (i >> k) & 1); set(input.data(i), k, (i >> k) & 1); }
                    set(input.data(i), idBits, 1);
                }
                for (u64 j = 0; j < block; ++j)
                {
                    const auto row = (i * block + j) % n;
                    const auto key = (i < blocks / 2 ? x : y).data(row);
                    copy(input.data(i), idBits + 1 + j * bits, key, 0, bits);
                    if (!j) copy(med.data(i), idBits, key, 0, bits);
                }
            }
            co_await medians.apply(med, sortedMed, sock);
            AdditivePerm gather;
            gather.mShare.resize(blocks);
            for (u64 i = 0; i < blocks; ++i) gather.mShare[i] = integer(sortedMed.data(i), 0, idBits);
            co_await blockPerm.derandomize(gather, sock);
            ordered.resize(blocks, blockBits);
            co_await blockPerm.apply<u8>(PermOp::Regular, input.mData, ordered.mData, sock);
        }
        {
            BinMatrix shifted(blocks, blockBits), controls(blocks, 1);
            for (u64 i = 0; i < blocks; ++i)
            {
                copy(shifted.data(i), 0, ordered.data(i ? i - 1 : 0), 0, blockBits);
                if (!i) shifted(i, idBits / 8) &= ~(u8(1) << (idBits % 8));
                else controls(i, 0) = get(ordered.data(i), idBits - 1)
                    ^ get(ordered.data(i - 1), idBits - 1) ^ (role == 0);
            }
            co_await prefix.apply(shifted, controls, strays, sock);
        }
        {
            BinMatrix b(2 * n, bits + 1), s(2 * n, bits + 1);
            BinMatrix hi(2 * n, bits + 1), valid(2 * n, 1);
            auto keyTag = [&](u8* dest, const u8* src, u64 j)
            { set(dest, 0, get(src, idBits - 1)); copy(dest, 1, src, idBits + 1 + j * bits, bits); };
            for (u64 i = 0; i < blocks; ++i)
                for (u64 j = 0; j < block; ++j)
                {
                    const auto r = i * block + j;
                    keyTag(b.data(r), ordered.data(i), j); keyTag(s.data(r), strays.data(i), j);
                    if (i + 1 < blocks) keyTag(hi.data(r), ordered.data(i + 1), 0);
                    else if (!role) for (u64 k = 0; k < bits + 1; ++k) set(hi.data(r), k, 1);
                    valid(r, 0) = get(strays.data(i), idBits);
                }
            mask.setInput(0, b); mask.setInput(1, s); mask.setInput(2, hi); mask.setInput(3, valid);
            co_await mask.run(sock); mask.getOutput(0, active);
            if (!role) for (u64 i = 0; i < 2 * n; ++i) active(i, 0) ^= 1;
            mask.clear(); // No persistent wire storage after the stage.
        }
        record(0);
        BinMatrix real(4 * n, 1);
        oc::Matrix<u32> indices(4 * n, 1);
        {
            const auto payloadWidth = offsetBits + 2;
            BinMatrix input(blocks, 2 * block * (bits + 2) + 2), merged(blocks, 2 * block * payloadWidth);
            for (u64 i = 0; i < blocks; ++i)
                set(input.data(i), 2 * block * (bits + 2), get(strays.data(i), idBits));
            for (u64 i = 0; i < blocks; ++i)
                for (u64 side = 0; side < 2; ++side)
                    for (u64 j = 0; j < block; ++j)
                    {
                        const auto off = (side * block + j) * (bits + 2);
                        auto d = input.data(i);
                        const auto src = (side ? strays : ordered).data(i);
                        set(d, off, get(src, idBits - 1));
                        copy(d, off + 1, src, idBits + 1 + j * bits, bits);
                        set(d, off + bits + 1, (active(i * block + j, 0) >> side) & 1);
                    }
            tinyMerge.setInput(0, input);
            co_await tinyMerge.run(sock); tinyMerge.getOutput(0, merged); tinyMerge.clear();
            input = {}; active = {};
            BinMatrix a(4 * n, idBits), b(4 * n, idBits), choose(4 * n, 1), recovered(4 * n, idBits);
            for (u64 i = 0; i < 4 * n; ++i)
            {
                copy(a.data(i), 0, ordered.data(i / (2 * block)), 0, idBits);
                copy(b.data(i), 0, strays.data(i / (2 * block)), 0, idBits);
                const auto off = (i % (2 * block)) * payloadWidth;
                choose(i, 0) = get(merged.data(i / (2 * block)), off + offsetBits);
                real(i, 0) = get(merged.data(i / (2 * block)), off + offsetBits + 1);
            }
            recover.setInput(0, a); recover.setInput(1, b); recover.setInput(2, choose);
            co_await recover.run(sock); recover.getOutput(0, recovered); recover.clear();
            for (u64 i = 0; i < 4 * n; ++i)
                indices(i, 0) = (integer(recovered.data(i), 0, idBits) << offsetBits)
                    | integer(merged.data(i / (2 * block)), (i % (2 * block)) * payloadWidth, offsetBits);
        }
        record(1);
        BinMatrix a(blocks, idBits), b(blocks, idBits), sum(blocks, idBits), sumPlusOne(blocks, idBits);
        for (u64 i = 0; i < blocks; ++i)
        {
            copy(a.data(i), 0, ordered.data(i), 0, idBits - 1);
            copy(b.data(i), 0, strays.data(i), 0, idBits - 1);
        }
        ordered = {}; strays = {};
        blockRanks.setInput(0, a); blockRanks.setInput(1, b);
        co_await blockRanks.run(sock); blockRanks.getOutput(0, sum); blockRanks.getOutput(1, sumPlusOne);
        blockRanks.clear();
        BinMatrix ranks(4 * n, idBits + offsetBits);
        for (u64 i = 0; i < 4 * n; ++i)
        {
            const auto j = i % (2 * block), row = i / (2 * block);
            u32 high = integer((j < block ? sum : sumPlusOne).data(row), 0, idBits);
            if (j >= block && !role) high ^= (u32(1) << idBits) - 1;
            const u32 rank = (high << offsetBits) | (role ? 0 : j % block);
            std::memcpy(ranks.data(i), &rank, ranks.bytesPerEntry());
        }
        co_await compact.applyRanked(real, ranks, indices, output, sock);
        record(2);
    }
}
