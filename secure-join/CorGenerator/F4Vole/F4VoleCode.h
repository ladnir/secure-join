#pragma once
#include "secure-join/Defines.h"
#include <libOTe/Tools/Spin/SpinCode.h>

namespace secJoin
{
    inline oc::SdConfig configureF4Code(u64 security, u64 size, oc::MultType type)
    {
        // libOTe tunes regular binary SPIN noise with a character factor of 2.
        // Uniform nonzero GF(4) coefficients give 4/3. Use its more conservative
        // factor-one partition count while still expanding fresh regular PPRFs.
        if (type == oc::MultType::Spin)
            return oc::SpinConfigure(security, size, oc::SdNoiseDistribution::Stationary);
        return oc::syndromeDecodingConfigure(security, size, type,
            oc::SdNoiseDistribution::Regular, {4.0 / 3.0});
    }

    inline void compressF4Spin(std::optional<oc::SpinCode>& code, u64 size, span<block> values)
    {
        // Same public code seed as libOTe regular silent OT.
        const block seed(12528943721987127, 98743297823479812);
        oc::SpinCode::prepare(code, size, seed, oc::SdNoiseDistribution::Regular);
        code->transpose(values);
    }
}
