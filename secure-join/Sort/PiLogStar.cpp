#include "PiLogStar.h"
#include "PackedLogStar.h"
#include "BatcherMerge.h"
#include "RadixSort.h"
#include "secure-join/AggTree/BatchPrefix.h"
#include "secure-join/Perm/AltModComposedPerm.h"
#include <chrono>
#include <cstring>
#include <limits>
#include <stdexcept>

namespace secJoin
{
    namespace
    {
        u64 ceilLog(u64 n) { u64 r = 0; for (u64 v = n - 1; v; v >>= 1) ++r; return r; }
        u64 pow2(u64 n) { return u64(1) << ceilLog(n); }
        bool isPow2(u64 n) { return n && !(n & (n - 1)); }
        u8 bit(const u8* p, u64 i) { return (p[i / 8] >> (i % 8)) & 1; }
        void put(u8* p, u64 i, u8 b) { p[i / 8] |= (b & 1) << (i % 8); }
        void copyBits(u8* dst, u64 offset, const u8* src, u64 count)
        { for (u64 i = 0; i < count; ++i) put(dst, offset + i, bit(src, i)); }
        u32 readIndex(const u8* p, u64 bits)
        { u32 r = 0; for (u64 i = 0; i < bits; ++i) r |= u32(bit(p, i)) << i; return r; }
        u64 rounds(const Gmw& g)
        { u64 n = 0; for (auto c : g.mCir.mLevelAndCounts) n += c != 0; return n; }
        u64 ands(const Gmw& g) { return g.mCir.mNonlinearGateCount * oc::roundUpTo(g.mN, 128); }

        // All three interval comparisons execute in parallel. Keys are never changed.
        BetaCircuit maskCircuit(u64 orderBits)
        {
            BetaCircuit c;
            BetaBundle b(orderBits), s(orderBits), lo(orderBits), hi(orderBits);
            BetaBundle flags(2), out(2); // b.real, s.real
            c.addInputBundle(b); c.addInputBundle(s); c.addInputBundle(lo);
            c.addInputBundle(hi); c.addInputBundle(flags); c.addOutputBundle(out);
            auto bHi = logstarLessThan(c, b, hi);
            auto sLo = logstarLessThan(c, s, lo);
            auto sHi = logstarLessThan(c, s, hi);
            BetaBundle t(1); c.addTempWireBundle(t);
            c.addGate(sLo, sHi, oc::GateType::na_And, t[0]);
            c.addGate(flags[0], bHi, oc::GateType::And, out[0]);
            c.addGate(flags[1], t[0], oc::GateType::And, out[1]);
            return c;
        }
    }

    struct PiLogStar::Impl
    {
        struct Level
        {
            u64 batches = 0, n = 0, block = 0, blocksPerBatch = 0, blocks = 0;
            BatcherMerge medians;
            AltModComposedPerm permGen;
            ComposedPerm perm;
            BatchPrefix prefix;
            Gmw mask;
        };
        u64 n = 0, padded = 0, keyBits = 0, indexBits = 0, orderBits = 0;
        u64 orderBytes = 0, rowBytes = 0, role = 0, expanded = 0;
        bool requested = false, preprocessed = false, prepareStarted = false, prepared = false, used = false;
        std::vector<std::unique_ptr<Level>> levels;
        std::unique_ptr<PackedLogStar> packed;
        BatcherMerge base;
        RadixSort compact;
        AltModComposedPerm compactGen;
        ComposedPerm compactPerm;
        std::vector<PiLogStarStage> stats;
        u64 totalRounds = 0, totalAnds = 0;

        void record(std::size_t i, coproto::Socket& sock, u64 sent, u64 received,
                    std::chrono::steady_clock::time_point start)
        {
            stats[i].sentBytes = sock.bytesSent() - sent;
            stats[i].receivedBytes = sock.bytesReceived() - received;
            stats[i].milliseconds = std::chrono::duration<double, std::milli>(
                std::chrono::steady_clock::now() - start).count();
        }
    };

    PiLogStar::PiLogStar() : m(std::make_unique<Impl>()) {}
    PiLogStar::~PiLogStar() = default;
    PiLogStar::PiLogStar(PiLogStar&&) noexcept = default;
    PiLogStar& PiLogStar::operator=(PiLogStar&&) noexcept = default;

