#pragma once
#include "secure-join/Defines.h"
#include <libOTe/Tools/CompilerHints.h>
#include <libOTe/Tools/Spin/SpinCode.h>
#if defined(ENABLE_SSE) && defined(__AVX2__)
#include <immintrin.h>
#endif

namespace secJoin
{
    // Reserve the two GF(4) choice bits in each expanded leaf. Keep this scan
    // outside the protocol coroutine so the loop state stays in registers.
    OC_NOINLINE inline void clearF4ChoiceBits(span<block> leaves)
    {
        const block mask(~0ull, ~3ull);
        u64 i = 0;
#if defined(ENABLE_SSE) && defined(__AVX2__)
        const auto wideMask = _mm256_broadcastsi128_si256(mask.mData);
        for (; i + 2 <= leaves.size(); i += 2)
        {
            auto* p = reinterpret_cast<__m256i*>(leaves.data() + i);
            _mm256_storeu_si256(p, _mm256_and_si256(_mm256_loadu_si256(p), wideMask));
        }
#endif
        for (; i < leaves.size(); ++i)
            leaves[i] &= mask;
    }

    inline oc::SdConfig configureF4Code(u64 security, u64 size, oc::MultType type,
        oc::SdNoiseDistribution noise = oc::SdNoiseDistribution::Regular)
    {
        if (noise != oc::SdNoiseDistribution::Regular &&
            (noise != oc::SdNoiseDistribution::Stationary || type != oc::MultType::Spin))
            throw std::invalid_argument("Stationary GF(4) VOLE requires SPIN");
        // libOTe tunes regular binary SPIN noise with a character factor of 2.
        // Uniform nonzero GF(4) coefficients give 4/3. Use its more conservative
        // factor-one partition count for both modes.
        if (type == oc::MultType::Spin)
            return oc::SpinConfigure(security, size, oc::SdNoiseDistribution::Stationary);
        return oc::syndromeDecodingConfigure(security, size, type,
            oc::SdNoiseDistribution::Regular, {4.0 / 3.0});
    }

    // One immutable public regular map per padded K for the life of the process.
    void prepareRegularSpin(std::optional<oc::SpinCode>& code, u64 size);

    inline void compressF4Spin(std::optional<oc::SpinCode>& code, u64 size,
        span<block> values, oc::SdNoiseDistribution noise, block& seed)
    {
        if (noise == oc::SdNoiseDistribution::Regular)
            prepareRegularSpin(code, size);
        else
            oc::SpinCode::prepare(code, size, seed, noise);
        code->transpose(values);
        if (noise == oc::SdNoiseDistribution::Stationary)
            seed = oc::mAesFixedKey.hashBlock(seed);
    }
}
