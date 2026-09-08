#include "BatchPrefix.h"
#include <algorithm>
#include <limits>
#include <stdexcept>

namespace secJoin
{
    namespace
    {
        u64 prefixProduct(u64 a, u64 b)
        {
            if (b && a > std::numeric_limits<u64>::max() / b)
                throw std::overflow_error("BatchPrefix dimensions overflow");
            return a * b;
        }

        u64 prefixSum(u64 a, u64 b)
        {
            if (a > std::numeric_limits<u64>::max() - b)
                throw std::overflow_error("BatchPrefix gate count overflow");
            return a + b;
        }
    }

    BetaCircuit BatchPrefix::combineCircuit(u64 valueBits, bool upward)
    {
        BetaCircuit cir;
        BetaBundle left(valueBits), right(valueBits), rightControl(1);
        BetaBundle leftControl(1), value(valueBits), control(1);
        BetaBundle difference(valueBits), masked(valueBits);
        cir.addInputBundle(left);
        cir.addInputBundle(right);
        cir.addInputBundle(rightControl);
        if (upward)
            cir.addInputBundle(leftControl);
        cir.addOutputBundle(value);
        if (upward)
            cir.addOutputBundle(control);
        cir.addTempWireBundle(difference);
        cir.addTempWireBundle(masked);

        // combine(a,b) = (a.c AND b.c, b.c ? a.v : b.v).
        // Every AND in this circuit is independent of every other AND.
        for (u64 bit = 0; bit < valueBits; ++bit)
        {
            cir.addGate(left[bit], right[bit], oc::GateType::Xor, difference[bit]);
            cir.addGate(difference[bit], rightControl[0], oc::GateType::And, masked[bit]);
            cir.addGate(right[bit], masked[bit], oc::GateType::Xor, value[bit]);
        }
        if (upward)
            cir.addGate(leftControl[0], rightControl[0], oc::GateType::And, control[0]);
        cir.levelByAndDepth();
        return cir;
    }

    BetaCircuit BatchPrefix::scalarAndCircuit()
    {
        BetaCircuit cir;
        BetaBundle left(1), right(1), output(1);
        cir.addInputBundle(left);
        cir.addInputBundle(right);
        cir.addOutputBundle(output);
        cir.addGate(left[0], right[0], oc::GateType::And, output[0]);
        cir.levelByAndDepth();
        return cir;
    }

