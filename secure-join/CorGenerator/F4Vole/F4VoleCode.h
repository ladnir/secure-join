#pragma once
#include "secure-join/Defines.h"
#include <libOTe/Tools/Spin/SpinCode.h>

namespace secJoin
{
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