    void PiLogStar::init(u64 n, u64 keyBits, CorGenerator& cor, const PiLogStarOptions& opts)
    {
        if (m->requested) throw std::invalid_argument("PiLogStar: instance already initialized");
        if (!n || n > (u64(1) << 28) || !keyBits || keyBits > 256)
            throw std::invalid_argument("PiLogStar: require 1 <= n <= 2^28 and 1 <= keyBits <= 256");
        if (!isPow2(opts.baseCase) || opts.baseCase < 2)
            throw std::invalid_argument("PiLogStar: baseCase must be a power of two >= 2");
        if (opts.blockSize && (!isPow2(opts.blockSize) || opts.blockSize < 2))
            throw std::invalid_argument("PiLogStar: blockSize must be zero or a power of two >= 2");
        if (!cor.initialized() || cor.mGenState->mMock || cor.mGenState->mDebug)
            throw std::invalid_argument("PiLogStar requires an initialized, real, non-debug correlation generator");
        m->n = n; m->padded = pow2(n); m->keyBits = keyBits; m->role = cor.partyIdx();
        m->indexBits = ceilLog(2 * m->padded);
        m->orderBits = m->indexBits + keyBits + 1; // explicit +infinity bit
        m->orderBytes = oc::divCeil(m->orderBits, 8);
        m->rowBytes = m->orderBytes + 1; // separately byte-addressable real flag
        const auto outerBlock = opts.blockSize ? opts.blockSize : pow2(std::max<u64>(1, ceilLog(m->padded)));
        if (opts.packed && n == m->padded && n > opts.baseCase &&
            outerBlock < n && outerBlock <= opts.baseCase && outerBlock <= 16)
        {
            m->packed = std::make_unique<PackedLogStar>();
            m->packed->init(n, keyBits, outerBlock, cor);
            m->expanded = 4 * n;
            m->stats = m->packed->stats;
            for (auto& s : m->stats) { m->totalRounds += s.gmwRounds; m->totalAnds += s.paddedAnds; }
            m->requested = true;
            return;
        }
        u64 count = 1, size = m->padded;
        while (size > opts.baseCase)
        {
            auto level = std::make_unique<Impl::Level>();
            auto& l = *level;
            l.batches = count; l.n = size;
            l.block = m->levels.empty() && opts.blockSize ? opts.blockSize : pow2(ceilLog(size));
            if (l.block >= size || size % l.block)
                throw std::invalid_argument("PiLogStar: blockSize must divide and be smaller than padded n");
            l.blocksPerBatch = 2 * size / l.block;
            l.blocks = count * l.blocksPerBatch;
            if (l.blocks >= (u64(1) << 32) || 2 * l.blocks * l.block >= (u64(1) << 32))
                throw std::invalid_argument("PiLogStar: expanded schedule exceeds 32-bit permutation capacity");
            std::vector<u64> blockIndexBits(32);
            for (u64 i = 0; i < 32; ++i) blockIndexBits[i] = m->orderBytes * 8 + i;
            l.medians.init(count, size / l.block, m->orderBits, (m->orderBytes + 4) * 8, cor, blockIndexBits);
            l.permGen.init(m->role, l.blocks, l.block * m->rowBytes + 1 + 4, cor);
            l.prefix.init(count, l.blocksPerBatch, l.block * m->rowBytes * 8, cor);
            l.mask.init(l.blocks * l.block, maskCircuit(m->orderBits), cor);
            m->stats.push_back({"partition", count, size, l.block,
                l.medians.numRounds() + l.prefix.numRounds() + rounds(l.mask),
                l.medians.numAnds() + l.prefix.numAnds() + ands(l.mask)});
            count = l.blocks; size = l.block;
            m->levels.push_back(std::move(level));
        }
        m->expanded = 2 * count * size;
        std::vector<u64> finalPayloadBits;
        for (u64 i = 0; i < m->indexBits; ++i) finalPayloadBits.push_back(i);
        finalPayloadBits.push_back(m->orderBytes * 8); // isReal
        m->base.init(count, size, m->orderBits, m->rowBytes * 8, cor, finalPayloadBits);
        m->stats.push_back({"base_merge", count, size, 0, m->base.numRounds(), m->base.numAnds()});
        if (!m->levels.empty())
        {
            // Stable one-bit radix partition. It returns scatter ranks, so apply Inverse.
            m->compact.mL = 1;
            m->compact.init(m->role, m->expanded, 1, cor);
            m->compactGen.init(m->role, m->expanded, 8, cor); // u32 data + u32 derandomization
            const auto& r = m->compact.mRounds[0];
            m->stats.push_back({"stable_compaction", 1, m->expanded, 0,
                rounds(r.mIndexToOneHotGmw) + rounds(r.mArithToBinGmw),
                ands(r.mIndexToOneHotGmw) + ands(r.mArithToBinGmw)});
        }
        for (auto& s : m->stats) { m->totalRounds += s.gmwRounds; m->totalAnds += s.paddedAnds; }
        m->requested = true;
    }