    void BatchPrefix::init(u64 batches, u64 leaves, u64 valueBits,
        CorGenerator& cor, u64 coreGroup)
    {
        if (!batches || !leaves || (leaves & (leaves - 1)))
            throw std::invalid_argument("BatchPrefix requires positive batches and power-of-two leaves");
        if (!valueBits || valueBits > std::numeric_limits<u32>::max() / 8)
            throw std::invalid_argument("BatchPrefix valueBits exceeds circuit limits");
        if (!cor.initialized() || cor.partyIdx() > 1)
            throw std::invalid_argument("BatchPrefix requires an initialized two-party CorGenerator");
        if (coreGroup && (coreGroup & (coreGroup - 1)))
            throw std::invalid_argument("BatchPrefix coreGroup must be zero or a power of two");
        if (coreGroup >= leaves) coreGroup = 0;
        const auto rows = prefixProduct(batches, leaves);
        prefixProduct(rows, (valueBits + 7) / 8);

        mStages.clear();
        mNumRounds = 0;
        mNumAnds = 0;
        auto upCir = combineCircuit(valueBits, true);
        auto downCir = combineCircuit(valueBits, false);
        auto andCir = scalarAndCircuit();
        auto addStage = [&](u64 stride, bool upward, bool core = false)
        {
            Stage stage;
            stage.stride = stride;
            stage.upward = upward;
            if (core)
            {
                for (u64 group = 0; group < leaves / coreGroup; ++group)
                    if (group % (2 * stride) >= stride)
                        stage.nodes.emplace_back((group / (2 * stride) * 2 * stride + stride) * coreGroup - 1,
                            (group + 1) * coreGroup - 1);
            }
            else
                for (u64 i = (upward ? 2 : 3) * stride - 1; i < leaves; i += 2 * stride)
                    stage.nodes.emplace_back(i - stride, i);
            stage.perBatch = stage.nodes.size();
            const auto pairs = prefixProduct(stage.perBatch, batches);
            if (pairs > std::numeric_limits<u64>::max() - 127)
                throw std::overflow_error("BatchPrefix SIMD dimensions overflow");
            const auto paddedPairs = ((pairs + 127) / 128) * 128;
            // A wide mux over one pair would otherwise pay for 128 copies of
            // every bit's AND. Flatten independent bit products into one SIMD
            // AND circuit when fewer than 128 pairs are present or more than
            // a quarter of word-SIMD lanes would be unused. Full word batches
            // retain the original circuit to avoid unnecessary bit packing.
            stage.flat = pairs < 128 || (paddedPairs - pairs) * 4 > paddedPairs;
            const auto lanes = stage.flat ? prefixProduct(pairs, valueBits + upward) : pairs;
            if (lanes > std::numeric_limits<u64>::max() - 127)
                throw std::overflow_error("BatchPrefix flattened SIMD dimensions overflow");
            const auto& circuit = stage.flat ? andCir : upward ? upCir : downCir;
            const auto paddedLanes = ((lanes + 127) / 128) * 128;
            auto ands = prefixProduct(paddedLanes, circuit.mNonlinearGateCount);
            mNumAnds = prefixSum(mNumAnds, ands);
            prefixProduct(mNumAnds, 2);
            stage.gmw = std::make_unique<Gmw>();
            stage.gmw->init(lanes, circuit, cor);
            // GMW may include local XOR-only levels in numRounds(). Count
            // only levels which exchange messages when reporting rounds.
            for (const auto count : stage.gmw->mCir.mLevelAndCounts)
                mNumRounds += count != 0;
            mStages.push_back(std::move(stage));
        };

        // Brent-Kung reduction: retain summaries at right subtree endpoints.
        for (u64 stride = 1; stride < (coreGroup ? coreGroup : leaves); stride *= 2)
            addStage(stride, true);
        if (coreGroup)
            for (u64 stride = 1; stride < leaves / coreGroup; stride *= 2)
                addStage(stride, true, true);
        // Distribute inclusive prefixes to the remaining endpoints. The left
        // operand is now a prefix whose control product is public zero; only
        // the output value is needed, saving one AND per combine.
        for (u64 stride = coreGroup ? coreGroup / 2 : leaves / 4; stride; stride /= 2)
            addStage(stride, false);

        mBatches = batches;
        mLeaves = leaves;
        mValueBits = valueBits;
        mRows = rows;
        mUsed = false;
    }

    void BatchPrefix::preprocess()
    {
        if (!mLeaves || mUsed)
            throw std::logic_error("BatchPrefix::preprocess requires an unused initialized instance");
        for (auto& stage : mStages)
            stage.gmw->preprocess();
    }

