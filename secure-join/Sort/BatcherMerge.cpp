#include "BatcherMerge.h"
#include <algorithm>
#include <limits>
#include <stdexcept>

namespace secJoin
{
    namespace
    {
        u64 checkedProduct(u64 a, u64 b)
        {
            if (b && a > std::numeric_limits<u64>::max() / b)
                throw std::overflow_error("BatcherMerge dimensions overflow");
            return a * b;
        }
    }

    u32 logstarLessThan(BetaCircuit& cir, const BetaBundle& left,
        const BetaBundle& right)
    {
        const auto orderBits = left.size();
        if (!orderBits || orderBits != right.size())
            throw std::invalid_argument("logstarLessThan requires equal positive input widths");
        if (orderBits > (std::numeric_limits<u32>::max() - cir.mWireCount) / 5)
            throw std::overflow_error("logstarLessThan exceeds circuit wire limits");
        // Segment summaries (equal, less) are combined in a balanced tree.
        // Higher bits dominate lower bits:
        //   less = lessHigh XOR (equalHigh AND lessLow).
        // The two terms are disjoint, so XOR implements OR at no extra cost.
        // This gives logarithmic AND depth even for non-power-of-two keys.
        BetaBundle temp(5 * orderBits - 3);
        cir.addTempWireBundle(temp);
        u64 next = 0;
        auto wire = [&]() { return temp[next++]; };
        struct Segment { u32 equal; u32 less; };
        std::vector<Segment> current;
        current.reserve(orderBits);
        for (u64 bit = 0; bit < orderBits; ++bit)
        {
            auto equal = wire();
            auto less = wire();
            cir.addGate(left[bit], right[bit], oc::GateType::Nxor, equal);
            cir.addGate(left[bit], right[bit], oc::GateType::na_And, less);
            current.push_back({ equal, less });
        }
        while (current.size() > 1)
        {
            std::vector<Segment> parents;
            parents.reserve((current.size() + 1) / 2);
            for (u64 i = 0; i < current.size(); i += 2)
            {
                if (i + 1 == current.size())
                {
                    parents.push_back(current[i]);
                    continue;
                }
                const auto lo = current[i];
                const auto hi = current[i + 1];
                auto equal = wire();
                auto lowWins = wire();
                auto less = wire();
                cir.addGate(hi.equal, lo.equal, oc::GateType::And, equal);
                cir.addGate(hi.equal, lo.less, oc::GateType::And, lowWins);
                cir.addGate(hi.less, lowWins, oc::GateType::Xor, less);
                parents.push_back({ equal, less });
            }
            current = std::move(parents);
        }
        return current[0].less;
    }

    BetaCircuit BatcherMerge::compareSwapCircuit(u64 orderBits, u64 rowBits,
        const std::vector<u64>& outputBits)
    {
        BetaCircuit cir;
        BetaBundle left(rowBits), right(rowBits);
        const auto outputWidth = outputBits.empty() ? rowBits : outputBits.size();
        BetaBundle smaller(outputWidth), larger(outputWidth);
        cir.addInputBundle(left);
        cir.addInputBundle(right);
        cir.addOutputBundle(smaller);
        cir.addOutputBundle(larger);
        BetaBundle leftKey = left, rightKey = right;
        leftKey.mWires.resize(orderBits);
        rightKey.mWires.resize(orderBits);
        const auto swap = logstarLessThan(cir, rightKey, leftKey);
        BetaBundle temp(2 * outputWidth);
        cir.addTempWireBundle(temp);
        u64 next = 0;
        auto wire = [&]() { return temp[next++]; };
        for (u64 outBit = 0; outBit < outputWidth; ++outBit)
        {
            const auto bit = outputBits.empty() ? outBit : outputBits[outBit];
            auto difference = wire();
            auto masked = wire();
            cir.addGate(left[bit], right[bit], oc::GateType::Xor, difference);
            cir.addGate(difference, swap, oc::GateType::And, masked);
            cir.addGate(left[bit], masked, oc::GateType::Xor, smaller[outBit]);
            cir.addGate(right[bit], masked, oc::GateType::Xor, larger[outBit]);
        }
        cir.levelByAndDepth();
        return cir;
    }