    void PiLogStar::preprocess()
    {
        if (!m->requested || m->preprocessed) throw std::logic_error("PiLogStar: invalid preprocess state");
        if (m->packed) { m->packed->preprocess(); m->preprocessed = true; return; }
        for (auto& l : m->levels)
        { l->medians.preprocess(); l->permGen.preprocess(); l->prefix.preprocess(); l->mask.preprocess(); }
        m->base.preprocess();
        if (!m->levels.empty()) { m->compact.preprocess(); m->compactGen.preprocess(); }
        m->preprocessed = true;
    }

    macoro::task<> PiLogStar::prepare(coproto::Socket& sock, PRNG& prng)
    {
        if (!m->preprocessed || m->prepareStarted) throw std::logic_error("PiLogStar: invalid prepare state");
        m->prepareStarted = true; // Burn the invocation even if transport/preparation fails.
        if (m->packed) { co_await m->packed->prepare(sock, prng); m->prepared = true; co_return; }
        for (auto& l : m->levels)
            co_await l->permGen.generate(sock, prng, l->blocks, l->perm);
        if (!m->levels.empty())
        {
            co_await m->compactGen.generate(sock, prng, m->expanded, m->compactPerm);
            co_await m->compact.genPrePerm(sock, prng);
        }
        m->prepared = true;
    }