    macoro::task<> BatchPrefix::apply(const BinMatrix& values,
        const BinMatrix& controls, BinMatrix& output, coproto::Socket& sock)
    {
        if (!mLeaves)
            throw std::logic_error("BatchPrefix::init must be called first");
        if (mUsed)
            throw std::logic_error("BatchPrefix correlations cannot be reused");
        if (values.rows() != mRows || values.bitsPerEntry() != mValueBits ||
            values.bytesPerEntry() != (mValueBits + 7) / 8 ||
            controls.rows() != mRows || controls.bitsPerEntry() != 1 ||
            controls.bytesPerEntry() != 1)
            throw std::invalid_argument("BatchPrefix input dimensions do not match init");
        mUsed = true;

        // Copy first to support aliasing, and keep every data-dependent
        // operation inside GMW. All gather/scatter addresses are public.
        BinMatrix work = values;
        BinMatrix bits = controls;
        for (u64 batch = 0; batch < mBatches; ++batch)
            bits(batch * mLeaves, 0) = 0;

        for (auto& stage : mStages)
        {
            const auto lanes = stage.perBatch * mBatches;
            if (stage.flat)
            {
                const auto products = mValueBits + stage.upward;
                const auto flatLanes = lanes * products;
                // Use the backend's packed wire format directly: one bit per
                // product, with no byte-per-bit matrix or transpose overhead.
                TBinMatrix difference(flatLanes, 1, sizeof(block));
                TBinMatrix select(flatLanes, 1, sizeof(block));
                TBinMatrix masked(flatLanes, 1, sizeof(block));
                auto get = [](const u8* data, u64 bit, u64 count)
                {
                    const auto shift = bit % 8;
                    auto value = unsigned(data[bit / 8]) >> shift;
                    if (shift + count > 8)
                        value |= unsigned(data[bit / 8 + 1]) << (8 - shift);
                    return value & ((1u << count) - 1);
                };
                auto put = [](u8* data, u64 bit, u8 value, u64 count)
                {
                    const auto shift = bit % 8;
                    value &= (1u << count) - 1;
                    data[bit / 8] |= unsigned(value) << shift;
                    if (shift + count > 8)
                        data[bit / 8 + 1] |= unsigned(value) >> (8 - shift);
                };
                const auto fullBytes = mValueBits / 8;
                const auto tailBits = mValueBits % 8;
                u64 pair = 0;
                for (u64 batch = 0; batch < mBatches; ++batch)
                    for (const auto& node : stage.nodes)
                    {
                        const auto r = batch * mLeaves + node.second;
                        const auto l = batch * mLeaves + node.first;
                        const auto begin = pair++ * products;
                        const auto control = bits(r, 0) & 1;
                        const auto controlByte = static_cast<u8>(0u - control);
                        for (u64 byte = 0; byte < fullBytes; ++byte)
                        {
                            put(difference.data(), begin + 8 * byte,
                                work(l, byte) ^ work(r, byte), 8);
                            put(select.data(), begin + 8 * byte, controlByte, 8);
                        }
                        if (tailBits)
                        {
                            put(difference.data(), begin + 8 * fullBytes,
                                work(l, fullBytes) ^ work(r, fullBytes), tailBits);
                            put(select.data(), begin + 8 * fullBytes, controlByte, tailBits);
                        }
                        if (stage.upward)
                        {
                            put(difference.data(), begin + mValueBits, bits(l, 0), 1);
                            put(select.data(), begin + mValueBits, control, 1);
                        }
                    }
                auto& gmw = *stage.gmw;
                gmw.mapInput(0, difference);
                gmw.mapInput(1, select);
                gmw.mapOutput(0, masked);
                co_await gmw.run(sock);
                pair = 0;
                for (u64 batch = 0; batch < mBatches; ++batch)
                    for (const auto& node : stage.nodes)
                    {
                        const auto r = batch * mLeaves + node.second;
                        const auto begin = pair++ * products;
                        for (u64 byte = 0; byte < fullBytes; ++byte)
                            work(r, byte) ^= get(masked.data(), begin + 8 * byte, 8);
                        if (tailBits)
                            work(r, fullBytes) ^= get(masked.data(), begin + 8 * fullBytes, tailBits);
                        if (stage.upward)
                            bits(r, 0) = get(masked.data(), begin + mValueBits, 1);
                    }
                stage.gmw.reset();
                continue;
            }
            BinMatrix left(lanes, mValueBits), right(lanes, mValueBits);
            BinMatrix rightControl(lanes, 1), leftControl;
            BinMatrix combined(lanes, mValueBits), combinedControl;
            if (stage.upward)
            {
                leftControl.resize(lanes, 1);
                combinedControl.resize(lanes, 1);
            }
            u64 lane = 0;
            for (u64 batch = 0; batch < mBatches; ++batch)
                for (const auto& node : stage.nodes)
                {
                    const auto r = batch * mLeaves + node.second;
                    const auto l = batch * mLeaves + node.first;
                    std::copy(work[l].begin(), work[l].end(), left[lane].begin());
                    std::copy(work[r].begin(), work[r].end(), right[lane].begin());
                    rightControl(lane, 0) = bits(r, 0);
                    if (stage.upward)
                        leftControl(lane, 0) = bits(l, 0);
                    ++lane;
                }
            auto& gmw = *stage.gmw;
            gmw.setInput<u8>(0, left.mData);
            gmw.setInput<u8>(1, right.mData);
            gmw.setInput<u8>(2, rightControl.mData);
            if (stage.upward)
                gmw.setInput<u8>(3, leftControl.mData);
            co_await gmw.run(sock);
            gmw.getOutput<u8>(0, combined.mData);
            if (stage.upward)
                gmw.getOutput<u8>(1, combinedControl.mData);
            lane = 0;
            for (u64 batch = 0; batch < mBatches; ++batch)
                for (const auto& node : stage.nodes)
                {
                    const auto r = batch * mLeaves + node.second;
                    std::copy(combined[lane].begin(), combined[lane].end(), work[r].begin());
                    if (stage.upward)
                        bits(r, 0) = combinedControl(lane, 0);
                    ++lane;
                }
            // Correlations are single-use; release the completed evaluator.
            stage.gmw.reset();
        }
        work.trim();
        output = std::move(work);
    }
}
