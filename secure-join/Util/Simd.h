#pragma once
#include "secure-join/Defines.h"
#include <cstring>
#ifdef ENABLE_SSE
#include <immintrin.h>
#endif


namespace secJoin
{

#ifdef ENABLE_SSE

    OC_FORCEINLINE block shuffle_epi8(const block& a, const block& b)
    {
        return _mm_shuffle_epi8(a, b);
    }

    template<int s>
    OC_FORCEINLINE block slli_epi16(const block& v)
    {
        return _mm_slli_epi16(v, s);
    }

    OC_FORCEINLINE  int movemask_epi8(const block v)
    {
        return _mm_movemask_epi8(v);
    }
#else
    OC_FORCEINLINE block shuffle_epi8(const block& a, const block& b)
    {
        // _mm_shuffle_epi8(a, b): 
        //     FOR j := 0 to 15
        //         i: = j * 8
        //         IF b[i + 7] == 1
        //             dst[i + 7:i] : = 0
        //         ELSE
        //             index[3:0] : = b[i + 3:i]
        //             dst[i + 7:i] : = a[index * 8 + 7:index * 8]
        //         FI
        //     ENDFOR

        block dst;
        for (u64 i = 0; i < 16; ++i)
        {
            const auto index = b.get<u8>(i);
            dst.set<u8>(i, index & 0x80 ? 0 : a.get<u8>(index & 15));
        }
        return dst;
    }

    template<int s>
    OC_FORCEINLINE block slli_epi16(const block& v)
    {
        block r;
        static_assert(s >= 0);
        for (u64 i = 0; i < 8; ++i)
            r.set<u16>(i, s < 16 ? static_cast<u16>(u32(v.get<u16>(i)) << s) : 0);
        return r;
    }

    OC_FORCEINLINE int movemask_epi8(const block v)
    {
        int r = 0;
        for (u64 i = 0; i < 16; ++i)
            r |= (v.get<u8>(i) >> 7) << i;
        return r;
    }

#endif



    // Gather byte zero of 16 consecutive blocks in lane order.
    OC_FORCEINLINE block gatherLowBytes(const block* x)
    {
#ifdef ENABLE_SSE
        const auto a = _mm_unpacklo_epi16(_mm_unpacklo_epi8(x[0], x[1]), _mm_unpacklo_epi8(x[2], x[3]));
        const auto b = _mm_unpacklo_epi16(_mm_unpacklo_epi8(x[4], x[5]), _mm_unpacklo_epi8(x[6], x[7]));
        const auto c = _mm_unpacklo_epi16(_mm_unpacklo_epi8(x[8], x[9]), _mm_unpacklo_epi8(x[10], x[11]));
        const auto d = _mm_unpacklo_epi16(_mm_unpacklo_epi8(x[12], x[13]), _mm_unpacklo_epi8(x[14], x[15]));
        return _mm_unpacklo_epi64(_mm_unpacklo_epi32(a, b), _mm_unpacklo_epi32(c, d));
#else
        block bytes;
        for (u64 i = 0; i < 16; ++i)
            bytes.set<u8>(i, x[i].get<u8>(0));
        return bytes;
#endif
    }

    template<int bit>
    OC_FORCEINLINE u16 packByteBits(block bytes)
    {
        static_assert(bit >= 0 && bit < 8);
        return static_cast<u16>(movemask_epi8(slli_epi16<7 - bit>(bytes)));
    }

    //FOR j := 0 to 7
    //	i := j*16
    //	dst[i+15:i] := ( a[i+15:i] == b[i+15:i] ) ? 0xFFFF : 0
    //ENDFOR
    OC_FORCEINLINE  block cmpeq_epi16(const block& a, const block& b)
    {
#ifdef ENABLE_SSE
        return _mm_cmpeq_epi16(a, b);
#else
        block r;
        for (i64 i = 0; i < 8; ++i)
        {
            r.set<u16>(i, a.get<i16>(i) == b.get<i16>(i) ? 0xFFFF : 0);
        }
        return r;
#endif // ENABLE_SSE
    }


    OC_FORCEINLINE void storeu_si32(void* mem, const block& a)
    {
#ifdef ENABLE_SSE
        _mm_storeu_si32(mem, a);
#else
        i32 lane = a.get<i32>(0);
        std::memcpy(mem, &lane, sizeof(lane));
#endif
    }
}
