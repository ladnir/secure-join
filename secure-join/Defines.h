#pragma once
#include "cryptoTools/Common/Defines.h"
#include <iostream>
#include <sstream>
#include <iomanip>
#include "Paillier/Defines.h"
namespace secJoin
{
    using ::memcpy;

    template<typename D, typename S>
    OC_FORCEINLINE void memcpy(span<D> dst, span<S> src)
    {
        assert(dst.size_bytes() == src.size_bytes());
        ::memcpy(dst.data(), src.data(), dst.size_bytes());
    }

    template<typename D, typename S>
    OC_FORCEINLINE void memcpyMin(span<D> dst, span<S> src)
    {
        ::memcpy(dst.data(), src.data(), std::min(src.size_bytes(), dst.size_bytes()));
    }
    using ::memset;

    template<typename D>
    OC_FORCEINLINE void memset(span<D> dst, char v)
    {
        ::memset(dst.data(), v, dst.size_bytes());
    }

    inline std::string hex(oc::span<u8> d)
    {
        std::stringstream ss;
        for (u64 i = d.size() - 1; i < d.size(); --i)
            ss << std::hex << std::setw(2) << std::setfill('0') << int(d[i]);
        return ss.str();
    }

}