    void BatcherMerge::init(u64 batches, u64 halfSize, u64 orderBits,
        u64 rowBits, CorGenerator& cor, const std::vector<u64>& finalOutputBits)
    {
        if (!batches || !halfSize || (halfSize & (halfSize - 1)))
            throw std::invalid_argument("BatcherMerge requires positive batches and power-of-two halfSize");
        if (!orderBits || orderBits > rowBits)
            throw std::invalid_argument("BatcherMerge requires 0 < orderBits <= rowBits");
        // BetaCircuit numbers wires with u32. Leave a margin for all input,
        // output and temporary bundles and prevent arithmetic overflow here.
        if (rowBits > std::numeric_limits<u32>::max() / 16)
            throw std::invalid_argument("BatcherMerge rowBits exceeds circuit limits");
        if (!cor.initialized() || cor.partyIdx() > 1)
            throw std::invalid_argument("BatcherMerge requires an initialized two-party CorGenerator");
        auto selected = finalOutputBits;
        std::sort(selected.begin(), selected.end());
        if (!selected.empty() && (selected.back() >= rowBits ||
            std::adjacent_find(selected.begin(), selected.end()) != selected.end()))
            throw std::invalid_argument("BatcherMerge finalOutputBits must be unique valid row-bit positions");
        const auto rows = checkedProduct(checkedProduct(batches, halfSize), 2);
        checkedProduct(rows, (rowBits + 7) / 8);
        const auto pairs = rows / 2;
        if (pairs > std::numeric_limits<u64>::max() - 127)
            throw std::overflow_error("BatcherMerge SIMD dimensions overflow");
        const auto paddedPairs = ((pairs + 127) / 128) * 128;
        auto cir = compareSwapCircuit(orderBits, rowBits);
        const auto andsPerStage = checkedProduct(cir.mNonlinearGateCount, paddedPairs);
        auto finalCir = finalOutputBits.empty() ? cir : compareSwapCircuit(orderBits, rowBits, finalOutputBits);
        const auto finalAnds = checkedProduct(finalCir.mNonlinearGateCount, paddedPairs);

        u64 layers = 0;
        for (auto distance = halfSize; distance; distance /= 2)
            ++layers;
        const auto precedingAnds = checkedProduct(andsPerStage, layers - 1);
        if (finalAnds > std::numeric_limits<u64>::max() - precedingAnds)
            throw std::overflow_error("BatcherMerge AND count overflow");
        const auto ands = precedingAnds + finalAnds;
        checkedProduct(ands, 2); // GMW consumes two binary OLEs per AND.

        mStages.clear();
        mStages.reserve(layers);
        mNumRounds = 0;
        for (u64 layer = 0; layer < layers; ++layer)
        {
            const auto& stageCir = layer + 1 == layers ? finalCir : cir;
            auto stage = std::make_unique<Gmw>();
            stage->init(pairs, stageCir, cor);
            mNumRounds += std::count_if(stageCir.mLevelAndCounts.begin(),
                stageCir.mLevelAndCounts.end(), [](auto count) { return count != 0; });
            mStages.push_back(std::move(stage));
        }
        mBatches = batches;
        mHalfSize = halfSize;
        mRowBits = rowBits;
        mRows = rows;
        mNumAnds = ands;
        mFinalOutputBits = finalOutputBits;
        mUsed = false;
    }

    void BatcherMerge::preprocess()
    {
        if (mStages.empty() || mUsed)
            throw std::logic_error("BatcherMerge::preprocess requires unused initialized correlations");
        for (auto& stage : mStages)
            stage->preprocess();
    }

    macoro::task<> BatcherMerge::apply(const BinMatrix& input,
        BinMatrix& output, coproto::Socket& sock)
    {
        if (mStages.empty())
            throw std::logic_error("BatcherMerge::init must be called first");
        if (mUsed)
            throw std::logic_error("BatcherMerge correlations cannot be reused");
        if (input.rows() != mRows || input.bitsPerEntry() != mRowBits ||
            input.bytesPerEntry() != (mRowBits + 7) / 8)
            throw std::invalid_argument("BatcherMerge input dimensions do not match init");
        mUsed = true;

        // The second ascending run is reversed to create a bitonic sequence.
        // Copy first so output may alias input without destroying source rows.
        BinMatrix work = input;
        const auto runSize = 2 * mHalfSize;
        for (u64 batch = 0; batch < mBatches; ++batch)
        {
            const auto second = batch * runSize + mHalfSize;
            for (u64 i = 0; i < mHalfSize / 2; ++i)
            {
                auto a = work[second + i];
                auto b = work[second + mHalfSize - 1 - i];
                std::swap_ranges(a.begin(), a.end(), b.begin());
            }
        }
        const auto pairs = mRows / 2;
        BinMatrix left(pairs, mRowBits), right(pairs, mRowBits);
        BinMatrix smaller(pairs, mRowBits), larger(pairs, mRowBits);
        u64 layer = 0;
        for (u64 distance = mHalfSize; distance; distance /= 2, ++layer)
        {
            const bool project = distance == 1 && !mFinalOutputBits.empty();
            u64 pair = 0;
            for (u64 batch = 0; batch < mBatches; ++batch)
                for (u64 base = batch * runSize; base < (batch + 1) * runSize; base += 2 * distance)
                    for (u64 i = 0; i < distance; ++i, ++pair)
                    {
                        std::copy(work[base + i].begin(), work[base + i].end(), left[pair].begin());
                        std::copy(work[base + i + distance].begin(), work[base + i + distance].end(), right[pair].begin());
                    }
            auto& gmw = *mStages[layer];
            gmw.setInput<u8>(0, left.mData);
            gmw.setInput<u8>(1, right.mData);
            co_await gmw.run(sock);
            if (project)
            {
                smaller.resize(pairs, mFinalOutputBits.size());
                larger.resize(pairs, mFinalOutputBits.size());
            }
            gmw.getOutput<u8>(0, smaller.mData);
            gmw.getOutput<u8>(1, larger.mData);
            // Correlations are consumed once. Drop the circuit wire storage
            // before the next layer to keep memory proportional to one layer.
            mStages[layer].reset();
            if (project) work.setZero();
            pair = 0;
            for (u64 batch = 0; batch < mBatches; ++batch)
                for (u64 base = batch * runSize; base < (batch + 1) * runSize; base += 2 * distance)
                    for (u64 i = 0; i < distance; ++i, ++pair)
                    {
                        if (!project)
                        {
                            std::copy(smaller[pair].begin(), smaller[pair].end(), work[base + i].begin());
                            std::copy(larger[pair].begin(), larger[pair].end(), work[base + i + distance].begin());
                        }
                        else
                        {
                            for (u64 bit = 0; bit < mFinalOutputBits.size(); ++bit)
                            {
                                const auto dst = mFinalOutputBits[bit];
                                work(base + i, dst / 8) |= ((smaller(pair, bit / 8) >> (bit % 8)) & 1) << (dst % 8);
                                work(base + i + distance, dst / 8) |= ((larger(pair, bit / 8) >> (bit % 8)) & 1) << (dst % 8);
                            }
                        }
                    }
        }
        work.trim();
        output = std::move(work);
    }
}