    macoro::task<> PiLogStar::merge(const BinMatrix& x, const BinMatrix& y,
        AdditivePerm& output, coproto::Socket& sock, PRNG& prng)
    {
        if (!m->prepared || m->used) throw std::logic_error("PiLogStar: prepare once before single-use merge");
        if (x.rows() != m->n || y.rows() != m->n || x.bitsPerEntry() != m->keyBits || y.bitsPerEntry() != m->keyBits)
            throw std::invalid_argument("PiLogStar: input dimensions differ from public configuration");
        m->used = true;
        if (m->packed)
        {
            co_await m->packed->merge(x, y, output, sock, prng);
            m->stats = m->packed->stats;
            co_return;
        }
        BinMatrix rows(2 * m->padded, m->rowBytes * 8);
        for (u64 side = 0; side < 2; ++side)
            for (u64 j = 0; j < m->padded; ++j)
            {
                auto dst = rows.data(side * m->padded + j);
                if (j < m->n)
                {
                    copyBits(dst, m->indexBits, (side ? y : x).data(j), m->keyBits);
                    if (!m->role)
                    {
                        auto idx = side * m->n + j;
                        for (u64 k = 0; k < m->indexBits; ++k) put(dst, k, (idx >> k) & 1);
                        dst[m->orderBytes] = 1;
                    }
                }
                else if (!m->role)
                {
                    put(dst, m->orderBits - 1, 1);
                    auto idx = side * m->padded + j;
                    for (u64 k = 0; k < m->indexBits; ++k) put(dst, k, (idx >> k) & 1);
                }
            }

        for (std::size_t depth = 0; depth < m->levels.size(); ++depth)
        {
            auto& l = *m->levels[depth];
            auto sent = sock.bytesSent(), received = sock.bytesReceived();
            auto start = std::chrono::steady_clock::now();
            auto blockBytes = l.block * m->rowBytes;
            BinMatrix med(l.blocks, (m->orderBytes + 4) * 8), sortedMed;
            BinMatrix blocks(l.blocks, (blockBytes + 1) * 8), ordered(l.blocks, (blockBytes + 1) * 8);
            for (u64 i = 0; i < l.blocks; ++i)
            {
                std::memcpy(med.data(i), rows.data(i * l.block), m->orderBytes);
                std::memcpy(blocks.data(i), rows.data(i * l.block), blockBytes);
                if (!m->role)
                {
                    u32 idx = static_cast<u32>(i);
                    std::memcpy(med.data(i) + m->orderBytes, &idx, 4);
                    blocks(i, blockBytes) = (i % l.blocksPerBatch) >= l.blocksPerBatch / 2;
                }
            }
            co_await l.medians.apply(med, sortedMed, sock);
            AdditivePerm gather;
            gather.mShare.resize(l.blocks);
            for (u64 i = 0; i < l.blocks; ++i)
                std::memcpy(&gather.mShare[i], sortedMed.data(i) + m->orderBytes, 4);
            co_await l.perm.derandomize(gather, sock);
            co_await l.perm.apply<u8>(PermOp::Regular, blocks.mData, ordered.mData, sock);

            BinMatrix shifted(l.blocks, blockBytes * 8), controls(l.blocks, 1), strays;
            for (u64 i = 0; i < l.blocks; ++i)
            {
                const bool first = i % l.blocksPerBatch == 0;
                // S_0 is a dummy copy of B_0: sorted keys without a -infinity sentinel.
                std::memcpy(shifted.data(i), ordered.data(first ? i : i - 1), blockBytes);
                if (first)
                {
                    for (u64 j = 0; j < l.block; ++j) shifted(i, j * m->rowBytes + m->orderBytes) = 0;
                }
                else
                    controls(i, 0) = (ordered(i, blockBytes) ^ ordered(i - 1, blockBytes) ^ (m->role == 0)) & 1;
            }
            co_await l.prefix.apply(shifted, controls, strays, sock);

            const auto total = l.blocks * l.block;
            BinMatrix bk(total, m->orderBits), sk(total, m->orderBits), low(total, m->orderBits), high(total, m->orderBits);
            BinMatrix flags(total, 2), active(total, 2);
            for (u64 i = 0; i < l.blocks; ++i)
                for (u64 j = 0; j < l.block; ++j)
                {
                    const auto r = i * l.block + j;
                    const auto b = ordered.data(i) + j * m->rowBytes;
                    const auto s = strays.data(i) + j * m->rowBytes;
                    std::memcpy(bk.data(r), b, m->orderBytes);
                    std::memcpy(sk.data(r), s, m->orderBytes);
                    std::memcpy(low.data(r), ordered.data(i), m->orderBytes);
                    bool last = (i + 1) % l.blocksPerBatch == 0;
                    if (!last) std::memcpy(high.data(r), ordered.data(i + 1), m->orderBytes);
                    // Every real record has infinity=0. This public maximal
                    // key is above every real row, even a maximal user key.
                    // Equal sentinel dummies may be dropped; they are inactive.
                    else if (!m->role) std::memset(high.data(r), 0xff, m->orderBytes);
                    flags(r, 0) = (b[m->orderBytes] & 1) | ((s[m->orderBytes] & 1) << 1);
                }
            l.mask.setInput(0, bk); l.mask.setInput(1, sk); l.mask.setInput(2, low);
            l.mask.setInput(3, high); l.mask.setInput(4, flags);
            co_await l.mask.run(sock);
            l.mask.getOutput(0, active);
            l.mask.clear();
            BinMatrix next(2 * total, m->rowBytes * 8);
            for (u64 i = 0; i < l.blocks; ++i)
                for (u64 j = 0; j < l.block; ++j)
                {
                    auto b = next.data(2 * i * l.block + j);
                    auto s = next.data((2 * i + 1) * l.block + j);
                    std::memcpy(b, ordered.data(i) + j * m->rowBytes, m->orderBytes);
                    std::memcpy(s, strays.data(i) + j * m->rowBytes, m->orderBytes);
                    auto f = active(i * l.block + j, 0);
                    b[m->orderBytes] = f & 1; s[m->orderBytes] = (f >> 1) & 1;
                }
            rows = std::move(next);
            m->record(depth, sock, sent, received, start);
        }
        {
            auto sent = sock.bytesSent(), received = sock.bytesReceived();
            auto start = std::chrono::steady_clock::now();
            BinMatrix merged;
            co_await m->base.apply(rows, merged, sock);
            rows = std::move(merged);
            m->record(m->levels.size(), sock, sent, received, start);
        }
        if (m->levels.empty())
        {
            // The only dummies are public +infinity padding, already at the end.
            output.mShare.resize(2 * m->n);
            for (u64 i = 0; i < 2 * m->n; ++i) output.mShare[i] = readIndex(rows.data(i), m->indexBits);
            co_return;
        }
        {
            auto sent = sock.bytesSent(), received = sock.bytesReceived();
            auto start = std::chrono::steady_clock::now();
            BinMatrix dead(m->expanded, 1);
            oc::Matrix<u32> indices(m->expanded, 1), compacted(m->expanded, 1);
            for (u64 i = 0; i < m->expanded; ++i)
            {
                dead(i, 0) = (rows(i, m->orderBytes) ^ (m->role == 0)) & 1;
                indices(i, 0) = readIndex(rows.data(i), m->indexBits);
            }
            AdditivePerm scatter;
            co_await m->compact.genPerm(dead, scatter, sock, prng);
            co_await m->compactPerm.derandomize(scatter, sock);
            co_await m->compactPerm.apply<u32>(PermOp::Inverse, indices, compacted, sock);
            output.mShare.assign(compacted.data(), compacted.data() + 2 * m->n);
            m->record(m->levels.size() + 1, sock, sent, received, start);
        }
    }

    const std::vector<PiLogStarStage>& PiLogStar::stages() const { return m->stats; }
    u64 PiLogStar::paddedSize() const { return m->padded; }
    u64 PiLogStar::expandedSize() const { return m->expanded; }
    u64 PiLogStar::gmwRounds() const { return m->totalRounds; }
    u64 PiLogStar::onlineRoundBound() const
    { return m->totalRounds + (m->packed ? 9 : m->levels.empty() ? 0 : 5 * (m->levels.size() + 1) + 4); }
    u64 PiLogStar::paddedAnds() const { return m->totalAnds; }
}
