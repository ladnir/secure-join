#pragma once

#include "secure-join/CorGenerator/CorGenerator.h"
#include "secure-join/Perm/AdditivePerm.h"
#include "secure-join/Util/Matrix.h"
#include <memory>
#include <string>

namespace secJoin
{
    struct PiLogStarOptions
    {
        // Public tuning parameters. Zero blockSize chooses a power of two near log2(n).
        u64 baseCase = 16;
        u64 blockSize = 0; // Override at the outermost recursion only.
        bool packed = true; // Exact one-partition specialization for power-of-two inputs.
    };

    struct PiLogStarStage
    {
        std::string name;
        u64 batches = 0, halfSize = 0, blockSize = 0;
        u64 gmwRounds = 0, paddedAnds = 0;
        u64 sentBytes = 0, receivedBytes = 0;
        double milliseconds = 0;
    };

    // Two-party, semi-honest merge of equal-length, unsigned, sorted XOR-shared lists.
    // Output is an XOR-shared gather permutation: sorted[i] = (X || Y)[pi[i]].
    // Equal keys are ordered by original position (all equal X keys precede Y keys).
    // Only jointly shuffled extraction flags/ranks are opened; keys and indices stay shared.
    // Correlations and this instance are single-use; construct a fresh instance per merge.
    class PiLogStar
    {
    public:
        PiLogStar();
        ~PiLogStar();
        PiLogStar(PiLogStar&&) noexcept;
        PiLogStar& operator=(PiLogStar&&) noexcept;
        PiLogStar(const PiLogStar&) = delete;
        PiLogStar& operator=(const PiLogStar&) = delete;

        // All requests are registered here, BEFORE CorGenerator::start().
        void init(u64 n, u64 keyBits, CorGenerator& cor,
                  const PiLogStarOptions& options = {});
        void preprocess();

        // Independent of the input. Run concurrently with cor.start(), or after
        // cor.start() finishes when preprocess() has started every request.
        macoro::task<> prepare(coproto::Socket& sock, PRNG& prng);

        // Requires prepare() to have completed. Dimensions are public and checked.
        macoro::task<> merge(const BinMatrix& x, const BinMatrix& y,
                             AdditivePerm& permutation, coproto::Socket& sock, PRNG& prng);

        const std::vector<PiLogStarStage>& stages() const;
        u64 paddedSize() const;
        u64 expandedSize() const;
        u64 gmwRounds() const;
        // Online depth: AND layers plus 9 one-way steps for the packed path;
        // general path: 5 per derandomized permutation plus 4 for rank conversion.
        // Excludes input sharing, preprocessing, transport setup, and output opening.
        u64 onlineRoundBound() const;
        u64 paddedAnds() const;

    private:
        struct Impl;
        std::unique_ptr<Impl> m;
    };
}
