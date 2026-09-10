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

        // Same width-balanced selector comparison as RootMerge. The low
        // segment's equality is never consumed, so its equality wire is omitted.
        u32 compactLessThan(BetaCircuit& c,const BetaBundle& a,const BetaBundle& b)
        {
            auto gate=[&](u32 x,u32 y,oc::GateType type)
            {BetaBundle t(1);c.addTempWireBundle(t);c.addGate(x,y,type,t[0]);return t[0];};
            struct Segment {u32 equal,less;};
            auto build=[&](auto&& self,u64 begin,u64 size) -> Segment
            {
                if(size==1)return {gate(a[begin],b[begin],oc::GateType::Nxor),
                    begin?b[begin]:gate(a[begin],b[begin],oc::GateType::na_And)};
                auto lo=self(self,begin,size/2),hi=self(self,begin+size/2,size-size/2);
                auto difference=gate(lo.less,hi.less,oc::GateType::Xor);
                auto less=gate(hi.less,gate(hi.equal,difference,oc::GateType::And),oc::GateType::Xor);
                return {begin?gate(lo.equal,hi.equal,oc::GateType::And):u32(-1),less};
            };
            return build(build,0,a.size()).less;
        }
    }

    u32 logstarLessThan(BetaCircuit& cir, const BetaBundle& left,
        const BetaBundle& right, bool optimized)
    {
        const auto orderBits = left.size();
        if (!orderBits || orderBits != right.size())
            throw std::invalid_argument("logstarLessThan requires equal positive input widths");
        if (orderBits > (std::numeric_limits<u32>::max() - cir.mWireCount) / 5)
            throw std::overflow_error("logstarLessThan exceeds circuit wire limits");
        if (optimized) return compactLessThan(cir, left, right);
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
        const std::vector<u64>& outputBits, bool optimizedComparison, u64 publicLowBits, u64 publicLowXor)
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
        // Initial left positions precede initial right positions. Thus equal
        // remaining keys stay left and the public tie-breaker needs no circuit.
        leftKey.mWires.erase(leftKey.mWires.begin(), leftKey.mWires.begin() + publicLowBits);
        rightKey.mWires.erase(rightKey.mWires.begin(), rightKey.mWires.begin() + publicLowBits);
        const auto swap = optimizedComparison ? compactLessThan(cir, rightKey, leftKey)
                                             : logstarLessThan(cir, rightKey, leftKey);
        BetaBundle temp(2 * outputWidth);
        cir.addTempWireBundle(temp);
        u64 next = 0;
        auto wire = [&]() { return temp[next++]; };
        for (u64 outBit = 0; outBit < outputWidth; ++outBit)
        {
            const auto bit = outputBits.empty() ? outBit : outputBits[outBit];
            if (bit < publicLowBits)
            {
                // Their XOR is n for odd-even and 2*n-1 after bitonic reversal.
                // A public difference of one needs only XOR with the swap bit.
                if ((publicLowXor >> bit) & 1)
                {
                    cir.addGate(left[bit], swap, oc::GateType::Xor, smaller[outBit]);
                    cir.addGate(right[bit], swap, oc::GateType::Xor, larger[outBit]);
                }
                else
                {
                    cir.addCopy(left[bit], smaller[outBit]);
                    cir.addCopy(right[bit], larger[outBit]);
                }
                continue;
            }
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
        u64 rowBits, CorGenerator& cor, const std::vector<u64>& finalOutputBits,
        bool optimizedComparison, bool oddEven, bool publicLocalPositions)
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
        u64 publicLowBits = 0;
        if (publicLocalPositions)
        {
            for (auto size = 2 * halfSize; size > 1; size /= 2) ++publicLowBits;
            if (publicLowBits >= orderBits)
                throw std::invalid_argument("BatcherMerge public positions require a remaining key field");
        }
        auto cir = compareSwapCircuit(orderBits, rowBits, {}, optimizedComparison);
        auto finalCir = finalOutputBits.empty() ? cir : compareSwapCircuit(orderBits, rowBits, finalOutputBits, optimizedComparison);
        auto firstCir = publicLowBits ? compareSwapCircuit(orderBits, rowBits,
            halfSize == 1 ? finalOutputBits : std::vector<u64>{}, optimizedComparison,
            publicLowBits, oddEven ? halfSize : 2 * halfSize - 1) : (halfSize == 1 ? finalCir : cir);

        u64 layers = 0;
        for (auto distance = halfSize; distance; distance /= 2)
            ++layers;
        u64 ands = 0, comparisons = 0;
        // Derive the whole public schedule before registering correlations.
        for (auto distance = halfSize; distance; distance /= 2)
        {
            auto stagePairs = checkedProduct(batches, halfSize - (oddEven && distance < halfSize ? distance : 0));
            const auto& stageCir = distance == halfSize ? firstCir : distance == 1 ? finalCir : cir;
            auto stageAnds = checkedProduct(stageCir.mNonlinearGateCount, oc::roundUpTo(stagePairs, 128));
            if (stageAnds > std::numeric_limits<u64>::max() - ands ||
                stagePairs > std::numeric_limits<u64>::max() - comparisons)
                throw std::overflow_error("BatcherMerge schedule overflow");
            ands += stageAnds;
            comparisons += stagePairs;
        }
        checkedProduct(ands, 2); // GMW consumes two binary OLEs per AND.

        mStages.clear();
        mStages.reserve(layers);
        mNumRounds = 0;
        for (auto distance = halfSize; distance; distance /= 2)
        {
            const auto& stageCir = distance == halfSize ? firstCir : distance == 1 ? finalCir : cir;
            auto stagePairs = batches * (halfSize - (oddEven && distance < halfSize ? distance : 0));
            auto stage = std::make_unique<Gmw>();
            stage->init(stagePairs, stageCir, cor);
            mNumRounds += std::count_if(stageCir.mLevelAndCounts.begin(),
                stageCir.mLevelAndCounts.end(), [](auto count) { return count != 0; });
            mStages.push_back(std::move(stage));
        }
        mBatches = batches;
        mHalfSize = halfSize;
        mRowBits = rowBits;
        mRows = rows;
        mNumAnds = ands;
        mNumComparisons = comparisons;
        mOddEven = oddEven;
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
        co_await applyOwned(input, output, sock);
    }

    macoro::task<> BatcherMerge::applyOwned(BinMatrix input,
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
        // The owned buffer can be moved directly, including when output aliases
        // the caller's original (now moved-from) matrix.
        BinMatrix work = std::move(input);
        const auto runSize = 2 * mHalfSize;
        if (!mOddEven) for (u64 batch = 0; batch < mBatches; ++batch)
        {
            const auto second = batch * runSize + mHalfSize;
            for (u64 i = 0; i < mHalfSize / 2; ++i)
            {
                auto a = work[second + i];
                auto b = work[second + mHalfSize - 1 - i];
                std::swap_ranges(a.begin(), a.end(), b.begin());
            }
        }
        BinMatrix left, right, smaller, larger;
        std::vector<u8> retainedMask((mRowBits + 7) / 8);
        for (auto bit : mFinalOutputBits) retainedMask[bit / 8] |= 1 << (bit % 8);
        u64 layer = 0;
        for (u64 distance = mHalfSize; distance; distance /= 2, ++layer)
        {
            const bool project = distance == 1 && !mFinalOutputBits.empty();
            // Odd-even's interior layers compare [offset,runSize-offset).
            // Boundary rows are already final. No explicit wire list is needed.
            const auto offset = mOddEven && distance < mHalfSize ? distance : 0;
            const auto pairs = mBatches * (mHalfSize - offset);
            left.resize(pairs, mRowBits, 1, oc::AllocType::Uninitialized);
            right.resize(pairs, mRowBits, 1, oc::AllocType::Uninitialized);
            smaller.resize(pairs, project ? mFinalOutputBits.size() : mRowBits);
            larger.resize(pairs, project ? mFinalOutputBits.size() : mRowBits);
            u64 pair = 0;
            for (u64 batch = 0; batch < mBatches; ++batch)
                for (u64 base = batch * runSize + offset; base < (batch + 1) * runSize - offset; base += 2 * distance)
                    for (u64 i = 0; i < distance; ++i, ++pair)
                    {
                        std::copy(work[base + i].begin(), work[base + i].end(), left[pair].begin());
                        std::copy(work[base + i + distance].begin(), work[base + i + distance].end(), right[pair].begin());
                    }
            auto& gmw = *mStages[layer];
            gmw.setInput<u8>(0, left.mData);
            gmw.setInput<u8>(1, right.mData);
            co_await gmw.run(sock);
            gmw.getOutput<u8>(0, smaller.mData);
            gmw.getOutput<u8>(1, larger.mData);
            // Correlations are consumed once. Drop the circuit wire storage
            // before the next layer to keep memory proportional to one layer.
            mStages[layer].reset();
            // Odd-even's untouched boundary rows must retain their payload.
            // Other bits still become public zero, as promised by the API.
            if (project)
            {
                if (!mOddEven) work.setZero();
                else for (u64 row = 0; row < mRows; ++row)
                        for (u64 byte = 0; byte < retainedMask.size(); ++byte)
                            work(row, byte) &= retainedMask[byte];
            }
            pair = 0;
            for (u64 batch = 0; batch < mBatches; ++batch)
                for (u64 base = batch * runSize + offset; base < (batch + 1) * runSize - offset; base += 2 * distance)
                    for (u64 i = 0; i < distance; ++i, ++pair)
                    {
                        if (!project)
                        {
                            std::copy(smaller[pair].begin(), smaller[pair].end(), work[base + i].begin());
                            std::copy(larger[pair].begin(), larger[pair].end(), work[base + i + distance].begin());
                        }
                        else
                        {
                            if (mOddEven)
                            {
                                std::fill(work[base + i].begin(), work[base + i].end(), 0);
                                std::fill(work[base + i + distance].begin(), work[base + i + distance].end(), 0);
                            }
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
